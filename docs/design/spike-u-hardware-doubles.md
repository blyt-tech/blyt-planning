# Spike U — Hardware doubles end-to-end: `ilp32d` and RV32D in rv32emu

**Status:** Not started — proposed.

> **This spike is not backed by an existing ADR.** Every other spike in this
> series validates a decision already recorded in the ADR log and leans on those
> ADRs for context. This one runs *ahead* of the decision: it exists to produce
> the evidence (and a functional branch) that a future ADR would be written
> against. Because there is no backing ADR, this document carries its own
> context in full so it can be executed in a clean session with no prior
> reading. If it passes, it feeds proposals to amend ADR-0001 (ISA),
> ADR-0005 (numeric model), and ADR-0108 (Rust target), plus a new ADR for the
> `ilp32d` ABI choice.

---

## Self-contained context (read first)

The console today is a 32-bit machine with **single-precision-only** hardware
floating point:

- **ISA / ABI.** Carts target `riscv32imafc-unknown-none-elf` with the
  `ilp32f` hard-float ABI (single-precision floats in FP registers, `FLEN=32`).
  This was validated in Spike O: the ABI witness `set_volume(0, 0.5f)` lands as
  `vol=3f000000` in an FP register, and `readelf -A` reports
  `rv32i…f2p2_c2p0` + `EF_RISCV_FLOAT_ABI_SINGLE`. There is **no** `D`
  extension and no upstream Rust target with `D` for RV32.
- **Numeric model.** ADR-0005 declares an `i32`/`f32`-everywhere model. `f64`
  the *type* still works — `double`/`f64` lowers to soft-float libcalls
  (`__adddf3`, `__muldf3`, …) executing as integer instructions — but there is
  no hardware double path, and no part of the stack passes a `double` in
  registers. The concrete symptom is **Spike Q's `__extendsfdf2` finding**:
  `libblyt32lua.so`'s double-precision stubs return `0.0`, so Lua's
  number→string path (which casts `float`→`double`) renders float results as
  `"0.0"`. That stub exists *because* RV32IMAFC has no `D`.
- **Determinism.** All FP is routed through **Berkeley SoftFloat** so results
  are bit-identical across hosts. In the rv32emu fork
  (`blyt-tech/rv32emu`) softfloat is vendored at `src/softfloat`
  (`ucb-bar/berkeley-softfloat-3`) and the `F` extension is implemented against
  it; the four-way arm64/amd64 × Rust/C digest equality in Spikes O/D/K depends
  on this. Berkeley SoftFloat-3 already ships the **complete `f64` kernel set**
  (`f64_add`, `f64_mul`, `f64_div`, `f64_sqrt`, `f64_to_f32`, `f32_to_f64`, the
  `f64` compares) — they are compiled in today but wired to no opcodes.
- **Lua.** Built with `LUA_32BITS=1` (= `LUA_INT_INT` + `LUA_FLOAT_FLOAT`):
  32-bit `lua_Integer`, 32-bit `lua_Number`. The cart `build.rs` propagates
  `LUA_32BITS=1` to **every** C library so the Lua C API sees one
  `lua_Number` width. The WASM frontend
  (`frontends/wasm/CMakeLists.txt`) sets the same define and additionally pins
  `luai_makeseed()=0x424C5954u` for cross-path `pairs()`/`sort`/`random` order.
- **Execution paths.** (1) **Emulated** — cart RV32 ELF runs inside rv32emu
  (softfloat). (2) **WASM Lua-direct** — the host-side Lua VM is compiled to
  WASM and runs Lua carts directly; rv32emu is also compiled into the same WASM
  module for native/C/Rust carts and the ECALL-bridge. (3) **Metal** — rv32
  user processes run in compat mode on **rv64 silicon**; the c-sky ILP32 kernel
  from Spike S forces PIE carts (AT_PHDR mishandling for ET_EXEC).
- **build-std is already in play.** Carts compile `core`/`alloc` from source
  (`-Z build-std=core,alloc`, pinned nightly + `rust-src`) because the cart is
  PIE → everything must be PIC, and because the rebuilt `compiler_builtins`
  supplies the `f64` soft-float intrinsics core needs. A custom target slots
  into this with near-zero marginal toolchain cost.

**The bet this spike tests.** Because the metal target is rv64 silicon running
rv32 in compat mode, the hardware **physically has `D`** (RV64GC cores have
64-bit FP registers and the `D` instructions; the compat kernel already
saves/restores 64-bit FP context). The `ilp32f` choice was a *definition*
decision, not a hardware constraint — and `ilp32f` is the niche float ABI,
whereas `rv32imafdc`/`ilp32d` is the standard rv32-Linux baseline. So enabling
hardware doubles is reachable end-to-end, and the only piece that does not
already exist is `D` support in the rv32emu fork (the emulated and WASM paths).

---

## Motivation — why `D` is on the table

This spike is **not driven by a functional requirement.** No cart, API, or
feature needs hardware doubles; `f64` already works as soft-float today. `D` is
worth revisiting because the f32-everywhere model (ADR-0005) turns out to be an
**early default that was never driven by a particular need** — and three things
converged to make that both visible and cheap to fix now:

1. **A language-onboarding probe surfaced it.** The omission never broke for
   Lua, Rust, or C, because all three default to (or trivially pin) a 32-bit
   float — none of them pushed on the assumption. Swift was the first
   prospective language whose *defaults* lean on `Double`, and the friction it
   created is what made the f32-only choice look arbitrary rather than designed
   (see the companion language-generality probe, Spike V). This is the classic
   architecture-probe payoff: a baked-in assumption that no existing consumer
   happened to stress, found with a throwaway cart rather than a shipped one.

2. **The original justification no longer load-bears.** The historical reason to
   avoid hardware FP is non-determinism — specifically x87 80-bit excess
   precision rounding unpredictably across machines. Berkeley SoftFloat already
   removes that (bit-identical IEEE results on any host), so the determinism
   argument that partly justified f32-everywhere was solved another way. The
   metal target compounds the point: it is rv64 silicon running rv32 in compat
   mode, so the hardware **physically has `D`** regardless. `ilp32f` was a
   definition choice, not a constraint — and it is the *niche* float ABI, where
   `ilp32d` is the standard rv32-Linux baseline.

3. **The window to change it cheaply is open now.** The project is pre-cart and
   pre-1.0: nothing external depends on the ABI, numeric model, or snapshot
   format, so this is a rebuild-and-regenerate exercise, not a migration. And the
   toolchain is already in `build-std` territory for unrelated reasons (PIE/PIC),
   so a custom `ilp32d` target adds near-zero marginal surface.

So the question is not "do we need doubles." It is: the absence is an unexamined
default, a probe exposed it, the original rationale is gone, and the cheap window
is open — **is enabling it actually reachable end-to-end, and what does it
cost?** Whether the project then *adopts* doubles remains the ADR-level identity
decision (see *What this spike does and does not decide*).

---

## The question

Can the whole stack be moved to **`rv32imafdc` / `ilp32d` with hardware
double-precision**, end-to-end, on all three execution paths, while preserving
the cross-host bit-determinism the project depends on — and specifically:

- Can `D` be added to the `blyt-tech/rv32emu` fork (decode + interpreter +
  64-bit FP register file + NaN-boxing), wired to the already-vendored Berkeley
  SoftFloat `f64` kernels, such that `D` instructions execute correctly and
  deterministically on both the native-emulator and WASM builds?
- Does a custom `riscv32imafdc` / `ilp32d` Rust target (custom JSON +
  `build-std`) link cleanly against an `ilp32d`-rebuilt sysroot, with a
  `double` crossing the `extern "C"` boundary in FP registers per the psABI?
- Does flipping Lua to `LUA_INT_INT` + `LUA_FLOAT_DOUBLE` (int32 + float64)
  across **every** build (guest RV32, host WASM) keep cross-path digests
  byte-equal, with the FP-register-file widening absorbed by the save-state
  format?

**Deliverable beyond the answer:** a functional `blyt` branch
(`spike-u-ilp32d`) that builds and runs the double-enabled stack on the
emulated and WASM paths and could be polished and merged if the decision is
taken. The branch is the artefact; this document is the brief.

---

## Why this is a risk

1. **rv32emu `D` is new code, and the FP register file widening has blast
   radius.** Today the fork's FP file is 32-bit (`softfp.h` treats `f` as
   `uint32_t`; the `FMASK_*` constants are single-precision layout). RV32D
   mandates **64-bit** FP registers over 32-bit GPRs. Widening the file forces
   every existing `F` op to **NaN-box** single results into the high bits (and
   unbox on read) — get this wrong and you get silent wrong answers, not
   crashes — and it changes the **snapshot format** (save-state, hot-reload,
   the cross-host determinism digests all serialise FP state). The numerics are
   free (softfloat `f64` is already compiled in); the register-width change is
   the part that touches the guarantees the project is built on.

2. **ABI consistency is unforgiving.** RISC-V encodes the float ABI in the ELF
   header; lld/bfd refuse to link objects with mismatched float ABIs. Moving to
   `ilp32d` means **the entire sysroot** — `blyt-tech/musl`, `libblytcommon.so`,
   `libblytc.so`, `libblyt32.so`, `libblyt32lua.so` — must be rebuilt `ilp32d`,
   and the Rust side must emit `ilp32d` codegen (not just pass `-mabi` to the
   Clang importer). A single stale `ilp32f` object is an *ABI* corruption (a
   `double` read as a single out of one register), not a rounding bug.

3. **Lua's `lua_Number` width is baked into every TU.** `LUA_FLOAT_DOUBLE`
   changes the signature of every Lua C API entry point. The guest RV32 Lua VM
   and the host WASM Lua VM are **separate compilations**; if their `luaconf.h`
   drifts, dev-mode and hardware disagree at the language level (integer/float
   subtype, `math.type`, number formatting `%.7g`→`%.14g`, `math.random`
   representation). The fixed hash seed handles ordering, not float width.

4. **Determinism must survive on three paths with two FP implementations.**
   The emulated/WASM paths execute `D` via softfloat; metal executes hardware
   `D`. IEEE basic ops (`+ − × ÷ √`, fma) are correctly-rounded on all of
   them, so they agree — but transcendentals come from **two different musl
   lineages** (emscripten's bundled musl libm on WASM vs `blyt-tech/musl` on
   RV32), which can disagree in the last ulp. Widening to `f64` widens the
   surface where that can show. This spike must witness it, not assume it.

---

## What to build

The spike is staged so each stage has an independent gate. Stages 0–2 prove the
toolchain and the emulator; Stage 3 carries it into Lua; Stage 4 proves
save-state survives the register widening; Stage 5 is the determinism
cross-check; Stage 6 is the optional metal confirmation.

### Stage 0 — Toolchain and `ilp32d` sysroot

- **Custom Rust target.** There is no upstream `riscv32imafdc` target, so write
  a target spec JSON (`riscv32imafdc-blyt-none-elf.json`) with
  `"llvm-target": "riscv32"`, `"features": "+m,+a,+f,+d,+c"`,
  `"llvm-abiname": "ilp32d"`, `"relocation-model": "pic"`, `panic-strategy
  abort`, matching the existing cart flags otherwise. Build with the existing
  `-Z build-std=core,alloc` machinery (this is why being already in build-std
  territory matters — no new toolchain surface).
- **Rebuild the sysroot `ilp32d`.** Rebuild `blyt-tech/musl` and the guest
  libraries (`libblytcommon.so`, `libblytc.so`, `libblyt32.so`,
  `libblyt32lua.so` / `-bridge`) with `-march=rv32imafdc -mabi=ilp32d`. Confirm
  every output's ELF header reports `EF_RISCV_FLOAT_ABI_DOUBLE` and
  `rv32…f…d…` via `readelf -h -A`.
- **Verify Rust codegen, not just the importer.** Confirm the emitted Rust
  object is genuinely `ilp32d` (not `ilp32f` with `-Xcc` only affecting the C
  importer) by checking the object's ELF float-ABI flag with `readelf -h`.

**Gate:** musl + all guest libs + a trivial Rust object all link into one ELF
with no float-ABI mismatch error; `readelf -A` on the final ELF shows
`rv32i…m…a…f…d…c…` and `EF_RISCV_FLOAT_ABI_DOUBLE`.

### Stage 1 — `D` in the rv32emu fork

Add the `D` extension to `blyt-tech/rv32emu`, mirroring the existing `F`
implementation in each path it touches:

- **Feature flag.** Add `RV32_FEATURE_EXT_D` to `feature.h` + Kconfig, gated by
  the existing `RV32_HAS(EXT_D)` macro pattern. (`EXT_A` is already on; the JIT
  is off by default — `RV32_FEATURE_JIT=0` — and FP forces interpreter
  fallback, so **no T1C/T2C work is expected**. Confirm by grepping
  `rv32_jit.c`/`t2c_template.c` for an existing `fadd.s` before assuming.)
- **Register file.** Widen the FP register file from 32-bit to 64-bit. Add the
  64-bit siblings of the single-precision helpers in `softfp.h`
  (`calc_fclass_d`, `is_nan_d`, the `FMASK_*_D` / `RV_NAN_D` constants). The
  rounding-mode and exception-flag helpers (`set_rounding_mode`, `set_fflag`)
  are width-agnostic and reused as-is.
- **NaN-boxing.** Make every existing `F` op NaN-box its single result into the
  upper bits and check the boxing on read. This is the correctness-critical
  step.
- **Decoder.** Add the `D` opcodes to `decode.c`/`decode.h`. The decoder
  already carries `rs3` + `rm` for the existing `fmadd.s` family, so **no new
  decode fields** — just opcode entries for: `fld`, `fsd`, `fadd.d`, `fsub.d`,
  `fmul.d`, `fdiv.d`, `fsqrt.d`, `fmin.d`, `fmax.d`, the `fmadd.d`/`fmsub.d`/
  `fnmadd.d`/`fnmsub.d` family, `fcvt.d.s`, `fcvt.s.d`, `fcvt.w.d`,
  `fcvt.wu.d`, `fcvt.d.w`, `fcvt.d.wu`, `feq.d`, `flt.d`, `fle.d`, `fclass.d`,
  `fsgnj.d`/`fsgnjn.d`/`fsgnjx.d`. (No `fmv.x.d`/`fmv.d.x` on RV32 — moves go
  through memory.)
- **Interpreter bodies.** One handler per `D` opcode in `rv32_template.c`,
  wired to the corresponding Berkeley SoftFloat `f64_*` function. This is the
  bulk by line count and the smallest by difficulty — a direct copy of the `F`
  bodies with `f64` calls swapped in.
- **constopt.** Add `D` ops to `rv32_constopt.c` as pass-through mirrors of
  `F` (FP isn't constant-folded — rounding/exception side effects).
- **arch-test.** Wire the `riscv-arch-test` `D` suite for conformance.

**Gate:** the `riscv-arch-test` `D` suite passes; a hand-written guest program
doing `fadd.d`/`fmul.d`/`fsqrt.d`/`fcvt.*` produces correct IEEE results;
existing `F` arch-tests still pass (NaN-boxing didn't regress single-precision).

### Stage 2 — `ilp32d` ABI witness

Port Spike O's ABI witness to doubles. A toy Rust (and C reference) cart calls
an `extern "C"` function taking a `double`; the callee stub emits the raw IEEE
754 bits of the argument as received.

- Under `ilp32d`, a `double` argument passes in an FP register (`fa0`), 64-bit.
  `set_param(0, 0.5)` must land as `param=3fe0000000000000` (IEEE 754 for
  `0.5` double) — contrast Spike O's `ilp32f` witness `0.5f → 3f000000`.
- Run on arm64 and amd64; both must produce identical bits.
- **Resolve the Spike Q `__extendsfdf2` symptom.** With `D` present, the
  double-precision path is real, so Lua's number→string of a non-integer
  float must render correctly (e.g. `tostring(0.5)` → `"0.5"`, not `"0.0"`).
  This is the concrete user-visible proof the change worked.

**Gate:** `param=3fe0000000000000` on both hosts; `readelf -A` shows
`EF_RISCV_FLOAT_ABI_DOUBLE`; the Q `__extendsfdf2` `"0.0"` symptom is gone.

### Stage 3 — Lua `int32` + `float64`

- Flip the Lua build from `LUA_32BITS=1` to explicit `LUA_INT_TYPE=LUA_INT_INT`
  + `LUA_FLOAT_TYPE=LUA_FLOAT_DOUBLE` in **both** the cart `build.rs`
  propagation set **and** `frontends/wasm/CMakeLists.txt`. Confirm the
  `lua_Number`-width propagation reaches every C library (the existing
  `lua_lib_defines` mechanism).
- Verify language-level behaviour matches across paths: `math.type`,
  integer/float division, `tostring` precision (`%.14g`), and that
  `math.random` streams are identical guest-vs-WASM (same source, same seed) —
  the *representation* of returned values changes from the f32 build, but must
  be consistent across paths.

**Gate:** a Lua cart exercising `double` arithmetic, `math.*`, and
`tostring(float)` produces byte-equal per-frame digests on the rv32 path
(arm64/amd64) and the WASM Lua-direct path.

### Stage 4 — Save-state across the widened FP file

- Confirm the snapshot/serialisation format absorbs the 64-bit FP register
  file. Save-state, hot-reload (ADR-0045), and the determinism digests all
  serialise FP state; bump the snapshot format/version as needed (pre-cart, so
  no migration — just regenerate checked-in digests).
- Run the Spike K cross-host round-trip (save on arm64, restore on amd64,
  continue) on a `D`-using cart; continuation digests must match a same-host
  run.

**Gate:** cross-host save/restore byte-equal on a cart whose live FP state
includes doubles.

### Stage 5 — Determinism cross-check + libm parity witness

- **Basic-op parity (expected free).** A `D`-heavy cart (`fadd.d`/`fmul.d`/
  `fdiv.d`/`fsqrt.d`/fma) digests byte-equal across rv32/arm64, rv32/amd64, and
  WASM-Lua-direct. Basic IEEE ops are correctly-rounded on softfloat and WASM
  `f64` alike, so this should pass without new work.
- **Transcendental parity (the witness).** A `sin`/`cos`/`exp`/`log`/`pow`-heavy
  `double` cart, digested rv32-softfloat vs WASM. This is the one place
  same-source assumptions can split: emscripten's bundled musl libm vs
  `blyt-tech/musl`. If they diverge, the fix is to compile `blyt-tech/musl`'s
  libm into the WASM module rather than rely on emscripten's. Record the result
  either way — do not assume.

**Gate:** basic-op digests byte-equal across all three runs; transcendental
parity result recorded, with the libm-source remediation applied if it diverges.

### Stage 6 — Metal confirmation (optional, hardware-gated)

- On rv64-compat silicon (the Spike S / Spike H substrate), enable `D` for the
  32-bit personality (the silicon has it; this is a kernel-exposure question,
  not a hardware one) and run a `D`-using cart natively. Confirm hardware-`D`
  results match the softfloat-emulated digests for basic ops.

**Gate:** native hardware-`D` basic-op digests match the emulated softfloat
digests. (Deferred if hardware is unavailable; emulator + WASM are sufficient
to answer the design question.)

---

## Success criterion

- **Stage 0:** `ilp32d` sysroot + Rust object link with no float-ABI mismatch;
  ELF flags confirm `rv32imafdc` / double float ABI.
- **Stage 1:** `riscv-arch-test` `D` suite passes; `F` suite still passes
  (NaN-boxing safe); JIT confirmed not to need `D` work.
- **Stage 2:** `double` crosses `extern "C"` in `fa0` as
  `3fe0000000000000` on both hosts; Q `__extendsfdf2` `"0.0"` symptom resolved.
- **Stage 3:** int32+float64 Lua digests byte-equal across rv32 (both hosts)
  and WASM Lua-direct.
- **Stage 4:** cross-host save/restore byte-equal with doubles in live FP state.
- **Stage 5:** basic-op determinism byte-equal across all three paths;
  transcendental libm parity characterised (and remediated if needed).
- **Stage 6 (if run):** hardware-`D` matches softfloat on metal.
- **Deliverable:** the `spike-u-ilp32d` blyt branch builds and runs the
  double-enabled stack on the emulated and WASM paths and is in a state that
  could be polished to merge.

## What this spike does and does not decide

- **Decides** whether hardware doubles are reachable end-to-end (`ilp32d` +
  RV32D in rv32emu + int32/float64 Lua) without breaking cross-host
  determinism, and produces a functional branch demonstrating it.
- **Decides** the concrete effort and blast radius of adding `D` to the
  rv32emu fork — in particular whether the FP-register-file widening and
  snapshot-format change are as contained as the design analysis suggests.
- **Does not decide** whether the project *should* adopt hardware doubles. That
  is an identity question (ADR-0005's `f32`-everywhere model) and is left to the
  ADR this spike would feed. The spike makes the option real and cheap to
  choose; it does not make the choice.
- **Does not decide** the `Blyt32`→`Blyt2D` variant rename, which is the
  branding consequence of loosening the 32-bit numeric identity but is
  orthogonal to the technical validation here. (Noted as related; out of scope.)
- **Does not require** the metal hardware path to answer the core question —
  Stage 6 is confirmation, not a gate on the spike's conclusion.

## Starting points

- **blyt repo:** `devtool/src/build.rs` (cart Rust flags, `build-std`,
  `LUA_32BITS` propagation), `frontends/wasm/CMakeLists.txt` (WASM Lua-direct +
  embedded guest libs + softfloat + fixed seed), `runtime/host/src/libblyt/`
  (state buffer, save), the guest libraries under the host SDK build, and
  `third_party/musl` (`blyt-tech/musl`).
- **rv32emu fork (`blyt-tech/rv32emu`):** `src/feature.h` (extension gating),
  `src/decode.c`/`decode.h` (decoder), `src/rv32_template.c` (interpreter
  bodies), `src/softfp.h` (softfloat wrappers + single-precision helpers),
  `src/softfloat` (vendored `berkeley-softfloat-3`, already supplies `f64_*`),
  and the FP register file definition.
- **Prior spikes for harness reuse:** Spike O (Rust cart pipeline, ABI witness,
  Docker images), Spike D/K (determinism + cross-host save-state harness), Spike
  T (WASM Lua bridge, fixed-seed parity), and the rv32emu lineage
  (spike-a → … → spike-t) for the emulator build.

## Relationship to the ADR log (inverted)

Unlike spikes A–T, this spike has **no backing ADR**. Instead it would *produce*
proposals:

- **ADR-0001 (ISA)** — amend `RV32IMAFC` → `RV32IMAFDC`.
- **ADR-0005 (numeric model)** — revisit the `f32`-everywhere identity; record
  the `ilp32d` / hardware-double decision and its consequences.
- **ADR-0108 (Rust target)** — replace `riscv32imafc-unknown-none-elf` with the
  custom `riscv32imafdc` / `ilp32d` target + `build-std` recipe.
- **New ADR** — the `ilp32d` ABI choice across the whole stack (sysroot, Lua
  `LUA_FLOAT_DOUBLE`, snapshot format), and its determinism consequences.

## Dependency

Spike O (Rust cart pipeline + ABI-witness methodology + Docker images), Spike
D/K (cross-host determinism + save-state round-trip harness), Spike T (WASM
Lua-direct bridge + fixed-seed parity). Requires the `blyt-tech/rv32emu` fork
(Berkeley SoftFloat already vendored) as the emulator base. Stage 6 additionally
requires the Spike S / Spike H ILP32-capable rv64 substrate; all other stages
run on the Docker/QEMU/Node infrastructure the prior spikes already use.
