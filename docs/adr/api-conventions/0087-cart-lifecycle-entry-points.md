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
void blyt_cart_on_save(void);
void blyt_cart_on_load(void);
void blyt_cart_cleanup(void);
void blyt_cart_on_credits(void);
void blyt_cart_on_quit(void);

// Native carts only — not part of the Lua lifecycle (ADR-0083)
void blyt_cart_panic(blyt_panic_reason_t reason);
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
| `blyt_cart_on_save` | `on_save` |
| `blyt_cart_on_load` | `on_load` |
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
Cart start:      zero state → init → update/draw loop
Load save:       blyt_save_read → state restored → on_load → loop continues
Reset:           (autosave written if slot set) → zero state → init → loop
Credits:         on_credits → blyt_credits_done → runtime resumes
Quit:            on_quit → blyt_quit_ready → cleanup → exit
```

**Load does not call `init`.** A save load happens during normal cart
execution (typically from a title screen menu). The runtime restores tracked
state buffers and fires `on_load`; the `update`/`draw` loop then continues.
`init` is not called again. Non-saveable state (resources, palette, Lua
locals) was established by `init` at session start and carries through
untouched — only tracked state buffers are overwritten by the restore.

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
- If an autosave slot is set: runtime writes the autosave silently, then
  zeros state and calls `init`. No confirmation prompt.
- If no autosave slot is set: runtime shows a confirmation dialog ("Restart
  the game? Your progress will be lost." or cart-customised text). On
  confirm, zeros state and calls `init`. On cancel, resumes.

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

**`on_save` is not called for autosaves.** In a fixed-timestep model, tracked
state is always consistent between frames — there is nothing for the cart to
flush. `on_save` exists for manual saves where the cart may want to record
custom metadata or take other pre-save actions. Autosave is a pure runtime
operation that snapshots tracked state directly.

**Filesystem sync.** Manual saves and the autosave triggered immediately
before a reset issue a filesystem sync (e.g. `fsync` on Linux, equivalent on
other platforms) before returning. The data is durable before play continues.
Periodic running autosaves do not sync — if a system crash occurs between
periodic writes the previous autosave remains intact, and losing the most
recent periodic write is an acceptable trade-off against continuously
hammering storage.

**On reset.** If an autosave slot is set, the runtime writes to it and syncs
immediately before zeroing state and calling `init`, regardless of the
periodic interval.

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

## Consequences

- The lifecycle is minimal and predictable: three required symbols, five
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
- `on_return_to_title` is deliberately absent from v1. The reset mechanism
  covers the primary use case; richer flows are v2 work.
