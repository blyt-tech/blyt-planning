# Spike K — task checklist

This is a per-stage checklist mirroring the structure in `PLAN.md`.
Tick boxes show what has been completed and what remains.

## Stage 1 — save-state format and the floor (whetstone) case

- [x] Directory layout (`cart_runtime/`, `lib/`, `ports/`, `workloads/`,
      `digests/`, `buffers/`, `elfs/`).
- [x] Symlinks from spike-D's `cart_runtime` and `workloads`; symlinks
      excluded from the Docker build context via `.dockerignore` and
      brought in at build time via `COPY --from=spike-d` (named build
      context "spike-d" passed by the Makefile).
- [x] `cart_runtime/save_state.{c,h}` — header (magic / version /
      layout_hash / frame / total_size), `save_state_save / _load /
      _emit_hex`, the FNV-1a-64 layout hasher.
- [x] `cart_runtime/runtime_tracked.{c,h}` — string-sink helpers, the
      registry walker, `runtime_tracked_describe()` /
      `runtime_tracked_total_size()`.
- [x] `cart_runtime/region_frame_state.{c,h}` — the floor-case region.
      Save callback canonicalizes f32 NaN at the *write* boundary.
- [x] `cart_runtime/cart_state_whetstone.{c,h}` — whetstone's tiny POD
      cart_state_t (`a`, `b`, `c`, `d`, `e1[4]`, `t`, `u`).  Holds the
      module-local accumulators that would otherwise live as `main`
      locals; participating in save/load is what makes the spike-K
      continuation match spike-D's straight-through frame-16+ stream.
- [x] `cart_runtime/save_io.{c,h}` — direct ecall-based open/read/close
      (rv32emu syscalls 1024/63/57) so the load cart can read the saved
      buffer hex from a host file path.  Reads chunked to 4 KiB to dodge
      a host-side buffer-overflow bug in rv32emu's `syscall_read`.
- [x] `ports/rv32emu/whetstone_save.c` — runs frames 0..N, snapshots,
      emits one `BUFFER <frame> <hex...>` line on stdout.
- [x] `ports/rv32emu/whetstone_load.c` — reads the buffer file via
      argv[1], deserializes, continues from frame N+1 emitting digests.
- [x] `ports/rv32emu/regions_whetstone.c` — registry array
      `[frame_state, cart_state_whetstone]`.
- [x] `ports/rv32emu/Makefile` — builds whetstone_save.elf and
      whetstone_load.elf; reuses spike-B's lua_runtime / softfloat-glue
      / crt0.S unchanged.
- [x] `Dockerfile` — extends `fc32-spike-d-{arm64,amd64}`, copies spike-K
      additions, brings spike-D verbatim sources in via the named
      "spike-d" build context.
- [x] Top-level `Makefile` — `docker-build-{arm64,amd64}`, `elf-bytes`,
      `stage-1-save`, `stage-1-buffer-diff`, `stage-1-load`,
      `stage-1-diff`, `stage-1`, `corruption-tests`, `all`, `clean`.
- [x] **Stage 1 PASS** — both Docker images build, both cart ELFs are
      byte-identical across hosts, both saved buffers are byte-identical
      across hosts, all four continuation digest streams are byte-equal
      to each other AND byte-equal to spike-D's straight-through whetstone
      frame-16+ stream.

## Stage 2 — Lua workload save-state (POD state buffer)

NOT YET IMPLEMENTED.  The remaining work, broken down:

- [ ] Decide cart_state_t layout for `det_doom_tick.lua`.  The existing
      Lua workload carries per-mob simulation state (angle, hp, state,
      tics, alive) in Lua tables.  For the save-state to round-trip
      without changing the Lua workload, those fields need to live in a
      C-side POD region.  Two viable shapes:
      (a) extend `frame_state_mob` with the extra fields, or
      (b) declare a separate `cart_state_doom_tick_t` POD region.
      Plan §Stage 2 step 8 recommends (b): "frame_state_t is the digest's
      input; cart_state_t is the persistent simulation state — for spike
      K they overlap by design".  For Lua carts they cannot fully
      overlap given the existing workload's per-mob extras.  Decision
      point: extend the workload (push state through a new console.*
      API) or extend frame_state.  Either way the cart_state_t / new
      console.* binding is a net new addition.
- [ ] `cart_runtime/cart_state_doom_tick.{c,h}` and
      `cart_runtime/cart_state_entity_update.{c,h}` — per-cart POD
      structs and layout descriptors.
- [ ] New Lua bindings (in a spike-K variant of `lua_det_bindings.c`)
      that expose typed-buffer mutators backed by C-side state instead
      of Lua tables.  The exact shape depends on the layout decision
      above.
- [ ] Lua-side shells: `lua_cart_save.c` (loads embedded workload,
      runs to save frame, emits buffer) and `lua_cart_load.c` (reads
      buffer, restores, runs Lua workload starting at the resume frame
      — workload must support entry at a non-zero frame, or the harness
      must skip-replay through frame N silently).
- [ ] Cross-load matrix on `det_doom_tick` and `det_entity_update`
      (4-way diff matching spike-D's frame-N+1+ stream).
- [ ] Write-up: confirm Lua-side `init()` rebuilds caches, no cart-side
      save-aware code was required.

Stage 2 entry criterion: a `cart_state_t` design that captures enough
of the Lua workload's per-frame inputs to let the load-side cart resume
deterministically.  Until that is settled, the Lua workloads cannot
round-trip via the same simple memcpy mechanism Stage 1 demonstrates.

## Stage 3 — Audio voice-end queue (ADR-0106)

NOT YET IMPLEMENTED.

- [ ] `lib/synthetic_mixer.{c,h}` — deterministic per-cart schedule of
      `(handle, ends_at_frame)` tuples; reports voice-end events at
      end-of-frame.
- [ ] `cart_runtime/region_voice_end_queue.{c,h}` — `runtime_voice_end_queue_t`
      (logical_view_bits + pending FIFO) per ADR-0106; save/load that
      preserves handle order.
- [ ] `workloads/det_audio_branch.lua` — two voices, scheduled ends at
      frames 5 / 12, accumulator branches on `is_playing` answers so any
      one-frame divergence shows in the digest.
- [ ] Save points 11 (post-application) and 5 (pending-application);
      cross-load matrix on both.

## Stage 4 — Coroutine save hooks (ADR-0012)

NOT YET IMPLEMENTED.

- [ ] `cart_runtime/region_coroutine_save_blob.{c,h}` — fixed-slot table
      mapping coroutine-id → POD save struct (PLAN.md §Stage 4
      simplification — recursive Lua-table flattening is out of scope).
- [ ] `workloads/det_cutscene.lua` — uses `blyt32.coroutine.create{
      start, save, restore }`; resumed each frame.
- [ ] `workloads/det_transient_coroutine.lua` — negative test: transient
      `coroutine.create()` across save/restore must throw
      `RuntimeError: coroutine crossed a save/restore boundary`.
- [ ] Save at frame 7 mid-cutscene; cross-load digest match;
      transient-coroutine error string match on stderr (both hosts).

## Stage 5 — Screen shake (ADR-0051)

NOT YET IMPLEMENTED.

- [ ] `cart_runtime/region_screen_shake.{c,h}` — 4-field POD;
      deterministic per-frame offset from `(frame_count, seed,
      intensity)`.
- [ ] `workloads/det_shake.lua` — 20-frame shake at frame 3; folds
      offset bytes into `frame_state.accum_misc`.
- [ ] Save at frame 10 mid-shake; cross-load digest match.

## Stage 6 — Cross-host matrix and corruption-detection gates

PARTIAL.

- [x] Whetstone × 4 directions PASS (Stage 1).
- [x] Corruption tests (magic / version / layout_hash / truncation /
      total_size > buffer / clean buffer) — **PASS on both hosts**.
- [ ] Lua workloads × 4 directions (gated by Stages 2–5).
- [ ] Buffer SHA-256 manifest (`digests/buffer-hashes.txt`) — pending
      Stages 2–5 buffers.
- [ ] Digest SHA-256 manifest (`digests/digest-hashes.txt`) — pending
      Stages 2–5 streams.
- [ ] One-field-reordered cart_state rebuild + load-rejection negative
      test — pending Stage 2's cart_state_t.

## Results write-up

- [x] `docs/design/spike-k-results.md` — Stage 1 written up; Stages 2–5
      flagged as remaining.
