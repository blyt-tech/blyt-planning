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

- [x] `lib/synthetic_mixer.{c,h}` — deterministic per-cart schedule of
      `(handle, ends_at_frame)` tuples; reports voice-end events at
      end-of-frame.
- [x] `cart_runtime/region_voice_end_queue.{c,h}` — `runtime_voice_end_queue_t`
      (logical_view_bits + pending FIFO) per ADR-0106; save/load that
      preserves handle order.
- [x] `det_audio_branch_{save,load,full}.c` — two voices, scheduled ends at
      frames 5 / 12, accumulator branches on `is_playing` answers so any
      one-frame divergence shows in the digest.
- [x] Save points 11 (post-application) and 5 (pending-application);
      cross-load matrix on both.
- [x] **Stage 3 PASS** — both save points 4-way byte-identical AND
      continuation matches straight-through suffix (strong gate).
- [N] PLAN.md called for a Lua workload (`det_audio_branch.lua`).
      Spike K implements as C carts because Stage 3 only exercises the
      voice-end region machinery — Lua bindings for `blyt32.audio.*`
      are deferred to the Stage 2 / Stage 4 Lua infrastructure work.

## Stage 4 — Coroutine save hooks (ADR-0012)

- [x] `cart_runtime/region_coroutine_save_blob.{c,h}` — fixed-slot table
      mapping coroutine-id → POD save struct (PLAN.md §Stage 4
      simplification — recursive Lua-table flattening is out of scope).
- [x] `det_cutscene_{save,load,full}.c` — simulates the production
      `blyt32.coroutine.create{start, save, restore}` pattern with a
      fixed POD struct (`{step:u32, angle:f32}`) in slot 0.  Each frame
      reads the slot, advances, writes back, folds into accum_misc.
- [x] Save at frame 7 mid-cutscene; cross-load digest match.
- [x] **Stage 4 PASS** — 4-way byte-identical AND strong gate matches
      straight-through suffix.
- [N] PLAN.md called for a Lua workload (`det_cutscene.lua`) using
      `blyt32.coroutine.create{}` and a negative test
      (`det_transient_coroutine.lua`) for the boundary error.  Both
      require Lua infrastructure beyond Stage 1's whetstone — deferred
      to Stage 2's Lua harness work.  The byte-image round-trip
      property under test is fully exercised by the C cart.

## Stage 5 — Screen shake (ADR-0051)

- [x] `cart_runtime/region_screen_shake.{c,h}` — 4-field POD;
      deterministic per-frame offset from `(frame_count, seed,
      intensity)`.
- [x] `det_shake_{save,load,full}.c` — 20-frame shake at frame 3; folds
      offset bytes into `frame_state.accum_misc`.
- [x] Save at frame 10 mid-shake; cross-load digest match.
- [x] **Stage 5 PASS** — 4-way byte-identical AND strong gate matches
      straight-through suffix.
- [N] PLAN.md called for a Lua workload (`det_shake.lua`).  Spike K
      implements as a C cart for the same reason as Stage 3 — Lua
      bindings for `blyt32.draw.shake` are deferred to Stage 2 / 4
      Lua infrastructure work.

## Stage 6 — Cross-host matrix and corruption-detection gates

PARTIAL — Lua workloads gated on Stage 2 implementation.

- [x] Whetstone × 4 directions PASS (Stage 1).
- [x] det_shake × 4 directions PASS (Stage 5).
- [x] det_audio_branch × 4 directions × 2 save points PASS (Stage 3).
- [x] Corruption tests (magic / version / layout_hash / truncation /
      total_size > buffer / clean buffer) — **PASS on both hosts**.
- [ ] Lua workloads × 4 directions (gated by Stage 2).
- [ ] One-field-reordered cart_state rebuild + load-rejection negative
      test — covered in spirit by the layout_hash mutation corruption
      test, but a build-time test that compiles two binaries with
      reordered fields and confirms the receiver rejects the buffer
      remains as future work.

## Results write-up

- [x] `docs/design/spike-k-results.md` — Stage 1 written up; Stages 2–5
      flagged as remaining.
