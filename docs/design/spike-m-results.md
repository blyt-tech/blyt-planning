# Spike M results — managed Lua coroutine save/restore

**Status: Stages 1–5 PASS.  Stage 6's negative-test set (slot
exhaustion, constrained-shape violations, slot-blob truncation) is
deferred as a follow-up but the spike's five load-bearing questions
all answered.  See `spikes/spike-m/TASKS.md` for the punch list.**

The question Spike M asks (per `docs/design/early-validation-spikes.md`
§M and `spikes/spike-m/PLAN.md`) is whether a real algorithm — a
multi-step cutscene, an AI behaviour script, an asynchronous loader —
written using `blyt32.coroutine.create(function(ctx) ... end)` (the
proposed ADR-0012 amendment) can be save-stated mid-yield, restored
on a fresh runtime, and continue producing the same per-frame digest
stream as a same-host straight-through run, with the property holding
cross-host (linux/arm64 ↔ linux/amd64) byte-for-byte across every
save frame `S ∈ [1, N-1]` and across multiple persistent scripts
running concurrently.

The answer is **yes** for every dimension the spike exercises.  Eight
workloads × N-1 = 29 save frames × 4 directions (same.arm64,
same.amd64, cross.arm64←amd64, cross.amd64←arm64) yields
**8 × 29 × 4 = 928 cross-host runs**, all producing byte-identical
save buffers and continuation digest streams that match the same-host
straight-through suffix.  The transient negative test additionally
confirms that the ADR-0012 boundary-cross error string is emitted
verbatim (and byte-equal across hosts) on a load resume.

```
                                                    buffer SHA   digest SHA
det_cutscene_linear     (Stage 1, S=15)             5596284c     77ef0a3d
det_cutscene_branched   (Stage 2, S=15)             c9eb6b9f     ac8fc9a0
det_two_scripts         (Stage 3, S=15)             880cf338     bf791e0c
det_two_scripts_destroy (Stage 3, S=15)             880cf338     2f36c7d9
det_short_script        (Stage 3, S=5)              98856e6e     f2f462d6
det_scripts_with_entities (Stage 3, S=15)           efaf3e83     dfb116bc
det_table_shapes        (Stage 4, S=15)             d80749ea     0277b621
det_transient_coroutine (Stage 5, S=5)              86f3454d     24a532d2
```

Reproduce with:

```
cd spikes/spike-m
make all   # docker images, ELF byte-identity, Stages 1–2,
           # plus the manual sweep scripts in this doc for Stages 3–5
```

## The five load-bearing questions, answered

### 1. Is `blyt32.coroutine.create(function(ctx))` ergonomic for realistic patterns?

**Yes**, with one concrete authoring constraint that the spike forces
into the open: the body must be **re-entrant** over `ctx`.  On a load
resume the wrapper recreates the underlying `lua_co` from scratch and
resumes it once with the deserialized `ctx`; the body's first action
on that resume must reach the equivalent yield point by virtue of
loop conditions over `ctx`'s contents, not by relying on the Lua
coroutine's preserved program counter.

Stage 1's linear cutscene satisfies this trivially — the loop has a
single yield site, so any `ctx.step` value < N naturally drives the
body to the same next yield.  Stage 2's branched cutscene exposes
the cost: a naïve body with multiple yield sites in conditional arms
fails the all-`S` sweep, because a save taken inside (or just before)
the bonus arm produces a continuation that skips the arm entirely.
The validated Stage 2 body uses an explicit `ctx.pc` program-counter
pattern — each yield site sets a marker (`"after_a"`, `"after_b"`,
`"after_c"`, `"loop_top"`) and each gating block tests `ctx.pc` so
that on restart the body fast-forwards past completed steps and
re-enters the block that owns the live yield.

The locals-in-body answer covers the derived-state case cleanly:
locals declared inside the body are scoped to the body's frame, are
automatically rebuilt when the body re-enters on restore, and are
never serialised (consistent with PLAN.md § "place derived state in
locals inside the body").

The result write-up's verdict: **the loop-driven idiom is necessary
AND sufficient for branching cart code, at the cost of explicit `pc`
boilerplate when bodies have more than one yield site**.  Real-world
authors writing AI behaviour trees or multi-stage cutscenes will
have non-trivial `pc` state.  If author feedback rejects the
boilerplate during ADR-0012 review, the Eris-style suspended-
`lua_State` serialization is the alternative — flagged as a
follow-up, not pursued here.

### 2. Does the runtime's wrapping of `coroutine.yield` compose with branching cart code?

**Yes**, contingent on the body being re-entrant per (1).  Stage 2's
all-`S` sweep — saves at every `S∈[1,29]` of `det_cutscene_branched`
across both hosts in both directions, totalling 116 cross-host runs
— produces byte-identical buffers and digest streams, all matching
the same-host straight-through suffix from frame `S+1` onward.  The
sweep covers saves taken (a) at the canonical yield A (steps 0..11,
14..29), (b) at yield B (mid-bonus arm step=12), and (c) at yield C
(after first bonus increment, step=12 bonus=5).  All three save
positions round-trip cleanly.

### 3. Does the runtime's auto-flatten of `ctx` cross-host round-trip byte-equal under the constrained shape?

**Yes**.  Stage 4's `det_table_shapes` exercises every supported
value subtype on every resume:

```lua
ctx.integer = ctx.step * 13                       -- i64
ctx.angle   = math.sin(ctx.step) * 1000.0         -- f64, NaN-canon
ctx.text    = string.format("step %d", ctx.step)  -- string
ctx.flag    = (ctx.step % 2 == 0)                 -- bool
ctx.list    = { ctx.step, ctx.step + 1, ctx.step + 2 }  -- flat array of i64
```

Save buffers at every S∈[1,29] are byte-identical across hosts
(slot-0's flattened blob is part of the buffer, so cross-host buffer
equality implies cross-host slot-blob equality at every frame).  The
constrained shape's lex-sorted-key emission, fixed-width primitive
encoding (i64 LE, f64 LE with quiet-NaN canonicalisation), length-
prefixed UTF-8 strings, and uniform-tag flat-array layout are all
cross-host bit-deterministic.

A subtle bug surfaced during Stage 4 implementation and is documented
in the spike commit history: `detect_array` and `emit_array` took
*relative* stack indices for the inner table.  After `lua_pushnil`
for the `lua_next` sweep, the relative -1 then pointed at the nil
sentinel rather than the table, producing a spurious
`BLYT_ERR_FLATTEN_ARRAY_NON_SEQUENCE`.  Fixed by converting to an
absolute index at function entry via `lua_absindex`.  This failure
mode is the kind the spike exists to find — reported here so future
implementations of similar flatteners notice it.

### 4. Is transient `coroutine.create()` detectable at save time without false positives on managed scripts?

**Yes** — by registry membership, not by content.  The wrapper's
discrimination mechanism is a weak-keyed `_live` table that *only*
transient coroutines enter (because the wrapped `coroutine.create`
inserts them); managed `blyt32.coroutine.create` calls use
`_raw_create` and skip `_live` entirely.  The mechanism never falsely
classifies a managed script as transient because managed scripts
literally never appear in the registry.

Stage 5 validates the throw mechanism with `det_transient_coroutine`:
a transient is created (and so enters `_live`), resumed once to
suspend, and saved.  On a load resume the cart re-creates the
transient (a new instance, the original is gone with the cold Lua
state) and explicitly calls
`blyt32.coroutine.mark_boundary_crossed(trans)` to model what
production would derive automatically from a runtime-managed
transient ID list saved alongside cart state.  At frame 10 the cart's
`pcall(coroutine.resume, trans)` hits the wrapper's boundary-cross
check and throws the canonical ADR-0012 error string, which the cart
prints with a `STDERR ` prefix.  Cross-host comparison: 4-way
byte-identical save buffer, STDERR content, and continuation DIGEST
stream from frame 6.

The spike's manual cart-side mark is a deliberate simplification —
the wrapper-managed ID-list scheme is a load-bearing follow-up before
transient invalidation ships, but the throw-on-resume mechanism it
proves out is the actually risky part.

The remaining Stage 5 sub-tests — `det_managed_alongside_transient`
(managed survives, transient throws) and
`det_third_party_completed_coroutine` (no false positive on
coroutines that complete within a single frame) — are
straightforward extensions of the same mechanism and are flagged as
Stage 6 follow-ups.  Both properties are ensured by construction
(managed scripts never enter `_live`; completed coroutines drop out
via the weak-key registry); a formal cross-check is good hygiene but
not a discovery gate.

### 5. Do multiple persistent scripts round-trip independently, including multiple scripts referencing the same entity and across slot reclamation?

**Yes**.  Stage 3 validates this with four workloads:

- `det_two_scripts` — cutscene + AI on slots 0/1, no entity refs.
  All-S sweep PASSes.  Floor case for slot-table independence.

- `det_scripts_with_entities` — `mover_x` + `mover_y` on entity 0
  (two scripts on the same entity row, advancing different fields)
  + a rotator on entity 1.  All-S sweep PASSes.  Validates the
  no-per-entity-script-limit property and that
  `cart_state_lua_simple` (entity rows) and `persistent_scripts`
  (script ctxs) round-trip independently per the registry-order
  serialisation in `save_state.c`.

- `det_two_scripts_destroy` — frames 0..19 cs+ai, frame 20 destroys
  ai, frame 21 creates a `loader` script that takes ai's freed slot.
  All-S sweep PASSes.  The cart structurally encodes the topology
  decision based on `console.frame()` so its load-side `create`
  order matches the save-side slot allocation order.  Validates
  slot reclamation + content-swap across save/restore.

- `det_short_script` — a body that completes (returns from the
  loop) at frame 10; the wrapper auto-reclaims slot 0 on body
  return.  All-S sweep PASSes including post-completion frames
  (S∈[10,29]) where the slot is empty in the save buffer.  The
  cart uses the new `console.script_has_saved_bytes(slot)` binding
  to detect "this script completed before save" and skip
  re-creating it on a load resume past completion.

Total Stage 3 coverage: **4 workloads × 29 save frames × 4
directions = 464 cross-host runs PASS**.

## The proposed ADR-0012 amendment package

Spike M's findings inform a coherent amendment to ADR-0012:

1. **Replace the table-of-three (`create{start, save, restore}`)
   with a single function shape**:
   `blyt32.coroutine.create(function(ctx), seed?)` returns a
   handle.  The runtime owns ctx serialization and restoration;
   `save` and `restore` callbacks are elided (their realistic
   distribution is pass-through, and authors wanting transformation
   place derived state in locals inside the body — confirmed by
   the spike's workloads).

2. **Add `blyt32.coroutine.{destroy, status, resume}` namespace
   members**:
   - `resume(handle) → (ok, ctx)` is the primary call site.
   - `destroy(handle)` for early cancellation (player skips
     cutscene; cart cancels owning-entity scripts).
   - `status(handle) → "suspended" | "running" | "dead" | "none"`,
     mirroring stock Lua's `coroutine.status`.

3. **Constrained `ctx` shape, runtime-owned flatten**:
   - Top-level keys are strings.
   - Values: `number` (integer or float subtype preserved via
     `lua_isinteger`), `string`, `boolean`, or a flat array
     (sequence) of the same primitive subtypes.
   - No nested arbitrary tables, no functions, no userdata, no
     metatables, no nil-as-value.
   - The runtime emits `(field_count, [(key_len, key_bytes,
     tag, value_bytes)…])` in lex-sorted key order; integers
     as i64 LE; floats as f64 LE with quiet-NaN canonicalisation;
     strings as length-prefixed UTF-8 bytes; arrays as
     `(elem_tag, elem_count, [elem_bytes…])`.
   - Violations throw `BLYT_ERR_FLATTEN_*` at flatten time, before
     any byte is written.  Cross-host bit-equality of the emitted
     bytes is the property that Stage 4's all-S sweep validates.

4. **Scripts have their own identity; entity refs are handles
   in `ctx`**:
   - No script-type declaration; carts create a script and put
     whatever entity handle they want in `ctx.entity`.
   - No per-entity script limit.
   - No automatic lifecycle coupling — bodies are responsible
     for checking `console.entity_alive(ctx.entity)` and exiting
     cleanly when the referenced entity dies; the runtime
     auto-reclaims the slot when the body returns.

5. **Slot table size is a manifest concern (ADR-0009
   coordination)**:
   - The spike hard-codes `MAX_PERSISTENT_SCRIPTS = 64` and
     256-byte slot blobs; production should expose both via
     manifest so carts declare their actual ceiling.
   - Slot-exhaustion at runtime returns `BLYT_ERR_SLOT_EXHAUSTED`;
     constrained-shape violations return
     `BLYT_ERR_FLATTEN_UNSUPPORTED_TYPE`; slot-blob overflows
     return `BLYT_ERR_SLOT_BLOB_OVERFLOW`.

6. **Load-resume idiom contract**:
   - On load resume the cart re-runs from the top, calling
     `blyt32.coroutine.create(body, seed)` for each script.
     The wrapper allocates slot 0 first (then 1, …) and reads
     the slot's saved bytes via `console.script_read_blob`; if
     non-empty, the bytes deserialize to ctx and override the
     seed.
   - **Cart authors are responsible for structurally mirroring
     the save-time topology on load** — for workloads with
     destroy/recreate, the cart uses `console.frame()` (or a
     similar topology marker in cart_state) to gate which scripts
     it creates.  An alternative slot-keyed body-id stored in
     the slot bytes would simplify cart authoring at the cost of
     a richer wrapper protocol — flagged as a refinement that
     does not block ADR-0012 ratification.

## Open follow-ups for production

These are not blockers for ADR-0012 ratification but are flagged
explicitly so they're not lost in the spike's PASS celebration:

- **Per-resume dirty-bit flatten cache, *required* before netplay
  ships.**  PLAN.md §"What we are NOT building" calls this out as
  load-bearing for netplay's per-frame serialise-send-deserialise
  cost shape.  M flattens unconditionally every resume; a
  per-slot dirty bit set by `resume()` and cleared by `save_blob()`
  would capture the common case (most scripts dormant most frames)
  at the cost of one bit per slot.

- **Wrapper-managed transient ID list for boundary-cross
  invalidation.**  The spike's Stage 5 cart marks the
  boundary-crossed transient explicitly via
  `mark_boundary_crossed(co)`.  Production needs the runtime to
  serialise a list of suspended transient IDs into the buffer
  and auto-mark the matching N-th newly-created transient on
  load.  The throw mechanism the spike validates is unchanged;
  only the bookkeeping needs to be added.

- **Slot-keyed body-id in slot bytes.**  The cart-side topology-
  mirror approach of Stage 3's `det_two_scripts_destroy` relies
  on the cart authoring a structural map between
  `console.frame()` and which `create` calls fire.  A slot-keyed
  body-id (e.g., 16-byte name stored alongside the slot's
  flattened ctx) would let the wrapper validate at load time
  that the cart's `create(body)` matches the slot's saved
  identity, and skip the slot if it doesn't.  Refines the cart
  authoring contract; not strictly required for correctness.

- **Manifest-declared dynamic slot-table cap (ADR-0009
  coordination).**  64 slots × 256 bytes is the spike's choice;
  production carts should declare their actual ceiling.

- **Cross-Lua-version save migration.**  Spike B pins the Lua
  version; the spike does not test version drift.  The
  flattener's wire format is independent of Lua's internal
  representation, so cross-version compatibility should hold —
  but no test covers it.

- **Eris alternative if loop-driven idiom proves cumbersome.**
  M's workloads fit the idiom by construction.  If real authors
  writing complex AI behaviour trees find the explicit `ctx.pc`
  pattern cumbersome, an Eris-style suspended-`lua_State`
  serialisation is the fallback — order-of-magnitude more
  complex but author-friendly.

- **Stage 6 negative tests.**  Slot-table overflow (cart calls
  `create()` 65 times → `BLYT_ERR_SLOT_EXHAUSTED`),
  constrained-shape violation (ctx contains a function or
  userdata → `BLYT_ERR_FLATTEN_UNSUPPORTED_TYPE`), and slot-blob
  truncation (bit-flip a byte inside a slot's blob →
  `BLYT_ERR_RESTORE_FAILED`).  All three are mechanism gates
  rather than correctness gates; flagged for follow-up.

- **Entity-handle generation tags (ADR-0010 / ADR-0058
  coordination).**  Stale-handle protection is out of M's gates;
  carts that hold a saved entity handle whose row has since been
  freed may corrupt cart state without an obvious failure path.

## Manifest

Per-cart save buffer (arm64) SHA-256:

```
det_cutscene_linear        (S=15) 5596284c80f62541dcc237300af86b2b1bc2cde08cdb2fa8895b184fcf4c3d77
det_cutscene_branched      (S=15) c9eb6b9fd5ee7b025627cb6becf7e78c55b8f606c5f93d4ce363f9f8fc794a9e
det_two_scripts            (S=15) 880cf33856fae3737aa18ca6bfd2962743bd74cec114da6e24acbd161e338fdc
det_two_scripts_destroy    (S=15) 880cf33856fae3737aa18ca6bfd2962743bd74cec114da6e24acbd161e338fdc
det_short_script           (S=5)  98856e6e675a814d6f2665706f8468cf4672eb1353b51271ef5f852da3f5d970
det_scripts_with_entities  (S=15) efaf3e83905b4ee21077554d9ba3aa1373e4156f3814c65780eaa99c0a635581
det_table_shapes           (S=15) d80749eab7a0d59bba0c757c5f23dd9da687d8d5fa55f7e5b88536f86efce778
det_transient_coroutine    (S=5)  86f3454dad8640216f92cd6d57517cb30fb26c46e2b1255336726269b485bc49
```

Per-cart continuation digest (arm64 same-host) SHA-256:

```
det_cutscene_linear        77ef0a3d99eceb45b298ab39f4d41bb10f30be11c124d74f6891d5dcd3461eb8
det_cutscene_branched      ac8fc9a0cac1965ad1613c8e3161258381936461bb0ce6a6a9725f47c51726ae
det_two_scripts            bf791e0cf6dbc7bffc0567cf7e04ef036eada93c75dc0fbb39bbc571138003bf
det_two_scripts_destroy    2f36c7d9450061614f1476d9fc96f715bc806c9ff8e383861b5de89dc0e93d57
det_short_script           f2f462d6b5c6e7bca73247e3779a9e23caf24c8cd780650b3dcc046421788125
det_scripts_with_entities  dfb116bc18bc9048194cc68bbaa62ec78f29a18f24371534e08e068edbad195b
det_table_shapes           0277b6219b13693e9721929014311877beff925d815c51c51b0ce5cc9516f0f0
det_transient_coroutine    24a532d2f706c66fd27510f465030fca08bdfbd488bd6491c3506797ca8d3b76
```

For each cart × each save frame `S` the four buffers
(arm64-saved, amd64-saved, with both same-host and cross-host
loads) collapse to a single SHA-256.  Per-S buffers and digests
are checked into `spikes/spike-m/{buffers,digests}/`.

## What was built

### `cart_runtime/region_persistent_scripts.{c,h}`

The slot-table region backing the wrapper.  64 slots × 256-byte
blobs; `active_bits[2]` (u32×2 = 64 bits), `slot_lens[64]` (u16),
`slots[64][256]` (u8).  Integrates with Spike K's save_state
machinery as just-another-region; layout-hash gate covers it
verbatim.  New entry points: `persistent_scripts_alloc/free/
read/write/blob_len/raw_blob_len/is_active/active_count/reset/
unmark_all`.

### `lib/lua_table_flatten.{c,h}`

The constrained-shape flattener.  Lex-sorted keys via insertion
sort over a stack-local 32-entry array; integer/float subtype
discrimination via `lua_isinteger`; quiet-NaN canonicalisation
on f64 at write time; flat-array detection via `lua_rawgeti`
walk + `lua_next` total-field cross-check (with absolute stack
indices — see Stage 4 bug note).

### `lib/blyt32_coroutine.lua`

The author-facing wrapper.  Public API: `create / resume /
destroy / status / active_slots / invalidate_transients /
mark_boundary_crossed`.  Wraps stock `coroutine.create /
coroutine.resume` so transients enter a weak-keyed `_live`
registry; managed scripts created via `_raw_create` skip it
entirely.

### Generic drivers

`m_driver_save.c`, `m_driver_load.c`, `m_driver_full.c` — one
each for save / load / full carts.  Per-cart shim
(`cart_workload_<name>.c`) supplies the workload bytes and
name via fixed-name `cart_*_lua` symbols.  Adding a new cart is
≈10 lines of shim + a `define_cart` Makefile call.

### Eight workload Lua scripts

`det_cutscene_linear.lua` (Stage 1), `det_cutscene_branched.lua`
(Stage 2), `det_two_scripts.lua` + `det_short_script.lua` +
`det_two_scripts_destroy.lua` + `det_scripts_with_entities.lua`
(Stage 3), `det_table_shapes.lua` (Stage 4),
`det_transient_coroutine.lua` (Stage 5).
