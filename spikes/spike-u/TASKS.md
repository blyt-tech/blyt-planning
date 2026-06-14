# Spike U — `ilp32d` / hardware doubles — task tracker

Brief: [`docs/design/spike-u-hardware-doubles.md`](../../docs/design/spike-u-hardware-doubles.md).

**Deliverable:** a functional `spike-u-ilp32d` branch in the `blyt` repo (plus a
`spike-u-rv32d` branch in the `blyt-tech/rv32emu` submodule) that builds and runs
the double-enabled stack on the emulated and WASM paths.

## Working branches

Per blyt's CLAUDE.md, all blyt work lives in a **`wtp`-managed worktree** under
`../blyt-worktrees/`, not the main checkout.

| Repo | Path | Branch | Base |
|------|------|--------|------|
| blyt (worktree) | `~/code/blyt-worktrees/spike-u-ilp32d` | `spike-u-ilp32d` | `main` |
| rv32emu (submodule in worktree) | `…/spike-u-ilp32d/third_party/rv32emu` | `spike-u-rv32d` | `4c3ab7c` (blyt-pinned, = blyt-patches +1) |

## Current-state notes (recon)

- ABI is centralised in blyt cmake: `cmake/blyt_rv32_toolchain.cmake:63`,
  `cmake/blyt_guest_libs.cmake` (lines ~50, ~167, ~876, ~885),
  `cmake/blyt_sdk.cmake` (libcxx flags), `CMakeLists.txt:259` (`LUA_32BITS`).
- Rust cart target: upstream `riscv32imafc-unknown-none-elf` (no `D`; needs a
  custom JSON target). Some `-mabi=ilp32d` PIE experiments already recorded in
  `.claude/settings.local.json` (metal/PIE groundwork).
- rv32emu submodule has Berkeley SoftFloat vendored at `src/softfloat/`
  (`source/` has `f64_*` already), `src/softfp.h`. `feature.h`: `EXT_F=1`,
  `EXT_A=1`, no `EXT_D`. FP register file is 32-bit (`softfp.h` `FMASK_*`,
  `calc_fclass`/`is_nan` all `uint32_t`).

## Stages (gates from the brief)

- [x] **Stage 0 — toolchain + `ilp32d` sysroot.** ✅ PASS. Custom
  `riscv32imafdc-blyt-none-elf` target JSON (`devtool/targets/`), wired via
  `RUST_TARGET_PATH` materialised into each cart's `--target-dir`; cmake
  march/abi flipped `rv32imafc/ilp32f` → `rv32imafdc/ilp32d`; musl + guest libs
  rebuilt. *Gate met:* `hello-rust` and `hello-c` build + link end-to-end with
  no float-ABI mismatch; all four guest libs + both cart ELFs report
  `Flags 0x5` (RVC + **double-float ABI**); arch attr `…f2p2_d2p2_c…zcd…`;
  Rust codegen genuinely `ilp32d` (build-std `compiler_builtins` object also
  `0x5`, not just the C importer).
  - **Required:** custom JSON targets need `-Zunstable-options` in RUSTFLAGS
    even on nightly — added to `cart_rustflags`.
  - **Caveat (not blocking Stage 0):** `tests/integration/tests/common/mod.rs`
    gates on `rustup target list` containing `riscv32imafc-unknown-none-elf`;
    custom JSON targets don't appear there. Update that check before relying on
    the integration suite (Stage 2+).
  - **Not yet rebuilt:** libc++ (`blyt_sdk.cmake`, flags flipped) — only needed
    for C++ carts (`hello-cpp`); rebuild when exercising that path.
- [x] **Stage 1 — `D` in rv32emu fork.** ✅ FUNCTIONAL (formal arch-test pending,
  see caveat). Commits on `spike-u-rv32d`: `57a9f52` (flag + f64 helpers),
  `70e8807` (64-bit FP file + NaN-box F ops), `244cfb3` (RV32D scalar +
  compressed). Host loader + submodule bump: blyt `a885912`.
  - Register file widened to `softfloat_float64_t F[32]`; `get_f32`/`set_f32`
    (NaN-box validate/box), `get_f32_bits` (raw FSW/FMV.X.W), `get_f64`/`set_f64`.
  - All 26 F ops + 4 compressed-F ops route through the accessors (fixed a latent
    NaN-box bug in C.FLW/C.FLWSP exposed by the widening).
  - 26 scalar D ops + 4 compressed D ops (C.FLD/C.FSD/C.FLDSP/C.FSDSP) — decode
    (funct3/funct7/fmt dispatch + rvc_jump_table), interpreter (SoftFloat
    `f64_*`), constopt. Host cart loader now expects `EF_RISCV_FLOAT_ABI_DOUBLE`.
  - *Validation (no riscof/Sail in this env — see caveat):* a double-exercising
    cart (fld/fsd incl. compressed, +−×÷, fsgnj family, flt.d, fcvt.d.w/w.d/s.d)
    digests **byte-identical to a host oracle** (correctly-rounded IEEE) at
    frames 10/20/30 — `dprobe` 2018961092 / -1899182279 / -1505155154; the
    single-precision f-probe is unchanged from the pre-D baseline; hello carts
    run; the double cart is deterministic under `--reset-every-frame`
    (save/restore over the widened FP file — early Stage 4 signal).
  - **Caveat:** the formal `riscv-arch-test` D suite needs RISCOF + a Sail
    reference sim + RISC-V GCC — impractical to stand up here. Validation is via
    host-oracle digest equality + F-regression instead. Run riscof on Linux/CI
    before calling the D extension production-conformant.
  - **Still to confirm:** misa/ISA-string advertises `d`; JIT stays off
    (`RV32_FEATURE_JIT=0`, FP forces interp — built clean without JIT D entries);
    gdbstub FP-register width on the debug path.

  **Implementation map (recon done — files in the worktree's
  `third_party/rv32emu`, branch `spike-u-rv32d`):**
  1. `src/feature.h` — add `RV32_FEATURE_EXT_D` (default 1) + `RV32_HAS(EXT_D)`
     gating, mirroring `EXT_F` (lines 44-45).
  2. `src/riscv.h:293` + `src/riscv_private.h:285` — FP register file is
     `riscv_float_t F[32]`, `riscv_float_t = softfloat_float32_t`
     (`struct {uint32_t v;}`). **Widen to 64-bit** (`softfloat_float64_t`
     storage). This is the blast-radius change; the spike warns wrong NaN-boxing
     = silent wrong answers. Add accessors (NaN-box on F write / unbox on read).
  3. `src/softfp.h` — add `calc_fclass_d`, `is_nan_d`, `FMASK_*_D`, `RV_NAN_D`
     (64-bit siblings of lines 16-87). `set_fflag` / `set_rounding_mode` are
     width-agnostic — reuse as-is.
  4. `~87` `rv->F[` sites (mostly `src/rv32_template.c`, e.g. fadds at ~1098):
     NaN-box single-precision results into the high 32 bits; check boxing on
     read. **Correctness-critical step.**
  5. `src/decode.h` — add D entries to `RV_INSN_LIST` (the `_(name, …, ENC(…))`
     macro; F entries at lines 172-197): `fld, fsd, faddd, fsubd, fmuld, fdivd,
     fsqrtd, fmind, fmaxd, fmaddd/fmsubd/fnmsubd/fnmaddd, fcvtds/fcvtsd,
     fcvtwd/fcvtwud/fcvtdw/fcvtdwu, feqd/fltd/fled, fclassd,
     fsgnjd/fsgnjnd/fsgnjxd`. No new ENC fields — rs3/rm already exist. No
     `fmv.x.d`/`fmv.d.x` on RV32 (moves go through memory).
  6. `src/decode.c` — decode OP-FP fmt=01 + LOAD-FP/STORE-FP funct3=011 (fld/fsd)
     + fmadd.d family fmt=01.
  7. `src/rv32_template.c` — one `RVOP` body per D op, wired to Berkeley
     SoftFloat `f64_*` (vendored at `src/softfloat`, already compiled in). Direct
     copy of the F bodies with `f64` calls.
  8. `src/rv32_constopt.c` — D ops as pass-through mirrors of F (no FP const-fold).
  9. arch-test: wire `riscv-arch-test` `D` suite (rv32emu `Makefile` arch-test
     target). JIT off by default + FP forces interp fallback → no T1C/T2C work
     expected (confirm: grep `rv32_jit.c`/`t2c_template.c` for an existing
     `fadd.s`).
  - **Validation cadence:** rebuild emulator + run `F` arch-test after the
    widening (step 4) *before* adding any D op, to prove NaN-boxing didn't
    regress single-precision; then add D ops and run the `D` suite.
  - **Note:** rv32emu is compiled into the blyt host runtime *and* built
    standalone (its own `Makefile`) — use the standalone build + its arch-test
    harness for fast Stage 1 iteration; the blyt-side rebuild is the integration
    check.
- [~] **Stage 2 — `ilp32d` ABI witness.** ✅ arm64 PASS. A `double` 0.5 crossing
  a `noinline` call boundary lands as `3fe0000000000000`; disassembly confirms
  the `double` arg is passed in `fa0` (`fsd fa0,…`) per ilp32d, not split across
  GPRs. Probe cart + host oracle saved in `probe-cart/`. **Deferred:** amd64
  side (cross-host) → folds into Stage 5 (needs Docker); Spike Q `__extendsfdf2`
  `"0.0"`→`"0.5"` is Lua's float→double number-string path → folds into Stage 3.
- [x] **Stage 3 — Lua int32 + float64.** ✅ PASS (emulated path). Number→string
  bug FIXED (`342c9e9`): the vendored Berkeley SoftFloat was built without
  `LITTLEENDIAN`, so `float128_t` used big-endian word order (high 64 bits at
  `v[0]`) on this LE target. f32/f64 are single-word (unaffected), but quad
  (`tf`) values silently mismatched the compiler's IEEE binary128 layout (high
  at `v[1]`) — SoftFloat↔SoftFloat roundtrips worked, but reading a
  compiler-materialised quad (the long-double constants in musl's `vfprintf`
  `fmt_fp`) swapped hi/lo → exponent read 0 → all float printf = 0. Fix: define
  `LITTLEENDIAN` in the generated softfloat `platform.h`.
  - Verified after fix: C `snprintf("%g",0.5)`→`0.5`; Lua `tostring(0.5)`→`"0.5"`
    (**Spike Q `__extendsfdf2` symptom resolved**), `tostring(1/3)`→
    `0.33333333333333331`, `math.type` float/integer, `%.14g` precision correct;
    Lua double cart deterministic incl. `--reset-every-frame`; hello/hello-rust/
    hello-c all run, no regression.
  - **Remaining for full gate:** cross-path digest equality (rv32 amd64 + WASM
    Lua-direct) — folds into Stage 5 (Docker/WASM). Emulated arm64 path complete.
  - *(superseded notes below kept for the diagnosis trail)*

- [~] ~~Stage 3 — number→string OPEN~~ (resolved, see above).
  Commits: lua `28bfe00` (luaconf `BLYT_LUA_I32_F64` branch), blyt `1428bc7`
  (build define swap + `__floatundidf` builtin + lua gitlink bump).
  - `LUA_32BITS=1` → `BLYT_LUA_I32_F64=1` across cart `build.rs`, guest-libs
    cmake, `blyt-luac`, `frontends/wasm`. lua_Number = double, lua_Integer = i32.
  - Added `__floatundidf` (u64→double) to `softfloat_builtins.c` (needed by the
    Lua VM under double).
  - **✅ Verified correct:** `math.floor(0.5*1000)=500`, `(1/3)*1e6 floor
    =333333`, `0.5==0.5`/`0.5<1.0` true, harmonic H₁₀ floor=2928;
    `math.type(0.5)=float`, `math.type(3)=integer`. Lua + double arithmetic,
    comparisons, and the int/float subtype split all work on the emulated path.
  - **🔴 OPEN — number→string formatting** (`tostring(float)`/`%g`/`%f`→0; Q
    `__extendsfdf2` symptom). Affects C carts too (`snprintf("%g",0.5)`→`"0"`),
    so it's **not Lua-specific** and predates Stage 3 (Stage-0 ilp32d musl).
    **Root cause (verified by probes):** musl's `vfprintf`/`fmt_fp` formats ALL
    floats via 128-bit `long double` (binary128 on RISC-V; `%f`/`%g` promote
    double→long double). The guest's **quad (`tf`) soft-float path is broken for
    quad *constants*:** a runtime-built quad converts correctly
    (`(long double)12345 → u32 12345`, `f64 40c81c80…`; volatile too), but a
    quad *constant* converts to 0 (`(double)2.0L → 0`) even though `2.0L` is
    materialized inline with correct bytes `[0,0,0,0x40000000]`. `fmt_fp` mixes
    the runtime value with quad constants (`2/LDBL_EPSILON`, `0x1p120`, etc.) →
    those read 0/inf → digit extraction emits all zeros.
    **Ruled out:** `va_arg` (cart va_arg(double) works; ints read in-position),
    variadic ABI, quad compares (`__gttf2` ok), and quad arithmetic
    (roundtrip `0.5+1→3ff8…` ok). musl `vfprintf.c` is stock upstream;
    `arch/riscv32/bits/float.h` correctly declares binary128 (`LDBL_MANT_DIG
    113`). The quad builtins live in `runtime/guest/src/libblyt32lua/
    softfloat_builtins.c` (by-pointer ABI, Berkeley SoftFloat f128).
    **Next:** disasm the quad-constant→`__trunctfdf2`/`__fixunstfsi` call to see
    why the inline-materialized constant reaches the builtin as 0 (codegen of
    128-bit constant store vs the by-ref builtin ABI), OR sidestep musl's
    long-double float path entirely. Probe carts: `probe-cart/` (C),
    `probe-cart-lua/` (Lua).
  - **Open design question (per Tom):** the determinism work that made Lua-C and
    Rust float output identical may bear on the right fix — e.g. whether to make
    `long double == double` so musl printf avoids the quad path, vs fixing the
    quad-constant builtin path.
  - **Gate not yet met** (cross-path digests) — blocked for any test that
    stringifies a float; pure-numeric Lua digests would pass.
- [x] **Stage 4 — save-state across widened FP file.** ✅ PASS (emulated).
  **Non-issue by architecture: no blyt save/restore path serializes `F[]`** —
  `blyt_reset_every_frame_cycle` snapshots the *state buffers* + preserves only
  the 32 integer regs + `csr_fcsr`; the ADR-0130 bridged-call snapshot likewise.
  So the 64-bit widening needs no snapshot-format change or version bump.
  State buffers have no f64 type (max f32) → persisted doubles truncate to f32
  deterministically (f32 SET canonicalises NaN, ADR-0010). Verified: an f64
  Basel-sum-derived value persists **byte-identically** under
  `--reset-every-frame` (`1549767731`/`1596163243`/`1612150117`), matching a
  host IEEE oracle. Cross-host (amd64) state-buffer equality = Spike K's result
  (unaffected by FP widening); needs Docker → Stage 5.
- [~] **Stage 5 — determinism + libm parity.** ✅ WASM Lua-direct PASS;
  amd64-emulated leg pending Docker. A Lua cart doing basic f64 ops *and*
  transcendentals (sin/cos/exp/log/pow, `x^1.5`) prints `%.17g` (uniquely
  identifies a double). **rv32-softfloat (arm64) and WASM-Lua-direct produce
  byte-identical output on every overlapping frame** — including the
  transcendentals, so the libm-parity witness PASSES (emscripten musl libm ==
  blyt-tech musl libm to full double precision for these inputs; no remediation
  needed). Basic-op parity holds (correctly-rounded IEEE). Probe: `probe-cart-lua/`.
  - **Remaining:** rv32 amd64 leg — the emulator is pure Berkeley SoftFloat for
    all FP (incl. D), so arm64↔amd64 is host-independent by construction (Spike
    D/K established this generally); a Docker `test-linux-docker` run with the
    D-cart is the formal confirmation. (Not run here — heavy; near-certain.)

- ⚙️ **Follow-ups surfaced by Spike U (per Tom):**
  - **Bridge-call FP snapshot (DONE, blyt `536b27d`).** ADR-0130's bridged-call
    register snapshot saved only the 32 integer regs + `fcsr`; with RV32D a
    bridged wrapper can hold live values in callee-saved FP regs (fs0–fs11), so
    the error-unwind would leave `F[]` corrupted. Now snapshots/restores the
    full 64-bit FP file. (Needs a targeted hybrid-Lua+C error-path test on the
    WASM target to exercise it.)
  - **f64 state-buffer field type (DONE — C/Lua/Rust).** Commits blyt `b8da371`
    (C path) + `eb9a93e` (Lua/Rust + lua_Number fix). Added type tag `8=f64`
    across config.rs / ecall.h / state_buffer.c; a dedicated 64-bit value path
    (`blyt_state_set64/get64`, `BUF_OP_GET_F64/SET_F64` with the value as a
    lo/hi register pair, since the scalar buf ABI is 32-bit); guest C API
    `blyt_buffer_set_f64/get_f64`; cart_load import allowlist; Lua
    `blyt.buf.get_f64/set_f64` + generated-proxy f64 branches; Rust
    `buffer::get_f64/set_f64`. **Verified:** C, Lua, and Rust carts store/read an
    f64 Basel sum at full double precision (`1.5497677311665408…`, matches host
    oracle); C+Lua persist byte-identically across `--reset-every-frame`.
    - **Bonus fix (latent Stage-3 bug):** `blyt_lua_internal.h` (cart-side
      minimal Lua API decl) still hardcoded `typedef float lua_Number`. Under
      double Lua + RV32D, any hybrid-Lua+C glue's `lua_tonumber` read a double
      returned in `fa0` as a NaN-boxed single → canonical NaN. Fixed to
      `double`. This affected all hybrid Lua+C carts, not just f64 buffers.
    - **Remaining:** WASM **bridged** f64 path (hybrid Lua+C on WASM — the
      BLYT_LUA_OP bridge doesn't yet carry a 64-bit buffer value; pure-Lua on
      WASM uses host state buffers, already covered); `.fbs` doc comment;
      integration-suite tests across the 3 legs.

- [ ] ~~Stage 5 (orig)~~ Basic-op byte-equal across 3
  paths; transcendental rv32-softfloat vs WASM characterised (remediate libm
  source if it diverges).
- [ ] **Stage 6 — metal confirmation (optional, hardware-gated).** Deferred
  unless rv64 ILP32 substrate is on hand.

## Progress log

- 2026-06-14 — Spike kicked off. Branches created in both repos; tracker added.
  Recon complete (see current-state notes). Starting Stage 0.
- 2026-06-14 — **Stage 0 PASS.** ilp32d toolchain + sysroot done; both Rust and
  C carts link with double-float ABI, no mismatch. Committed on `spike-u-ilp32d`.
- 2026-06-14 — **Process fix:** Stage 0 was initially committed on a branch in
  the *main* `~/code/blyt` checkout, against blyt's worktree workflow. Relocated:
  reset main checkout to `main`, created `wtp` worktree at
  `~/code/blyt-worktrees/spike-u-ilp32d` (carries commit `9578d7b`). Stage 0
  re-verified there: full build chain (configure/build/sdk/devtool) green;
  `hello-rust` links double-float ABI (`0x5`).
- 2026-06-14 — **Stage 1 started** (branch `spike-u-rv32d`, commit `57a9f52`):
  additive scaffolding — `EXT_D` flag + `f64` softfp helpers.
- 2026-06-14 — **Stage 1 FUNCTIONAL.** FP register file widened to 64-bit +
  NaN-boxing (`70e8807`, F-probe byte-identical pre/post); full RV32D scalar +
  compressed implemented (`244cfb3`); host loader accepts ILP32D + submodule
  bumped (`a885912`). Double cart digests match a host IEEE oracle exactly;
  F unchanged; deterministic under reset-every-frame. Formal riscv-arch-test
  deferred (no riscof/Sail locally).
- 2026-06-14 — **Stage 2 ABI witness PASS (arm64).** `double 0.5 →
  3fe0000000000000` across a call boundary; disasm confirms `fa0` passing.
  Probe cart + host oracle archived under `probe-cart/`.
- 2026-06-14 — **Stage 3 PASS (emulated).** Lua flipped to int32+float64; all
  values/arithmetic/comparisons/`math.type` correct. The number→string bug was
  root-caused (not musl, not va_arg) to a **SoftFloat `float128_t` endianness
  word-order mismatch** — guest softfloat lacked `LITTLEENDIAN`, so quad word
  order was big-endian and mismatched the compiler's binary128 when musl's
  long-double `fmt_fp` read quad constants. One-line fix (`LITTLEENDIAN` in
  softfloat platform.h, `342c9e9`). `tostring(0.5)="0.5"` (Q symptom resolved),
  precision/`math.type` correct, deterministic. Cross-path (amd64/WASM) digest
  gate folds into Stage 5.
