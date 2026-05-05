# Spike K results — cross-host save-state portability

**Status: Stages 1, 3, 4, 5 PASS.  Corruption-detection PASS.  Stage 2
(Lua workload save-state through cart_state_t bindings) deferred — the
mechanism Stages 1/3/4/5 validate is the load-bearing piece; Stage 2 is
Lua harness work on top.  See `spikes/spike-k/TASKS.md` for remaining
details.**

The question Spike K asks (per `docs/design/early-validation-spikes.md`
§K and `spikes/spike-k/PLAN.md`) is whether a save state serialized on
one host platform deserializes on a materially different host platform
and continues producing the same per-frame digests as a same-host
continuation would have produced — across the four region kinds the v1
runtime tracks (POD state buffers per ADR-0010, audio voice-end queue
per ADR-0106, screen shake per ADR-0051, coroutine save-hook output
per ADR-0012).

The answer for each implemented region kind is **yes**: every stage's
4-way cross-host matrix (same-host arm64, same-host amd64, cross-host
amd64-saved → arm64-loaded, cross-host arm64-saved → amd64-loaded)
produces a byte-identical continuation digest stream, AND that stream
is byte-equal to the suffix of the corresponding straight-through
("no save / load") run for frames N+1 onward — i.e. the restored state
is exactly what an in-place continuation would have held.

```
                                     buffer SHA   digest SHA   strong gate
Stage 1 — whetstone (POD floor)      fc6c4ba8     4d6a4400     PASS (matches spike-D suffix)
Stage 3 — voice-end queue (f11)      071ef22f     0054f603     PASS
Stage 3 — voice-end queue (f5 FIFO)  29265c49     4c0c10b9     PASS
Stage 4 — coroutine save-blob        7d71af65     a3c3b70d     PASS
Stage 5 — screen shake               5b623d91     a1a673ca     PASS
Corruption tests                                                PASS (all 5 negatives + control)
```

Reproduce with:

```
cd spikes/spike-k
make all   # docker images, ELF byte-identity, Stages 1/3/4/5, corruption tests
```

`make all` exits 0 if every gate passes.  Re-running on a clean tree
produces the same SHA-256s as the manifest below.

---

## What was built

### `cart_runtime/` — save-state format and tracked-region machinery

| File | Role |
|------|------|
| `save_state.{c,h}` | Wire format header (`SAVE_STATE_MAGIC=0x46433253` 'FC2S' / `SAVE_STATE_VERSION=1` / `layout_hash` / `frame` / `total_size`); `save_state_save / _load / _emit_hex`; FNV-1a-64 over a stable text description of every tracked region. |
| `runtime_tracked.{c,h}` | The string-sink helpers, the registry walker (`runtime_tracked_describe()`), and the API every region implements (`describe`/`save`/`load` callbacks). |
| `region_frame_state.{c,h}` | Spike D's `frame_state_t` lifted into a tracked region.  Save callback canonicalizes f32 NaN at the buffer-write boundary (stricter than spike-D's digest-time canonicalization). |
| `cart_state_whetstone.{c,h}` | Whetstone's tiny POD `cart_state_t` (`a`, `b`, `c`, `d`, `e1[4]`, `t`, `u`).  Mirrors what an ADR-0009 packer would emit. |
| `region_screen_shake.{c,h}` | ADR-0051 4-field POD region (`remaining_frames`, `intensity`, `decay`, `seed`); deterministic per-frame offset noise from FNV-1a-32 over `(frame, seed, axis)`. |
| `region_voice_end_queue.{c,h}` | ADR-0106 — `logical_view_bits` (u64 bitmap of currently-playing handles) plus a fixed-size pending FIFO (8 events of `(frame, handle, kind)`).  Save preserves both verbatim. |
| `region_coroutine_save_blob.{c,h}` | ADR-0012 simplified — fixed slot table (4 slots × 32 bytes) with `active_slots_bits`.  Each persistent coroutine writes a POD struct into a slot via `coroutine_blob_write()`; restore reads the bytes back. |
| `save_io.{c,h}` | Direct ecall-based `open` / `read` / `close` (rv32emu syscalls 1024 / 63 / 57) so the load cart can read the saved buffer hex from a host file path.  Reads chunked to 4 KiB to dodge a host-side overflow in rv32emu's `syscall_read`. |

Spike-D's `digest.{c,h}`, `pcg32.{c,h}`, `nan_canon.h`, `frame_state.h`,
the spike-D `math.h` shim, and the two existing Lua workloads are
inherited verbatim — symlinked in the host source tree (so editing
either side is a single place) and brought in at Docker build time via
the named build context "spike-d" (`docker build --build-context
spike-d=../spike-d ...`).

### `lib/synthetic_mixer.{c,h}`

A deterministic stand-in for the audible mixer (PLAN.md § "Synthetic
audible mixer").  The cart compiles in a `(handle, ends_at_frame)`
schedule; at end-of-frame the mixer reports voice-end events for any
voice whose `ends_at_frame` matches the current frame.  ADR-0106
already establishes that the audible mixer is not bit-identical
across hosts; the cart-observable behaviour is the recorded events,
which the synthetic mixer produces deterministically by construction.

### Cart variants

Three ELFs per workload — `<name>_save.elf` (runs to frame N, prints
the buffer hex), `<name>_load.elf` (reads buffer file, restores,
continues with digest emission), `<name>_full.elf` (straight-through
baseline used by the strong gate):

| Cart | Region(s) used | Save frame |
|------|----------------|------------|
| `whetstone` | frame_state + cart_state_whetstone | 15 |
| `det_audio_branch` | frame_state + voice_end_queue | 11 (post-application) and 5 (pending FIFO) |
| `det_cutscene` | frame_state + coroutine_save_blob | 7 |
| `det_shake` | frame_state + screen_shake | 10 |
| `corruption_tests` | (uses whetstone's registry to construct buffers in-process) | n/a |

### Wire format

Every save buffer starts with a packed 24-byte header followed by
each region's bytes in registry-declaration order:

```c
struct save_state_header {
    uint32_t magic;        // 'FC2S' = 0x46433253
    uint32_t version;      // 1
    uint64_t layout_hash;  // FNV-1a-64 over the regions' field-by-field text
    uint32_t frame;        // frame at which the save was taken
    uint32_t total_size;   // total bytes including this header
} __attribute__((packed));
```

The buffer is emitted on stdout as one line — `BUFFER <frame> <hex...>\n`
— and the load cart reads this file from a host path passed as
`argv[1]`, strips the prefix, parses the hex, and hands the bytes to
`save_state_load()`.

### `layout_hash` as the safety gate

The header carries a 64-bit hash over a stable text description of
every tracked region — `REGION:<name>:size=<bytes>:<field>:<type>@<offset>,...\0`
for each region, in declaration order.  Field offsets are evaluated
via `offsetof()` so the hash captures the compiler's actual layout.
On restore the runtime recomputes the hash from the *current* binary's
description and rejects the load if it differs.

The corruption-test cart confirms this fires for: a flipped magic
byte, a bumped version, a flipped layout-hash bit, a truncated buffer,
and a header `total_size` larger than the buffer handed in.  The
clean-buffer positive control loads as expected.

### Host-side orchestration

The top-level `Makefile` builds both Docker images (`fc32-spike-k-arm64`,
`fc32-spike-k-amd64`), then runs each stage's `<stage>-save → <stage>-
buffer-diff → <stage>-load → <stage>-diff` chain.  The diff step does
the 4-way matrix comparison and, where a `_full` ELF exists, also
runs the strong-gate comparison (continuation suffix == straight-
through suffix).  `make all` orchestrates everything in dependency
order.

---

## Answers to the spike's load-bearing questions

### 1. Does the by-memcpy-with-typed-layouts model round-trip across hosts?

**Yes**, for all four region kinds the spike implements:

- POD state buffers (frame_state + cart_state_whetstone) — Stage 1.
- Audio voice-end queue with FIFO (logical_view_bits + pending[8]) —
  Stage 3, exercised at both the post-application and pending-FIFO
  save points.
- Coroutine save-blob slot table — Stage 4 (simplified to fixed POD
  per slot per PLAN.md § Stage 4 simplification).
- Screen-shake 4-field POD — Stage 5.

For each, the same-host saved buffer bytes are byte-equal across
`linux/arm64` and `linux/amd64`.  This is *not* derivable from
spike-D's result — spike-D proves the digest stream is identical, but
the saved buffer exposes additional state (the layout-hash text,
header packing, region payloads not present in `frame_state_t`).
Stages 3, 4, and 5 confirm no padding-byte-ordering, struct-
alignment, or NaN-bit-pattern divergence shows up in any of the
non-floor region payloads.

### 2. Do non-POD tracked regions round-trip without ADR-level changes?

**Yes**, for the three the spike implements:

- **voice_end_queue** (ADR-0106).  The post-application save (frame 11
  — pending FIFO empty) and the pending-application save (frame 5 —
  pending FIFO has voice 1's end event) BOTH round-trip and produce
  the same continuation as the straight-through run.  ADR-0106's
  handle-order application rule is preserved by the FIFO's encoding
  (the queue clears bits via the FIFO entries; order across handles
  doesn't matter because each event clears its own bit independently).
- **coroutine_save_blob** (ADR-0012).  The simplification — fixed POD
  per slot rather than recursive Lua-table flattening — round-trips
  cleanly.  The simplification does NOT change the byte-image
  serialization mechanism; production Lua-table flattening is
  engineering on top of this property.
- **screen_shake** (ADR-0051).  4-field POD, deterministic noise
  from `(frame_count, seed, intensity)`.  Round-trips byte-identical;
  the post-restore offsets match the same-host continuation's
  offsets frame-by-frame.

No ADR-level changes were required.  The serializer mechanism the
spike validates is identical for every region kind — register a region
with `{describe, save, load}` callbacks and a stable name, and it
participates automatically.

### 3. Does a restored state equal what an in-place continuation would have held?

**Yes**, for every implemented stage.  The strong gate (continuation
digest stream byte-equal to straight-through frame-N+1+ slice) holds
unconditionally for whetstone, det_audio_branch (both save points),
det_cutscene, and det_shake.  This means the save → load → continue
path produces exactly the simulation state that an in-place
continuation would have held — not merely a self-consistent stream.

For whetstone this required lifting the module-local accumulators
into a tracked POD region (`cart_state_whetstone_t`).  Without that,
the locals reset to their fresh-init values on the load side and the
continuation diverged.  This finding is significant for production:
every cart-author-visible piece of simulation state — including what
feels like a stack-local scalar in a single-function loop — must live
in a declared tracked buffer.

---

## Open follow-ups

These are flagged for the production path:

- **Stage 2 — Lua workload save-state.**  The existing
  `det_doom_tick.lua` and `det_entity_update.lua` workloads (spike-D)
  carry per-mob/per-entity simulation state in Lua tables (angle, hp,
  state, tics, alive, ...).  For save-state to round-trip without
  changing the cart, this state needs to live in C-side cart_state_t
  POD regions, accessed via typed-buffer wrappers (ADR-0011 SOA).
  Spike K does not implement that wrapper system; the byte-image
  round-trip property under test is fully exercised by the C carts in
  Stages 1/3/4/5.  See `spikes/spike-k/TASKS.md` for the per-step
  punch list.

- **Negative test: transient `coroutine.create()` across save/restore
  must throw.**  ADR-0012 mandates a runtime error when a non-
  persistent coroutine survives a save/load.  Stage 4's C-side
  simulation does not exercise this path because there is no Lua
  coroutine; deferred to Stage 2's Lua harness.

- **Recursive Lua-table serialization for coroutine save hooks
  (ADR-0012 follow-up).**  Production needs a flattening protocol for
  arbitrary tables (numbers / strings / booleans / nested same).
  Whether that protocol is itself cross-host bit-identical is a
  separate question — Spike K does not exercise it.

- **`init()` non-determinism risk under ADR-0079's standard-library
  allowlist.**  Spike K's carts do not call any non-deterministic API,
  but the spike does not enforce this.

- **On-disk container format.**  Spike K emits the buffer as a single
  hex line on stdout.  Production needs a file header, atomic write,
  checksumming, and the cart-binary-hash tag from ADR-0013.  All
  out of spike scope.

- **Cart-side `static` mutable state audit.**  Whetstone's locals
  were the obvious case; production carts will have many more.  A
  static analyser pass that flags non-trivial cross-frame mutable
  state not declared in a tracked region would catch this class of
  bug at build time.

- **Build-time layout-mismatch test.**  Stage 6's corruption tests
  cover bit-flipped layout_hash via in-process mutation.  A more
  thorough negative test would compile two cart binaries with one
  field reordered, save from one and load on the other, and confirm
  the receiver rejects.  The mechanism is exercised; a dedicated
  test harness for it remains as future work.

- **rv32emu `syscall_read` host-side buffer overflow.**  When asked
  for more bytes than the file holds AND more than `PREALLOC_SIZE`
  (4 KiB), rv32emu's read implementation can write past its
  internal scratch.  Spike K works around it by chunking reads to
  4 KiB on the cart side; the upstream fix is a one-liner in
  `syscall.c::syscall_read` (compute the final fread's length as
  `min(count, PREALLOC_SIZE)`).

---

## Buffer / digest manifest

```
ELF byte hashes (linux/arm64 and linux/amd64 — identical):
  6931623829ec7b4df7c3e98fa83db65e3bde1c6eb505703920cfb7c5b9e9dc8a  whetstone_load.elf
  6a1387413d5cb0cc2ef3a5d69f6251b65278dd57c4683d3c525a2a527c6d2a4d  whetstone_save.elf
  e7d9184ff89c6ba99993fd0115d8d4577c6c3b8ff37093a9c64f5a788acc13f5  corruption_tests.elf
  d4bbac3f93548f100a8d0686ac5f89e190c6211c018ad85f1920db976f1d4c16  det_shake_save.elf
  3411d865bc080a22b53b346c11b68c1867451c721e4894020f832a457a9620c7  det_shake_load.elf
  e6ae441c3a8862584aec13caf43e5f02ec7b3cfa6987b0291ddca8fa29829e82  det_shake_full.elf
  23c7861a6782e3c7cc82d7b8b0f5b6bdb5aa2aa567094dfc57eae024bb1f2c62  det_audio_branch_save.elf
  66e7e6f7b93ff4c1219d8debe5decefee875df5fbd5513f749a677c3cafc8168  det_audio_branch_load.elf
  e56ddf24c057cf41e8b23632e1aacc7ebc8177b45d1849f94bc6f7695e056db7  det_audio_branch_full.elf
  fe5d32da1d734010877c4cdf4bcaa60e37061f3552a8abaa7e182db1e6ecb9bf  det_cutscene_save.elf
  1c26a249678e7671e856474fa41c1c4781444de0e6f66e2d34f3f2cbea9be010  det_cutscene_load.elf
  3e89cfdea81a3e9ba3f032f5ee78e253c5562a764b664f1312b733bd9298b383  det_cutscene_full.elf

Saved buffers (both hosts produce identical bytes):
  fc6c4ba8bb527f3382242d498d8abe71014b753253bbc40dec91eeb75e869ea3  buffers/whetstone.{arm64,amd64}.hex
  5b623d912cc0b3140bc052a62ecba4aace55e373933403ac8e0cd1ecb50df338  buffers/det_shake.{arm64,amd64}.hex
  071ef22f5cdf085ba71e9c6043020969eabe22161e64afc24bfd2fe343dbc08b  buffers/det_audio_branch.f11.{arm64,amd64}.hex
  29265c493cb7ff0518f597e785aeb4a5b4f9a0e448ac360046bd07538b29edff  buffers/det_audio_branch.f5.{arm64,amd64}.hex
  7d71af65e6859dac0a800b68e3f53bdaf2ea1a579e5fd9bfbf4bf739c4c19b8e  buffers/det_cutscene.{arm64,amd64}.hex

Continuation digest streams (4-way matrix per cart, all byte-equal):
  4d6a44003c2cffbdebc9daba1af96b1b34d61b63e76540c5b1daf91393788a2f  digests/whetstone.{same,cross}.{arm64,amd64}.f16.txt
  a1a673ca7f8bb97b45e6f507cffb78cf1b80ab8ebfa28dd3f640cf20b166845a  digests/det_shake.{same,cross}.{arm64,amd64}.f11.txt
  0054f603481d37d9edfa9a17ed6f54c9e31f03ef8f745d6bc4f41ab7f156d97a  digests/det_audio_branch.f11.{same,cross}.{arm64,amd64}.f12.txt
  4c0c10b99544401c74ce8a1d93de7b947fe621d44ecb41f01985495a3e7e9819  digests/det_audio_branch.f5.{same,cross}.{arm64,amd64}.f6.txt
  a3c3b70d06a80be85dfb379cc8c4ae3f446dff742755363f7ee4f1ff883fd981  digests/det_cutscene.{same,cross}.{arm64,amd64}.f8.txt

Straight-through baselines (used by the strong-gate suffix comparison):
  584db2f1b65fc781969acb94aaccba887b9c258947b73946de298bf22db84f05  digests/det_shake_full.{arm64,amd64}.txt
  a6b3f962be39414e8db3eb44c438ff3f3f2a3f3e8296b64aaf2edd18a43624f7  digests/det_audio_branch_full.{arm64,amd64}.txt
  d9857f6c4bddc2452cd45985231dc1567ce91b77a681269697059d0ce0a0c48f  digests/det_cutscene_full.{arm64,amd64}.txt
```

Whetstone's straight-through baseline is spike-D's existing `whetstone.elf`
output (last 30 lines of `spikes/spike-d/digests/digests.arm64.txt`); the
spike-K continuation matches its frame-16+ suffix.
