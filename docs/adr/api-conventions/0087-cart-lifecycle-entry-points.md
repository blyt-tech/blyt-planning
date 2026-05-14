# ADR-0087: Cart lifecycle entry points, reset, and autosave

## Status
Accepted

## Context

The §17 open item on cart lifecycle entry points covers the exact signatures
of the callbacks the runtime invokes on a cart, how optional callbacks are
handled, and the Lua-side naming convention. This ADR also resolves the
related reset and autosave design, which emerged from the lifecycle discussion.

## Decision

### C-level entry point signatures

The runtime discovers these as well-known exported symbols in the cart ELF.
Native carts implement them directly; the Lua shim exports them and forwards
into Lua.

```c
// Required — all carts must export these
void blyt_cart_init(void);
void blyt_cart_update(void);
void blyt_cart_draw(void);

// Optional — SDK provides weak-linked no-op defaults
void blyt_cart_on_new_state(void);
void blyt_cart_on_save_state(void);
void blyt_cart_on_load_state(blyt_load_reason_t reason);
void blyt_cart_cleanup(void);
void blyt_cart_on_credits(void);
void blyt_cart_on_quit(void);

// Native carts only — not part of the Lua lifecycle (ADR-0083)
void blyt_cart_panic(blyt_panic_reason_t reason);
```

The `blyt_load_reason_t` enum passed to `blyt_cart_on_load_state`:

```c
typedef enum {
    BLYT_LOAD_SAVE_GAME  = 0,  // cart called blyt_save_read
    BLYT_LOAD_SAVE_STATE = 1,  // frontend restored a save state (libretro or dev-mode)
    BLYT_LOAD_REWIND     = 2,  // rewind
    BLYT_LOAD_HOT_RELOAD = 3,  // dev-mode hot reload
} blyt_load_reason_t;
```

Cart signals back to the runtime:

```c
blyt_result_t blyt_quit_ready(void);     // called from on_quit when done
blyt_result_t blyt_credits_done(void);   // called from on_credits when done
```

**No `dt` parameter.** `update` and `draw` take no arguments. Time is
available via `blyt_time_frame()` — a deterministic frame counter. A `dt`
parameter would imply it could vary; it cannot (ADR-0037).

**Weak-linked defaults.** The SDK provides weak-linked implementations of all
optional callbacks. The default for `on_quit` immediately calls
`blyt_quit_ready()` so the runtime proceeds to exit without a cart-defined
handler. All other defaults are no-ops.

### Lua entry point names

The Lua shim looks up these global function names:

| C symbol | Lua name |
|---|---|
| `blyt_cart_init` | `init` |
| `blyt_cart_update` | `update` |
| `blyt_cart_draw` | `draw` |
| `blyt_cart_on_new_state` | `on_new_state` |
| `blyt_cart_on_save_state` | `on_save_state` |
| `blyt_cart_on_load_state` | `on_load_state` |
| `blyt_cart_cleanup` | `cleanup` |
| `blyt_cart_on_credits` | `on_credits` |
| `blyt_cart_on_quit` | `on_quit` |

The SDK preamble (`blyt_preamble.lua`) defines no-op implementations of all
optional functions. Cart code overrides what it needs. The shim invokes them
unconditionally; the preamble defaults handle the unimplemented case.

### State initialisation before `init`

Before every call to `blyt_cart_init` — whether at first launch or after a
reset — the runtime zeroes all cart-visible tracked state memory. This
ensures deterministic initial conditions regardless of any previous session
and removes any obligation on `init` to explicitly zero fields before use.

### Framebuffer state on entry to `blyt_cart_draw`

Before every call to `blyt_cart_draw`, the runtime unconditionally clears
the framebuffer to palette index 0. The cart receives a fresh, deterministic
canvas every frame; entering `draw` it never sees pixels from a prior frame.

The clear is not configurable and not opt-out. Carts that want a non-zero
background colour call `blyt_gfx_clear(color)` as the first operation in
`draw` — the cost is one extra 75 KB memset, negligible at 60 fps. Stage's
declarative draw pipeline (ADR-0102) does this automatically using the
scene's manifest-declared `clear:` colour; imperative `on_draw` handlers do
it themselves if they need a non-zero base.

Carts that want frame-to-frame pixel persistence (trail effects, accumulating
canvases) draw onto a persistent image surface they own and blit it each
frame. This is explicit, save-state-clean, and integrates with the dev-mode
draw inspector (ADR-0107) — backward stepping replays primitives from a
known-cleared starting state.

The runtime's auto-clear establishes the entry contract for every dev-mode
facility that re-renders a frame: save-state thumbnails, replay-driven
thumbnails, and the inspector's backward step all start from a deterministic
cleared buffer and produce bit-identical output to the live render.

### Lifecycle sequences

```
Cart start:       zero state → init → on_new_state → update/draw loop
Reset:            [on_save_state → autosave if slot set] → zero state → init → on_new_state → loop
Manual save:      on_save_state → runtime writes slot (fsync) → loop continues
Autosave:         on_save_state → runtime writes slot (no fsync) → loop continues
Save-game load:   blyt_save_read → state restored → on_load_state(SAVE_GAME) → loop continues
Save state:       state restored by frontend → on_load_state(SAVE_STATE) → loop continues
Rewind:           state restored from rewind snapshot → on_load_state(REWIND) → loop continues
Hot reload:       on_save_state → snapshot → reload → zero state → init → restore → on_load_state(HOT_RELOAD) → loop
Credits:          on_credits → blyt_credits_done → runtime resumes
Quit:             on_quit → blyt_quit_ready → cleanup → exit
```

**`on_new_state` — fresh-state initialisation.** `on_new_state` fires after
`init` whenever the runtime has zeroed state and is not about to immediately
restore a snapshot. It fires on cold start and on reset; it does not fire
during hot reload, where state is restored from a snapshot immediately after
`init`. This separates two concerns that `init` would otherwise conflate:

- **`init`** — session-scoped setup: lookup tables, audio registration,
  resource handles. Valid regardless of whether a new game or a loaded game
  follows.
- **`on_new_state`** — initial game-state setup: starting positions, HP,
  initial scene selection. Appropriate only when state should begin fresh.

Carts that have no distinction between these concerns continue to implement
everything in `init`; the SDK no-op default for `on_new_state` is correct
for them.

**Load does not call `init`.** A save load happens during normal cart
execution (typically from a title screen menu). The runtime restores tracked
state buffers and fires `on_load_state(BLYT_LOAD_SAVE_GAME)`; the
`update`/`draw` loop then continues. `init` is not called again.
Non-saveable state (resources, palette, Lua locals) was established by `init`
at session start and carries through untouched — only tracked state buffers
are overwritten by the restore.

**Rewind and save-state restores fire `on_load_state` with the appropriate
reason, with the same heap behaviour as save-game load.** The runtime restores
state buffers from the snapshot and fires `on_load_state`; `init` is not
called and the heap is not zeroed. Heap memory falls into two natural
categories: resource-derived data set up in `init` (lookup tables, parsed
resource structures) which remains valid across restores since resources do
not change; and state-derived data (entity lists, spatial indices) which may
be stale relative to the restored state buffers and must be refreshed in
`on_load_state`. Carts whose heap contains only resource-derived data need no
`on_load_state` implementation.

**`on_save_state` — pre-serialisation flush.** `on_save_state` fires before
all POD-buffer persistence operations: manual saves, autosaves, and hot-reload
snapshots. It does not fire before libretro save states or rewind, which
capture the entire cart heap atomically via `retro_serialize` and need no
flush step from the cart. `on_save_state` exists to let carts flush live state
into POD buffers before serialisation — most commonly when integrating
third-party libraries (physics engines, dialogue systems) whose internal state
is not natively stored in POD buffers. Well-formed carts that keep all mutable
state in POD buffers need not implement it; the SDK no-op default is correct
for them.

**`on_load_state` reason argument.** The `blyt_load_reason_t` argument
identifies what triggered the restore. Typical use is playing a load sound or
showing a notification only for player-visible loads. Carts should treat any
unrecognised reason value as "rebuild caches, no UI effect" — forward
compatibility for reasons added in later versions. `BLYT_LOAD_SAVE_STATE` and
`BLYT_LOAD_REWIND` are only reachable in libretro-frontend builds and
dev-mode native builds; standard native builds will only see
`BLYT_LOAD_SAVE_GAME` and `BLYT_LOAD_HOT_RELOAD`.

### Reset — always present in the pause menu

The pause menu always includes a reset option, mirroring the hardware reset
button available on real retro consoles. It is not conditional on any cart
capability.

**Default label:** "Restart". Carts customise via manifest:
```yaml
# cart.config.yaml
reset_label: "Return to title"
reset_confirm: "Unsaved progress will be lost."
```

**Reset flow:**
- If an autosave slot is set: runtime calls `on_save_state`, writes the
  autosave silently, zeros state, then calls `init` followed by
  `on_new_state`. No confirmation prompt.
- If no autosave slot is set: runtime shows a confirmation dialog ("Restart
  the game? Your progress will be lost." or cart-customised text). On
  confirm, zeros state and calls `init` followed by `on_new_state`. On
  cancel, resumes.

There is no cart callback for reset. The cart manages whether autosave is
active; the runtime handles the rest.

**Carts can implement their own return-to-title.** Nothing prevents a cart
from implementing a title screen flow entirely within its own logic — calling
`blyt_save_autosave_now()` at an appropriate point, zeroing the relevant state
fields, and transitioning to its title screen within `update`. The runtime's
reset mechanism is the player-facing escape hatch; the cart's internal flow
is the authored experience.

**`on_return_to_title` is deferred to v2.** V1 reset is a pure restart.
Richer "return to title with save prompt" flows requiring a runtime-managed
callback are left for a future version.

### Autosave

The cart opts in by designating a save slot in `init`. The runtime then
manages all saving automatically.

```c
blyt_result_t blyt_save_set_autosave_slot(uint32_t slot);
blyt_result_t blyt_save_set_autosave_interval(uint32_t frames);
blyt_result_t blyt_save_clear_autosave_slot(void);
blyt_result_t blyt_save_autosave_now(void);  // manual checkpoint trigger
```

Typical usage:

```c
void blyt_cart_init(void) {
    blyt_save_set_autosave_slot(0);
    // ... rest of init
}
```

Disabling during sensitive moments:

```c
blyt_save_clear_autosave_slot();   // entering a boss fight — no autosave

// boss defeated
blyt_save_set_autosave_slot(0);
blyt_save_autosave_now();          // checkpoint immediately
```

**Periodic saves.** With an autosave slot set, the runtime writes to that
slot at the configured interval (default 3600 frames — approximately once per
minute at 60 fps).

**Minimum interval.** In release builds, the minimum permitted interval is
3600 frames. This protects flash and SD card storage from excessive write
cycles. `blyt_save_set_autosave_interval` clamps silently to this floor in
release. In dev mode the floor is not enforced, allowing rapid save/load
testing without waiting a minute per cycle.

**`on_save_state` fires before every POD-buffer write**, including periodic
autosaves. See "on_save_state — pre-serialisation flush" in the Lifecycle
sequences section for the full semantics.

**Filesystem sync.** Manual saves and the autosave triggered immediately
before a reset issue a filesystem sync (e.g. `fsync` on Linux, equivalent on
other platforms) before returning. The data is durable before play continues.
Periodic running autosaves do not sync — if a system crash occurs between
periodic writes the previous autosave remains intact, and losing the most
recent periodic write is an acceptable trade-off against continuously
hammering storage.

**On reset.** If an autosave slot is set, the runtime calls `on_save_state`,
writes to the slot, and syncs immediately before zeroing state and calling
`init`, regardless of the periodic interval.

### Save slot metadata

Every save slot carries runtime-managed metadata for display in save/load UIs.
The metadata is stored as a lightweight header at the start of the save file,
separate from the full state data, so it can be read quickly without loading
the complete save.

```c
typedef struct {
    int32_t         total_frames;       // accumulated playtime; 0 if slot empty
    int32_t         elapsed_seconds;    // seconds since this save was written;
                                        //   -1 if platform has no real-time clock
    blyt_image_h      thumbnail;          // 80×60 image handle; 0 if slot empty
    const uint32_t *thumbnail_palette;  // 256 RGBA entries, runtime-owned
    const void     *description;        // save description buffer contents, or NULL
    uint32_t        description_size;   // size in bytes
} blyt_save_meta_t;

blyt_result_t blyt_save_get_meta(uint32_t slot, blyt_save_meta_t *out);
```

`blyt_save_get_meta` reads only the metadata header — it does not load the full
save state. Carts call it during their save selection screen's `update` to
populate the UI before the player chooses a slot; `blyt_save_read` loads the
full state when the player confirms.

**Elapsed time.** The runtime records an absolute wall-clock timestamp in the
save file header at write time (an implementation detail, never exposed
directly to cart code). `blyt_save_get_meta` computes the difference between
that stored timestamp and the current time, returning it as `elapsed_seconds`.
The cart receives a relative duration — "this save is 7,320 seconds old" —
and formats it however it chooses ("2 hours ago", "3 days ago"). No absolute
timestamp is ever visible to cart code, so determinism is unaffected.

A general real-world time API (`blyt_time_unix` or similar) is deliberately
absent from v1. Carts that use real-world time to drive game logic (Animal
Crossing-style time-sensitive games) require a well-understood "time-sensitive
mode" design; that is deferred to v2. `elapsed_seconds` is a narrow, safe
exception scoped entirely to save display.

**Save description buffer.** Carts can declare a state buffer with
`role: save_description` in the manifest. The runtime includes its full
contents verbatim in the save metadata header, making it available to
`blyt_save_get_meta` without loading the full save. The cart writes to it during
gameplay exactly like any other state buffer — the metadata copy is updated
each time a save is written.

```yaml
# cart.config.yaml
state:
  save_info:
    role: save_description
    fields:
      - { name: act,      type: u8  }
      - { name: hp,       type: u8  }
      - { name: max_hp,   type: u8  }
      - { name: location, type: u16 }
```

```lua
-- in update, kept current automatically
S.save_info.act    = current_act
S.save_info.hp     = player.hp
S.save_info.max_hp = player.max_hp
```

The loading screen reads `description` and `description_size` from the
metadata struct and interprets the bytes using the same packer-generated field
constants it uses everywhere else — no separate schema needed. A save
description buffer implicitly has a single entry — it is a flat record, not a
slotted buffer. Slot management does not apply. Recommended maximum size is 64
bytes; the runtime enforces a hard limit of 256 bytes. Carts that declare no
description buffer have `description = NULL`.

**Lua access.** In Lua the `description` field on the metadata object is a
read-only proxy wrapping the raw bytes with the same field layout as the live
`S.save_info` buffer. Field names, types, and access syntax are identical to
gameplay code — no byte arithmetic or separate schema:

```lua
local meta = save.get_meta(slot)
if meta then
    if meta.description then
        gfx.text(x, y,   "Act " .. meta.description.act)
        gfx.text(x, y+8, meta.description.hp .. "/" ..
                          meta.description.max_hp .. " HP")
    end
    if meta.thumbnail then
        meta.thumbnail:blit(tx, ty)
    end
    local h = math.floor(meta.total_frames / 3600)
    local m = math.floor((meta.total_frames % 3600) / 60)
    gfx.text(x, y+16, string.format("%dh %02dm", h, m))
end
```

**Read-only enforcement.** Attempting to write to `meta.description` raises a
clear dev-mode error: `"save description is read-only — write to S.save_info
during gameplay"`. In release the write is silently ignored.

**Missing fields.** If an older save predates a field added in a later cart
version, the field returns its declared default value (0 for numerics, false
for bools) — the same behaviour as `blyt_buffer_was_restored` for ordinary
state buffers. Dev mode logs a notice identifying which fields were absent.

**Thumbnail image handle.** `thumbnail` is a standard `blyt_image_h` valid
through the remainder of the current `update`/`draw` cycle. The cart blits it
with the normal image API. It is invalidated on the next call to
`blyt_save_get_meta` or at the next `init`.

The cart is responsible for palette management before blitting. For
single-palette carts the stored palette matches the active one and the
thumbnail can be blitted directly. For multi-palette games the load screen
needs a palette that covers both its own UI colours and an approximation of
thumbnail colours from any part of the game. The cart uses `thumbnail_palette`
(the RGBA values of the palette at save time) alongside `blyt_gfx_pal_remap` to
build an index-remapping table from thumbnail indices to load-screen indices
at display time. This is achievable with existing tools — a nearest-colour
search over the load-screen palette per thumbnail palette entry, applied as a
remap before blitting and cleared after.

A future enhancement could designate a small set of colours guaranteed to be
present in every palette (a "common palette" subset), giving the runtime a
fixed colour space it can quantise thumbnail captures to — this would
eliminate the per-cart remapping work. This is not a v1 concern; it is fully
implementable at the cart level in the meantime.

**Thumbnail capture.** Taken by the runtime from the framebuffer after the
`draw` call immediately preceding the save. Stored as 80×60 palette indices
(a 1/4 downscale of 320×240) plus a snapshot of the active palette.

**Total playtime.** The runtime accumulates frame counts across sessions.
`blyt_time_frame()` is session-local — starts at 0 on `init`, increments each
frame, does not reset on save loads. The runtime tracks two values internally:

- `playtime_base`: `total_frames` from the most recently loaded save (0 for
  a new game).
- `frame_at_load`: the value of `blyt_time_frame()` at the moment of the load
  (0 for a new game).

On each save:
```
new_total = playtime_base + (blyt_time_frame() - frame_at_load)
```

Time spent at the title screen before loading is excluded. Multiple loads
within one session are handled correctly — each load resets both values.

**i32 range.** Overflows after ~19,884 hours of play at 60 fps. Not a
practical concern.

## Amendment — blyt_load_info_t, saved_cart_version, and per-field restoration metadata

### `on_load_state` signature

The `blyt_cart_on_load_state` entry point is amended to receive a
`blyt_load_info_t` struct rather than a bare `blyt_load_reason_t`:

```c
typedef struct {
    bool        was_restored;       // false if entire buffer absent from save
    const bool *fields_restored;    // fields_restored[BLYT_FIELD_INDEX(field_h)]
} blyt_buffer_load_info_t;

typedef struct {
    blyt_load_reason_t             reason;
    uint32_t                       saved_cart_version;
    const blyt_buffer_load_info_t *buffers;  // indexed by blyt_buffer_h value
} blyt_load_info_t;

// Updated signature
void blyt_cart_on_load_state(blyt_load_info_t info);
```

`BLYT_FIELD_INDEX(fh)` extracts the field index from a `blyt_field_h`
(low 16 bits). The packer defines this macro in the generated header.

The Lua shim unpacks `blyt_load_info_t` into a table:

```lua
function on_load_state(info)
    -- info.reason          string  ("save_game", "save_state", "rewind", "hot_reload")
    -- info.cart_version    integer (saved_cart_version; 0 if not declared)
    -- info.buffers         table   indexed by buffer handle value
    --   .was_restored      bool
    --   .fields_restored   table   indexed by field index
end
```

### `saved_cart_version`

`blyt_load_info_t.saved_cart_version` carries the `save_version` integer
declared in `cart.info.yaml` by the cart that wrote the save (ADR-0125).
It is 0 when `reason` is not `BLYT_LOAD_SAVE_GAME`, or when the save
predates `save_version` being declared.

Cart authors increment `save_version` whenever they make a schema change
that requires migration logic. The cart reads `saved_cart_version` to
determine what migration is needed:

```c
void blyt_cart_on_load_state(blyt_load_info_t info) {
    if (info.reason == BLYT_LOAD_SAVE_GAME && info.saved_cart_version < 3) {
        // save predates quest system — initialise to starting chapter
        blyt_buffer_set_u8(S_QUESTS, 0, S_QUEST_CHAPTER, 1);
    }
}
```

### `buffers` — restoration metadata

By the time `on_load_state` fires, the runtime has completed the migration
walk (ADR-0125): matching fields have been copied to their current positions
and new fields have been zero-initialised. `buffers` reflects the outcome.

`info.buffers` is indexed by `blyt_buffer_h` value (1-based, matching
packer-generated buffer constants). `fields_restored` is indexed by
`BLYT_FIELD_INDEX(field_h)` (1-based field index, low 16 bits of
`blyt_field_h`). Both arrays are runtime-owned and valid only for the
duration of `on_load_state`; they must not be retained beyond the callback.

Usage:

```c
void blyt_cart_on_load_state(blyt_load_info_t info) {
    // Entire buffer absent from save (added in a later cart version)
    if (!info.buffers[S_QUESTS].was_restored) {
        blyt_buffer_set_u8(S_QUESTS, 0, S_QUEST_CHAPTER, 1);
    }

    // Specific field absent from save (added to existing buffer)
    if (!info.buffers[S_PLAYERS].fields_restored[BLYT_FIELD_INDEX(S_PLAYER_STAMINA)]) {
        float hp = blyt_buffer_get_f32(S_PLAYERS, 0, S_PLAYER_HP);
        blyt_buffer_set_f32(S_PLAYERS, 0, S_PLAYER_STAMINA, hp * 0.5f);
    }
}
```

`buffers` is populated for all load reasons, not only `BLYT_LOAD_SAVE_GAME`.
For hot reload, rewind, and save state loads, it reflects which buffers and
fields were present in the snapshot.

The standalone `blyt_buffer_was_restored` function described in earlier
design documents is superseded by `blyt_load_info_t.buffers` and is not
part of the v1 API.

### Updated lifecycle sequences

```
Save-game load:  blyt_save_read → migration walk → on_load_state(info) → loop
Hot reload:      on_save_state → snapshot → reload → zero state → init →
                   restore + migration walk → on_load_state(info) → loop
```

The `info.reason` field distinguishes the two cases for carts that need
different behaviour (e.g. playing a load sound only for player-visible loads).

## Consequences

- The lifecycle is minimal and predictable: three required symbols, six
  optional ones with sensible defaults, no surprises.
- Lua cart authors define only what they need; the preamble handles the rest.
- Zeroing state before every `init` eliminates a class of subtle bugs where
  leftover state from a previous session affects a fresh start.
- Clearing the framebuffer to palette index 0 before every `draw` gives
  every dev-mode rendering facility (save-state thumbnails, replay-driven
  thumbnails, ADR-0107's backward step) a deterministic starting point and
  removes the "what's in the back buffer from frame N-2" surprise that
  unguarded double-buffering creates.
- Reset is always available to players without any cart work. Autosave is
  opt-in and entirely runtime-managed once the slot is designated.
- The minimum autosave interval protects storage hardware in release builds
  while remaining unrestricted in dev mode for testing convenience.
- Carts retain full control of their own title screen and return-to-title
  flow; the runtime's reset is the player escape hatch, not a constraint on
  cart UX.
- Save UIs get thumbnail image handles, playtime, and elapsed time for free.
  Carts that want additional per-save context (act number, HP, location, etc.)
  declare a save description buffer in the manifest; the runtime includes it
  in the metadata header automatically and exposes it as a read-only proxy on
  the load screen using the same field access syntax as gameplay code.
- `on_new_state` separates resource setup (`init`) from initial game-state
  setup, so hot reload and state restores avoid running new-game initialisation
  superfluously. Carts that make no distinction continue to implement
  everything in `init` — the no-op default is correct for them.
- `on_save_state` fires before all POD-buffer snapshots, giving third-party
  library integrations a guaranteed flush point regardless of what triggered
  the save.
- The `blyt_load_reason_t` argument to `on_load_state` lets carts respond
  differently to player-visible loads versus transparent restores without
  coupling unrelated code paths.
- `on_return_to_title` is deliberately absent from v1. The reset mechanism
  covers the primary use case; richer flows are v2 work.
