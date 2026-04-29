# ADR-0086: API surface shape — getter/setter convention and complete module map

## Status
Accepted

## Context

ADR-0046 settled the core C API conventions: handles, error returns, the
`fc_<subsystem>_<verb>` naming pattern, two-header split, and bitmask flags.
The §17 open item on API surface shape has two remaining gaps.

**Inconsistent getter/setter convention.** The existing API design document
mixes approaches. Many subsystems use explicit `_set`/`_get` suffixes:

```c
fc_gfx_palette_set() / fc_gfx_palette_get()
fc_prefs_set_float() / fc_prefs_get_float()
fc_locale_set() / fc_locale_get()
fc_tilemap_set_tile() / fc_tilemap_get_tile()
```

The entire audio subsystem drops the suffix, with no getters:

```c
fc_audio_music_volume(float volume)     // implicit set, no get
fc_audio_sfx_volume(voice, float volume)
fc_audio_master_volume(float volume)
fc_audio_group_volume(group, float volume)
```

This inconsistency extends to Lua object methods — `voice:volume(0.5)` appears
in the design doc, contrasting with `CYCLE.WATER:set_interval(1)` elsewhere.

**Incomplete Lua module map.** The module list omits homes for easing
functions, collision/spatial queries, and procedural noise — all v1 API
primitives per ADR-0039. `console.color.*` is listed without definition.

## Decision

### Getter/setter convention

**Explicit `_set`/`_get` suffixes for all paired read-write accessors,
uniformly across C and Lua.**

```c
// C — explicit suffixes, always
fc_audio_music_set_volume(float volume);
fc_audio_music_get_volume(float *out);
fc_audio_sfx_set_volume(fc_voice_h voice, float volume);
fc_audio_sfx_get_volume(fc_voice_h voice, float *out);
fc_audio_master_set_volume(float volume);
fc_audio_master_get_volume(float *out);
fc_audio_group_set_volume(fc_voice_group_h group, float volume);
fc_audio_group_get_volume(fc_voice_group_h group, float *out);
```

```lua
-- Lua object methods — same explicit prefix
voice:set_volume(0.5)
voice:get_volume()
CYCLE.WATER:set_interval(4)
CYCLE.WATER:get_interval()
```

**Read-only queries** — values that have no paired setter — use a plain verb
or noun with no `_get` suffix:

```c
fc_audio_music_is_playing()  // predicate; no setter exists
fc_time_frame()              // read-only counter
fc_mem_used()                // read-only stat
fc_input_button_pressed()    // edge-triggered read; no write
```

The rule is: if a property can be both read and written, use `_get`/`_set`.
If it can only be read, use a plain verb. **Modal functions are not used** —
`fc_audio_music_volume(vol)` as an implicit setter with no getter is
ambiguous and is superseded by this rule throughout the API.

### Complete Lua module map

All `console.*` modules, including previously unassigned subsystems:

| Lua module | C prefix(es) | Scope |
|---|---|---|
| `console.gfx.*` | `fc_gfx_*`, `fc_image_*`, `fc_tilemap_*` | Framebuffer, primitives, images, tilemaps, palette, screen shake |
| `console.audio.*` | `fc_audio_*` | Master volume; sub-modules below |
| `console.audio.sfx.*` | `fc_audio_sfx_*` | SFX voice playback |
| `console.audio.music.*` | `fc_audio_music_*` | Music track playback |
| `console.audio.stream.*` | `fc_audio_stream_*` | Streamed audio (Large/Flagship only) |
| `console.audio.group.*` | `fc_audio_group_*` | Voice group control |
| `console.speech.*` | `fc_speech_*` | Voice-acted speech and lip sync |
| `console.input.*` | `fc_input_*` | Buttons, pointer, device info, text input |
| `console.state.*` | `fc_buffer_*` | State buffers and slot management |
| `console.resource.*` | `fc_resource_*` | Resource loading and unloading |
| `console.save.*` | `fc_save_*` | Cart save slots |
| `console.prefs.*` | `fc_prefs_*` | Persistent preferences |
| `console.time.*` | `fc_time_*` | Frame count and tick queries |
| `console.rng.*` | `fc_rng_*` | Named seedable RNG streams |
| `console.math.*` | `fc_math_*`, `fc_ease_*`, `fc_noise_*` | Deterministic math, easing functions, procedural noise |
| `console.spatial.*` | `fc_spatial_*`, `fc_collision_*` | Spatial queries and collision primitives |
| `console.color.*` | `fc_color_*` | Color math utilities (see below) |
| `console.loc.*` | `fc_loc_*`, `fc_locale_*` | Localisation key lookup, locale get/set |
| `console.log.*` | `fc_log_*` | Logging (debug/info/warn/error) |
| `console.dev.*` | `fc_dev_*` | Dev-mode instrumentation; no-op in release (ADR-0065) |
| `console.mem.*` | `fc_mem_*` | Memory usage introspection |
| `console.achievements.*` | `fc_achievement_*` | Achievements (deferred; ADR-0014) |
| `console.speedrun.*` | `fc_speedrun_*` | Speedrun tooling (ADR-0015) |

**Easing functions** (`fc_ease`, `fc_ease_lerp`, `fc_ease_lerp_i`) live in
`console.math.*`. They are mathematical functions. `fc_tween.lua` is a
separate cart-side SDK library providing tween-object ergonomics built on top
of these functions; it is not a `console.*` module and authors include it
optionally.

**Procedural noise** (Perlin, simplex, value noise, FBM) lives in
`console.math.*`.

**Collision and spatial queries** live in `console.spatial.*`. This covers
AABB checks, circle overlap, point-in-polygon, swept collision, and buffer
spatial queries (nearest-neighbour, rect query, raycast over typed state
buffers). The exact API for this module is a follow-on design task once the
state buffer and tilemap APIs are finalised, since spatial queries operate
over them.

### `console.color.*` scope

Color math utilities that operate on palette indices or color values without
requiring a draw call:

- Nearest palette index to a given RGB triple
- Perceptual luminance of a palette entry
- Palette-step operations (lighten or darken by N steps within the palette)
- Color interpolation between two palette indices

Palette manipulation (loading a palette, remapping entries during draw) stays
in `console.gfx.*`. `console.color.*` is for arithmetic on color values, not
for rendering.

### C-flat / Lua-nested duality

The C API is intentionally flat. All audio operations are at the same level,
distinguished by subsystem infix: `fc_audio_music_*`, `fc_audio_sfx_*`,
`fc_audio_stream_*`. The Lua API nests these under `audio.music.*`,
`audio.sfx.*`, `audio.stream.*` for ergonomics and tab-completion. The shim
maps between them. Both representations are correct; C is navigable by grep,
Lua is navigable by module structure. This pattern applies throughout: the
C namespace and the Lua module hierarchy are parallel views of the same ECALL
surface, not required to be structurally identical.

## Consequences

- The `_get`/`_set` rule eliminates ambiguity about whether a bare verb is a
  query or a write. All existing API design document instances of the implicit
  pattern (`fc_audio_music_volume`, `fc_audio_sfx_volume`, etc.) are
  superseded and must use the explicit form in implementation.
- Every Lua module now has a defined scope. The shim has an unambiguous home
  for each function.
- `console.math.*` absorbs easing and noise without adding new top-level
  modules — both are straightforwardly mathematical.
- `console.spatial.*` is a new module requiring its own follow-on API design.
- `console.color.*` has a defined scope; its specific functions are a
  follow-on design task.
- The audio subsystem gains getters it was missing. Carts can query current
  volume levels — necessary for save/restore of audio state and for UI sliders
  that display current values.
