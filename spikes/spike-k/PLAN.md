# Spike K — implementation plan

**Question (per `docs/design/early-validation-spikes.md` §K):** does a
save state serialized on one host platform deserialize on a materially
different host platform, restore the same simulation state byte-for-
byte, and continue producing the same per-frame digests as a same-host
continuation would have produced — across the full set of regions the
runtime tracks (POD state buffers per ADR-0010, RNG, audio voice-end
queue per ADR-0106, screen shake per ADR-0051, coroutine save-hook
output per ADR-0012)?

**Why this spike exists:** Spike D proved that the same cart, replayed
from frame 0 on `linux/arm64` and `linux/amd64`, produces bit-identical
per-frame digests. Save state is structurally different. It captures
simulation state mid-run, writes it to a portable byte buffer, and
restores it on a fresh runtime — possibly on a different host. Three
load-bearing assumptions in ADR-0009 / ADR-0010 / ADR-0013 are not yet
exercised end-to-end:

1. **The by-memcpy-with-typed-layouts model is sound across hosts.**
   ADR-0010 says save/load is essentially memcpy of tracked regions and
   that POD discipline plus NaN canonicalization makes the byte image
   portable. Padding bytes inside nested POD layouts, struct alignment
   variance, and any uncanonicalized NaN bit pattern at the buffer-
   write boundary (not just at digest emission, where Spike D already
   canonicalizes) can break this silently — same-host save still
   restores fine, but cross-host divergence shows up frames later in
   ways that look like cart bugs.

2. **The non-POD tracked regions round-trip.** Three regions are not
   simple typed buffers: the audio voice-end queue (ADR-0106 — a
   `(frame_n, voice_end, voice_handle)` tuple log applied at the start
   of the next frame, with the logical mixer view derived from it),
   the screen shake region (ADR-0051 — a 4-field struct deterministic
   in `(frame_count, seed, intensity)`), and the coroutine save-hook
   output (ADR-0012 — author-supplied tables returned from
   `blyt32.coroutine.create{}.save`). Each has its own serialization
   shape; each has its own restore semantics. Whether they all
   round-trip without ADR-level changes is what this spike tests.

3. **A restored state is exactly what an in-place continuation would
   have held.** Same-host save → restore is testable by memcmp of
   tracked regions before save and after restore. Cross-host save →
   restore is only testable by *running forward* and comparing the
   continuation digest stream against the same-host continuation. The
   gate is byte-for-byte equality of the digest stream from frame N+1
   onward across two platforms in two directions — exactly the
   property ADR-0007 names as the foundation under save state, rewind,
   replay, and netplay.

K validates the **save-state buffer format** and the **restore
semantics** for the four region kinds the v1 runtime tracks. L wraps
this buffer in libretro `retro_serialize` / `retro_unserialize` (Spike
L scope, building on K's format). M (Spike M) exercises a real
managed-coroutine algorithm using K's buffer for the coroutine save
blob. N validates hot-reload composition over K and M (Spike N).
(Spikes M / N here are the post-renumber labels — until the
managed-coroutine spike was carved out, the hot-reload spike held the
M label; cross-references in older docs that say "Spike M" in the
context of hot reload now mean "Spike N".)

**Dependencies:**
- Spike D (digest harness, the cart workloads, the FNV-1a-64 emitter,
  PCG32 reference impl, NaN canonicalization, the two-Docker-`--platform`
  build infrastructure, the deterministic musl libm subset). Spike K
  inherits Spike D's image and adds the save-state machinery on top.
- ADR-0007 (structural determinism — the property the digest stream
  measures and that save-state portability rests on).
- ADR-0009 (state buffers manifest-declared, packer-generated field
  constants — Spike K does *not* run a real packer; it uses a
  hand-authored layout descriptor that mirrors what a packer would
  emit, so the spike measures the cross-host serializer mechanism
  rather than the manifest pipeline).
- ADR-0010 (POD typed state buffers, NaN canonicalization on f32 field
  writes, save/load by memcpy of tracked regions).
- ADR-0012 (Lua coroutines — `blyt32.coroutine.create{start, save,
  restore}` for the persistent case; transient `coroutine.create`
  invalidated at boundaries).
- ADR-0013 (four save mechanisms; this spike is mechanism 2 — save
  state, platform-managed snapshot of entire runtime state).
- ADR-0051 (screen shake tracked in save state — 4-field runtime
  region, deterministic noise from `(frame_count, seed, intensity)`).
- ADR-0106 (audio voice-end events recorded as frame inputs; logical
  mixer view derived from those events; tracked vs untracked groups).

Can run in parallel with Spikes J, L, M per `early-validation-
spikes.md` §K dependency note.

---

## Key design decisions

### Save state is the byte image of all tracked regions, in a fixed order, behind a small versioned header

```c
// save_state.h — wire format. Do not reorder. Do not add padding.
#define SAVE_STATE_MAGIC   0x46433253u   // 'FC2S'
#define SAVE_STATE_VERSION 1u

typedef struct __attribute__((packed)) {
    uint32_t magic;             // SAVE_STATE_MAGIC
    uint32_t version;           // SAVE_STATE_VERSION
    uint64_t layout_hash;       // FNV-1a-64 over a stable description of every tracked region's name+type+size
    uint32_t frame;             // frame at which save was taken
    uint32_t total_size;        // total bytes including this header
} save_state_header_t;

// Body layout, in fixed order:
//   1. frame_state_t                              (Spike D's POD struct, reused verbatim)
//   2. runtime_screen_shake_t                     (ADR-0051 — 4 fields, 16 bytes packed)
//   3. runtime_voice_end_queue_t                  (ADR-0106 — count-prefixed array of (frame, handle) tuples + logical-view bitset)
//   4. cart_state_buffer_t (per declared buffer)  (ADR-0010 — count-prefixed POD region, NaN-canonicalized)
//   5. coroutine_save_blob_t                      (ADR-0012 — count-prefixed array of (slot_id, byte_count, bytes) tuples)
```

The body is concatenation of packed POD blocks. Every f32 field is
NaN-canonicalized at the *buffer-write boundary* — not only at digest
emission as Spike D does. (This catches a class of bugs Spike D
cannot: a NaN that travels through state but is canonicalized only at
the digest emit site is indistinguishable from a sane value in the
digest, but corrupts a real save-state restore.) Integer fields are
stored little-endian; both `linux/amd64` and `linux/arm64` are
little-endian and the guest is RV32 (LE), so no swap is required and
none is performed. (A big-endian host would need a swap layer; that
is out of scope per ADR-0007's reference platforms.)

### `layout_hash` is the safety gate, not just a version stamp

The header carries a 64-bit hash over a stable text description of
every tracked region: `region_name | type_name | size_in_bytes |
field_name:field_type:field_offset` for each field, in declaration
order, separated by NULs. The runtime computes this at cart-load time
and embeds it in the header on every save. On restore, the runtime
recomputes the hash from the *current* binary's tracked-region
description and rejects the load if it differs.

This catches the failure mode that breaks save state silently: two
cart builds with the same `frame_state_t` field count but different
field sizes (e.g. `i32` widened to `i64`), or with reordered fields,
or with a renamed field that changes meaning. Without the hash gate,
the restore memcpy succeeds and the cart runs forward producing
plausible but wrong state.

The hash is **not** a version of the cart's gameplay logic — two
binaries with identical layouts but different code resolve to the
same hash. That is correct: save state is a snapshot of the data, and
the data is what must round-trip.

### Cart-declared state buffers are described by a hand-authored layout descriptor

ADR-0009 says manifest-declared state buffers go through the packer.
Spike K does not run a packer. Instead each cart provides a
`cart_state_layout.c` file with a hand-authored descriptor:

```c
// cart_state_layout.c (det_doom_tick variant)
const cart_state_layout_field_t cart_state_fields[] = {
    {"player.x",       FIELD_F32, offsetof(cart_state_t, player_x)},
    {"player.y",       FIELD_F32, offsetof(cart_state_t, player_y)},
    {"enemies[0].hp",  FIELD_I32, offsetof(cart_state_t, enemies[0].hp)},
    /* ... */
};
const cart_state_layout_t cart_state_layout = {
    .name = "doom_tick_state",
    .size = sizeof(cart_state_t),
    .field_count = sizeof(cart_state_fields)/sizeof(cart_state_fields[0]),
    .fields = cart_state_fields,
};
```

This mirrors the packer's output shape closely enough that Spike L can
later swap a real packer-generated layout in without changing the
serializer, and it lets the spike measure the *cross-host serializer
mechanism* rather than the manifest pipeline. Documented as a spike
shortcut; the production path will use ADR-0009 packer-generated
descriptors verbatim.

### The cart restores by re-initialising and then deserializing into the freshly-allocated tracked regions

Spike K's restore sequence:

1. Fresh `lua_State` (for Lua carts) or fresh BSS (for the C cart).
2. Run cart `init()` to allocate Lua tables and any computed caches.
   `init` is required to be idempotent over save-restore — the cart
   contract per ADR-0010 says authors keep persistent state in
   declared buffers and rebuild caches in `init`. Spike K's carts
   honour this.
3. The runtime walks the save buffer body and writes each region's
   bytes into the freshly-allocated tracked region of the same name.
4. Apply the voice-end queue's logical-view bitset to the
   newly-initialised mixer state. Apply screen-shake fields directly.
   Re-create persistent coroutines via `blyt32.coroutine.create{}`
   and immediately call each one's `restore` callback with its
   serialized blob.
5. The cart resumes at frame N+1 — `update()` runs first, with the
   logical mixer view already containing the voice-end events queued
   at the time of the save (so the cart's first post-restore
   `is_playing` answers match the save instant).

This sequencing matters. If step 4 happens before step 2 (cart `init`
hasn't run yet, the Lua state has no `update` function bound), the
restore writes into uninitialized memory. If step 4 happens after the
first `update`, the cart sees `is_playing(handle) == false` for one
frame before the queue is restored — a one-frame divergence that
silently breaks the digest comparison.

### Voice-end queue serialization preserves handle order

ADR-0106 specifies that voice-end events are applied at the start of
the next logical update **in handle order**. The serialized form is a
fixed-size (handle-indexed) bitset for the logical view plus a
separately-serialized FIFO of any voice-end events that the mixer
reported at the *current* frame but that have not yet been applied to
the logical view (i.e. the events captured in step 1 of the next
update tick).

The serializer iterates handles in ascending order; the deserializer
reads them in the same order. Cross-host divergence here would be a
serialization-order bug and would manifest as a one-frame mismatch
that does not heal — exactly the failure mode the spike is set up to
catch.

### Coroutine save-hook output is treated as opaque bytes

ADR-0012's `blyt32.coroutine.create{save = ...}` returns an arbitrary
Lua table; the runtime is responsible for serializing it. Production
will likely require the returned table to be POD-compatible (numbers,
strings, booleans, and nested tables of the same — no functions, no
userdata) and serialize it as a known-shape blob. Spike K simulates
this: each persistent coroutine is given a fixed-size POD save struct
(not a Lua table) declared alongside the cart's state layout. The
`save` callback writes into the POD struct; `restore` reads from it.

This is a **simplification specific to Spike K**. The real
serialization-of-arbitrary-tables question is engineering on top of
the cross-host POD round-trip property this spike validates. If the
POD-table model proves too restrictive in production, the spike's
result is unaffected — the byte-image round-trip is what we are
testing, not the table-flattening protocol. Documented as a
spike-shortcut, with the production-path question (recursive table
serialization with strict POD type set) flagged for follow-up in the
result write-up.

### Synthetic audible mixer

Spike K does not link an audible mixer — SDL_mixer or any equivalent.
The carts that exercise voice-end behaviour use a synthetic mixer
that reports voice-end events on a deterministic frame schedule
(e.g. "voice handle 1 ends at frame 5, voice handle 2 ends at frame
12"). This is sufficient for the spike: ADR-0106 already establishes
that the audible mixer is not bit-identical across hosts, and that
the cart-observable behaviour (logical view, `is_playing` answers)
comes from the recorded events, not from the mixer. The synthetic
mixer's deterministic schedule is the recorded events; the spike
tests that *those* survive the round trip.

### Reuse Spike D's `frame_state_t` as the floor case

Spike D's `frame_state_t` (mob array, RNG, accum_*, frame counter) is
already a POD typed buffer — exactly the shape ADR-0010 requires.
Spike K's whetstone variant uses this struct as the cart's only
tracked state. That makes the floor case a *direct* test of the
serializer mechanism without any of the audio / coroutine / shake
complications. If whetstone fails to round-trip, none of the other
stages will; the spike short-circuits there.

---

## Inputs we already have

- **Spike D's `cart_runtime/`**: `frame_state.h`, `digest.{c,h}`,
  `pcg32.{c,h}`, `nan_canon.h`. Spike K uses these unchanged as the
  digest emission and floor-case state struct. The save-state writer
  walks `frame_state_t`'s field order and canonicalizes NaN at the
  write boundary the same way `digest.c` does at the hash-emit
  boundary; the canonicalization helper is reused.

- **Spike D's two-host Docker setup**: `--platform linux/arm64` and
  `--platform linux/amd64` from a single Dockerfile, both inheriting
  the `fc32-spike-b` image, both running rv32emu with the deterministic
  musl libm subset. Spike K's Dockerfile is `FROM fc32-spike-d AS
  builder` and adds only the cart-side and runtime-side save-state
  machinery.

- **Spike D's per-frame digest stream and its byte-equality SHA-256**
  (`f9fadde46c8b25b532570073f10513d8075f2c949a89cfc564cd4eda46e7c71f`,
  92 lines × 30 frames × 3 workloads). Spike K's continuation streams
  must match the *suffix* of this stream from frame N+1 onward, on
  both hosts in both directions, for the workloads that overlap with
  Spike D (det_doom_tick, det_entity_update, whetstone). The new
  workloads (det_audio_branch, det_cutscene, det_shake) produce their
  own digest baselines.

- **Spike D's `det_doom_tick.lua` and `det_entity_update.lua`**:
  used unchanged. Spike K wraps each in a Spike-K shell that calls
  `save_state_save(buf)` at frame N and `save_state_load(buf)` on the
  restore-side cart, then continues. The cart workloads themselves
  do not change — that is the whole point: Spike K validates that
  the save-state mechanism is invisible to cart authoring.

- **rv32emu's existing trace and state-dump capabilities**: rv32emu
  has a `--dump-registers` and trace output that the spike can use
  for ad-hoc inspection during debugging, but the spike's gate is the
  digest stream from the cart, not any rv32emu-side instrumentation.

- **ADR-0010's NaN canonicalization rule**: write-time canonicalization
  to `0x7FC00000` for any f32 buffer field. Spike D implements this
  only in `frame_state_t`'s digest hashing path; Spike K extends the
  rule to *every* tracked-region write site and adds a write-side
  helper `canonicalize_nan_at(p)` invoked by both `save_state_save()`
  and the cart-side typed-buffer write APIs.

---

## What we are NOT building

- **A real packer.** Cart state-buffer layouts are hand-authored
  C descriptors that mirror what an ADR-0009 packer would emit. The
  serializer walks the descriptor; the descriptor's shape is fixed.
  Real packer integration is post-spike work.
- **An audible mixer.** Voice-end events come from a synthetic
  schedule baked into the cart. No SDL_mixer link, no host audio
  device. ADR-0106 already establishes the audible mixer is not
  bit-identical and not under spike scope.
- **Recursive Lua-table serialization for coroutine save hooks.**
  Persistent coroutines use fixed-size POD save structs declared
  alongside cart state. The recursive-table case is engineering on
  top of the byte-image round-trip property this spike validates.
- **Save-state on the WASM target.** Spike D already covered WASM
  for the *replay-from-frame-0* path; save-state on WASM is a follow-
  up if K passes on native (per `early-validation-spikes.md` §K).
- **libretro `retro_serialize` / `retro_unserialize` wrapping.** Spike
  L's scope. Spike K's buffer is the *input* to that wrapper; this
  spike does not call any libretro entry point.
- **Rewind.** Per the spike doc: rewind is N consecutive save states.
  If one round-trip works, rewind is engineering on top.
- **Hot reload.** Spike N's scope (was Spike M before the renumber
  that inserted the managed-coroutine spike at M). K validates
  cross-host portability; N validates same-host save → edit cart →
  restore-into-edited.
- **Real x86-64 hardware.** Spike D's qemu-user-amd64-on-Apple-Silicon
  finding extends to Spike K. If Spike K passes in the two-Docker-image
  setup, a real-hardware run is a low-risk follow-up.
- **Save-state file format / on-disk container.** The spike emits the
  buffer as a hex dump over stdout, the same way Spike D emits its
  digest stream. Container format (file headers, atomic write,
  checksumming) is engineering on top.
- **Save-state across cart binary versions.** ADR-0013 says save
  states are tagged with cart binary hash and loading across versions
  warns the player. Spike K's `layout_hash` gate is a stricter check
  on data layout, but the cross-cart-version warning UI and the
  cart-binary-hash tagging are out of scope.
- **Cart-managed in-game saves** (ADR-0013 mechanism 1) and **cart
  preferences** (mechanism 4). Spike K covers mechanism 2 only.
- **Quota enforcement.** ADR-0013 mentions a 10 MB default for
  cart saves; ADR-0010 says the packer can statically compute total
  state-memory footprint. Spike K's buffers are tiny by construction
  (under 4 KB even with all four tracked region kinds populated);
  quota mechanics are not exercised.

---

## Approach

Six stages. Stage 1 establishes the format and the floor case; Stages
2–5 add one tracked-region kind at a time; Stage 6 closes the
cross-host matrix.

### Stage 1 — Save-state format and the floor (whetstone) case

1. Create `spikes/spike-k/` mirroring spike-d's layout: `Makefile`,
   `Dockerfile` (`FROM fc32-spike-d AS builder`), `cart_runtime/`,
   `lib/`, `ports/`, `workloads/`, `digests/`, `buffers/`. Symlink
   `cart_runtime/digest.{c,h}`, `cart_runtime/pcg32.{c,h}`, and
   `cart_runtime/nan_canon.h` from spike-d (no fork; spike-k extends
   without diverging).

2. Author `cart_runtime/save_state.{c,h}`:
   - `save_state_header_t` per the §Key design decisions block.
   - `void save_state_init(const cart_state_layout_t *layout);` — call
     once at cart entry; computes and caches the `layout_hash`.
   - `uint32_t save_state_save(uint8_t *buf, uint32_t cap);` — writes
     header + body to `buf`, returns total bytes written, or 0 if
     `cap` is too small.
   - `bool save_state_load(const uint8_t *buf, uint32_t size);` —
     verifies magic / version / `layout_hash` / `total_size`, then
     deserializes body into the runtime's tracked regions.
   - `void save_state_emit_hex(const uint8_t *buf, uint32_t size);` —
     prints `BUFFER <frame> <hex...>` to stdout, one line, no
     framing — the cross-host byte-equality input.

3. Author `lib/runtime_tracked.c` — the runtime-side machinery:
   - The single-source-of-truth registry of tracked regions:
     `frame_state_t`, `runtime_screen_shake_t`,
     `runtime_voice_end_queue_t`, `cart_state_t` (per descriptor),
     and the `coroutine_save_blob_t` array.
   - `runtime_tracked_describe(buf, cap)` — writes the stable text
     description that `layout_hash` is FNV-1a-64'd over.
   - The save and load callbacks per region.

4. Author `whetstone_save.c`: spike-d's `whetstone.c` plus a single
   call to `save_state_save()` at frame 15 emitting the buffer hex,
   plus exit. Build a sibling `whetstone_load.c` that reads the
   buffer hex from a fixed input source, calls `save_state_load()`,
   then continues from frame 16 to frame 30 emitting digest lines as
   spike-d does.

5. Build the two cart ELFs (`whetstone_save.elf`, `whetstone_load.elf`)
   in both Docker images. Hash them: same-host ELF byte identity is a
   precondition (spike-d already proves this for `whetstone.elf`;
   spike-k re-proves it for the new save/load variants).

6. Run `whetstone_save.elf` on `linux/amd64` and `linux/arm64`.
   Capture each emitted `BUFFER 15 <hex...>` line to
   `buffers/whetstone.{arch}.hex`. Diff the two files. If the
   buffers are byte-identical, the same-host save → same-bytes
   property holds for the floor case.

7. Cross-load: feed `buffers/whetstone.amd64.hex` into
   `whetstone_load.elf` running on `linux/arm64`, and vice versa.
   Capture the digest stream from frame 16 onward. Compare against
   the same-host continuation (which is `whetstone.elf` from spike-d
   simply continuing past frame 15) on the same arch. The four files
   that result:

   ```
   digests/whetstone.same.amd64.f16.txt    (amd64 same-host continuation, frames 16-30)
   digests/whetstone.same.arm64.f16.txt    (arm64 same-host continuation, frames 16-30)
   digests/whetstone.cross.amd64.f16.txt   (amd64 loaded an arm64-saved buffer, frames 16-30)
   digests/whetstone.cross.arm64.f16.txt   (arm64 loaded an amd64-saved buffer, frames 16-30)
   ```

   All four must be byte-identical. Since spike-d already proved
   `same.amd64 == same.arm64`, the new gate is `cross.amd64 ==
   same.amd64` and `cross.arm64 == same.arm64`.

Exit criterion: the four whetstone digest files are byte-equal;
`buffers/whetstone.amd64.hex == buffers/whetstone.arm64.hex`.

### Stage 2 — Lua workload save-state (POD state buffer)

8. Add `cart_state_t` to spike-d's `det_doom_tick.lua` cart: a packed
   POD struct with the 64-mob array, the player position, the frame
   counter, the RNG state. (`frame_state_t` is the digest's input;
   `cart_state_t` is the persistent simulation state — for spike K
   they overlap by design, since `frame_state_t` already captures
   what would otherwise be in `cart_state_t`. The struct is reused
   under both names via a typedef alias; the layout descriptor for
   `cart_state_t` is the field-by-field description used to compute
   `layout_hash`.)

9. Author `cart_state_layout.c` for `det_doom_tick`. Declare every
   field with explicit name, type, and offset. The layout descriptor
   is part of the spike's deliverable inventory and serves as the
   reference for what an ADR-0009 packer would emit.

10. Wrap `det_doom_tick.lua` in a Spike-K shell that calls
    `save_state_save()` at frame 15 and exits. Build the load
    variant that calls `save_state_load()` and continues to frame
    30. Repeat for `det_entity_update.lua`.

11. Cross-load matrix per Stage 1 step 7, on both Lua carts. Gate:
    digest streams from frame 16 onward match across all four
    files for each cart.

12. **Lua-side restore semantics check.** Spike-K's restore sequence
    runs `init()` on the fresh cart before deserializing. Confirm
    that the Lua side does *not* need to know save-restore is
    happening — `init()` rebuilds Lua-side caches (e.g. metatables
    for the SOA wrappers per ADR-0011); the deserializer fills the
    POD region; the cart's first post-restore `update()` sees state
    consistent with the save instant. Document any cart-side hook
    that proved unavoidable (none expected; if any are required,
    flag for ADR-0010 follow-up).

Exit criterion: cross-host continuation digest streams match
same-host continuations for both `det_doom_tick` and
`det_entity_update`; same-host buffer bytes match across hosts;
no cart-side save-aware code was required.

### Stage 3 — Audio voice-end queue (ADR-0106)

13. Author `lib/synthetic_mixer.c`: a deterministic stand-in for the
    audible mixer. The mixer takes a per-cart schedule of
    `(handle, ends_at_frame)` tuples at cart-load time and reports
    a `voice_end` event at end-of-frame for any voice whose
    `ends_at_frame` is the current frame. The schedule is part of
    cart compilation, identical across both hosts.

14. Author `runtime_voice_end_queue_t` — the runtime-side ADR-0106
    state:
    ```c
    typedef struct __attribute__((packed)) {
        uint64_t logical_view_bits;        // bit i set ⇔ voice handle i is in the logical view
        uint8_t  pending_count;            // events captured this frame, not yet applied
        struct __attribute__((packed)) {
            uint32_t frame;
            uint8_t  handle;
            uint8_t  kind;                 // 0 = voice_end, 1 = music_end
        } pending[8];
    } runtime_voice_end_queue_t;
    ```
    Voice-end events are recorded into `pending` at end-of-frame;
    applied to `logical_view_bits` at the start of the next
    `update()`; serialized verbatim if a save is taken between
    those points.

15. Author `det_audio_branch.lua`:
    - At frame 0, start two tracked voices (handles 1, 2). Schedule
      handle 1 to end at frame 5 and handle 2 to end at frame 12.
    - Each frame, accumulate `frame_state.accum_misc +=
      (is_playing(1) ? 1 : 10) + (is_playing(2) ? 100 : 1000)` —
      so the digest visibly diverges if `is_playing` answers
      diverge by even one frame.
    - Save at frame 11 (one frame before handle 2 ends — voice-end
      queue is non-empty and handle-1's logical-view bit is already
      cleared from the queue applied at frame 6).

16. Cross-load matrix per Stage 1 step 7 on `det_audio_branch`.
    Specific gate: at frame 12 of the continuation, exactly one
    voice-end (handle 2) is reported by the synthetic mixer; the
    cart's `is_playing(2)` answer is `false` for the rest of the
    run on both hosts in both directions.

17. **Pending-queue boundary check.** Add a second variant where
    the save is taken at frame 5 *after* the synthetic mixer has
    reported voice 1's end but *before* the next `update()` has
    applied the queue to the logical view. Specifically: the
    serializer must capture the pending FIFO. On restore, the
    next `update()` applies the FIFO before any cart code runs.
    Confirm cross-host continuation matches.

Exit criterion: `det_audio_branch` cross-host digest streams match
in both save-points (post-application, pending-application);
`logical_view_bits` and `pending[]` round-trip byte-identically.

### Stage 4 — Coroutine save hooks (ADR-0012)

18. Author `det_cutscene.lua`. Uses
    `blyt32.coroutine.create{start, save, restore}` (or, in the
    spike's simplified form, a shim that calls into a per-cart POD
    save struct):
    ```lua
    local cutscene = blyt32.coroutine.create{
        start = function(ctx)
            for step = 1, 30 do
                ctx.step = step
                ctx.angle = (step * 17) % 360
                coroutine.yield()
            end
        end,
        save = function(ctx) return {step = ctx.step, angle = ctx.angle} end,
        restore = function(data) return {step = data.step, angle = data.angle} end,
    }
    ```
    Each frame, resume `cutscene` and feed `(ctx.step, ctx.angle)`
    into the digest via `frame_state.accum_misc`.

19. Author the runtime-side coroutine save support: a fixed slot
    table mapping coroutine-id → POD save struct. The persistent
    coroutine's `save` callback writes into the slot's bytes; the
    `restore` callback reads from them. The Spike-K simplification
    is that the slot's struct shape is statically known per cart;
    real production needs recursive table flattening (out of scope
    per §What we are NOT building).

20. Save at frame 7 (mid-cutscene). Restore on the other host.
    Confirm: `cutscene` re-creates with `restore({step = 7, angle =
    ...})`, the next `update()` resumes it at step 8. Digest
    stream from frame 8 onward matches same-host continuation.

21. **Transient coroutine boundary enforcement (negative test).**
    Author `det_transient_coroutine.lua` that creates a transient
    `coroutine.create()`, yields it, takes a save, restores, then
    attempts to resume. ADR-0012 requires this to throw
    `RuntimeError: coroutine crossed a save/restore boundary`.
    Confirm the error fires on both hosts at the same frame; this
    is a string-equality check on stderr, not a digest check.

Exit criterion: `det_cutscene` cross-host digest streams match;
the persistent-coroutine save blob round-trips byte-identically;
the transient-coroutine restore correctly throws on both hosts.

### Stage 5 — Screen shake (ADR-0051)

22. Add `runtime_screen_shake_t` per ADR-0051 (4 fields,
    deterministic noise from `(frame_count, seed, intensity)`):
    ```c
    typedef struct __attribute__((packed)) {
        int32_t  remaining_frames;
        float    intensity;
        float    decay;
        int32_t  seed;
    } runtime_screen_shake_t;
    ```
    Implement `blyt_screen_shake(frames, intensity)` and the
    deterministic per-frame offset computation.

23. Author `det_shake.lua`: triggers a 20-frame shake at frame 3
    (intensity 4.0, decay 0.95). Each frame, fold the computed
    offset bytes into `frame_state.accum_misc` so the digest
    diverges if the shake state diverges.

24. Save at frame 10 (mid-shake, 13 frames remaining, intensity
    decayed to ~2.4). Restore on the other host. Confirm digest
    stream from frame 11 onward matches same-host continuation.
    The shake's deterministic noise must produce the same offset
    bytes after restore as it would have without the round-trip.

Exit criterion: `det_shake` cross-host digest streams match;
`runtime_screen_shake_t` round-trips byte-identically; the
post-restore offsets match the same-host continuation's offsets
frame-by-frame for the remaining 13 shake frames.

### Stage 6 — Cross-host matrix and corruption-detection gates

25. Run the full cross-host matrix:

    | Cart                    | save-host → load-host (×4 directions) |
    |-------------------------|---------------------------------------|
    | `whetstone`             | amd64→amd64, arm64→arm64, amd64→arm64, arm64→amd64 |
    | `det_doom_tick`         | (same four) |
    | `det_entity_update`     | (same four) |
    | `det_audio_branch`      | (same four, both save points) |
    | `det_cutscene`          | (same four) |
    | `det_shake`             | (same four) |

    Six carts × four directions = 24 cross-host runs (28 with the
    audio-branch second save point), each producing a digest stream.
    Each cart's four streams must be mutually byte-identical.

26. Same-host buffer-bytes equality: for each cart, the buffers
    written on amd64 and arm64 must be byte-equal. SHA-256 each;
    publish in `digests/buffer-hashes.txt`. Fewer SHAs than
    distinct buffers means cross-host save bytes diverge — that is
    a serializer bug, found before any restore runs.

27. **Corruption-detection (negative tests).** Verify the safety
    gates fire:
    - Mutate one byte of a saved buffer; confirm `save_state_load`
      rejects it (header `total_size` carries no checksum, but the
      `layout_hash` and any change to a magic/version field is
      caught; explicit corruption tests target each).
    - Build a cart with a one-field-reordered `cart_state_t`;
      confirm `layout_hash` differs and the load is rejected.
    - Truncate a buffer mid-region; confirm the load is rejected
      cleanly (bounded read; no crash).

28. `make all` runs Stages 1–5's per-cart matrices and Stage 6's
    full matrix; exits 0 only if every byte-equality and digest-
    equality gate passes and every corruption test produces the
    expected rejection.

Exit criterion: `make all` exits 0 in both Docker images; the
`digests/` and `buffers/` artefacts are reproducible by re-running
the spike and produce the same SHA-256s.

---

## Risk notes

- **Padding bytes inside nested POD structs.** Spike D's
  `frame_state_t` uses `__attribute__((packed))` at the top level and
  on `frame_state_mob`. If a cart-declared `cart_state_t` introduces
  a non-packed inner struct (or a field whose alignment requires
  inter-field padding even with `packed` on the outer), the byte
  image diverges between compilers even on the same host. Spike-K's
  layout descriptor walks the struct field-by-field and computes
  expected offsets from the descriptor; the runtime asserts at load
  time that `offsetof(cart_state_t, fieldname) == descriptor_offset`
  for every field, catching this class of bug at cart load rather
  than after a cross-host divergence appears in the digest.

- **NaN canonicalization at the buffer-write boundary.** Spike D
  canonicalizes only at digest emission. If a non-canonical NaN
  reaches a state-buffer field, the digest looks fine but the
  saved buffer bytes diverge across hosts whose libm produces
  different NaN payloads under the same arithmetic. Spike-K's
  serializer canonicalizes every f32 field at write time; if a
  later buffer-equality test fails on a NaN-containing field,
  the serializer's canonicalization helper is the suspect, not
  the cart code.

- **Spike-D's musl libm subset under cart-restore arithmetic.** The
  restore path replays `init()` and runs `update()` from frame N+1.
  Whether the libm calls during `update()` after restore produce the
  same outputs as libm calls during the same-host continuation is
  re-verified by the digest stream — but the failure mode would not
  be a save-state bug. It would be a libm-determinism regression
  that Spike D would also see; the spike's response is to re-run
  Spike D's fixed-replay digest stream and confirm parity, then
  isolate to Spike-K's restore path only.

- **Coroutine save-blob shape.** ADR-0012's `save = function(ctx)
  return {step = ctx.step, ...} end` returns a Lua table; the
  spike's simplification stores into a fixed POD struct. If the
  production runtime requires a different shape (e.g. recursive
  table serialization with strict POD type set) the spike's
  result is not invalidated — the cross-host POD round-trip is
  the property under test — but the table-flattening protocol
  is a separate piece of work, flagged for follow-up. The risk
  is that the simplification hides a problem (table-flattening
  introduces non-determinism) that would have surfaced here. The
  spike documents this explicitly and recommends a Spike-K
  follow-up *after* the production table-flattening protocol is
  drafted.

- **Voice-end pending FIFO ordering.** ADR-0106 specifies handle
  order. The serializer iterates `pending[0..pending_count]`
  in declaration order; the deserializer reads in the same
  order. A subtle bug would be: synthetic mixer reports events in
  a different order on the two hosts, the cart's serialized
  `pending[]` differs *before* save (not a serializer bug — a
  mixer-determinism bug). The synthetic mixer's schedule is fixed
  per cart at compile time, so the mixer side is deterministic by
  construction; if the spike sees a `pending[]` difference at
  save time, the bug is in the synthetic-mixer schedule
  evaluation, not in the cross-host serializer.

- **`layout_hash` collisions vs false-mismatches.** A 64-bit hash
  has a vanishing collision probability across the field-name
  space the spike exercises (a few hundred fields total). The more
  realistic risk is *false mismatches* from compiler-version drift
  changing how `offsetof` evaluates for a given declared
  `cart_state_t`. Spike-K's hash includes computed field offsets,
  not just names, precisely because the layout descriptor itself
  is meant to be derived from the binary's actual layout. Two
  cart binaries that *should* be layout-compatible but produce
  different `offsetof` values are layout-incompatible — the hash
  is right to reject them. Documented; no hash-narrowing escape
  hatch.

- **Lua-side `init()` non-determinism.** ADR-0010 says caches are
  rebuilt in `init()`. If a cart's `init()` reads non-deterministic
  inputs (e.g. `os.time()`, frame counter at init), restore-after-
  edit could differ across hosts. Spike-K's carts do not call any
  such API (the standard library subset is restricted by ADR-0079),
  but the spike-K result write-up flags this risk for production
  carts. The spike does not enforce it directly — the standard
  library allowlist is ADR-0079's scope.

- **Cross-host buffer hex transport.** The spike emits the buffer
  as a single line of hex on stdout (`BUFFER 15 <hex...>`). The
  cross-load harness reads stdin or a fixed file. If the transport
  ever inserts whitespace or re-encodes (the Docker image's
  default locale, a CRLF conversion under qemu-user-amd64), the
  hex parse on the receiving side could silently truncate. The
  spike pins the locale (`LC_ALL=C`) and encodes the hex with a
  fixed `[0-9a-f]{N}` regex check on the receiving side. Documented.

- **rv32emu register-file initialisation.** rv32emu zeroes all
  guest registers at process start. The cart's `crt0.S` runs
  before any cart code; the BSS region is zero-initialised; the
  save buffer is the only source of non-zero state at restore
  time. If rv32emu is invoked twice in a single Docker image
  (e.g. for the save-then-load sequence in a single `make` target)
  and any stale state persists between invocations, that is a
  spike harness bug, not a save-state bug. The spike's harness
  invokes rv32emu as a subprocess per run; no shared state.

---

## Deliverables

- `spikes/spike-k/Dockerfile` — extends `fc32-spike-d`, adds the
  spike-K cart-runtime files and the synthetic mixer; parameterised
  by `--platform` exactly as Spike D's Dockerfile is.
- `spikes/spike-k/Makefile` — orchestration:
  - `make docker-build-arm64` / `make docker-build-amd64` — image
    builds.
  - `make elf-bytes-{arm64,amd64}` / `make elf-bytes-diff` — same
    cart-ELF byte-identity gate Spike D runs (re-run for the new
    save / load variants).
  - `make save-{arm64,amd64}` — runs the save half of every cart
    on the named host; emits `buffers/<cart>.{arch}.hex`.
  - `make save-bytes-diff` — diffs `buffers/*.amd64.hex` vs
    `buffers/*.arm64.hex`; SHA-256s each.
  - `make load-{same,cross}-{arm64,amd64}` — runs the load half
    against the same-host or cross-host buffer; emits
    `digests/<cart>.{same,cross}.{arch}.f<N+1>.txt`.
  - `make diff` — diffs the four streams per cart; SHA-256s each;
    prints PASS/FAIL.
  - `make corruption-tests` — Stage 6 step 27 negative tests.
  - `make all` — every target above, orchestrated; the headline
    PASS gate.
- `spikes/spike-k/cart_runtime/save_state.{c,h}` — header / body
  serialization, layout-hash gate, hex emit/parse.
- `spikes/spike-k/cart_runtime/runtime_tracked.{c,h}` — the
  registry of tracked regions and the `runtime_tracked_describe()`
  function whose output feeds `layout_hash`.
- `spikes/spike-k/cart_runtime/cart_state_layout_*.c` — per-cart
  hand-authored layout descriptors (one per cart variant that
  declares a `cart_state_t`).
- `spikes/spike-k/lib/synthetic_mixer.{c,h}` — deterministic
  voice-end-event generator with per-cart schedule.
- `spikes/spike-k/ports/rv32emu/` — re-uses spike-d's port
  unchanged; adds the spike-K cart entry shells (the `_save` and
  `_load` variants).
- `spikes/spike-k/workloads/` — `det_audio_branch.lua`,
  `det_cutscene.lua`, `det_shake.lua`, `det_transient_coroutine.lua`.
  `det_doom_tick.lua` and `det_entity_update.lua` are symlinks to
  spike-d's workloads.
- `spikes/spike-k/buffers/` — `<cart>.{arm64,amd64}.hex` per cart.
- `spikes/spike-k/digests/` — `<cart>.{same,cross}.{arm64,amd64}.f<N+1>.txt`
  per cart; `buffer-hashes.txt`; `digest-hashes.txt`.
- `spikes/spike-k/elfs/` — built ELFs (gitignored), with
  `elfs.{arm64,amd64}.sha256`.
- `spikes/spike-k/TASKS.md` — per-stage checklist.
- `docs/design/spike-k-results.md` — the write-up: the cross-host
  matrix table (six carts × four directions, all PASS or with
  diagnosed FAILs), the buffer SHA-256s, the digest SHA-256s, the
  three corruption-test outcomes, and answers to the spike's
  three load-bearing questions:
  1. Does the by-memcpy-with-typed-layouts model round-trip
     across hosts? (Y/N, with the floor case and the four
     region-kind cases each addressed individually.)
  2. Do the non-POD tracked regions (voice-end queue, screen
     shake, coroutine save blob) round-trip without ADR-level
     changes? (Y/N per region, with any required ADR follow-ups
     enumerated.)
  3. Does a restored state equal what an in-place continuation
     would have held? (Y/N per cart, with frame-by-frame digest
     evidence.)

  Plus open items for production: the recursive Lua-table
  serialization protocol for coroutine save blobs (ADR-0012
  follow-up), the `init()` non-determinism risk under ADR-0079's
  standard-library allowlist, and the on-disk container format
  (out of spike scope; Spike L's libretro wrapping is the next
  consumer).
