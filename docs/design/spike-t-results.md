# Spike T — Results

**Questions answered:**

1. (a) Does the exchange-thread design (args `lua_xmove`d from the suspended
   game coroutine, ops executed via a `lua_pcall`'d dispatcher, results
   xmove'd back in the trampoline continuation) round-trip arguments and
   multiple returns correctly while the coroutine is suspended in
   `lua_yieldk`?

2. (b) Does `luaL_error` raised by a guest wrapper surface as a catchable
   Lua error in the calling script, with the coroutine resumable afterwards
   and guest registers/stack restored across repeated errors?

3. (c) Do string arguments and string returns round-trip byte-exactly,
   including the >4 KiB guest-arena retry path and embedded-NUL
   `lua_pushlstring`?

4. (d) Do table get/set/`geti`/`seti`/`rawlen`/`next` work from a guest
   wrapper, with fixed-seed `lua_next` order identical across paths?

5. (e) Does the Spike-Q-style byte-exact determinism gate pass for a hybrid
   cart exercising strings, tables, `next` iteration, multiple returns, and
   caught errors over multiple frames with evolving state — rv32 vs WASM?

6. (f) What is the per-call overhead of a bridged call (~10 ops) vs the
   typed fast path vs pure Lua?

All runs: 2026-06-07, macOS arm64 host; rv32 path = `blytplay --headless`
(rv32emu), WASM path = `node tests/wasm/run_cart.js` against the SDK
`share/wasm` runtime.  Implementation on the blyt branch
`spike-t-lua-bridge` (5 commits); carts and run scripts in
[`spikes/spike-t/`](../../spikes/spike-t/).

---

## Headline Answers

**Q(a): YES.**

The bridged trampoline xmoves the coroutine's arguments onto a
registry-anchored exchange thread (`lua_newthread`, status always
`LUA_OK`), invokes the guest wrapper with an opaque 32-bit call token as
its `lua_State *`, and services every `BLYT_ECALL_LUA_OP` against the
exchange thread's outer stack.  `sum5(1,2,3,4,5)` — five arguments
(impossible on the typed 4-register path), `lua_gettop`,
`luaL_checkinteger`, and **two return values** — round-trips correctly and
identically on both paths (stage 2).  The wrapper-visible stack semantics
(indices `1..n`, negative indices, `lua_CFunction`-style return count)
match the real API exactly.

Protection mechanism: ops that can run Lua code (metamethods) or allocate
are executed inside `lua_pcall` of a host helper function with copies of
the outer-stack operands as arguments and `LUA_MULTRET` results landing
back on the outer stack; errors are caught, never reach the panic handler,
and route into the (b) error path.  Stack-mutation fixups (e.g.
`lua_setfield` consuming its value, `lua_next` replacing the key) are done
from outside the frame after a successful pcall, where absolute outer
indices remain valid.

The direct-suspended-thread alternative was rejected at design time (an
error raised on a `LUA_YIELD` thread with no `errorJmp` reaches
`luaD_throw` → panic → aborts the whole WASM module, and `lua_pcall` on a
suspended thread violates the API's own checks); the exchange-thread
design made an empirical demonstration unnecessary since every error case
is exercised through the pcall'd dispatcher in stages 3–5.

**Q(b): YES.**

`luaL_error` from a guest wrapper surfaces as a catchable Lua error with
the exact message on both paths (`pcall(m.fail, "once")` →
`false, "boom: once"`).  Mechanism: the stub formats guest-side
(`vsnprintf`) and issues `ERRMSG`; the host pushes the message on the
exchange thread, halts emulation **without advancing the PC** (the stub
never resumes; a `__builtin_trap()` follows it), restores the full
register snapshot taken at `begin_bridged_call`, and
`blyt_session_run_frame` returns the new `BLYT_RUN_FN_ERROR`.  The
frontend resumes the coroutine with an error flag; the `lua_yieldk`
continuation xmoves the error value and raises it **inside the coroutine**
— sanctioned API, so a script-level `pcall` catches it (ADR-0084
preserved) and an uncaught error follows the normal resume-error path.

**1000 consecutive error calls** complete with the bridge fully
functional afterwards (`err_repeat:1000`, then a successful bridged call)
— the register snapshot/restore prevents guest-`sp` drift from the
abandoned wrapper frames.  Host-side validation failures (bad index, bad
guest pointer, type errors) surface through the same path
(`tbl_err:false|summarize: items must be a table`, identical both paths).

**Q(c): YES.**

- Small string in/out: `echo_upper("hello, Blyt!")` → `"HELLO, BLYT!", 12`.
- 5400-byte string argument and return: identical FNV-style checksum on
  both paths; exercises the guest arena `TOLSTRING` retry protocol (first
  call with a 64-byte buffer → `ST_RETRY` with full length → exact-size
  arena alloc → second call).
- Embedded NULs: `lua_pushlstring("a\0b\0c", 5)` arrives in Lua with
  `#s == 5` and bytes `97,0,98,0,99` on both paths.  Key/name ops reject
  embedded NULs host-side.

Lifetime rule validated in practice: `lua_tolstring` results live in the
guest-side per-call arena (reset on call-token change) until the wrapper
returns.

**Q(d): YES.**

`summarize(cfg)` reads string/integer fields (`getfield`), sums an array
(`rawlen` + `geti`), iterates the config table with `lua_next` building a
key list, and returns a **new result table** (`createtable` + `setfield`)
— the config-in/structured-result-out pattern that motivated ADR-0130.
`fill(t)` mutates a caller-owned table in place (`setfield` + `seti`).
With the fixed hash seed, `lua_next` order is identical across paths
(`tbl_keys:name;items;flag;scale;extra;` on both).

**Q(e): YES — byte-exact.  (Hard gate for ADR-0130 acceptance: PASS.)**

10-frame cart, per-frame bridged call exercising `next`-order digesting,
string digesting, table mutation (`setfield`/`seti`), multiple returns,
and a deliberate `luaL_error` every 3rd frame caught by the script.  The
11-line debug stream (init + 10 frames, including FNV digests over
iteration order and evolving state) is **byte-identical** between
`blytplay --headless` and the node WASM runtime.

Prerequisite finding: the Lua string-hash seed was **not fixed anywhere**
(`luaL_newstate` default = ASLR + time), so `pairs()` order already
differed per run and between paths for *pure-Lua* carts — a latent
ADR-0007 violation independent of this spike.  Fixed by defining
`luai_makeseed()` to a constant (`0x424C5954`) in every Lua build (guest
libraries, host WASM Lua, `blyt-luac`).  Stage 1 verifies `pairs()` order
parity for three insertion histories.  Note: in the WASM frontend the
define must be a compile *option* — CMake `COMPILE_DEFINITIONS` silently
drops function-style macros (this consumed one debugging round).

**Q(f): bridged ≈ 10.6 µs/call for a 10-op wrapper; typed ≈ 4.6 µs/call;
pure Lua ≈ 0.6 µs/call.** (node/arm64, 10k iterations, best of 3,
empty-cart baseline subtracted.)  The marginal cost is ≈ 0.6 µs per bridge
op on top of the shared yield/begin-call/ECALL-return overhead.  This is
**~10× under** the 100 µs exit threshold.  The guidance stands: typed fast
path for hot per-element calls (ADR-0039), bridged path for rich
per-tick/bulk calls.

---

## Summary of Findings

1. **The same wrapper source runs on both targets.** `BLYT_LUA_EXPORT_RAW`
   / `BLYT_LUA_MODULE_EXPORT_RAW` bodies (lua_CFunction shape, restricted
   Lua C API) compile once and run against the real Lua VM on rv32 and the
   ECALL bridge on WASM.  The cart binary is identical: the WASM frontend
   substitutes the guest-side implementation by embedding
   `libblyt32lua-bridge.so` (no Lua VM; every export an ECALL stub) under
   the DT_NEEDED name `libblyt32lua.so`.

2. **Typed fast path unaffected.** Old `.lua_exports` entries carry
   `flags = 0` and take the existing typed trampoline; stage 2 runs both
   side by side.  Full integration suite green on the branch (124 tests,
   13 binaries, 0 failures).

3. **Security posture as designed in ADR-0130.** Single dispatch switch =
   single enforcement point (opcode allowlist; loading/compiling class has
   no opcodes); validity window (`BLYT_ECALL_LUA_OP` outside a bridged
   call → fatal trap); per-call 32-bit token; ADR-0114-style validation
   (real stack indices only, pseudo-indices rejected, guest ptr+len
   bounds-checked, 16 MiB string cap, host-managed `lua_checkstack`).

4. **Cross-path API divergence found and fixed (stage 1):** the guest Lua
   runtime opened **no** standard libraries (`pairs` absent) while the
   WASM host opened base/math/string/table/coroutine.  The guest now opens
   the same set.  ADR-0079's allowlist remains the production shape; the
   spike aligned the two paths rather than implementing the full
   allowlist.

5. **One-call-per-tick scheduling fixed in passing:** the WASM frame loop
   serviced a single trampoline per animation tick (N native calls per
   frame previously cost N ticks).  `wasm_service_trampoline` now drains
   consecutive native calls within the tick, exiting on the frame-boundary
   yield / quit / error / GDB pause.  Required for meaningful (f) numbers;
   correct behaviour for real carts regardless.

## Limitations / notes for production

- **Arena large-string lifetime:** the guest arena keeps one growable heap
  block for >4 KiB `tolstring` results; a wrapper holding **two** live
  >4 KiB results simultaneously would see the first invalidated.
  Production: chain blocks or document single-large-string per call.
- **`bridge_fail_msg` / `ERRMSG` push** uses an unprotected
  `lua_pushstring` (OOM would panic).  Production: route through the
  pcall'd dispatcher.
- **`luaL_getsubtable` on pseudo-indices** (`LUA_REGISTRYINDEX`) is
  rejected by the bridge; only reachable from `cart_lua_modules` glue,
  which never runs on WASM.
- **No native calls from metamethods** triggered inside bridged ops
  (re-entrant `lua_yieldk` fails cleanly into a Lua error).  Reverse
  trampoline explicitly deferred (ADR-0130).
- **Number formatting**: digest prints use Lua-side integer→string
  conversion in both paths (same `lobject.c`); float formatting through
  the bridge rides on Spike Q's softfloat builtin fix and was not
  separately stressed here.
- The benchmark is wall-clock over full node runs with baseline
  subtraction — adequate for an order-of-magnitude answer, not a
  microbenchmark.

## Proposed ADR amendments

- **ADR-0130**: flip Status to *Accepted* — all six headline questions
  answer YES and the (e) hard gate passes.  Incorporate the arena
  large-string note and the ERRMSG-push note into its Consequences.
- **ADR-0066 / ADR-0007**: the fixed-seed amendment (already drafted with
  ADR-0130) is validated by stage 1.
- **ADR-0111 / ADR-0118**: dated amendment sections (already drafted with
  ADR-0130) stand as written.

## Production follow-ups

- Rust `#[lua_export(raw)]` proc-macro variant (C macros landed in the
  spike; the Rust attribute is designed in ADR-0130 but not implemented).
- Typed conveniences (`&str` args, `String` returns) generated as raw
  wrappers.
- ADR-0079 stdlib allowlist implementation for both Lua state setups
  (the spike aligned the two paths at the WASM host's current set).
- Arena block-chaining; protected ERRMSG push; opcode dispatch unit tests
  against adversarial register values (fuzz the a0–a5 space).
- Surface `BLYT_RUN_FN_ERROR` in the libretro frontend if/when it gains
  the hybrid Lua path.
