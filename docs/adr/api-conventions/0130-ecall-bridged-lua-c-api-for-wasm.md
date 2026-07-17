# ADR-0130: ECALL-bridged Lua C API for the WASM target

## Status

Accepted — validated by Spike T (2026-06-07): all six headline questions
answered YES; the byte-exact determinism gate (hard block) passes; bridged
10-op call ≈ 10.6 µs vs typed ≈ 4.6 µs on the WASM path (~10× under the
exit threshold); full integration suite green (124 tests).  See
[`spike-t-results.md`](../../design/spike-t-results.md).

## Context

ADR-0111 restricts Lua↔native arguments and return values to primitives
that fit CPU registers (`f32`, `i32`, `u32`, `bool`), because on the WASM
target Lua runs in the WASM host process while native cart code runs
inside rv32emu. The constraint was framed as deliberate: "functions that
need to interact with the Lua VM directly do not belong in the Rust
binding layer."

Implementation experience with hybrid carts shows the pure-computation
framing is too restrictive in practice:

- **Strings.** Passing names, messages, serialized data, or text in
  either direction is a basic need (the very first hybrid example wants
  `greeting.hello()` to return a string). Today this requires encoding
  text into integer handles or pre-staged guest buffers.
- **Table-shaped data.** Configuration in, structured results out.
  Without table access, native functions return single scalars and Lua
  reassembles structure from repeated calls — more boundary crossings,
  not fewer.
- **Arity.** Four arguments and one return is a register-file limit, not
  a design point. The real Lua C API has neither limit.
- **Errors.** `luaL_error` ergonomics (argument validation surfacing as
  catchable Lua errors per ADR-0084) are unavailable to native code on
  WASM; the typed trampoline can only return a value.

Meanwhile, on rv32 targets none of these restrictions exist: the
generated wrappers use the real (restricted, ADR-0118) Lua C API in a
shared address space. The two targets have diverged: one wrapper
mechanism on rv32 (`.lua_regtab` + Lua C API), a different one on WASM
(`.lua_exports` typed trampolines), with the WASM one strictly weaker.

Three observations make a stronger design possible:

1. **The host owns guest memory.** On WASM, the rv32emu guest address
   space is a 256 MiB linear buffer inside the same WASM module
   (`vm_attr->mem`). The host can read and write any guest address with
   bounds checking — `BLYT_ECALL_CONSOLE_DEBUG` (a0=ptr, a1=len) already
   does exactly this. Guest pointers *can* cross the boundary, because
   the host can translate them. Only host pointers can never cross: the
   guest has no mechanism to dereference them.

2. **The host services ECALLs synchronously while the `lua_State` is
   alive.** During a Lua→native trampoline call, the host is stepping
   the emulator from inside its own frame loop; the rv32emu `on_ecall`
   handler is a host C function with full access to host state,
   including the Lua VM. A guest-issued ECALL can therefore execute a
   real Lua C API operation and return its result to the guest, within
   one guest instruction step.

3. **The guest-side Lua library is already substitutable.** The WASM
   frontend embeds `libblyt32lua.so` into the guest, where it is
   currently dead weight (host-side Lua is authoritative). Replacing
   that one embedded artifact with a stub build changes the guest-side
   implementation of every `lua_*` symbol without touching the cart ELF.

Discovered context recorded here for accuracy: the vendored Lua is
**5.5.1** (ADR-0066 says 5.4; see the ADR-0066 amendment), and no build
fixes the Lua string-hash seed (`luaL_newstate` derives it from ASLR and
time). The unfixed seed means `pairs()` iteration order already differs
between the rv32 and WASM paths for pure-Lua carts — a latent
determinism bug independent of this ADR, and a prerequisite fix for
deterministic bridged table iteration.

## Decision

The WASM target gains an **ECALL-bridged Lua C API**: the restricted Lua
C API subset (ADR-0118) is implemented, in the guest, as stubs that trap
to the host via a single new ECALL. The host executes each operation
against the real host-side `lua_State` and returns the result. The same
generated wrapper source — reading arguments from the Lua stack, pushing
results, raising errors — compiles once and runs on both targets: on
rv32 the `lua_*` symbols are the real Lua VM; on WASM they are bridge
stubs. The cart binary is identical across targets.

The typed register path from ADR-0111 is retained as a **fast path** for
primitive ≤4-argument signatures (zero bridge ECALLs per call). The
bridged path is opt-in per export via a flags bit.

### Bridge protocol

One new ECALL number, `BLYT_ECALL_LUA_OP = 10`, with an opcode in `a0`:

```
a7 = 10 (BLYT_ECALL_LUA_OP)
a0 = opcode
a1 = call token (see validity window)
a2–a5 = operation arguments (max 3 used; e.g. SETFIELD: idx, k_ptr, k_len)

returns:
a0 = status   (0 = ok; <0 = protocol status, e.g. RETRY for tolstring;
               Lua errors never return — see error model)
a1 = primary value (integer / f32 bit pattern / bool / type code / length)
a2 = auxiliary     (isnum out-flag for to*x; full length for tolstring)
```

A single ECALL number with an opcode switch (rather than a number range)
keeps the validity-window check, token check, and opcode allowlist in
one dispatch point, which is also the ADR-0118 enforcement point on this
target.

**v1 opcode set** (the ADR-0118 restricted surface, minus
registration-class ops, plus the table accessors needed for useful
wrappers):

```
GETTOP   SETTOP   PUSHVALUE  TYPE
PUSHNIL  PUSHBOOLEAN  PUSHINTEGER  PUSHNUMBER  PUSHLSTRING
TOINTEGERX  TONUMBERX  TOBOOLEAN  TOLSTRING
CREATETABLE  GETFIELD  SETFIELD  GETI  SETI  RAWLEN  NEXT
GETGLOBAL  SETGLOBAL
ERROR  ERRMSG
```

Composed guest-side with no ECALL: `lua_typename` (static table),
`lua_pushstring` (over PUSHLSTRING), `luaL_checkinteger` /
`luaL_checknumber` / `luaL_checkstring` / `luaL_opt*` / `luaL_argerror`
(composed from `to*x` + ERRMSG), and the usual macros (`lua_pop`,
`lua_newtable`, `lua_istable`, `lua_tointeger`, `lua_tonumber`).

Registration-class ops (`luaL_newlib`, `luaL_setfuncs`,
`lua_pushcclosure`) need **no opcodes**: on WASM, registration is
host-driven from `.lua_exports`; the `cart_lua_modules` / `.lua_regtab`
iteration runs only on rv32 targets.

### Execution model: the exchange thread

Direct C API access to the suspended game coroutine is not safe. The
coroutine is parked in `lua_yieldk` inside the trampoline; `lua_getfield`
/ `lua_setfield` can invoke metamethods — running Lua code on a thread
with `status == LUA_YIELD` and no active error-recovery point. Any error
inside reaches `luaD_throw` with no `errorJmp`, which panics and aborts
the entire WASM module. `lua_pcall` cannot be used on a suspended thread.
(Spike T documents this failure mode empirically.)

Instead, all bridged operations execute on a dedicated **exchange
thread**:

- At cart load the host creates `g_exch = lua_newthread(g_lua)`,
  anchored in the registry. Its status is always `LUA_OK`; every
  documented C API operation on it is fully defined.
- The bridged trampoline (host side, registered per flagged
  `.lua_exports` entry): `n = lua_gettop(co)`; `lua_xmove(co, g_exch, n)`;
  mint a call token; `blyt_session_begin_fn_call(s, invoke_shim_addr, 2,
  {wrap_addr, token})`; `lua_yieldk`. The wrapper sees its arguments at
  stack indices `1..n` of the exchange thread — exactly the real-API
  view.
- Every bridge op executes against `g_exch` inside a `lua_pcall`'d
  dispatcher function, so metamethod and out-of-memory errors are caught
  and routed into the error model below; nothing can panic the host.
- The guest-side invoke shim (`__blyt_lua_bridge_invoke(wrap_fn,
  token)`) resets the string arena, calls the wrapper with the token as
  its opaque `lua_State *`, and returns the wrapper's result count —
  wrapper semantics are identical to a real `lua_CFunction`.
- On `BLYT_ECALL_HOST_FN_RETURN`, `a0` is the return count `m`. The host
  validates `0 ≤ m ≤ lua_gettop(g_exch)`, the continuation
  `lua_checkstack(co, m)` + `lua_xmove(g_exch, co, m)` and returns `m`
  to Lua. The exchange stack is cleared on every completion path.

Re-entrancy: if a metamethod running on the exchange thread calls
another native export's closure, that closure's `lua_yieldk` fails
("attempt to yield from outside a coroutine"), is caught by the per-op
`pcall`, and surfaces as a clean Lua error. Native calls from
metamethods triggered inside bridged ops are unsupported in v1; a
reverse-trampoline design is explicitly deferred.

Rejected alternatives:

- *Direct suspended-coroutine access* — unprotected metamethod/error
  execution panics, per above.
- *Synchronous run-to-completion inside the trampoline (no yield)* —
  blocks the browser main thread for the whole guest call; the yield
  architecture exists for responsiveness and the DAP/GDB pause paths
  thread through it.

The exchange thread costs two `lua_xmove`s per bridged call —
negligible.

### String marshalling

**Guest → host** (`lua_getfield`'s `k`, `lua_setglobal`'s `name`,
`lua_pushlstring` data): the stub passes `ptr + len` (computing `strlen`
for NUL-terminated entry points). The host bounds-checks every byte
against guest RAM (the `CONSOLE_DEBUG` pattern), copies to a host
buffer, and NUL-terminates. Length cap 16 MiB (host-OOM guard). Embedded
NULs are legal for `PUSHLSTRING` (length-delimited) and rejected for
key/name ops (C strings in the underlying API).

**Host → guest** (`lua_tolstring`): `TOLSTRING(token, idx, buf, cap)` —
the host writes `min(len, cap)` bytes plus NUL and returns the full
`len`; status `RETRY` if `len ≥ cap`. The guest stub hides this behind a
per-call scratch arena (static 4 KiB in the bridge library, growable via
guest `malloc`): it retries once with an exact-size buffer and returns a
plain `const char *`. **Lifetime rule: pointers returned by bridged
`lua_tolstring` are valid until the wrapper returns** — a strict subset
of the real API's "while the value is on the stack" guarantee, so
portable wrapper code is automatically correct on both targets. The
arena is owned by the bridge stub library and reset at wrapper entry.

### Pointer-crossing rules

Stated once, normatively:

- **Guest pointers may cross** the boundary in either direction, because
  the host translates and bounds-checks them against the guest memory
  buffer. They are meaningless to Lua itself and are only ever
  dereferenced by the host on the guest's behalf.
- **Host pointers never cross.** `lua_State *` on the guest side is an
  opaque token, never dereferenced; the bridge stub library contains no
  Lua VM. Lua-owned memory (interned strings, table internals) is never
  exposed to the guest.
- **Strings cross by copy** in both directions. Lua's GC owns Lua
  values; the guest owns guest allocations; neither ever frees the
  other's memory.

### Error model

`lua_error` / `luaL_error` longjmp on rv32. On WASM the host cannot
unwind guest frames, and the game coroutine is suspended — but the
trampoline *continuation* runs in the coroutine's context and may
legitimately raise. The bridge therefore converts guest-side error
raises into a host-orchestrated abort-and-raise:

1. The `luaL_error` stub formats the message guest-side (`vsnprintf`
   into the arena — the guest has libc formatting) and issues
   `ERRMSG(token, msg_ptr, msg_len)`. The `lua_error` stub issues
   `ERROR(token)` (error value = top of the exchange stack).
2. The host moves/pushes the error value onto the exchange thread, marks
   the in-flight call failed, halts emulation **without advancing the
   PC** — the guest stub never resumes (and is followed by
   `__builtin_trap()` for defence in depth).
3. `blyt_session_run_frame` reports a new result,
   `BLYT_RUN_FN_ERROR`. The host restores a full guest register snapshot
   taken at `begin_fn_call`, so the abandoned guest frame does not leak
   guest stack (repeated errors must not drift `sp`). Guest *global*
   state mutated by the half-run wrapper stays mutated — the same
   contract as a Lua error thrown mid-function.
4. The frontend clears the trampoline state, resumes the coroutine with
   an error flag; the continuation `lua_xmove`s the error value from the
   exchange thread and calls `lua_error` **inside the coroutine**. A
   script-level `pcall` around the native call catches it (ADR-0084
   semantics preserved); uncaught, it reaches the existing resume-error
   path. The coroutine is never corrupted: raising from a continuation
   is sanctioned Lua API.

The same path serves host-detected failures: opcode argument validation
errors, `lua_checkstack` failure, metamethod errors caught by the per-op
`pcall`, and ADR-0115 step-limit expiry mid-call ("native call exceeded
step limit") — all become catchable Lua errors rather than hangs or
aborts.

### Validity window, tokens, and validation

- `BLYT_ECALL_LUA_OP` is accepted **only while a bridged call is in
  flight**. Outside the window it falls to the existing unknown-ECALL
  trap (fatal), which is correct for a protocol violation.
- The call token is a 32-bit nonzero nonce minted per call and passed to
  the wrapper as its opaque `lua_State *`. Every op checks it; mismatch
  is a fatal trap (a stashed or forged token, not a recoverable
  mistake).
- Per-op validation (ADR-0114 style): stack indices must be real
  (`1..top` or `-top..-1`); **pseudo-indices are rejected** (no registry
  or upvalue access in v1); `SETTOP` is bounded; guest `ptr+len` is
  fully bounds-checked; the host calls `lua_checkstack(g_exch, …)`
  before every push-class op and before moving results back. The guest
  never manages Lua stack headroom.
- Failures *inside* a valid window become Lua errors (recoverable);
  failures *outside* the window are fatal traps.

Security framing: on rv32, ADR-0118 concedes that Layer 2 is not
enforceable against native cart code (it can call any exported Lua
symbol directly). On WASM, the bridge opcode table **is** the Layer 2
surface, and it is *actually enforceable* — the host mediates every
operation. The loading/compiling class on ADR-0118's do-not-export list
has no opcodes by construction. The WASM target ends up with a strictly
stronger Layer 2 than rv32.

### Registration: flags byte and dual paths

- `.lua_exports` entries repurpose `_pad[0]` as `flags`; bit 0 =
  `BLYT_LUA_EXPORT_BRIDGED`. Existing carts carry 0 and take the typed
  fast path unchanged — full backwards compatibility including
  already-built carts.
- For bridged entries the host resolves `wrap_sym` (the wrapper, not the
  bare function) with ADR-0112-style validation (address inside an
  executable LOAD segment) and calls it through the invoke shim. The C
  export macros drop `static` from the `__lua_export_*` wrappers so they
  are resolvable (Rust wrappers already are; carts link
  `--export-dynamic`).
- The typed fast path remains the recommendation for hot, primitive
  signatures (ADR-0039: per-element math). Each bridge op costs one
  synchronous host C call plus one `pcall`'d Lua op — fine for the tens
  of ops a rich wrapper performs, wrong for per-pixel call rates.

### SDK surface

- **C:** `BLYT_LUA_EXPORT_RAW(name)` and
  `BLYT_LUA_MODULE_EXPORT_RAW(module, name)` — the author writes the
  wrapper body directly: `int body(lua_State *L)` returning the result
  count, against the restricted API. This is the natural C shape for
  strings, tables, and multiple returns.
- **Rust:** `#[lua_export(raw)]` on
  `fn(L: *mut lua_State) -> core::ffi::c_int`. Typed conveniences
  (`&str` arguments, `String` returns) generated *as* raw wrappers are a
  production follow-up, not part of this decision.
- The existing typed macros and `#[lua_export]` signatures are
  unchanged.

### rv32 parity

So the same wrapper compiles and links on rv32, the guest export surface
(`blyt_lua_internal.h`, `blyt32lua.sym`) is extended with: `lua_gettop`,
`lua_pushnil`, `lua_pushstring`, `lua_pushlstring`, `lua_tolstring`,
`lua_error`, `lua_geti`, `lua_seti`, `lua_rawlen`, `lua_next`,
`lua_typename`. All are on (or consistent with) the ADR-0118 allowed
list; see the ADR-0118 amendment.

### Determinism

`luai_makeseed()` is fixed to a constant in **every** Lua build (guest
library, host WASM, `blyt-luac`). Same source tree + `LUA_32BITS` + same
seed ⇒ identical string hashing ⇒ identical `lua_next` / `pairs` order
for string- and number-keyed tables with identical insertion history,
identical `table.sort` pivot selection, and identical default
`math.random` streams across the rv32 and WASM paths. Hash-DoS hardening
is irrelevant here — a cart can only hurt itself.

Residual rule: tables keyed by reference values (tables, functions)
hash by address and can never iterate identically across host and guest.
Carts must not depend on their iteration order (documented, not
mechanically enforced).

f32 values cross as bit patterns in both directions; string copies are
byte-exact; number→string conversion runs through the same `lobject.c`
code on both sides. The Spike Q byte-exact gate is extended (Spike T) to
a hybrid cart exercising strings, table access, `lua_next` iteration,
multiple returns, and a caught error.

### Explicitly deferred

- Guest `lua_pushcclosure` / runtime registration of guest functions
  (requires a reverse trampoline).
- Registry access and `luaL_ref`.
- Userdata and metatable manipulation from the guest.
- `lua_call` / `lua_pcall` from the guest.
- `lua_gettable` / `lua_settable` (stay omitted, matching the current
  internal header).
- Coroutine ops from the guest; native calls from metamethods.

## Validation (Spike T)

This ADR is Proposed pending Spike T, which must answer:

- (a) Exchange-thread round-trip (xmove in, `pcall`'d ops, xmove out,
  multiple returns) works while the game coroutine is suspended.
- (b) `luaL_error` from a guest wrapper surfaces as a catchable Lua
  error; coroutine resumable afterwards; no guest register/stack drift
  over 1000 consecutive error calls.
- (c) String argument and string return round-trip byte-exact, including
  the >4 KiB retry path and embedded-NUL `PUSHLSTRING`.
- (d) Table get/set/`geti`/`next` from a guest wrapper; fixed-seed
  `next` order identical across paths.
- (e) Byte-exact determinism gate (Spike Q methodology) for the full
  hybrid cart, rv32 vs WASM. Hard block for Accepted status.
- (f) Overhead measurement: bridged call with ~10 ops vs typed fast path
  vs pure-Lua baseline.

Fallbacks on failure are recorded in the spike definition
(`early-validation-spikes.md`, Spike T).

## Consequences

- One wrapper source and one cart binary serve all targets. The
  WASM-target restriction to 4 scalar args / 1 scalar return is lifted
  for bridged exports; strings, tables, arbitrary arity, multiple
  returns, and `luaL_error` work identically on rv32 and WASM.
- ADR-0111's "pure computation only" rationale is superseded for bridged
  exports (see ADR-0111 amendment). The typed fast path — and its
  guidance to keep hot per-element calls primitive — remains.
- The WASM target gains an enforceable Layer 2: the bridge opcode
  allowlist is host-mediated, unlike the rv32 export-surface restriction
  which native cart code can bypass (ADR-0118).
- New host attack surface: the opcode dispatcher parses adversarial
  guest input. It is covered by the single-dispatch-point design,
  ADR-0114-style argument validation, the validity window, and the token
  check; out-of-window or malformed traffic is a fatal trap.
- Bridged calls are slower than typed calls (one ECALL per Lua op).
  Authors choose per export; the typed path remains the hot-path
  recommendation per ADR-0039.
- Fixing the Lua hash seed makes pure-Lua `pairs()` iteration
  deterministic across targets — repairing a pre-existing divergence —
  at the cost of forgoing hash-DoS randomisation, which has no threat
  model here.
- The error model gives native wrapper code first-class ADR-0084 error
  semantics on both targets, and converts step-limit expiry inside
  bridged calls into a catchable error instead of a hang.
- The deferred list keeps `lua_CFunction` registration host-controlled;
  the Lua-visible function surface on WASM remains exactly the
  `.lua_exports` section contents.

## Amendment (#262, 2026-07-17): the reverse-trampoline (native→Lua calls)

The original v1 deliberately shipped **no call opcode** and stated that "a
reverse-trampoline design is explicitly deferred" — the native half could be
*called by* Lua but could not *call* Lua back. This amendment implements that
deferred design so a bridged/raw wrapper (and the rv32/bare-metal full-lib path)
can invoke a Lua value it has pushed. Without it, native→Lua callbacks worked
only on the (retiring) emulated legs and on bare-metal RISC-V, and were missing
outright on every host-Lua leg — a cross-runtime divergence against ADR-0007.

**New opcode.** One opcode, `PCALL` (a0 = 25), carrying `a2 = nargs`,
`a3 = nresults` (`LUA_MULTRET = -1`), `a4 = is_protected`. The function and its
`nargs` arguments are already on the exchange stack (pushed via the existing
ops). The host **always** runs `lua_pcall` internally — an unprotected
`lua_call` error would `longjmp` past the host's own C frames — and the flag
decides the guest-visible behaviour: `lua_pcall` returns the Lua status (the
error object is left on the exchange stack); `lua_call` re-raises into the guest
(halt-without-advancing, the existing ADR-0084 error path). Argument/result
counts are bounds-checked; a bad count is a catchable Lua error, not a trap.

**Export-surface additions.** `lua_pcall` and `lua_call` join the allowed
`libblyt32lua.so` export list (ADR-0118 amendment). They call an *existing* Lua
value and load no new code, so they stay within ADR-0118's restricted surface
(the do-not-export list targets the loading/compiling class). `msgh` is
restricted to 0 in v1 — the bridge cannot run a message handler, and the
constraint is enforced identically on the rv32/bare-metal wrapper so the
reverse-trampoline behaves the same on every leg. `lua_pcall`/`lua_call` are
macros over `lua_pcallk`/`lua_callk` in `lua.h`, so the full lib materialises
real symbols (`runtime/guest/src/libblyt32lua/lua_reverse_tramp.c`); the bridge
stub provides the ECALL versions.

**Re-entrancy (native→Lua→native), now supported.** v1 declared native calls
from within bridged ops "unsupported"; the reverse-trampoline makes them work.
The parity anchor is **bare-metal RISC-V**, where the whole cart shares one
address space and re-entrant native→Lua→native runs on the natural call stack.
The host-Lua legs reproduce that with two mechanisms, both on the
determinism-critical path:

1. **CPU + bridge-state save/restore.** The single rv32 CPU drives every native
   call, so a nested `begin_fn_call`/`begin_bridged_call` would clobber the
   suspended outer call's PC, register file, `trace_fn_addr`, and bridged-call
   token. The `PCALL` op saves that state (32 GPR + PC + fcsr + 32 FP + the
   bridge snapshot + token/active/error/fn_return_done) before its internal
   `lua_pcall` and restores it after — a protected boundary, so the restore runs
   even if the callback raised. `run_frame` save/restores the static run-context
   pointer instead of nulling it, so a nested drive leaves the outer context
   intact.
2. **Exchange-thread pool.** A reverse-trampoline callback runs *on* an exchange
   thread, so a nested native call cannot reuse it (its args would alias the
   outer frame at absolute indices `1..n`). Each frontend pre-creates a small
   pool of exchange threads (depth `BLYT_HOSTLUA_EXCH_POOL_DEPTH`) as scaffolding
   in the VM build — before the `guest_heap_used` baseline, so it is excluded
   from `cart_allocations` (ADR-0029) and identical across host-Lua legs. The
   trampoline drives nesting level `d` on `pool[d]` (guaranteed distinct from the
   caller) and pops back on the way out, before any `lua_error` longjmp. Nesting
   beyond the pool depth is a clean Lua error, not corruption.

**Debugging.** On the native host-Lua path the DAP master hook is armed on every
pool thread (hooks are per-thread in Lua 5.4), so a breakpoint in a Lua function
reached via a native→Lua callback fires — the block-a-thread pause model needs
no yieldable boundary. On WASM, a breakpoint *inside* such a callback would have
to `lua_yield` across the `lua_pcall(exch)` and rv32-interpreter C frames, which
the exchange-thread architecture cannot do today; that case is deferred (#272),
as is DAP *stepping* across the native↔Lua thread boundary (#273).
