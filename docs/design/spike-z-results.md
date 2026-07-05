# Spike Z — results

> **Status (2026-07-06):** **GO on FP determinism for native host-Lua.** The
> native host-Lua VM reproduces the Berkeley-SoftFloat reference **bit-for-bit**
> on FMA silicon, cross-arch (arm64 real / x86-64 real / wasm), for the
> transcendental + Zone-1 surface. Q1–Q3 (the decision gate) are green; Q2
> recommends **Mode A**; Q5 non-FP smoke is green (and caught a native-only
> divergence). Q4 (strtod/number-format) is gated on Phase B and deferred per the
> sequencing. Combined with Spike Y's throughput case, this **removes the
> determinism blocker** from the host-Lua-everywhere decision. Landed on the
> `host-lua` integration branch via blyt#226 (blyt#225 stays open for Q4).

## Question

Per [`spike-z-native-hostlua-fp-determinism.md`](spike-z-native-hostlua-fp-determinism.md):
does native host-Lua (x86-64 / arm64), compiled `-ffp-contract=off` with the
ADR-0135 `blyt_fpm` seam, reproduce the SoftFloat reference bit-for-bit where
WASM structurally cannot (WASM MVP has no scalar FMA)? Which native seam mode
ships? Is the cross-arch host-vs-host digest identical (the netplay proof)?

## What was built

| Component | Path (blyt repo, `host-lua`) | Purpose |
|-----------|------------------------------|---------|
| Native host-Lua leg | `frontends/native-hostlua/hostlua_runner.c` | Extracts a cart's `.cart.lua` bytecode, runs it in the seam-compiled native fork VM (mirrors `wasm_main.c`); `blyt.debug.print` + `blyt.quit` only. Not a shipped player. |
| Build recipe | `frontends/native-hostlua/CMakeLists.txt` | Mirrors the wasm `blyt_hostlua_fpm` static lib (`blyt_fpm_soft.c` + musl `src/math`, `-ffp-contract=off -fno-fast-math -fno-builtin`, `-O2`); seam-compiled native fork VM (`onelua.c`, `BLYT_LUA_I32_F64`, `BLYT_HOSTLUA_FP_SEAM`, pinned hash seed); real leg + FMA negative-control variant. |
| Core-digest split | `tests/integration/tests/fp_parity.rs` | Adds `[blyt:fphash-core]` (transcendental + Zone-1 only) alongside the existing `[blyt:fphash]` (full, incl. Phase-B conversions), so the cross-arch gate isolates the seam surface from host-strtod. |
| Non-FP smoke | `tests/integration/tests/native_hostlua.rs` | Integer/bitwise/string/table-iteration digest across all legs + native. |

Faithfulness: the leg loads the **same cart bytecode** every leg loads (not
re-parsed source, which would drag in host `strtod`), uses the **same fork**,
**same fixed hash seed**, **same seam** — only the compile target differs.

## Results

### Q1 — Zone-1 basic ops on FMA silicon

Core digest `031a4987` bit-identical native(`-ffp-contract=off`) == SoftFloat
reference on **arm64 (real Apple Silicon)** and **x86-64 (real Intel/AMD, CI)**
— `fp_native_hostlua_core_parity`.

Findings:

- **The real host-Lua path is contraction-invariant.** Flipping
  `-ffp-contract=fast` does *not* move the core digest, because (a) a Lua cart's
  `a*b+c` compiles to two separate rounded VM ops (`OP_MUL` then `OP_ADD`) the C
  compiler cannot fuse, and (b) the in-house musl kernels are written
  contraction-safe. So `-ffp-contract=off` is load-bearing only for **C-level**
  multiply-add — not for the interpreter or the transcendental kernels. (The
  brief's "contraction-torture *cart*" premise does not survive contact with how
  Lua compiles arithmetic; the torture must live in C.)
- **Teeth (negative control) — a C Horner/dot-product torture** compiled with the
  variant's contraction flag diverges under `-ffp-contract=fast` on FMA hardware
  (off `d002fa13` != fma `05dbe5f4`), proving the flag is load-bearing for C
  multiply-add and the harness detects an FMA divergence — `fp_native_hostlua_contraction_teeth`.
  On real x86-64 CI this is genuine `vfmadd` divergence.
- **x86-64 has no baseline FMA.** `-march=x86-64` (SSE2) lacks FMA3 (Haswell+), so
  the compiler cannot emit `vfmadd` and `-ffp-contract=fast` has nothing to fuse
  unless `-mfma` is added — a real finding: a default-baseline x86-64 host-Lua
  build cannot contract regardless of the flag; the risk only appears under
  `-march=native`/`-mfma`. AArch64 FMA is baseline (always available).
- **`-O0` masks contraction** — the compiler never fuses at `-O0` even with
  `=fast`; the leg is built `-O2` (representative of a shipped build and the
  reference guest libs), which is where fusion actually happens.

### Q2 — which native seam mode ships

**Recommend Mode A** (native f64 + pinned in-house musl kernels +
`-ffp-contract=off` + hermetic) — it reproduces the reference by construction on
both arches (the Q1 core-parity result). This is the same realization ADR-0135
pins on WASM.

**Mode B (`-msoft-float`) is not viable on either target:**

- **x86-64:** `-msoft-float` alone still emits SSE (no real soft-float); adding
  `-mno-sse` fails to compile musl's `double exp(double)` — *"SSE register return
  with SSE disabled"* (the SysV ABI returns doubles in `xmm`; kernel helpers pass
  `double` internally, so the `uint64_t` seam boundary cannot rescue it).
- **arm64:** clang ignores `-msoft-float` (*"argument unused"*); the AArch64
  equivalent `-mgeneral-regs-only` rejects the double ABI (*"darwinpcs does not
  support it"*).

Mode B is also **unnecessary** (Mode A already reproduces the reference), so the
ADR-0135 "future native `-msoft-float` lowering" is neither needed nor realizable
with these toolchains.

### Q3 — cross-arch host-vs-host gate (the decision point)

**One core digest `031a4987` across arm64 host-Lua, x86-64 host-Lua, wasm
host-Lua, and the SoftFloat reference — identical. GO.**

Real-silicon status: arm64 = genuine (local Apple Silicon); x86-64 = genuine via
**CI (`ubuntu-latest`, real Intel/AMD)** — the local `linux/amd64` Docker leg is
Rosetta/QEMU on the arm64 CPU (`vendor_id: VirtualApple`), a high-fidelity
behavioral mirror, **not** the silicon measurement. CI job green (13m36s), all
three native tests confirmed run + pass on the real x86-64 runner.

Scoped to the CORE (transcendental + Zone-1) digest; the number-format/strtod
surface is Phase B (still host libc here) and is deliberately excluded so a
host-`strtod` difference cannot masquerade as a seam divergence.

### Q4 — strtod / number-format (Phase B) — gated, deferred

Gated on Q1–Q3 green (now satisfied) and requires the Phase-B renamed-musl-stdio
mechanism as a prerequisite; not front-loaded per the sequencing. The *full*
digest `f7a69261` (which includes conversions) happens to match on arm64 and
x86-64 today — but via **host libc `strtod`/`printf`**, unpinned, exactly the
coupling Phase B removes. Q4 folds conversions into the cross-arch gate once
Phase B lands. blyt#225 stays open for this.

### Q5 — non-FP parity matrix (enumerate + smoke)

- **Smoked green on the native leg:** integer (i32) overflow/wrap, bitwise,
  integer div/mod (floor semantics), string interning + byte ops, and **table
  iteration order** — digest `ff5ba48c` identical native == emulated == wasm
  (`native_hostlua_nonfp_parity`).
- **Caught + fixed a native-only divergence:** the native VM initially used the
  default `time()`-based `luai_makeseed`, randomizing string hashing → table
  iteration order diverged (`717e35f0` vs `ff5ba48c`). Fixed by pinning
  `luai_makeseed()=0x424C5954` exactly as the WASM/guest builds do. **A shipped
  native player MUST carry this pin.**
- **NaN:** the FP corpus exercises invalid-op NaN (`asin(2)`, `sqrt(-1)`, `0/0`);
  canonicalized (ADR-0010) the native leg matches (`asin2_nan`/`sqrtneg_nan` =
  `7ff8…`). `blyt_canon_f64` is shared freestanding code applied at the
  state-buffer boundary.
- **Follow-on** (needs the full runtime the minimal FP leg omits): GC step
  timing/order, gfx/surface fast-path rasterization, `guest_heap_used`
  byte-accounting (blyt#158), and state-buffer NaN canonicalization on a
  state-buffer-wired native leg.

## ADR-0135 feedback

1. Native realization = **Mode A only**; drop the Mode-B `-msoft-float`
   aspiration (not viable on x86-64 or arm64, and unnecessary).
2. Invariant 1 (`-ffp-contract=off`) is not load-bearing for the
   transcendental/Zone-1 surface (contraction-invariant), but IS for any C-level
   multiply-add; keep it as the cheap correct guard. Record the `-O2` and x86-64
   `-mfma` subtleties (FMA is neither emitted at `-O0` nor available on baseline
   `-march=x86-64`).
3. The hash-seed pin (`luai_makeseed()=0x424C5954`) is a hard requirement for the
   native host-Lua player, not just the WASM/guest builds (Q5).

## Verdict

A clear **GO on FP determinism for native host-Lua**: Q1 bit-identical (+ the
negative control diverges) on real FMA silicon both arches; Q2 recommends Mode A
with data (and rules out Mode B); Q3 one digest across x86-64/arm64/wasm == the
reference; Q4/Q5 recorded. Green here does not *make* the host-Lua-everywhere
decision — it removes the determinism blocker from it, leaving a
throughput/UX/product call (Spike Y already made the throughput case).
