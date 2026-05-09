# ADR-0108: Rust as a first-class cart language

## Status

Accepted — confirmed end-to-end by Spike O (2026-05-08). All five
load-bearing questions answered; no design amendments required. Rust is
the primary native cart language alongside Lua. ADRs with API ergonomics
implications should treat Rust as a first-class consumer alongside Lua;
C is supported for interoperability and library bindings.

## Context

The console's authoring story is: **Lua** for approachable scripting,
**Rust** as the primary native language, and **C** available when needed
for interoperability with native libraries, language bindings, or low-level
platform work. This positions the console for an audience of Rust developers
— a large and growing community of systems programmers who want the
correctness guarantees and ergonomics of Rust without writing C. The Rust
ecosystem is substantial; games written in Rust are an established and
growing category.

Rust compiles to `riscv32imafc-unknown-none-elf` (the A extension is
required by the target; there is no `riscv32imfc` Rust target in upstream
nightly — see ADR-0001). Rust provides memory safety, a strong type system,
and zero-cost abstractions without a runtime, all compatible with the
console's constraints (no dynamic allocation required, fixed timestep, POD
state buffers).

Rust raises design questions about how the existing C API shape
(opaque `uint32_t` handles, flat function naming, `blyt_result_t` error
returns, `#define` constants) translates to idiomatic Rust, and whether
any design choices should be revisited for Rust ergonomics. This ADR
records those decisions.

## Decision

### SDK structure — two layers

The Rust SDK splits into two pieces with different owners:

- **SDK crate (written once, not generated).** Provides `extern "C"`
  declarations for all `libblyt32` functions, `#[repr(transparent)]`
  newtype wrappers for every handle type, safe Rust wrappers with
  idiomatic signatures, and `bitflags!` definitions for bitmask flag
  parameters. This is the Rust equivalent of `blyt32.h` — authored by the
  SDK team, not the packer.

- **Packer-generated modules (per cart).** The packer emits Rust `const`
  items instead of C `#define`s. Output goes to Cargo's `OUT_DIR`; carts
  consume it via `include!(concat!(env!("OUT_DIR"), "/resources.rs"))` or
  equivalent. Generated files are excluded from the cart source tree,
  consistent with the gitignored-header practice for C carts.

### Opaque handles — newtypes

Every `uint32_t` handle typedef becomes a `#[repr(transparent)]` Rust
newtype:

```rust
#[derive(Copy, Clone, PartialEq, Eq)]
#[repr(transparent)]
pub struct ImageHandle(u32);

pub const BLYT_INVALID_HANDLE: u32 = 0;
```

This is strictly stronger than C typedefs: newtypes are distinct types
at the compiler level, not aliases. Passing an `ImageHandle` where a
`VoiceHandle` is expected is a compile error in Rust; in C it is a
warning at best. Method dispatch (`impl ImageHandle { … }`) gives
IDE autocomplete and removes the need for the `blyt_image_*` prefix to
locate related functions.

### Error model — two tiers, not uniform `Result`

The C API returns `blyt_result_t` from every fallible function. In Rust
this does not map to `Result<T, E>` uniformly, because almost no errors
occur during normal execution (see context below).

**Tier 1 — genuinely fallible, return `Result`:**

Operations that can legitimately fail during correct gameplay:

```rust
// Slot allocation: buffer may be full (game logic must handle this)
fn alloc_slot(&self) -> Option<Slot>

// Save/load: storage failure, version mismatch, slot not found
fn save_write(slot: u8, data: &SaveData) -> Result<(), SaveError>
fn save_read(slot: u8) -> Result<SaveData, SaveError>
```

**Tier 2 — programming-error-only, panic in debug / infallible in release:**

Operations that can only fail if the caller has a bug (wrong handle,
using a handle after release, field/buffer mismatch). These would be
caught during development by debug-build panics. In release builds they
follow the same elide-the-check policy as C. Wrapping these in `Result`
would require callers to handle errors that provably do not occur in
correct code.

```rust
// These do not return Result — error == bug, caught in debug
fn image_load(&self, res: ResourceHandle) -> ImageHandle
fn buffer_set_f32(&self, slot: Slot, field: FieldHandle<Self>, val: f32)
fn audio_sfx_set_volume(&self, voice: VoiceHandle, vol: f32)
```

The packer validates all name-to-ID mappings at build time, so the
entire generated-constants layer is safe by construction. Resource
loading from the cart's own bundle should never fail; a failure there
indicates a broken cart, not a runtime condition.

**Error strings.** `blyt_last_error()` returns a `*const c_char` into
internal storage with no expressible lifetime. The Rust SDK wrapper clones
it immediately into `String` and embeds it in `BlytError`. This pays one
allocation per error path; acceptable given errors are exceptional.

### Method-style API

The C API is intentionally flat (`blyt_image_blit(img, x, y, flags)`).
Rust supports methods on newtypes at zero cost, and the Lua API already
uses method style for the same reason (ADR-0068: "Lua handles are
objects"). The Rust SDK follows Lua's lead, not C's:

```rust
img.blit(x, y, BlitFlags::FLIP_H)?;
voice.set_volume(0.5);
buf.alloc_slot()?;
```

This is consistent with how cart authors will read the Lua documentation
and with Rust idiom. The flat C API remains the canonical ECALL surface;
the Rust method layer is a zero-cost ergonomics mapping above it.

### Typed field handles

The C `blyt_field_h` type embeds the buffer ID in its upper bits
(`S_PLAYER_X = 0x00010001`) so the runtime can debug-assert that a field
handle is used with the correct buffer. This check is runtime-only and
elided in release.

The Rust SDK promotes this to a compile-time constraint via a type
parameter:

```rust
pub struct FieldHandle<B: Buffer>(u32, PhantomData<B>);

// Packer generates:
pub const S_PLAYER_X: FieldHandle<PlayersBuffer> = FieldHandle::new(0x00010001);

// SDK function signature:
pub fn buffer_set_f32<B: Buffer>(&self: B, slot: Slot, field: FieldHandle<B>, val: f32);
```

Passing a `players` field handle to an `enemies` buffer call is a
compile error rather than a debug assertion. The underlying integer
encoding is unchanged; only the Rust type wrapping changes.

### State buffer access style

Two approaches are possible for state buffer field access in Rust:

- **Shallow methods** (recommended): `players.set_f32(slot, S_PLAYER_X, x)`.
  Method on the buffer handle; the field constant is an argument.
  No aliasing complications.

- **Deep proxy** (not recommended for v1): `players[slot].x = x`. A proxy
  struct holding `&mut` into the buffer, with field access mapped to
  get/set calls. This is the most ergonomic reading surface and matches
  what Lua's SOA metatable does (ADR-0011), but Rust's aliasing rules
  prevent holding two mutable slot proxies simultaneously (even to
  different slots), since the borrow checker cannot prove they do not
  alias. The workaround — `Cell`-based interior mutability, index-split
  helpers, or explicit setter calls — negates much of the ergonomic gain.

Shallow methods are the right first-version choice. A typed deep proxy
approach may be revisited if ergonomics prove insufficient in practice.

### Packer-generated Rust output

The packer emits one Rust module per constant namespace into `OUT_DIR`:

```
OUT_DIR/
  resources.rs    — pub const R_HERO_SPRITES: ResourceHandle = ResourceHandle(1);
  state.rs        — pub const S_PLAYER_X: FieldHandle<PlayersBuffer> = …;
  handlers.rs     — pub const HANDLER_AI_PATROL: HandlerHandle = HandlerHandle(1);
  events.rs       — pub const EVT_DAMAGE: EventType = EventType(1);
  …
```

Constants are in `pub mod` namespaces where preferred (e.g.
`pub mod r { pub const HERO_SPRITES: ResourceHandle = … }`) or as flat
`pub const` items matching the C name — the packer's choice to be
settled during implementation.

**Cart override of built-ins.** The C approach (`#undef BLYT_FONT_BUILTIN`
/ `#define BLYT_FONT_BUILTIN ((blyt_resource_h)3)`) has no Rust
equivalent. In Rust the packer controls the generated module entirely: if
a cart declares a resource that overrides a built-in, the packer emits the
override value directly and omits the built-in constant. No preprocessor
trick is required; the generated module is the single source of truth.

### Stage API

Stage (ADR-0089) is already implemented as a C library with a thin
language-binding layer above it. Rust carts call Stage via `extern "C"`
declarations, exactly as C carts do. A thin Rust ergonomics crate sits
above the raw bindings — the same relationship as the Lua Stage bindings
have to the C Stage library.

**Handlers.** Handlers are registered as bare `fn` function pointers, not
closures. Rust's type system enforces this: `fn(i32)` is a function
pointer; `Fn(i32)` is a closure. The registration API accepts only `fn`,
so the "handlers must not capture state" constraint (required for
save-state correctness, per ADR-0090) is a compile-time guarantee in Rust
rather than a documentation rule.

**Sequences.** The Lua recording model (`stage.sequence(function(S) …
end)`) is Lua-specific sugar that builds a static step array at
registration time. In Rust, the same static array is expressed directly:

```rust
const INTRO_STEPS: &[SeqStep] = &[
    SeqStep::Emit(EVT_DIALOGUE_START, 0),
    SeqStep::WaitEvent(EVT_DIALOGUE_DONE),
    SeqStep::Wait(60),
    SeqStep::Emit(EVT_DOOR_UNLOCK, 0),
];
stage.register_seq(HANDLER_CUTSCENE_INTRO, INTRO_STEPS);
```

Or via a declarative macro:

```rust
stage.register_seq(HANDLER_CUTSCENE_INTRO, sequence![
    emit(EVT_DIALOGUE_START, 0),
    wait_event(EVT_DIALOGUE_DONE),
    wait(60),
    emit(EVT_DOOR_UNLOCK, 0),
]);
```

Rust's `async`/`await` is not used for sequences. Standard Rust futures
capture arbitrary locals across await points; the resulting state machine
is not POD-serializable. The sequence mechanism's save state is exactly
two integers (`step_index: u16`, `remaining_frames: u16`); this is
preserved by the static step-array model, not by a future. For branching
scripts (ADR-0012 coroutine territory), Rust is in the same position as C:
explicit save/restore callbacks are required.

**FSM helper.** Rust enums model FSM states more cleanly than C integer
constants. The constraint that AI state is stored as a POD field (a `u8`
or `u32` in the buffer) means the enum is the code-side representation;
`match` provides exhaustiveness checking the C switch statement does not:

```rust
match AiState::from(enemies.get_u8(slot, S_ENEMY_AI_STATE)) {
    AiState::Patrol => { … }
    AiState::Chase  => { … }
    AiState::Dead   => { … }
}
```

### Build integration

A Rust cart declares itself in `cart.build.yaml` (ADR-0073 amendment):

```yaml
language: rust
```

The packer generates Rust constant modules into `OUT_DIR`. The cart's
`build.rs` runs the packer (or the packer is run as a pre-build step) and
tells Cargo where to find the output. Cargo compiles the Rust cart to an
RV32IMAFC ELF. The packer then packs that ELF with the cart's assets. The
`language: rust` declaration also suppresses the packer from generating
C headers or Lua modules.

For carts that include C libraries alongside Rust game logic, the
multi-language form is used with explicit codegen control per language
(ADR-0073 amendment):

```yaml
languages:
  rust:
    codegen: true
  c:
    codegen: false
    sources:
      - vendor/physics.c
```

### Single-threaded execution model

Carts run single-threaded on RV32. The Rust SDK marks handle types as
`!Send + !Sync`. This removes the entire class of multi-threaded aliasing
concerns from the SDK's design space and from cart authors' cognitive load.

The ISA includes the A (atomics) extension (ADR-0001). In a single-threaded
context LR/SC and AMO instructions always succeed on the first attempt, so
they introduce no non-determinism. A is included because Rust's standard
concurrency primitives — `AtomicU32`, `AtomicBool`, `Once`, and many
ecosystem crates — use atomic operations for interior-mutability and
one-time-init patterns even without threads. Excluding A would require
patching or avoiding those primitives across Rust's `no_std` ecosystem,
which is not a reasonable expectation for cart authors.

### Heap allocator

The `blyt32` SDK crate provides a `#[global_allocator]` implementation
backed by a fixed-size heap region carved out of cart memory at load time.
This unlocks the full `alloc` crate — `Arc`, `Vec`, `Box`, `String`, and
any ecosystem crate that depends on allocation.

Heap size is declared in `cart.build.yaml` (default: zero — carts that do
not use `alloc` pay no overhead):

```yaml
heap_size: 65536   # bytes; omit or set to 0 for alloc-free carts
```

The runtime carves out the declared region beyond the save-state buffers
and exposes it to the SDK crate via a well-known symbol or startup call;
cart code never interacts with allocator setup directly.

**The heap is not part of the save state.** Save state captures the
declared POD state buffers only (ADR-0009/0010). The heap lives beyond
the save-state region and is not captured on save, load, or rewind.

The intended pattern is: state buffers are the source of truth; heap
allocations are a derived cache layer built from state buffers and
resources. Data derived from **resources** (sprite caches, physics
material tables, etc.) is unaffected by rewind since resources don't
change. Data derived from **state buffers** (entity lists, spatial
indices, etc.) must be rebuilt when state is restored.

The existing `blyt_cart_on_load` lifecycle hook (ADR-0087) fires after
both save-load and rewind restores. The heap is not zeroed — `init`-time
resource-derived heap data (lookup tables, parsed structures) remains valid
since resources do not change. `on_load` is responsible only for refreshing
state-derived heap data that is stale relative to the restored state buffers.
The no-op default means carts whose heap contains only resource-derived or
transient data need not implement it.

Note that `Arc` vs `Rc`: in single-threaded cart code `Rc<T>` suffices
for shared ownership and avoids the atomic overhead `Arc` carries.
However, ecosystem crates frequently use `Arc` internally regardless, so
both are supported. `Arc`-shared resource data requires no restore
handling; `Arc`-shared data derived from state buffers must be rebuilt
in `on_load`.

## Consequences

- Spike O (2026-05-08) confirmed all five load-bearing questions: float ABI
  correctness (`ilp32f` across arm64 and amd64), packer–Cargo integration,
  compile-time guarantees (`FieldHandle<B>` cross-buffer rejection, closure
  handler rejection), semantic transparency (four-way digest equality:
  Rust/arm64 = Rust/amd64 = C/arm64 = C/amd64), and API findings (eight
  findings; none require design changes, four are production follow-ups).
- Rust cart authors get a native Rust experience: method dispatch,
  `Result` only where errors are genuinely expected, compile-time type
  safety on field handles, and `fn`-enforced handler purity.
- The SDK crate is a one-time investment by the SDK team; it is not
  generated and does not change with each cart.
- ADRs that make API design choices with ergonomics trade-offs (e.g. output
  parameter style, error reporting granularity, handle type granularity)
  should consider the Rust impact alongside Lua. A choice that is ergonomic
  in C but forces unsafe or verbose patterns in Rust is a cost worth naming
  even if the C decision stands.
- The deep state-buffer proxy pattern is deferred. If the shallow method
  style proves insufficient, a proxy approach can be revisited — it does
  not require changes to the C runtime API, only to the Rust SDK crate and
  packer output.
- Rust's sequence model (explicit step arrays) is more honest than Lua's
  recording abstraction about the underlying data structure, at the cost
  of being slightly more verbose for simple cases.
