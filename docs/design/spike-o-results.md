# Spike O — Rust cart end-to-end: results

**Date:** 2026-05-08
**Status:** PASS — all five load-bearing questions answered with numerical evidence.

---

## The five load-bearing questions, answered

### Q1: Does `riscv32imafc-unknown-none-elf` Rust link cleanly against `libblyt32.so` under `ilp32f` ABI, with float arguments crossing the `extern "C"` boundary correctly?

**YES.**

Stage 2 ran the ABI witness on both arm64 and amd64. The toy Rust cart calls
`blyt32_audio_sfx_set_volume(0, 0.5f32)` via a raw `extern "C"` declaration.
The stub emits the raw IEEE 754 bit pattern of the `vol` argument received in the
callee.

| Host  | Output |
|-------|--------|
| arm64 | `SET_VOL voice=00000000 vol=3f000000` |
| amd64 | `SET_VOL voice=00000000 vol=3f000000` |

`0x3f000000` is the IEEE 754 representation of `0.5f`. Under `ilp32f` hard-float
ABI, `f32` arguments pass in FPRs; under `ilp32` soft-float they would pass in
GPRs and the received bits would not be `3f000000`. Both values are identical
across hosts.

`readelf -A` on the Rust cart ELF confirms:

```
Tag_RISCV_arch: rv32i2p1_m2p0_a2p1_f2p2_c2p0
EF_RISCV_FLOAT_ABI_SINGLE (flags: 0x3)
```

**Finding O-1 (non-issue):** The correct Rust target is `riscv32imafc-unknown-none-elf`,
not `riscv32imfc-unknown-none-elf`. The latter does not exist in Rust
nightly-2025-08-01 (`rustc --print=target-list` confirms it is absent). The
former adds the A (atomics) extension, which does not affect the `ilp32f` float
ABI or the calling convention used for `f32` arguments. ADR-0108 should
reference `riscv32imafc-unknown-none-elf`; no design change required.

---

### Q2: Does the packer–Cargo integration work end-to-end?

**YES.**

Stage 4 exercised the full `build.rs → stub_packer_rust → OUT_DIR →
include!(concat!(env!("OUT_DIR"), "/resources.rs"))` chain on both hosts.

- `stub_packer_rust --lang rust --out $OUT_DIR --cart cart.build.yaml` emits
  `resources.rs`, `state.rs`, and `handlers.rs` into `$OUT_DIR`.
- `include!(concat!(env!("OUT_DIR"), "/resources.rs"))` compiles without errors.
- `cargo:rerun-if-changed=cart.build.yaml` is declared; Cargo skips the packer
  on unchanged rebuilds.
- The Stage 4 digest stream is byte-identical to Stage 3 (hardcoded constants),
  confirming the packer emits the same values.

**Finding O-2 (spike-scope simplification):** The stub packer hardcodes constant
values matching the toy cart's `cart.build.yaml` rather than parsing it. The
`build.rs → OUT_DIR` integration point is proven; the full manifest-parse
implementation is a production follow-up (see below).

**Finding O-3 (spike-scope simplification):** The `build.rs` locates the packer
binary via `env!("CARGO_MANIFEST_DIR")` with a relative `../../harness/` path.
A production packer invocation should use an absolute path or a path injected
via a Cargo feature or environment variable, not a relative path from the crate
root. The relative path works within Docker where the directory layout is
fixed; it would break if the crate were relocated.

---

### Q3: Do the ADR-0108 compile-time guarantees hold in practice?

**YES — both guarantees hold, confirmed by failed compilations with the expected
error types.**

**Guard 1 — `FieldHandle<B>` cross-buffer misuse.**

```rust
const S_CTR: FieldHandle<MainBuffer> = FieldHandle::new(0x00010001);
// ...
EnemiesBuffer.set_u32(slot, S_CTR, 42);  // S_CTR is FieldHandle<MainBuffer>
```

Compiler error (both hosts, Rust nightly-2025-08-01):
```
error[E0308]: mismatched types
   expected `FieldHandle<EnemiesBuffer>`, found `FieldHandle<MainBuffer>`
   = note: expected struct `FieldHandle<EnemiesBuffer>`
              found struct `FieldHandle<MainBuffer>`
```

The `PhantomData<B>` type parameter on `FieldHandle<B>` makes the buffer type
a compile-time token. There is no runtime overhead and no `unsafe` escape hatch.
The guarantee holds exactly as ADR-0108 specifies.

**Guard 2 — `fn`-typed handler registration rejects capturing closures.**

```rust
let captured: u32 = 42;
blyt32::register_handler(HANDLER_AI, |ctx| { ctx + captured });
```

Compiler error (both hosts):
```
error[E0308]: mismatched types
   expected fn pointer `fn(u32) -> u32`
        found closure `{closure@src/lib.rs:32:42: 32:47}`
```

A non-capturing closure would coerce to `fn` and pass silently. The guard cart
deliberately captures `captured` to prevent the coercion. The `fn`-typed
parameter in `register_handler` enforces purity at the call site.

**Finding O-4 (note):** A non-capturing closure DOES coerce to `fn(u32) -> u32`
silently. The guard cart must capture at least one variable to trigger the error;
a guard written with a non-capturing closure would pass the gate accidentally.
This is documented in `guard_closure_handler/src/lib.rs` with a comment.

---

### Q4: Does the SDK layer preserve the C API's observable behaviour?

**YES — four-way digest equality across all runs.**

The toy Rust cart and an equivalent C reference cart call the same five stub
functions in the same order with the same arguments. Per-frame FNV-1a-64 digest
streams over frame index and the state buffer value:

| Run | Host  | Cart | First digest         |
|-----|-------|------|----------------------|
| A   | arm64 | Rust | `e8bd5042d485b2e7`   |
| B   | amd64 | Rust | `e8bd5042d485b2e7`   |
| C   | arm64 | C    | `e8bd5042d485b2e7`   |
| D   | amd64 | C    | `e8bd5042d485b2e7`   |

A = B = C = D across all 30 frames. The Rust SDK wrapper layer (`blyt32::*`) is
semantically transparent: it makes exactly the same calls with exactly the same
argument values as equivalent C code.

---

### Q5: What API findings arose from real Rust cart code?

Four findings were recorded. None require an ADR-0108 amendment; two are
design confirmations, two are production follow-ups.

**Finding O-1** — Target name correction. See Q1 above.
*Classification: non-issue (naming clarification only).*

**Finding O-2** — Stub packer does not parse `cart.build.yaml`.
*Classification: spike-scope simplification; production follow-up.*

**Finding O-3** — `build.rs` packer path is relative, not absolute.
*Classification: spike-scope simplification; production follow-up.*

**Finding O-4** — Guard closure must be capturing to trigger the compile error.
*Classification: non-issue confirmed in practice; documented in guard cart source.*

**Finding O-5 (implementation detail):** Rust static archives (`staticlib`) only
contribute symbols that the linker needs at link time. The cart entry points
`fc_cart_init`, `fc_cart_update`, `fc_cart_draw` are declared `#[no_mangle]
pub extern "C"` in Rust but are referenced only as `__attribute__((weak))`
from `libconsole.so`, not from `crt0.o`. The linker drops them. The fix is
`-Wl,-u,fc_cart_init -Wl,-u,fc_cart_update -Wl,-u,fc_cart_draw` at link time.
*Classification: toolchain integration detail; must be documented in the production
cart link recipe for ADR-0073. No design change to ADR-0108 required.*

**Finding O-6 (design confirmation):** `FieldHandle<B>(u32, PhantomData<B>)` with
`PhantomData<B>` gives covariant `B` by default. For the spike's zero-sized marker
structs (no lifetime parameters), covariance is harmless. Production should audit
variance if any `Buffer` implementor acquires a lifetime parameter.
*Classification: non-issue confirmed in practice; production note.*

**Finding O-7 (design confirmation):** The `BlytError { message: &'static str }`
spike simplification (static string instead of heap-allocated `String`) compiles
and links correctly under `no_std`. The `no_std` constraint does not preclude a
`'static str`-based error type. The ADR-0108 heap-clone path for the full
`blyt_last_error()` C-string → owned-`String` route remains a production follow-up.
*Classification: spike-scope simplification confirmed workable; production follow-up.*

**Finding O-8 (implementation finding):** Spike K's rv32emu lineage
(spike-a → spike-b → spike-d → spike-k) does not include Spike C's `-L <libdir>`
dynamic loader patch. Spike O must apply the Spike I multi-dynload patch from
source during its own builder stage. This is a Docker image lineage issue, not
an ADR-0108 design issue.
*Classification: implementation detail; documented in spike-o Dockerfile.*

---

## ADR amendments

No ADR-0108 amendments are required. The spike confirms the design as written.

**ADR-0108 clarifications (not amendments):**
1. Reference `riscv32imafc-unknown-none-elf` as the Rust target (not `imfc`).
2. Document the `-Wl,-u,fc_cart_init` link requirement in the cart build recipe.
3. Note that `build.rs` packer paths must be absolute (via `CARGO_MANIFEST_DIR`).

**ADR-0073 clarification (not amendment):**
- The `language: rust` dispatch in `cart.build.yaml` must invoke the packer
  with `--lang rust --out $OUT_DIR --cart <manifest>` and the `build.rs` must
  declare `cargo:rerun-if-changed=cart.build.yaml` for Cargo incremental build
  to work correctly.

---

## Production follow-ups

- **Production `blyt32` SDK crate.** Full handle set, full error surface,
  `bitflags!` for flag parameters, `Display` and `Debug` impls, `blyt32-sys` /
  `blyt32` crate split for crates.io publication.
- **Full packer Rust codegen.** Beyond hardcoded constants: parse `cart.build.yaml`,
  generate typed `FieldHandle<B>` constants for all declared state buffer fields,
  `ResourceHandle` constants for all assets.
- **`cart.build.yaml` language-dispatch integration.** ADR-0073's `language: rust`
  path should route through the packer's Rust codegen and invoke `cargo build`
  with `build-std` and the correct target flags.
- **Cross-language carts.** Rust game logic + C library (ADR-0108 `languages:
  rust: … c: …` multi-language form). Not exercised in this spike.
- **Milk-V Duo native target.** Run the Rust cart on real RV32 silicon.
  The float ABI correctness is validated by rv32emu's instruction-accurate
  simulation; real-silicon confirmation is a follow-up.
- **WASM target.** Rust→WASM is a separate compilation story; not in scope here.
- **`blyt_last_error()` heap-clone path.** ADR-0108's `Result<T, BlytError>`
  Tier 1 surface using a heap-allocated error string. The spike uses a
  `&'static str` simplification.
- **Deep state-buffer proxy.** ADR-0108's deferred `players[slot].x` proxy
  pattern. The spike implements shallow-method style only; the proxy is
  production post-v1 work.
- **Sequence API.** `SeqStep` array, `sequence!` macro. Not exercised.
- **`async`/`await` for sequences.** ADR-0108 rules this out; still relevant
  to confirm the ruling is correct once the sequence API shape is clearer.
