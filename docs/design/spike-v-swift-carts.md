# Spike V — Language-onboarding generality (Swift as probe)

**Status:** Not started — proposed. **Depends on Spike U.**

> **Not backed by an existing ADR, and not a proposal to ship Swift.** This
> spike runs ahead of any decision and carries its own context so it can be
> executed in a clean session. Its output would inform a *meta* question —
> whether a language-onboarding extension point should be formalised — not a
> decision to adopt the specific language it uses as a probe.

---

## What this spike actually is

This is an **architecture probe**, not a feature. The question is not "add
Swift"; it is: **if we want more cart languages in the future, can the
architecture take them, and where are the seams that are secretly shaped like
an existing consumer?** Swift is the probe vehicle, chosen precisely because it
is a non-Rust LLVM language whose defaults are *deliberately awkward* for this
stack:

- its path-of-least-resistance float type is `Double`, not `Float` (it leans on
  the assumption the f32-everywhere model made);
- its standard library carries more weight than `no_std` Rust (it leans on
  whatever footprint assumptions the cart format and budgeting model bake in);
- it is a fourth language behind the same LLVM/ELF/`ilp32d` machinery, so if it
  slots in by *reusing* the existing extension points unchanged, that is
  evidence the architecture generalises — and if it forces a change to something
  that *should* have been language-agnostic, that change is the finding.

The method has **already paid off once**, which is the proof of concept for
running it deliberately: the hardware-doubles question (Spike U) exists only
because Swift's `Double`-leaning defaults made the f32-everywhere omission look
arbitrary rather than designed. The f32-only model never broke for Lua, Rust, or
C because none of them stressed it; Swift was the first to push, and the push
exposed a baked-in assumption with no functional driver. That is exactly the
failure mode an onboarding probe is supposed to catch — not a bug, but an
unexamined default — and finding it now with a throwaway cart is far cheaper
than finding it when someone actually wants Zig, Nim, or AssemblyScript in two
years. The doubles discovery is **entry zero in this spike's friction log**.

---

## Self-contained context (read first)

The probe assumes **Spike U has landed**: the stack is on `rv32imafdc` /
`ilp32d` with hardware doubles, the `blyt-tech/musl` sysroot and guest libraries
(`libblytcommon.so`, `libblytc.so`, `libblyt32.so`, `libblyt32lua.so`) are
rebuilt `ilp32d`, and the `blyt-tech/rv32emu` fork executes `D` via Berkeley
SoftFloat with cross-host bit-identical digests. Spike V reuses all of that.

Mechanically, **Swift is the same shape of problem as Rust** (Spike O / Spike
P): an LLVM frontend emitting RV32 objects that link against the `ilp32d`
sysroot. The project-specific hard parts — PIE/PIC discipline, compiling the
stdlib from source, the `ilp32d` musl, the ECALL/bridge surface, the
determinism harness, the cart-link recipe (`-Wl,-u` entry-point retention, PIE
linker script) — are already solved. **Embedded Swift** is the relevant mode: it
drops reflection, existentials, and ABI stability to produce freestanding
binaries, bridges C headers natively (so `libblyt32.so` is directly importable),
and — now that `A` is in the profile (Spike O/P) — lowers ARC to native AMOs
with no atomic libcall, exactly as Spike P confirmed for Rust's `Arc`.

The point of using a `Double`-leaning language *on the `ilp32d` stack* is that
the awkwardness it would have on `ilp32f` is gone — so the probe tests
*onboarding generality* cleanly, without the integration being dominated by the
very assumption Spike U just removed.

---

## The question

**Primary (the actual point):** Does adding a non-Rust LLVM language exercise
**only the intended per-language extension points**, and what assumptions does
it expose? For every place the integration touches the system, is that place a
clean, language-agnostic seam — or is it secretly Rust-shaped (or `no_std`-shaped,
or f32-shaped) and in need of generalising?

**Secondary (the probe's mechanics — must work to run the probe):** Can a Swift
cart, compiled with Embedded Swift to `riscv32` / `ilp32d`, link against the
Spike U sysroot and console C API, run inside rv32emu, and produce a per-frame
digest **byte-identical** to the equivalent C and Rust carts on both hosts?

**Deliverable:** a **friction log** — every point where the integration touched
something that was not a clean per-language seam, each classified as *expected
extension point* or *leaked assumption that should be generalised* — with the
doubles finding as entry zero; plus a functional `spike-v-swift` blyt branch
that builds and runs the probe cart on the emulated path.

---

## Why this is a risk

The meta-risk is that the architecture has **seams that are shaped like an
existing consumer** without anyone having noticed, because every existing
consumer fits that shape. A probe is the only way to find them. Beneath that,
the probe's own mechanics carry three concrete unknowns:

1. **Embedded Swift emitting `riscv32 / ilp32d` is the one genuine technical
   unknown.** The `F`/`D`/`C` target-features must reach Swift's *own* codegen
   (likely `-Xfrontend -target-feature -Xfrontend +d` etc.), not just the Clang
   importer; rv32 Embedded Swift on a hosted-ish target is less trodden than the
   bare-metal ARM/ESP path. Embedded Swift is behind
   `-enable-experimental-feature Embedded` on a development snapshot, so
   toolchain pinning (as the Rust path pins a nightly) is part of the cost.

2. **Footprint assumptions.** Swift's heavier stdlib is a deliberate stress on
   the cart format and budgeting model — the risk being not "Swift is big" but
   "something in the packer or loader silently assumes a `no_std`-Rust-sized
   footprint."

3. **Subset coverage.** Embedded Swift removes reflection, existentials
   (`any`-typed values), and metatypes. The risk for the *probe* is only that the
   subset is too thin to express a representative cart; the risk for the
   *architecture* is whether the cart API shape implicitly assumed capabilities a
   restricted language can't provide.

Everything else is well-precedented by Spike O/P/U and is mechanical — which is
the point: in a generalisable architecture, most of this should be boring.

---

## What to build

Six stages, mirroring Spike O's shape with a Swift frontend. **Each stage feeds
the friction log**: the gate is not only "does it pass" but "what did making it
pass require touching, and was that touch a clean seam?"

### Stage 0 — Toolchain: Embedded Swift emitting `riscv32 / ilp32d`

- Pin an Embedded Swift development-snapshot toolchain;
  `-enable-experimental-feature Embedded`.
- Get Swift to emit a `riscv32` object with `F`/`D`/`C`/`A` target-features
  reaching codegen (`-Xfrontend -target-feature …`), `-wmo -Osize`.
- **Verify the emitted object's ELF float-ABI flag is `ilp32d`** via
  `readelf -h` — the same check as Spike U Stage 0. Do not trust the triple.

**Gate:** a trivial Swift function compiles to a `riscv32` object whose
`readelf -A`/`-h` reports `rv32…f…d…c…` + `EF_RISCV_FLOAT_ABI_DOUBLE`.
**Friction-log prompt:** did this need anything the Rust target JSON didn't, or
is "LLVM triple + features + abiname" a genuinely language-agnostic recipe?

### Stage 1 — Link + run against the `ilp32d` sysroot

- Bridge the console C API via a bridging header (Embedded Swift imports C
  natively). Provide entry points as `@_cdecl("…")`; apply the existing
  `-Wl,-u,…` entry-point retention (Spike O Finding O-5).
- Link against the Spike U `ilp32d` guest libraries; run in rv32emu.

**Gate:** a minimal Swift cart (`init`/`update`/`draw`, one console call) links
with no float-ABI mismatch and runs to completion.
**Friction-log prompt:** is the cart-link recipe language-agnostic, or did it
encode Rust-specific assumptions (crate layout, `staticlib` symbol behaviour,
`fc_cart_*` retention) that a second language exposed?

### Stage 2 — `ilp32d` ABI witness (Swift)

- A Swift `@_cdecl` function takes a `Double`; the stub emits the raw IEEE 754
  bits received. `set_param(0, 0.5)` → `fa0 = 3fe0000000000000` on both hosts
  (same gate as Spike U Stage 2, different compiler).

**Gate:** `param=3fe0000000000000` on arm64 and amd64.

### Stage 3 — Runtime symbols, ARC, allocator

- A Swift cart using **classes** (forcing ARC) and an allocation runs under
  rv32emu. Confirm ARC lowers to native AMOs (not libcalls) — the Swift analogue
  of Spike P's `Arc` result — and that `swift_*` refcount shims, `malloc`/`free`/
  `posix_memalign`, and `mem*` resolve against the sysroot.

**Gate:** the class-using cart runs without illegal-instruction traps or
allocator panics; digests deterministic across hosts.
**Friction-log prompt:** were the runtime symbols Swift needs already provided by
the sysroot as a language-agnostic set, or is the provided set Rust-shaped?

### Stage 4 — Digest equivalence vs C and Rust

- A Swift toy cart and the equivalent C and Rust carts (reuse Spike O's) make
  the same console calls in the same order with the same arguments. Per-frame
  FNV-1a-64 digest streams must be **byte-equal** across Swift / Rust / C on both
  hosts.

**Gate:** Swift = Rust = C, byte-for-byte, across all save frames on both hosts.
This is the headline evidence that a new language is *observably transparent* —
the determinism contract is language-agnostic.

### Stage 5 — Footprint assumptions (reframed: probe, not pass/fail on size)

- Measure the Swift cart's `.text`/`.rodata`/total loadable size. The number
  itself is informational. The **gating** question is the architecture one:
  does the cart format, packer, or loader contain a hardcoded or implicit size
  assumption that Swift's heavier footprint *violates*? A fixed buffer, a budget
  constant, an alignment or section-count expectation tuned to `no_std` Rust is
  a **leaked-assumption finding** even if Swift is never shipped — and is worth
  more than the kilobyte count.

**Gate:** either the footprint flows through the existing budgeting model with no
hardcoded assumption (clean), or every assumption it trips is logged as a
generalisation target.

### Stage 6 — Subset / ergonomics audit (secondary output)

- Record which Embedded Swift idioms are available to cart authors and which are
  not (`any`-typed values, metatypes, reflection), and — more importantly for
  the probe — whether the **cart API shape** implicitly assumed capabilities a
  restricted language cannot provide. Feeds the friction log and the meta-ADR.

---

## Success criterion

A **green result means "the architecture can take another language,"** not "we
are taking this one." Concretely:

- Stages 0–4 pass: Embedded Swift emits verified `ilp32d`, links against the
  Spike U sysroot, crosses the `Double` ABI boundary correctly, lowers ARC to
  native AMOs, and produces Swift = Rust = C byte-equal digests on both hosts.
- The **friction log** is complete: every integration touch classified as
  *expected extension point* or *leaked assumption*, with doubles as entry zero.
  A pass is "the touches were overwhelmingly clean seams"; a *useful* result even
  on a mechanical failure is "here are the Rust-/`no_std`-/f32-shaped seams that
  need generalising."
- The footprint stage (Stage 5) resolves to either "flows through cleanly" or a
  logged list of hardcoded-footprint assumptions.
- **Deliverable:** the `spike-v-swift` branch builds and runs the probe cart on
  the emulated path.

## What this spike does and does not decide

- **Decides** whether the architecture's language-onboarding seams are genuinely
  language-agnostic, by exercising them with a deliberately-awkward non-Rust LLVM
  language, and produces a classified inventory of every seam that is not.
- **Explicitly does not commit to shipping Swift.** Swift is the probe, not the
  product. A green result says the architecture generalises; it says nothing
  about whether Swift should be a supported language.
- **Does not address** a Swift-direct-to-WASM story (Swift carts would run
  through rv32emu on WASM as Rust/C carts do, per Spike I/Q; out of scope).
- **Does not build** a production Swift SDK — that would be engineering *if* a
  separate decision to adopt Swift were ever taken.

## Starting points

- **Spike U deliverable:** the `ilp32d` sysroot, rv32emu `D` support, cross-host
  determinism harness — hard substrate.
- **Spike O / P:** Rust cart pipeline, ABI-witness methodology, `-Wl,-u`
  entry-point retention, the C/Rust reference carts and Docker images, the
  `Arc`/atomics result Swift's ARC mirrors. These are also the **baseline the
  friction log compares against** — a "clean seam" is one Swift uses exactly as
  Rust did.
- **blyt repo:** `libblyt32.so` C API (bridging-header surface), the cart-link
  recipe, the digest harness, and the packer/loader size-budgeting path (Stage 5
  target).
- **Toolchain:** a pinned Embedded Swift development snapshot,
  `-enable-experimental-feature Embedded`, `-Xfrontend -target-feature` for the
  RV32 F/D/C/A features.

## Relationship to the ADR log (inverted, and meta)

Produces a *meta* question, not a language proposal:

- **Would inform whether a language-onboarding extension point should be
  formalised** — i.e. whether the architecture should grow an explicit,
  documented "to add a cart language, implement X/Y/Z" contract, justified by
  how clean (or not) the Swift integration turned out. The friction log is the
  raw input to that decision.
- Any *language-specific* ADR (Swift as a supported cart language, parallel to
  ADR-0108 for Rust; amending ADR-0001's language list) is **out of scope** and
  would only be written if a separate adoption decision were taken later.

## Dependency

Spike U (the `ilp32d` stack + rv32emu `D` — hard dependency), Spike O/P (Rust
cart pipeline, ABI-witness + digest harness, atomics result, and the comparison
baseline), Spike I (cart format and packing). Runs on the same Docker/QEMU/Node
infrastructure as the prior spikes; no new hardware required.
