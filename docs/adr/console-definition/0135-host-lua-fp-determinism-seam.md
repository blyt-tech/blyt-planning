# ADR-0135: Host-Lua floating-point determinism — a softfloat-backed math seam

## Status

Accepted (2026-07-05) — being implemented for the WASM host-Lua fast path
(blyt#223 Phase A). The broader decision on whether host-Lua becomes the primary
Lua execution path on emulated hosts (Spike Y follow-on) remains open; this ADR
records the *mechanism* that makes that direction safe, and its Phase-A landing
pins the WASM fast path today. See **Implementation notes (2026-07-05)** below
for how the mechanism is realized on WASM (which cannot soft-float) and what
defers to the future native host-Lua leg.

## Context

Lua carts run two ways today:

- **Emulated / native-metal path** — the Lua VM compiled to RV32
  (`libblyt32lua.so`), dispatched by rv32emu on emulated hosts or executed
  natively on real RISC-V hardware. This path is a **tight deterministic
  reference**, verified in the build:
  - Every Lua VM TU (`lvm.c`, `lobject.c`, `lmathlib.c`, all of `lua/*.c`) and
    blyt-tech musl are compiled with `-ffp-contract=off -fno-fast-math`
    (`LIBBLYTC_CFLAGS` in `cmake/blyt_guest_libs.cmake`).
  - Transcendentals resolve to blyt-tech musl's **generic-C** `src/math/*.c`.
  - f64 execution is Berkeley SoftFloat with the **RISC-V NaN-propagation
    specialization** + `SOFTFLOAT_ROUND_ODD`, round-nearest-even — the same
    softfloat build/config rv32emu uses (ADR-0132), so metal and emulator agree
    bit-for-bit.
- **Host-Lua fast path** — the Lua VM compiled *natively for the host* (WASM
  today: the "S proxy + state buffers" path for pure-Lua carts; hybrids via the
  ECALL bridge, ADR-0130). This is the path Spike Y recommends extending to the
  native player (`blytplay`/libretro) for the ~50× per-pixel / Lua-VM speedup on
  aarch64 handhelds, where the emulated Lua VM is emulation-hostile.

A build audit (2026-07-04) found the host-Lua fast path is **not
softfloat-backed** and is not pinned to the reference:

- No `-msoft-float` ⇒ Lua's f64 basic ops compile to **native `f64.add`**, not
  softfloat.
- No `src/math` compiled in ⇒ `math.sin/cos/pow` resolve to **Emscripten's
  bundled libm**, not blyt-tech musl.
- No `-ffp-contract=off` on the Lua sources.
- The softfloat present in the WASM target backs only rv32emu's F/D emulation,
  not host-Lua.

For **basic ops** this is benign: `+ − × ÷ √` are IEEE-754 correctly-rounded, so
native hardware equals softfloat bit-for-bit *given matched contraction* (and
WASM MVP has no scalar FMA, which masks the unpinned contraction — a mask that
does **not** hold on native x86/arm64, which have FMA).

For **transcendentals** it is a live, unpinned coupling. ADR-0132's Spike U
Stage 5 spot-verified that the two musl lineages (Emscripten bundled and
blyt-tech) "agree to full double precision for the tested transcendentals," with
formal cross-host confirmation deferred. That agreement is *coincidental and
outside our control*: it holds only for the sampled functions, only for
Emscripten's current musl version, and only on WASM's single target. Generalizing
host-Lua to every platform without pinning would multiply a foreign, unpinned
libm dependency across x86/arm64/riscv64 — each with its own `libm` — turning a
lucky coincidence into N independent determinism liabilities. Determinism is the
core contract (ADR-0007); netplay/replay/rewind cannot rest on "Emscripten's
libm happens to match, this release."

## Decision

Introduce a **softfloat-backed math seam** for host-Lua so its FP results
reproduce the Berkeley-SoftFloat reference by construction, and structure it so
native hardware can later be reclaimed *provably* where IEEE mandates it.

**One softfloat, one config, one reference.** Host-Lua links the *same* Berkeley
SoftFloat sources and config as rv32emu and the guest: RISC-V NaN
specialization, `SOFTFLOAT_ROUND_ODD`, round-nearest-even. Every FP definition
targets *softfloat f64 bit patterns* — the value the emulated legs and metal
already produce.

**Two zones, split by IEEE mandate.**

| | Zone 1 — basic (switchable) | Zone 2 — composite (soft-locked) |
|---|---|---|
| Ops | `+ − × ÷ √`, compares, int↔f64, `floor/ceil/trunc/round/rint`, `fmod`, `modf` | `sin cos tan asin acos atan atan2`, `exp/log` family, **`pow` (Lua `^`)**, `hypot cbrt sinh…`, **`strtod` / number-format** |
| Equal because | IEEE mandates correct rounding ⇒ native == softfloat, bit-for-bit (contraction off) | *not* mandated ⇒ equal only as "same musl algorithm, same softfloat primitives" |
| Host impl | mode A: explicit `blyt_f64_*` softfloat calls · mode B: native operators + `-ffp-contract=off` | **always** softfloat internals; the firm reference |
| Switch | `BLYT_HOSTLUA_FP=soft\|native` (macro bodies in `luaconf.h`) | never flips |

`^` sits in Zone 2 — it looks arithmetic but `pow` is transcendental, so it
always crosses the seam.

**The seam interface — `blyt_fpm.h`, doubles cross as `uint64_t` bit patterns:**

```c
uint64_t blyt_fpm_sin(uint64_t x);   /* cos tan exp log pow atan2 … */
uint64_t blyt_fpm_str2d(const char *s, char **end);
int      blyt_fpm_d2str(uint64_t x, char *buf, size_t n);
```

`uint64_t` passes in integer registers under every ABI, so Zone 2 can be
soft-float-lowered internally while its caller is compiled native — no ABI
collision. It is the `d2u`/`u2d` pattern already in `softfloat_builtins.c`,
promoted to a module boundary.

**This confines `-msoft-float`.** Basic-op mode A is *explicit function calls*
(`luai_numadd(a,b) → u2d(blyt_f64_add(d2u(a),d2u(b)))`) in normally-compiled TUs,
so Zone 1 never needs a soft-float *ABI* in either mode. Codegen lowering is
required only *inside* the Zone-2 kernels (musl's `sin` does raw `double` ops
that can't be macro-intercepted), sealed behind the `uint64_t` wall. The Lua
interpreter and the frontend link ABI are never soft-float.

**Where it lands:**
- `luaconf.h` — the `luai_num*` VM arithmetic macros become the Zone-1 switch;
  `luai_numpow` → `blyt_fpm_pow` unconditionally; `luai_nummod`/`numidiv`
  (`fmod`/`floor` + basic ops) stay Zone-1.
- `lmathlib.c` — `l_mathop(op)` (today `= op` on the f64 build) becomes
  `blyt_fpm_*` for transcendentals; `sqrt/floor/ceil/abs/fmod` stay Zone-1.
- `l_str2d` / `lua_number2str` → `blyt_fpm_str2d`/`d2str` (closes the `strtod`
  divergence).
- `blyt_fpm_soft.c` + the specific blyt-tech musl kernels it wraps — the only
  soft-float-lowered TUs; link the same softfloat sources/flags as rv32emu.

**Ship Mode A first; retrofit Mode B as a build-flag flip.** Mode A
(`BLYT_HOSTLUA_FP=soft`) is the firm, provable-by-construction baseline. Mode B
(`native`) reclaims native speed for Zone-1 only, where it is IEEE-provable — a
compile-flag change that cannot perturb Zone 2 because the seam is a bit-pattern
boundary. Mode A is kept as a permanent CI build-variant; Mode B is validated by
diffing frame hashes against it and host-vs-host across x86/arm64/riscv64/wasm.

**Invariants (most already hold on the guest):**
1. `-ffp-contract=off` on every participant — guest ✓, carts ✓; **add to both
   host builds**. Without matched contraction, "native == softfloat" is false.
2. One softfloat config everywhere: RISC-V NaN specialization + `SOFTFLOAT_ROUND_ODD`
   + round-nearest-even (✓ shared guest/WASM today).
3. Zone-2 kernels are blyt-tech musl's **generic-C** implementations, not any
   host's per-arch asm (`src/math/x86_64/…`) — the one new discipline in the
   host musl build.
4. **Hermetic:** host-Lua reaches *only* blyt-tech musl + `blyt_fpm` — never the
   host libc/libm.

## Consequences

- **Fixes the current unpinned WASM coupling.** Host-Lua transcendentals stop
  depending on Emscripten's libm; the WASM fast path and every emulated leg
  compute the same softfloat-defined value by construction, immune to emsdk
  bumps.
- **Makes host-Lua trustworthy as a primary execution path.** Without the seam,
  "host-Lua everywhere" would propagate a foreign-libm dependency to every
  platform. With it, the determinism contract holds structurally, so the
  broader Spike Y direction becomes safe to commit.
- **Preserves the native-speed option, provably.** Zone-1 basic ops (the bulk
  of game-logic FP — position/velocity integration) can move to native hardware
  later on an IEEE proof, not corpus luck. The full-soft Mode A is never thrown
  away — it is the CI oracle Mode B diffs against.
- **`-msoft-float` cost is bounded** to a handful of leaf math TUs behind the
  bit-pattern wall; the interpreter and frontend ABI are untouched, so per-arch
  soft-float codegen quality (x86-64 being the awkward one) is a non-issue for
  the hot path.
- **The RV32 Lua build stays** — as real-hardware execution (native, not
  emulated) and as the determinism reference the seam is pinned to. Retiring
  *emulated-RV32-Lua as a shipped runtime path* on non-RISC-V hosts is a
  separate decision (Spike Y follow-on); this ADR does not make it.
- **New surface:** `blyt_fpm.h`/`blyt_fpm_soft.c`, the `BLYT_HOSTLUA_FP` build
  axis, and a host-vs-host cross-arch parity gate.

## Implementation notes (2026-07-05)

The parity investigation (blyt#223 AC1) and Phase-A implementation surfaced two
facts that refine — not contradict — the mechanism above:

- **WASM cannot soft-float.** `clang`/`emcc` ignore `-msoft-float` on wasm32
  ("argument unused"); wasm always uses native `f64`. So the literal "Mode A =
  explicit `blyt_f64_*` calls / `-msoft-float` inside the Zone-2 kernels" cannot
  be realized on the only host-Lua target that exists today. It is not needed
  there: the AC1 parity gate proves native `f64` reproduces the Berkeley-SoftFloat
  reference **bit-for-bit** across an adversarial transcendental/NaN/subnormal
  corpus (WASM MVP has no scalar FMA, so contraction-off native == softfloat).
  **WASM therefore realizes Mode A as: pinned blyt-tech musl generic-C kernels
  compiled into the host-Lua VM behind the `blyt_fpm` seam + native `f64` +
  `-ffp-contract=off` + hermetic (no host libm).** The value delivered is the
  in-house, version-pinned libm — immune to emsdk bumps — not a bit change today.
  The `uint64_t` seam boundary is kept so a future native host-Lua leg (x86-64 /
  arm64, which *do* have FMA) can add true `-msoft-float` lowering inside the
  kernels without touching the interpreter ABI; that is where Mode A's soft-float
  form and the Mode-B native-Zone-1 flip become both feasible and necessary.

- **Phased Zone-2 landing.** Phase A routes the transcendental surface — `sin cos
  tan asin acos atan atan2 exp log log2 log10` (lmathlib) and `pow` (Lua `^`, via
  `luai_numpow`) — through `blyt_fpm` to the in-house musl kernels. `strtod` /
  number-format (`lua_str2number` / `lua_number2str`) is the other Zone-2 surface
  and is **Phase B**: pinning it to musl on WASM requires a *renamed* musl
  stdio/stdlib subset, because musl's `strtod`/`printf` would otherwise override
  the whole module's libc (unlike the math kernels, whose global override is
  benign). It remains behaviorally covered by the AC1 gate (`tostring`/`tonumber`
  round-trips) meanwhile.

- **AC4 (cross-arch host-vs-host) and AC2's FMA-contraction test** both require a
  native-player host-Lua build, which blyt#223 lists as out of scope. On a
  wasm-only host there is one arch and no FMA, so both **defer to the native
  host-Lua leg**. The committed WASM-vs-emulated parity gate (AC1) stands as the
  determinism guard until then.

- **Seam lives in the `blyt-tech/lua` fork.** The `l_mathop`/`luai_numpow`
  routing edits are in `lmathlib.c`/`llimits.h`, gated behind
  `BLYT_HOSTLUA_FP_SEAM` (defined only by the WASM host-Lua build, so the
  RV32-guest/emulated reference is byte-unchanged). Landing the seam therefore
  requires a new fork tag + pinned tarball; developed against a local
  `third_party/lua` override.

- **NaN sign/payload is NOT pinned by the seam — and does not need to be.** The
  "bit-identical by construction" guarantee is for *finite* results. An
  invalid-operation NaN (`sqrt(-1)`, `asin(2)`, `0/0`, `inf−inf`) has a
  hardware-/spec-nondeterministic sign bit on the host-Lua path: the WASM spec
  leaves an arithmetically-produced NaN's sign to the engine (x86-64 yields
  `0xfff8…`, arm64 yields the RISC-V-canonical `0x7ff8…`), and native x86-64 vs
  arm64 diverge the same way. This surfaced in the blyt#223 parity gate the moment
  it ran on x86-64 CI (arm-only local runs matched the reference by luck). It is
  **not** a contract divergence: **ADR-0010** canonicalizes NaN at the
  state-buffer boundary (`blyt_canon_f64` → `0x7ff8000000000000`), so the
  *observable* value is identical across every host. The determinism guarantee for
  NaN therefore rests on ADR-0010 boundary canonicalization, not on the FP unit or
  the seam kernels — the seam pins the transcendental *values*, ADR-0010 pins the
  NaN *representation*. (The parity gate canonicalizes NaN before hashing to test
  the contract-relevant value; `tostring(NaN)` — which could stringify to `-nan`
  on a sign-set host — is governed by the Phase-B number-format work.)

## Related ADRs

- ADR-0007 (structural determinism) — the contract this protects.
- ADR-0132 (ilp32d / hardware doubles) — establishes SoftFloat as the FP
  reference on emulated/WASM paths; its Spike U Stage 5 note is the coincidental
  transcendental agreement this ADR pins.
- ADR-0005 (numeric model) — i32/f64 first-class types the seam operates on.
- ADR-0130 (ECALL-bridged Lua C API) — the host-Lua hybrid path that must obey
  the same reference; ADR-0066 (Lua 5.4/5.5) — the VM whose `luaconf.h`/`lmathlib`
  carry the seam.
- ADR-0082 (emulator MIPS cap) — Spike A/B/Y throughput context motivating
  host-Lua on aarch64 handhelds.
