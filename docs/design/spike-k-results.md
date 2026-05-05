# Spike K results — cross-host save-state portability

**Status: Stage 1 PASS, corruption-detection PASS.  Stages 2–5 are
not yet implemented (the format and mechanism Stage 1 validates is the
load-bearing piece for Stages 2–5; remaining work is per-region region
implementations and per-cart layouts — see
`spikes/spike-k/TASKS.md`).**

The question Spike K asks (per `docs/design/early-validation-spikes.md`
§K and `spikes/spike-k/PLAN.md`) is whether a save state serialized on
one host platform deserializes on a materially different host platform
and continues producing the same per-frame digests as a same-host
continuation would have produced — across the four region kinds the v1
runtime tracks (POD state buffers per ADR-0010, RNG, audio voice-end
queue per ADR-0106, screen shake per ADR-0051, coroutine save-hook
output per ADR-0012).

Stage 1 answers that question for the **floor case** — a pure C cart
whose only persistent state is a packed POD struct (`frame_state_t`
plus a small `cart_state_whetstone_t`).  The answer for the floor case
is **yes**, with the gates below holding green:

- **ELF byte identity** across `linux/arm64` and `linux/amd64` for
  every cart binary (`whetstone_save.elf`, `whetstone_load.elf`,
  `corruption_tests.elf`).
- **Saved buffer byte identity** across hosts — the bytes
  `whetstone_save.elf` writes on arm64 are byte-for-byte equal to the
  bytes `whetstone_save.elf` writes on amd64, SHA-256
  `fc6c4ba8bb527f3382242d498d8abe71014b753253bbc40dec91eeb75e869ea3`.
- **Continuation digest stream identity** across the 4-way matrix —
  same-host arm64, same-host amd64, cross-host (amd64-saved → arm64-
  loaded), cross-host (arm64-saved → amd64-loaded) — all 14 frames of
  the post-restore digest stream are byte-equal, SHA-256
  `4d6a44003c2cffbdebc9daba1af96b1b34d61b63e76540c5b1daf91393788a2f`.
- **Strong gate:** the spike-K continuation digest stream is byte-
  equal to spike-D's straight-through `whetstone.elf` frames 16–29.
  The save → load → continue path produces exactly the simulation
  state that an in-place continuation would have held — not merely a
  self-consistent stream.
- **Corruption-detection gates** all fire on both hosts: magic-byte
  mutation, version mismatch, `layout_hash` mutation, truncation, and
  `total_size > buffer` are each rejected by `save_state_load()`; an
  unmodified buffer is accepted (positive control).

Reproduce with:

```
cd spikes/spike-k
make all   # docker images, ELF byte-identity, Stage 1, corruption tests
```

`make all` exits 0 if every gate passes.  Re-running on a clean tree
produces the same SHA-256s as above.

---

## What was built

### `cart_runtime/` — save-state format and tracked-region machinery

| File | Role |
|------|------|
| `save_state.{c,h}` | Wire format header (`SAVE_STATE_MAGIC=0x46433253` 'FC2S' / `SAVE_STATE_VERSION=1` / `layout_hash` / `frame` / `total_size`); `save_state_save / _load / _emit_hex`; FNV-1a-64 over a stable text description of every tracked region. |
| `runtime_tracked.{c,h}` | The string-sink helpers, the registry walker (`runtime_tracked_describe()`), and the API every region implements (`describe`/`save`/`load` callbacks). |
| `region_frame_state.{c,h}` | Spike D's `frame_state_t` lifted into a tracked region.  Save callback canonicalizes f32 NaN at the buffer-write boundary (stricter than spike-D's digest-time canonicalization). |
| `cart_state_whetstone.{c,h}` | Whetstone's tiny POD `cart_state_t` (`a`, `b`, `c`, `d`, `e1[4]`, `t`, `u`).  Mirrors what an ADR-0009 packer would emit. |
| `save_io.{c,h}` | Direct ecall-based `open` / `read` / `close` (rv32emu syscalls 1024 / 63 / 57) so the load cart can read the saved buffer hex from a host file path.  Reads chunked to 4 KiB to dodge a host-side overflow in rv32emu's `syscall_read`. |

Spike-D's `digest.{c,h}`, `pcg32.{c,h}`, `nan_canon.h`, `frame_state.h`,
the spike-D `math.h` shim, and the two existing Lua workloads are
inherited verbatim — symlinked in the host source tree (so editing
either side is a single place) and brought in at Docker build time via
the named build context "spike-d" (`docker build --build-context
spike-d=../spike-d ...`).  This was the path of least resistance for
keeping spike-D the authoritative source while still allowing
BuildKit's no-symlink-escape rule to apply.

### Wire format

Every save buffer starts with this header (packed, all little-endian
on the two reference hosts):

```c
struct save_state_header {
    uint32_t magic;        // 'FC2S' = 0x46433253
    uint32_t version;      // 1
    uint64_t layout_hash;  // FNV-1a-64 over the regions' field-by-field text
    uint32_t frame;        // frame at which the save was taken
    uint32_t total_size;   // total bytes including this header
} __attribute__((packed));  // 24 bytes
```

The body is the concatenation of region blocks in registry-declaration
order.  For Stage 1's whetstone cart the registry is `[frame_state,
cart_state_whetstone]`; the body therefore is `frame_state_t` (1316
bytes packed) followed by `cart_state_whetstone_t` (32 bytes packed),
total body 1348 bytes, total buffer 1372 bytes (24 + 1348).

The buffer is emitted on stdout as one line:

```
BUFFER 15 53324346010000003c1c61cb...
```

(literal `BUFFER`, single space, decimal frame, single space,
lower-case hex with no internal whitespace, terminating newline).  The
load cart reads this file from a host path passed as `argv[1]`,
strips the prefix, parses the hex, and hands the bytes to
`save_state_load()`.

### `layout_hash` as the safety gate

The header carries a 64-bit hash over a stable text description of
every tracked region — `REGION:<name>:size=<bytes>:<field>:<type>@<offset>,...\0`
for each region, in declaration order.  Field offsets are evaluated
via `offsetof()` so the hash captures the compiler's actual layout
(not just the source-level declaration).  On restore the runtime
recomputes the hash from the *current* binary's tracked-region
description and rejects the load if it differs.

The corruption-test cart confirms this fires for: a flipped magic
byte, a bumped version, a flipped layout-hash bit, a truncated buffer,
and a header `total_size` larger than the buffer handed in.  The
clean-buffer positive control loads as expected.

### Per-cart layout descriptors

`cart_state_whetstone.c` declares its descriptor field-by-field:

```c
const runtime_tracked_region_t region_cart_state_whetstone = {
    .name     = "cart_state_whetstone",
    .size     = sizeof(cart_state_whetstone_t),
    .describe = describe,    // emits a:f32@0,b:f32@4,c:f32@8,...
    .save     = save,
    .load     = load,
};
```

This is the spike's stand-in for an ADR-0009 packer-generated
descriptor; the production path will substitute a packer-emitted
descriptor without changing the serializer mechanism.

### Whetstone save / load shells

`whetstone_save.elf` runs frames 0..N (default N=15) without emitting
any digests, then takes a snapshot via `save_state_save()` and prints
one `BUFFER` line to stdout.  `whetstone_load.elf` reads the buffer
from the file path in `argv[1]`, deserializes, restores the local
PCG32 from the just-loaded `frame_state`, and continues from frame N+1
emitting the digest stream exactly as spike-D's `whetstone.elf` would
have for those frames.

The module-local accumulators that previously lived as `main` locals
in spike-D's `whetstone.c` (`a`, `b`, `c`, `d`, `e1`, `t`, `u`) now
live in `cart_state_whetstone_t` — a tracked region — so the save
captures everything that affects future frames.  Without that lift the
spike-K continuation diverged from spike-D's straight-through stream
because the locals reset to their fresh-init values on the load side.

### Host-side orchestration

The top-level `Makefile` runs:

1. `docker-build-{arm64,amd64}` (depends on spike-D's images).
2. `elf-bytes-diff` — sha256s every cart ELF in each image, asserts
   byte equality.
3. `stage-1-save` — runs `whetstone_save.elf 15` in each image, captures
   `BUFFER` line into `buffers/whetstone.{arm64,amd64}.hex`.
4. `stage-1-buffer-diff` — sha256s the two buffer files, asserts
   byte equality.
5. `stage-1-load` — runs `whetstone_load.elf` in each image against
   each buffer (4-way matrix), captures continuation digests into
   `digests/whetstone.{same,cross}.{arm64,amd64}.f16.txt`.
6. `stage-1-diff` — sha256s the four digest files, asserts they are
   mutually equal.
7. `corruption-tests-{arm64,amd64}` — runs `corruption_tests.elf` in
   each image and checks every expected rejection fires.

`make all` chains the lot.

---

## Answers to the spike's load-bearing questions (Stage 1 scope)

### 1. Does the by-memcpy-with-typed-layouts model round-trip across hosts?

**Yes**, for the floor case (POD typed buffers + RNG state + module-
local accumulators in a packed cart_state_t).  Concretely:

- The same-host saved buffer bytes are byte-equal across `linux/arm64`
  and `linux/amd64`.  This is *not* derivable from spike-D's result —
  spike-D proves the digest stream is identical, but the saved buffer
  exposes additional state (the layout-hash text, header packing,
  cart_state fields not present in `frame_state_t`).  Stage 1 confirms
  no padding-byte-ordering, struct-alignment, or NaN-bit-pattern
  divergence shows up in the buffer payload.
- The cross-host load → continue path produces the same continuation
  digest stream as the same-host load → continue path, AND that stream
  is byte-equal to the slice of spike-D's straight-through whetstone
  digest stream covering the same frames.
- The `layout_hash` gate catches every mutation we mounted (magic /
  version / hash / truncation / total_size); no failure mode reached
  the body deserializer.

### 2. Do non-POD tracked regions round-trip without ADR-level changes?

**NOT YET ANSWERED.**  Stages 3 (voice-end queue), 4 (coroutine save
blob), and 5 (screen shake) are unimplemented.  The Stage 1 mechanism
(registry walker, layout-hash gate, NaN canonicalization at the buffer-
write boundary) generalises to those region kinds without scheme
changes — the open question is per-region implementation and the
Lua-side workload changes that the audio / coroutine variants need.

### 3. Does a restored state equal what an in-place continuation would have held?

**Yes**, for whetstone — the strong gate (spike-K continuation byte-
equal to spike-D straight-through frame-16+) holds.  This required
lifting the module-local accumulators into a tracked POD region;
without it the locals reset to their fresh-init values on restore.

This finding is significant for production: every cart-author-visible
piece of simulation state — including what feels like a stack-local
scalar in a single-function loop — must live in a declared tracked
buffer.  The contract is non-negotiable, and the cost of catching
this kind of mistake at build time (e.g. a static analyser that flags
non-trivial `static` mutable scalars in cart code) is much lower than
catching it via cross-host divergence late in QA.

---

## Open follow-ups

These are flagged for the production path:

- **Recursive Lua-table serialization for coroutine save hooks
  (ADR-0012 follow-up).**  Spike K simplifies to fixed-size POD save
  structs declared per-coroutine.  Production needs a flattening
  protocol for arbitrary tables (numbers / strings / booleans / nested
  same).  Whether that protocol is itself cross-host bit-identical is
  a separate question — Spike K does not exercise it.
- **`init()` non-determinism risk under ADR-0079's standard-library
  allowlist.**  Spike K's carts do not call any non-deterministic API
  (`os.time()`, etc.), but the spike does not enforce this.
- **On-disk container format.**  Spike K emits the buffer as a single
  hex line on stdout.  Production needs a file header, atomic write,
  checksumming, and the cart-binary-hash tag from ADR-0013.  All
  out of spike scope.
- **Cart-side `static` mutable state audit.**  Whetstone's locals were
  the obvious case; production carts will have many more.  A static
  analyser pass that flags non-trivial cross-frame mutable state not
  declared in a tracked region would catch this class of bug at build
  time.
- **rv32emu `syscall_read` host-side buffer overflow.**  When asked
  for more bytes than the file holds AND more than `PREALLOC_SIZE`
  (4 KiB), rv32emu's read implementation can write past its
  internal scratch.  Spike K works around it by chunking reads to
  4 KiB on the cart side; the upstream fix is a one-liner in
  `syscall.c::syscall_read` (compute the final fread's length as
  `min(count, PREALLOC_SIZE)`).
- **Stages 2–5.**  See `spikes/spike-k/TASKS.md` for the per-stage
  punch list.

---

## Buffer / digest manifest

```
ELF byte hashes (linux/arm64 and linux/amd64 — identical):
  6931623829ec7b4df7c3e98fa83db65e3bde1c6eb505703920cfb7c5b9e9dc8a  whetstone_load.elf
  6a1387413d5cb0cc2ef3a5d69f6251b65278dd57c4683d3c525a2a527c6d2a4d  whetstone_save.elf
  e7d9184ff89c6ba99993fd0115d8d4577c6c3b8ff37093a9c64f5a788acc13f5  corruption_tests.elf

Stage 1 buffer (saved at frame 15, both hosts):
  fc6c4ba8bb527f3382242d498d8abe71014b753253bbc40dec91eeb75e869ea3  buffers/whetstone.{arm64,amd64}.hex

Stage 1 continuation digest streams (frames 16–29, four-way matrix):
  4d6a44003c2cffbdebc9daba1af96b1b34d61b63e76540c5b1daf91393788a2f  digests/whetstone.{same,cross}.{arm64,amd64}.f16.txt
```

The digest stream above is the suffix of spike-D's straight-through
whetstone digest stream covering frames 16–29.
