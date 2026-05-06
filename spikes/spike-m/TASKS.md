# Spike M — task checklist

Per-stage checklist mirroring `PLAN.md`'s structure.  Tick boxes show what is
complete and what remains.

## Stage 1 — `blyt32.coroutine.create(function(ctx))` author API on a real Lua VM

- [x] Directory layout (`cart_runtime/`, `lib/`, `ports/`, `workloads/`,
      `digests/`, `buffers/`, `elfs/`).
- [x] Symlinks from spike-K's `cart_runtime` (digest, pcg32, nan_canon,
      frame_state, save_state, runtime_tracked, region_frame_state, save_io,
      cart_state_lua_simple); excluded from the Docker build context via
      `.dockerignore` and brought in at build time via
      `COPY --from=spike-k` (named build context "spike-k" passed by the
      Makefile).
- [x] `cart_runtime/region_persistent_scripts.{c,h}` — slot table region
      backing the Lua wrapper.
      - **Sizing deviation from PLAN:** `MAX_PERSISTENT_SCRIPTS = 64`
        (vs. 256) and `PERSISTENT_SCRIPT_SLOT_BYTES = 256` (vs. 4096).
        The spike's workloads use ctx tables under 64 bytes, so 64 ×
        256 = 16 KiB is generous for the spike while keeping the save
        buffer footprint manageable.  The slot-exhaustion negative test
        (Stage 6 step 30) creates 65 scripts instead of 257 to exercise
        the same property; production should expose both numbers via
        manifest (ADR-0009).
- [x] `lib/lua_table_flatten.{c,h}` — constrained-shape flattener with
      LTF_TAG_{BOOLEAN,INTEGER,FLOAT,STRING,ARRAY}.  Lex-sorted keys
      via insertion sort.  NaN-canonicalises f64 at flatten time.
- [x] `lib/blyt32_coroutine.lua` — Lua wrapper module:
      `blyt32.coroutine.{create, resume, destroy, status, active_slots,
      invalidate_transients}`.  Wraps stock `coroutine.create` /
      `coroutine.resume` so transient coroutines enter a weak-keyed
      `_live` registry; managed coroutines never enter it.
- [x] `workloads/det_cutscene_linear.lua` — Stage 1 workload (single
      managed coroutine; ctx = {step, angle}; 30 frames).
- [x] `ports/rv32emu/lua_det_bindings_m.c` — `console.*` bindings:
      Spike K's set + `script_alloc`, `script_free`, `script_write_blob`,
      `script_read_blob`, `script_is_active`, `max_persistent_scripts`,
      `lua_table_flatten`, `lua_table_unflatten`.
- [x] `ports/rv32emu/det_cutscene_linear_{save,load,full}.c` — per-cart
      drivers (load Lua, register bindings, run wrapper module + workload,
      save / load / straight-through).
- [x] `ports/rv32emu/regions_det_cutscene_linear.c` — registry array
      `[frame_state, cart_state_lua_simple, persistent_scripts]`.
- [x] `ports/rv32emu/Makefile` — builds three ELFs; reuses spike-B's
      `lua_runtime` / `softfloat-glue` / `crt0.S` and spike-K's libm-rv32.a.
- [x] `Dockerfile` — extends `fc32-spike-k-{arm64,amd64}`; spike-K
      sources brought in via the named "spike-k" build context.
- [x] Top-level `Makefile` — `docker-build`, `elf-bytes-diff`,
      `stage-1-{save,buffer-diff,load,diff}`, `stage-1`, `clean`.
- [x] **Stage 1 PASS** — Docker images build; cart ELFs byte-identical
      across hosts (`sha256` matches arm64↔amd64); saved buffers
      byte-identical across hosts (`sha256 5596284c…`); four continuation
      digest streams (same-arm64, same-amd64, cross-arm64←amd64,
      cross-amd64←arm64) collapse to a single sha256 (`77ef0a3d…`),
      and that hash matches the same-host straight-through full run's
      frame 16..29 suffix.  Workload: linear cutscene, single managed
      coroutine, ctx={step, angle}, save at frame 15.

## Stage 2 — Branching cutscene + the all-`S` save-frame sweep

- [x] `workloads/det_cutscene_branched.lua` — bonus-arm conditional yield.
      *Important note:* the naïve form of the body (no `ctx.pc` marker)
      fails the all-S sweep because, on a load resume, the freshly
      created Lua coroutine restarts at line 1 of the body — so a save
      taken inside or just before the bonus arm produces a continuation
      that skips the arm entirely.  The body therefore uses an explicit
      `ctx.pc` program-counter pattern: each yield site sets a marker
      (`"after_a"`, `"after_b"`, `"after_c"`, `"loop_top"`) and each
      gating block tests `ctx.pc` so that on restart the body
      fast-forwards past completed steps and re-enters the block that
      owns the live yield.  The naïve form is documented in the result
      write-up as the motivation for the loop-driven idiom — and as
      the trigger for the Eris alternative discussion if real authors
      reject the boilerplate.
- [x] Generic drivers (`m_driver_{save,load,full}.c`) + per-cart shim
      (`cart_workload_<name>.c`) factored out in this stage; adding a
      new cart is now a 10-line shim + region registry + workload .lua
      + a `define_cart` Makefile invocation.
- [x] `make stage-2-sweep` — loop over S∈[1,N-1] across both hosts.
- [x] `make stage-2-diff` — collapse 4 streams per S to one hash;
      compare against same-host straight-through suffix.
- [x] **Stage 2 PASS** — every S∈[1,29] of det_cutscene_branched
      produces (a) byte-identical save buffers across hosts and
      (b) four byte-identical continuation digest streams that match
      the same-host straight-through suffix from frame S+1.  Total:
      29 × 4 = 116 cross-host runs PASS.  Result confirms the
      author-facing `ctx.pc` re-entrancy idiom is necessary AND
      sufficient for branching cart code.

## Stage 3 — Multiple concurrent scripts and the entity-handle pattern

- [x] `workloads/det_two_scripts.lua` — cutscene + AI script, slots 0
      and 1, PASSes the all-S sweep.  Validates the floor case for
      slot-table independence.
- [x] `workloads/det_scripts_with_entities.lua` — two scripts on entity
      0 (mover_x + mover_y, advancing different fields of the same
      entity row) + one rotator on entity 1.  PASSes the all-S sweep.
      Validates: no per-entity script limit; cart_state_lua_simple +
      persistent_scripts round-trip independently per save_state's
      registry-order serialisation.
- [x] `workloads/det_two_scripts_destroy.lua` — frames 0..19 cs+ai;
      frame 20 destroy ai; frame 21 create loader → slot 1.
      PASSes the all-S sweep.  Cart structurally encodes the
      topology decision based on `console.frame()` so its load-side
      create order matches the save-side slot allocation order.
      Validates slot reclamation across save/restore.
- [x] `workloads/det_short_script.lua` — body completes at frame 10;
      auto-reclaim on body return.  PASSes the all-S sweep.  Cart
      uses the new `console.script_has_saved_bytes(slot)` binding
      to detect "this slot was empty at save time" and skip
      re-creating the script on a load resume past completion.
- [x] **Stage 3 PASS** — 4 workloads × 29 save frames × 4 directions
      = 464 cross-host runs PASS.  Slot independence, slot
      reclamation (both explicit destroy and implicit body-return),
      and the entity-handle pattern (same entity referenced by
      multiple scripts; entity rows round-trip via existing
      cart_state_lua_simple region) all validated.

Stage 3 design choices flagged for the result write-up:
- The cart's load-side logic must structurally mirror the save-side
  topology (which scripts existed at save time; which slots they
  occupied).  For workloads with destroy/recreate, the cart uses
  `console.frame()` to gate its `create` calls.  An alternative
  (slot-keyed body-id stored in the slot bytes) keeps cart
  authoring simpler at the cost of a richer wrapper protocol —
  the spike's structural approach minimises the wrapper changes
  but pushes a discipline burden onto cart authors.
- New binding `console.script_has_saved_bytes(slot)` exposes
  `slot_lens[slot] > 0` regardless of `active_bits`.  Required
  for the auto-reclaim case where the slot was occupied at save
  time but the body completed before the save snapshot.
- Wrapper now falls through to `seed` when the loaded slot has
  zero bytes (instead of unflatten'ing an empty string).  Handles
  the "destroyed-then-realloc'd-to-different-body" edge case.

## Stage 4 — Constrained-table flattener cross-host validation

- [x] `workloads/det_table_shapes.lua` — exercises every value subtype
      on every resume: i64 (`integer`), f64+NaN-canon (`angle`),
      string (`text`), boolean (`flag`), flat-array of i64 (`list`).
      Per-frame digest folds in fields from each subtype so a
      flattener bug in any one subtype localises to a digest divergence.
- [x] **Bug found and fixed:** `detect_array` and `emit_array` used
      a *relative* stack index for the inner table; after
      `lua_pushnil` for the `lua_next` sweep, the relative -1 then
      pointed at the nil sentinel rather than the table, producing
      a spurious `BLYT_ERR_FLATTEN_ARRAY_NON_SEQUENCE`.  Fixed by
      converting to an absolute index at function entry.
- [x] Cross-host slot-blob byte-equality validated implicitly via
      the save-buffer byte equality gate (the persistent_scripts
      region's bytes are part of the save buffer; equal buffers
      across hosts ⟹ equal flattened slot blobs across hosts).
- [x] **Stage 4 PASS** — every save frame S∈[1,29] of
      det_table_shapes produces (a) byte-identical save buffers
      across hosts (and therefore byte-identical slot-0 flattened
      blobs) and (b) four byte-identical continuation digest
      streams that match the same-host straight-through suffix.
      29 × 4 = 116 cross-host runs PASS.  The constrained-shape
      flattener is cross-host bit-deterministic for every
      supported subtype.

## Stage 5 — Transient `coroutine.create` boundary enforcement

- [ ] `workloads/det_transient_coroutine.lua` — transient suspended
      across save→load; resume on the load side throws ADR-0012 string.
      *Open question:* the spike's no-Eris design means transients are
      not literally preserved across save; the current wrapper records
      transient counts at save, replays the count on load, and marks
      the matching number of newly-created transients as "boundary-
      crossed".  TASKS.md flags the design choice for Stage 5
      implementation.
- [ ] `workloads/det_managed_alongside_transient.lua` — managed
      survives, transient throws.
- [ ] `workloads/det_third_party_completed_coroutine.lua` — no false
      positive on coroutines that complete before save.
- [ ] **Stage 5 PASS** — stderr byte-equal across hosts; managed-vs-
      transient discrimination correct.

## Stage 6 — Cross-host matrix, ADR-0012 amendment, write-up

- [ ] Run the full matrix (178 save-frame configurations × 4 directions
      = 712 cross-host runs).
- [ ] Buffer-bytes equality SHA-256 per (cart, S).
- [ ] Negative tests: slot-table overflow, constrained-shape violation,
      slot-blob truncation.
- [ ] `make all` — top-level PASS gate.
- [ ] `docs/design/spike-m-results.md` — five answers + ADR-0012
      amendment + open follow-ups.

## Open follow-ups & deviations

- **MAX_PERSISTENT_SCRIPTS = 64 (vs. PLAN's 256).**  Documented above.
  Production should derive both `MAX_PERSISTENT_SCRIPTS` and slot byte
  size from a manifest entry (ADR-0009).
- **Per-resume dirty-bit flatten cache.**  PLAN § "What we are NOT
  building" calls this out as load-bearing for netplay; M flattens
  unconditionally every resume.  Recommended as the top production
  follow-up.
- **Eris fallback if loop-driven idiom proves cumbersome.**  M's
  workloads fit the idiom by construction; the result write-up flags
  this if real authors find it strained.
- **Manifest-declared dynamic slot cap (ADR-0009 coordination).**
- **Cross-Lua-version save migration.**  Lua version is pinned by
  Spike B; M does not test version drift.
- **Entity-handle generation tags (ADR-0010 / ADR-0058).**  Stale-
  handle protection is out of M's gates.
