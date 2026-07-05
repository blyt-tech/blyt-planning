# Spike Z — Native host-Lua FP determinism: the FMA + cross-arch gate before host-Lua-everywhere

**Status:** proposed — this is the go/no-go data-gathering spike for the
"retire emulated-RV32-Lua, run host-Lua everywhere on non-RISC-V hosts"
decision (the Spike Y follow-on). It does **not** make that decision; it
produces the determinism evidence the decision hinges on.
**Depends on:** blyt#223 Phase A (the `blyt_fpm` seam + parity gate — `done`,
WASM only), Spike Y (native host-Lua runner + throughput — `done`), Spike U
(SoftFloat FP reference / `ilp32d` — `done`), Spikes D / K (cross-host
determinism methodology — `done`).
**Where the code is:** blyt#223 Phase A merged via **blyt#224 onto the `host-lua`
integration branch, NOT `main`** (squash commit `719870c`). All this spike's work
also targets `host-lua`, and a session picking this up must **check out
`host-lua`** — `main` has no seam. `host-lua` has CI.
**Sequencing (blyt#225 discussion):** this spike is the **decision gate** and
runs **before Phase B** (the strtod/number-format seam). Its determinism core —
Stages 1–3 (Q1–Q3) — is the go/no-go for the whole direction and needs *no* Phase
B. Only Stage 4 (Q4) needs Phase B, and it is gated on Stages 1–3 passing. So:
Q1–Q3 (decide) → Phase B (build the mechanism) → Q4/Q5. Do not invest in the
Phase-B build ahead of the Q1–Q3 result.
**Hardware gate:** none exotic. The FMA hosts *are* the dev machines: arm64
(Apple silicon / Pi Zero 2 W A53) and x86-64 (a `linux/amd64` container or Intel
host) both have hardware FMA; the riscv64 reference runs under the existing QEMU
gate. Real-silicon confirmation on the K230D/Milk-V floor is optional and
deferrable (cf. Spike U Stage 6).

---

## Self-contained context (read first)

A clean session does not need the chat history — everything load-bearing is
below or cited.

### The direction this gates

Spike Y measured native host-Lua at **~44–58× faster** than the emulated RV32
Lua VM on the ADR-0082 floor (Pi Zero 2 W): the order-of-magnitude lever for Lua
carts on aarch64 handhelds is *running Lua native* (the host-Lua fast path), not
per-pixel VM patches. That motivates **retiring emulated-RV32-Lua as a shipped
runtime path on non-RISC-V hosts** and running host-Lua everywhere — the WASM
fast path model, extended to the native players (`blytplay` / libretro). The
RV32 Lua *build* stays for real RISC-V hardware (native there, not emulated) and
as the determinism reference; rv32emu stays for native cart code and hybrid
native halves. Only the RV32 *Lua VM as a shipped exec path* on non-RISC-V hosts
would be retired.

### Why that decision is not yet safe to make

Determinism — bit-identical behaviour across every platform — is the core
contract (ADR-0007); netplay, replay, rewind and save-states all depend on it.
The emulated/native-metal Lua path is a tight softfloat reference: blyt-tech musl
generic-C `src/math` over Berkeley SoftFloat (RISC-V NaN specialization +
`SOFTFLOAT_ROUND_ODD`, ADR-0132), `-ffp-contract=off`. **ADR-0135** introduced a
softfloat-backed math seam (`blyt_fpm`) so host-Lua reproduces that reference by
construction, and **blyt#223 Phase A landed it for WASM**: `math.*` transcendentals
and `^` route through in-house musl kernels; the parity gate is bit-identical
(`f7a69261`) across native/wasm/libretro; hermetic (no host libm for the
transcendental path).

But Phase A is **WASM-only**, and WASM is the one host that cannot exercise the
sharpest risk:

- **WASM MVP has no scalar FMA.** ADR-0135's Zone-1 claim — basic ops
  (`+ − × ÷ √`, `a*b+c`) are IEEE-mandated so *native == softfloat given
  `-ffp-contract=off`* — is exactly the thing the absence of FMA on WASM
  **masks**. On x86-64 / arm64, hardware FMA is present, and a single
  un-suppressed contraction turns `a*b+c` into a fused op with a different last
  bit. That claim has never been run on FMA silicon.
- **The native host-Lua path does not exist.** Spike Y built a ~150-line native
  Lua runner to *measure throughput*; there is no native host-Lua execution leg
  wired to the state buffers / gfx / `blyt_fpm` seam the way a shipped one would
  be.
- **The cross-arch host-vs-host gate has never run.** The parity gate proved
  wasm-vs-emulated. The actual netplay proof — an x86-64 desktop and an arm64
  handheld computing bit-identical results — is unmeasured.
- **`-msoft-float` on native is designed but unexercised.** ADR-0135's `uint64_t`
  seam boundary is meant to let a native host soft-float-lower the Zone-2 kernels
  (the `softfloat_builtins.c` `__adddf3` pattern the RV32 guest already uses);
  whether that produces correct, bit-identical results on x86-64 (the awkward
  soft-float-codegen target) and arm64 is unknown.

So the direction is *credible* (a validated mechanism exists) but the
*decision* is not derisked: the load-bearing validation lives on the native FMA
hosts, which Phase A did not touch.

### State of the implementation repo (`../blyt`)

- **All of the below is on the `host-lua` branch, not `main`** (blyt#223 Phase A,
  merged via blyt#224). Check out `host-lua` first.
- `blyt_fpm` seam: `runtime/shared/blyt_fpm.{h,soft.c}` — `uint64_t` bit-pattern
  seam + inline double wrappers; wraps in-house musl `sin/cos/…/pow`. Compiled
  into the WASM host-Lua VM (`frontends/wasm/CMakeLists.txt`, gated
  `BLYT_HOSTLUA_FP_SEAM`). The WASM CMake recipe (musl `src/math` +
  `blyt_fpm_soft.c` as a scoped static lib `blyt_hostlua_fpm`) is the pattern to
  mirror for the native leg; the musl-source-dir passthrough is in
  `cmake/blyt_sdk.cmake`.
- Soft-float ABI shim: `runtime/guest/src/libblyt32lua/softfloat_builtins.c`
  (`__adddf3`/`__muldf3`/… over Berkeley SoftFloat, RISC-V config) — the exact
  template for a native `-msoft-float` realization.
- The Lua fork (`blyt-tech/lua`, pinned `v5.5.0-blyt-v0-p2`): `lmathlib.c` /
  `llimits.h` route Zone-2 through `blyt_fpm` when `BLYT_HOSTLUA_FP_SEAM` is
  defined; RV32-guest/emulated build is byte-unchanged.
- Parity harness: `tests/integration/tests/fp_parity.rs` — the adversarial
  transcendental/NaN/subnormal corpus, folded to an FNV-1a digest and asserted
  identical across legs via `run_cart_all_legs`.
- Spike Y's native runner is the *starting-point pattern* — a ~150-line
  standalone runner linking the native-compiled Lua fork (`onelua.c`,
  `BLYT_LUA_I32_F64`, `-O2`), per-pixel API bound to native C, no rv32emu. It was
  built in a working session and is **not committed**, so a cold pickup rebuilds
  it from the description in `spike-y-lua-per-pixel.md` (§"What was built" →
  "Native leg") rather than expecting to find it. This spike extends that shape to
  wire in the `blyt_fpm` seam + the parity cart.
- The `liblua_host` static lib (`CMakeLists.txt`) is **inspection-only**
  (`blyt_cart_lua_lifecycle_mask`) — it links host `m` (libm) but never runs
  gameplay math; it is *not* a determinism-relevant execution path and is not
  what this spike builds.

---

## Motivation — why this is the gate before the decision

The host-Lua-everywhere decision is expensive to reverse (it changes the shipped
runtime for every Lua cart on desktop/handheld) and it is the exact move that
would **multiply a foreign-libm-per-platform dependency** across x86-64 / arm64 /
riscv64 if determinism does not hold. ADR-0135 exists to prevent that; this spike
proves whether the seam actually delivers it *off* WASM. If Q1 or Q3 fails, the
decision is no-go until the seam is fixed; if they pass, the decision becomes a
throughput/UX call (Spike Y already made the throughput case), not a determinism
gamble.

Everything else about host-Lua-everywhere is understood engineering (the WASM
fast path is the working model). The open questions are FP-determinism-shaped and
FMA-host-specific — which is precisely what has not been measured.

---

## The questions

**Q1 (Zone-1 basic ops on FMA silicon — the sharpest unknown).** On a native
host *with hardware FMA* (x86-64 and arm64), does the host-Lua VM compiled
`-ffp-contract=off` produce Zone-1 results — `+ − × ÷ √`, compares, round-to-int,
`fmod`, and especially contraction-prone `a*b+c` / Horner-form expressions —
**bit-identical to the SoftFloat reference**? I.e. is ADR-0135's "native ==
softfloat given contraction off" true on real FMA hardware, or does a stray
contraction (or any other native-f64 subtlety) break it? This is the claim WASM
structurally cannot test.

**Q2 (Zone-2 native realization — which mode ships).** Compile the `blyt_fpm`
musl kernels native, two ways, and diff each against the reference on x86-64 and
arm64:
- **(a) native f64** (as on WASM) — musl generic-C kernels + `-ffp-contract=off`
  + our own `fma.c`. Does a stray contraction inside a kernel, or an FMA-host
  `fma()` resolution, diverge?
- **(b) `-msoft-float`** — every double op in the kernels lowered to
  `__adddf3`/… over the shared SoftFloat (the `softfloat_builtins.c` pattern),
  sealed behind the `uint64_t` seam. Does clang emit correct soft-float codegen
  on **x86-64** (the awkward target) and **arm64**, and does it reproduce the
  reference by construction?
  Recommend Mode A (full soft) vs Mode B (native Zone-1) for the native ship, with
  data — not corpus luck.

**Q3 (cross-arch host-vs-host — the netplay proof).** Run the blyt#223 parity
gate plus an expanded FP corpus on native host-Lua across **x86-64, arm64, and
wasm**, and assert the digest is **bit-identical** to the SoftFloat reference on
all of them (the RV32 native-metal path on riscv64 *is* the reference). This is
the actual determinism proof netplay/replay needs — an x86 desktop and an arm64
handheld agreeing to the last bit.

**Q4 (Phase B — strtod / number-format, native).** Extend the seam to
`strtod` / `tostring` (`lua_str2number` / `lua_number2str`) on the native
host-Lua build — the renamed-musl-stdio approach deferred on WASM — and validate
it inside the Q3 cross-arch gate. Full hermeticity requires this; the risk it
mitigates (per-platform foreign `strtod`/`printf`) is *worse* on native than on
WASM.

**Q5 (beyond FP — enumerate + smoke the rest of the host-Lua parity matrix).**
host-Lua-everywhere rides on more than FP: GC timing/order, table-iteration
order, string interning, integer overflow, the surface/gfx fast-path
rasterization, and the S-proxy / state-buffer / resource-heap accounting (the
`guest_heap_used` byte-match, #158). These are proven on *WASM* host-Lua (the 396
integration tests) but not on *native* host-Lua. Enumerate the matrix and
smoke-test it on the native leg; full validation is follow-on, but a native leg
that diverges on GC order or heap accounting would also block the decision.

---

### Known finding to account for — NaN sign is not FP-pinned (ADR-0010)

blyt#223 Phase A already surfaced one determinism subtlety this spike must build
in from the start: an **invalid-operation NaN's sign bit is nondeterministic on
the host-Lua path** — native x86-64 yields `0xfff8…`, arm64 (and the softfloat
reference) yield the RISC-V-canonical `0x7ff8…`. It is **not** a contract
divergence: **ADR-0010** canonicalizes NaN at the state-buffer boundary, so the
observable value is identical. Implications for this spike: (1) the Q1/Q3 digests
must **canonicalize NaN before hashing** (as the #223 gate now does) so they test
the contract-relevant value, not the transient sign — otherwise the cross-arch
gate red-herrings on a difference ADR-0010 already handles; (2) Q5 must
**positively confirm** that boundary canonicalization (`blyt_canon_f64`) covers
every NaN that reaches a state buffer on the native leg, since that — not the FP
unit — is what makes NaN deterministic when host-Lua runs native. `tostring(NaN)`
divergence (`-nan` on a sign-set host) belongs to Q4 (number-format).

## Why this is a risk

- **Q1 is the signature risk class** (cross-target *execution*, not algorithms),
  concentrated at the one spot WASM hides. It is *probably* fine — `-ffp-contract=off`
  is a well-understood knob and x86-64/arm64 both do IEEE f64 in SSE2/NEON (no
  x87 excess precision at 64-bit) — but "probably" is not the determinism
  contract, and a single contracted expression in the interpreter or a Zone-2
  kernel is a silent one-ULP divergence that only surfaces in a netplay desync
  weeks later. The spike converts "probably" into a measured digest.
- **Q2(b) is genuinely uncertain.** Soft-float codegen quality on x86-64 is the
  historically awkward case; if clang won't lower it cleanly there, Mode A's
  literal form is unavailable on that arch and the ship must lean on Mode B
  (native Zone-1, IEEE-provable) — which then makes **Q1 load-bearing for the
  ship**, not just reassuring.
- **Q3 is the whole point** and has simply never been run; a green Q1/Q2 on each
  arch *individually* still has to be confirmed *jointly* identical.

---

## What to build

Minimal, mirroring prior spikes — a native host-Lua execution leg wired faithfully
enough to be trustworthy, plus the cross-arch harness. **No** shipping of the
native host-Lua path in the players (that is implementation once green).

**Order (the decision gate comes first).** Stages 0–3 are the go/no-go and use
only the already-landed transcendental seam — run them first. **Stage 3 is the
decision point: if the cross-arch digest is not bit-identical there, the direction
is no-go — stop and report before any Phase-B work.** Stage 4 is *gated on Stages
1–3 passing* and has a hard prerequisite (the Phase-B strtod/number-format
mechanism); Stage 5 can run any time. Do not front-load Stage 4 / Phase B ahead of
the Q1–Q3 result.

### Stage 0 — Native host-Lua leg (x86-64 + arm64)

- Promote Spike Y's native runner into a faithful native host-Lua leg: link the
  `blyt-tech/lua` fork compiled native (`onelua.c`, `BLYT_LUA_I32_F64`,
  `-ffp-contract=off`), with `BLYT_HOSTLUA_FP_SEAM` defined and the `blyt_fpm`
  seam + in-house musl kernels compiled in (reuse the WASM CMake recipe:
  `blyt_fpm_soft.c` + musl `src/math` as a scoped static lib). Wire enough of the
  runtime (the `blyt.debug.print` output channel + `blyt.quit`) to run the
  existing parity cart unmodified. It must be the *same* Lua sources and seam the
  WASM leg uses — only the compile target differs.
- Build it for **both** x86-64 and arm64 (a `linux/amd64` container + native
  arm64), each with hardware FMA enabled (default). Confirm the seam static-lib +
  `-msoft-float` variant both configure.

### Stage 1 — Q1: Zone-1 FMA/contraction probe

- A contraction-torture cart: `a*b+c`, `a*b-c`, `fma`-shaped Horner polynomials,
  dot-products, and the Zone-1 surface (`+ − × ÷ √ fmod` round-to-int) over the
  adversarial corpus. Fold raw f64 bits to a digest (reuse `fp_parity.rs`'s
  mechanism).
- Run on native x86-64 + arm64 host-Lua and assert bit-identical to the SoftFloat
  reference (blytplay-emulated / the checked-in golden). Compile once *with* and
  once *without* `-ffp-contract=off` — the without-run should **diverge on FMA
  hardware**, proving the flag is load-bearing and the test has teeth (a negative
  control, mirroring the AC2 intent that WASM can't provide).

### Stage 2 — Q2: Zone-2 native realizations

- Build the seam kernels **native-f64** (mode a) and **`-msoft-float`** (mode b)
  on each arch. Run the blyt#223 transcendental corpus through each; diff against
  the reference. Record: does mode-a diverge anywhere on FMA hardware? does mode-b
  compile + run + reproduce-by-construction on x86-64 and arm64? Produce the
  ship-mode recommendation.

### Stage 3 — Q3: cross-arch host-vs-host gate (THE DECISION POINT)

- Run the transcendental + Zone-1 parity corpus (the seam surface that already
  exists — **not** conversions; those arrive in Stage 4) on **x86-64 host-Lua,
  arm64 host-Lua, wasm host-Lua**, and the SoftFloat reference. Assert one digest
  across all. This is the promotable artifact — a `native_hostlua` parity leg
  alongside the existing `run_cart_all_legs`. Run both cross-host directions with
  many iterations, as Spikes D/K/U did.
- **Decision:** identical ⇒ FP determinism holds → the direction is viable →
  proceed to Phase B / Stage 4. Not identical ⇒ **no-go**: stop, report the
  divergence, and do not start Phase B.

### Stage 4 — Q4: strtod / number-format (Phase B) — GATED

- **Runs only after Stages 1–3 are green** (direction confirmed). **Prerequisite:
  Phase B** — the strtod/number-format seam deferred from blyt#223 (a
  *renamed* musl stdio/stdlib subset: `strtod` / `floatscan` / `vfprintf` float
  path under a `blyt_fpm_`-prefixed namespace so it does not override the module
  libc). Build Phase B here (or reuse it if it has already landed on `host-lua`),
  route `lua_str2number` / `lua_number2str` through it, then fold conversions into
  the Stage-3 cross-arch gate. Phase B's WASM half hardens the shipping path
  regardless of the strategic decision; its *native* validation is this stage.

### Stage 5 — Q5: non-FP parity matrix (enumerate + smoke)

- Enumerate the non-FP host-Lua parity surface (GC order, table iteration, string
  interning, integer overflow, `guest_heap_used` accounting, gfx/surface fast
  path) and run the relevant existing integration carts against the native
  host-Lua leg. Full validation is follow-on; the goal here is to surface any
  native-only divergence that would independently block the decision.

---

## Success criterion

- **Q1 met:** the Zone-1 + `a*b+c` corpus hashes bit-identical native-vs-softfloat
  on x86-64 **and** arm64 with `-ffp-contract=off`, and the no-flag negative
  control diverges (proving the test has teeth on FMA hardware). Or: the specific
  contracted corner that breaks is documented.
- **Q2 met:** at least one native `blyt_fpm` realization reproduces the reference
  bit-for-bit on both arches; the Mode A-soft / Mode B-native ship recommendation
  is made with data (including whether `-msoft-float` is viable on x86-64).
- **Q3 met:** one parity digest across x86-64 / arm64 / wasm host-Lua == the
  SoftFloat reference. This is the go signal for determinism.
- **Q4 / Q5 recorded:** strtod/format pinned + validated cross-arch (or scoped
  with a reason); the non-FP matrix enumerated and smoke-tested on native, with
  any divergence flagged.

**Overall verdict shape:** a clear go / no-go on *FP determinism for native
host-Lua*, which — combined with Spike Y's throughput case — is the evidence the
host-Lua-everywhere decision needs. Green here does not *make* the decision; it
removes the determinism blocker from it.

---

## What this spike does and does not decide

**Decides:**
- Whether native host-Lua FP is bit-identical to the SoftFloat reference on FMA
  hosts and across arches — the determinism go/no-go for host-Lua-everywhere.
- Which native seam mode ships (full-soft vs native-Zone-1), with data.
- Whether `-msoft-float` is a viable realization on x86-64/arm64.

**Does not decide (deliberately out of scope):**
- The strategic decision itself (retire emulated-RV32-Lua / host-Lua everywhere)
  — that is a product/UX/throughput call, informed by this + Spike Y, made by the
  overseer.
- Shipping the native host-Lua path in `blytplay` / libretro (the implementation
  once determinism is green — a separate, larger piece).
- Full validation of the non-FP parity matrix (Q5 is enumerate-and-smoke; the
  complete cross-arch non-FP gate is follow-on).
- Anything about real-silicon (K230D/Milk-V) confirmation — deferrable like Spike
  U Stage 6; the QEMU/dev-host FMA coverage is the gate.

---

## Starting points (implementation repo `../blyt`)

- Seam: `runtime/shared/blyt_fpm.{h,soft.c}`; WASM wiring +
  `blyt_hostlua_fpm` static lib recipe in `frontends/wasm/CMakeLists.txt`
  (the reusable pattern for the native leg).
- Soft-float ABI template: `runtime/guest/src/libblyt32lua/softfloat_builtins.c`.
- Fork seam edits: `lmathlib.c` / `llimits.h` (gated `BLYT_HOSTLUA_FP_SEAM`),
  Lua pin `v5.5.0-blyt-v0-p2` in `CMakeLists.txt`.
- Parity harness + corpus: `tests/integration/tests/fp_parity.rs`
  (`fp_transcendental_parity_across_legs`, `fp_seam_hermetic_no_host_libm_transcendentals`).
- Native Lua runner starting point: Spike Y's harness (`blyt:bench/spike-y/` /
  session scratchpad).
- SoftFloat config: `SF_DEFINES` (RISC-V specialize + `SOFTFLOAT_ROUND_ODD`) in
  `cmake/blyt_guest_libs.cmake`; musl `src/math` glob + include set in the same
  file.
- riscv64 reference leg: the existing QEMU native gate (`build-riscv64/blyt_native`,
  `native_qemu` suite).

## Relationship to the ADR log

Validates the implementability/determinism of decisions already made — it does
not propose new ones. Central: **ADR-0135** (the seam this spike exercises off
WASM; its *Implementation notes* already flag Q1/Q2/Q3 as deferred to "the native
host-Lua leg"). Also ADR-0007 (determinism contract), ADR-0132 (SoftFloat
reference), ADR-0005 (numeric model), ADR-0082 (MIPS cap / floor hardware),
ADR-0130 (host-Lua bridge). If Q2(b) finds `-msoft-float` unviable on an arch, or
Q1 finds an irreducible contraction, that feeds back into ADR-0135 (a note
scoping the native realization) — flag it rather than work around it.

## Dependencies

- blyt#223 Phase A (`done`) — the seam + parity harness this spike runs on native.
- Spike Y (`done`) — the native host-Lua runner starting point + the throughput
  half of the host-Lua-everywhere case.
- Spike U (`done`) — the SoftFloat reference + cross-path digest methodology.
- Spikes D / K (`done`) — cross-host/cross-platform determinism methodology to
  reuse (both directions, many runs).
- No external blockers. FMA hosts are the dev machines; the riscv64 reference runs
  under the existing QEMU gate; real-silicon confirmation is optional/deferrable.
