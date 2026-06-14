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
- [ ] **Stage 2 — `ilp32d` ABI witness.** `double` in `fa0` →
  `3fe0000000000000` on arm64+amd64; Spike Q `__extendsfdf2` `"0.0"` resolved.
- [ ] **Stage 3 — Lua int32 + float64.** `LUA_INT_INT` + `LUA_FLOAT_DOUBLE`
  across cart `build.rs` + `frontends/wasm/CMakeLists.txt`. *Gate:* Lua digests
  byte-equal rv32 (arm64/amd64) + WASM Lua-direct.
- [ ] **Stage 4 — save-state across widened FP file.** Snapshot absorbs 64-bit
  FP regs; bump format/version; regen digests. Spike K cross-host round-trip.
- [ ] **Stage 5 — determinism + libm parity.** Basic-op byte-equal across 3
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
  deferred (no riscof/Sail locally). Next: Stage 2 (formal ABI witness:
  `fa0=3fe0000000000000`) then Stage 3 (Lua `LUA_FLOAT_DOUBLE`).
