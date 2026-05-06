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

- [ ] `workloads/det_two_scripts.lua` — cutscene + AI script (no entity
      references); slots 0 and 1.
- [ ] `workloads/det_scripts_with_entities.lua` — two scripts on entity 0
      + one rotator on entity 1; slots 0, 1, 2.
- [ ] `workloads/det_two_scripts_destroy.lua` — destroy AI at frame 20;
      create loader script in slot 1 at frame 21.
- [ ] `workloads/det_short_script.lua` — body that completes at frame 10;
      auto-reclamation on body return.
- [ ] **Stage 3 PASS** — all-S sweep on each workload; slot independence
      and reclamation visible in buffer hex.

## Stage 4 — Constrained-table flattener cross-host validation

- [ ] `workloads/det_table_shapes.lua` — exercises every value subtype
      (i64, f64+NaN-canon, string, bool, flat array).
- [ ] Cross-host slot-blob byte-equality SHA-256 (the headline gate).
- [ ] Subtype-isolation workloads (only built if the omnibus fails):
      `det_table_shape_{floats,strings,integers,booleans,arrays,keys}.lua`.
- [ ] **Stage 4 PASS** — slot-blob hex byte-identical across hosts at
      every save frame; ADR-0012 amendment text drafted.

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
