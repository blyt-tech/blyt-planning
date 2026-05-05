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

- [x] `cart_runtime/cart_state_lua_simple.{c,h}` — POD struct with 8
      entities × (x, y, a) f32; hand-authored layout descriptor mirrors
      what an ADR-0009 packer would emit.
- [x] `ports/rv32emu/lua_det_bindings_k.c` — spike-K's Lua bindings:
      `console.{rng, unit_float, add_misc, frame, commit_frame,
      set_entity, get_entity, is_load_resume, num_frames}`.
- [x] `workloads/det_lua_simple.lua` — Lua workload that keeps every
      bit of per-entity state in cart_state_lua_simple (no Lua tables),
      reads it back via `get_entity` each frame, and observes a
      `is_load_resume()` flag to skip its init phase on resume.
- [x] `ports/rv32emu/lua_simple_{save,load,full}.c` — Lua-VM cart
      drivers (load / register bindings / `lua_pcall(workload)`).
- [x] **Stage 2 PASS** — 4-way cross-host matrix byte-identical AND
      strong gate matches straight-through suffix (same Lua workload
      run end-to-end).
- [x] **Lua-side restore semantics check.**  The cart's `init` (the
      Lua-side `init_entities()` call) is gated by `is_load_resume()`;
      no cart-side save-aware code beyond that single conditional.
      The deserializer fills cart_state_lua_simple from the buffer;
      `console.frame()` returns `save_frame + 1`; the Lua loop picks
      up at that frame.  No metatables / userdata / Lua-side caches
      to rebuild in this minimal demo.

Deferred — extending the existing spike-D workloads:
- [ ] PLAN.md called for wrapping the existing `det_doom_tick.lua` and
      `det_entity_update.lua` workloads.  Those workloads carry per-mob
      / per-entity simulation state (angle, hp, state, tics, alive,
      ...) in Lua tables.  Either the workloads need to be modified to
      keep that state in a C-side cart_state region (the
      `det_lua_simple.lua` pattern this spike validates), or the
      runtime needs ADR-0011 SOA wrappers that back Lua tables with
      C memory transparently.  The byte-image round-trip property
      under test is fully exercised by `det_lua_simple` — extending
      to the existing workloads is engineering on top of that.

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
