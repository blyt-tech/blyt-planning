# ADR-0086: API surface shape — getter/setter convention and complete module map

## Status
Accepted

## Context

ADR-0046 settled the core C API conventions: handles, error returns, the
`blyt_<subsystem>_<verb>` naming pattern, two-header split, and bitmask flags.
The §17 open item on API surface shape has two remaining gaps.

**Inconsistent getter/setter convention.** The existing API design document
mixes approaches. Many subsystems use explicit `_set`/`_get` suffixes:

```c
blyt_gfx_palette_set() / blyt_gfx_palette_get()
blyt_prefs_set_float() / blyt_prefs_get_float()
blyt_locale_set() / blyt_locale_get()
blyt_tilemap_set_tile() / blyt_tilemap_get_tile()
```

The entire audio subsystem drops the suffix, with no getters:

```c
blyt_audio_music_volume(float volume)     // implicit set, no get
blyt_audio_sfx_volume(voice, float volume)
blyt_audio_master_volume(float volume)
blyt_audio_group_volume(group, float volume)
```

This inconsistency extends to Lua object methods — `voice:volume(0.5)` appears
in the design doc, contrasting with `CYCLE.WATER:set_interval(1)` elsewhere.

**Incomplete Lua module map.** The module list omits homes for easing
functions, collision/spatial queries, and procedural noise — all v1 API
primitives per ADR-0039. `blyt32.color.*` is listed without definition.

## Decision

### Getter/setter convention

**Explicit `_set`/`_get` suffixes for all paired read-write accessors,
uniformly across C and Lua.**

```c
// C — explicit suffixes, always
blyt_audio_music_set_volume(float volume);
blyt_audio_music_get_volume(float *out);
blyt_audio_sfx_set_volume(blyt_voice_h voice, float volume);
blyt_audio_sfx_get_volume(blyt_voice_h voice, float *out);
blyt_audio_master_set_volume(float volume);
blyt_audio_master_get_volume(float *out);
blyt_audio_group_set_volume(blyt_voice_group_h group, float volume);
blyt_audio_group_get_volume(blyt_voice_group_h group, float *out);
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
blyt_audio_music_is_playing()  // predicate; no setter exists
blyt_time_frame()              // read-only counter
blyt_mem_used()                // read-only stat
blyt_input_button_pressed()    // edge-triggered read; no write
```

The rule is: if a property can be both read and written, use `_get`/`_set`.
If it can only be read, use a plain verb. **Modal functions are not used** —
`blyt_audio_music_volume(vol)` as an implicit setter with no getter is
ambiguous and is superseded by this rule throughout the API.

### Complete Lua module map

The Lua API surface splits into two groups (per ADR-0105's dual-namespace
model):

- **Shared modules** — exposed canonically as `blyt.*`, available on every
  variant. They are also accessible via the variant top-level
  (`blyt32.audio == blyt.audio` is true) for cart authors who prefer a
  uniform variant-prefixed call site. Cross-variant Lua libraries reach
  these via `blyt.*`.
- **Variant-specific modules** — exposed only on the variant top-level
  (`blyt32.*` here). They have no `blyt.*` alias because the underlying
  surfaces are not portable across variants. BlyTTY and Blyt3D will
  expose their own variant-specific modules under `blytty.*` and
  `blyt3d.*` (e.g. `blytty.term.*`, `blytty.keyboard.*`); these are not
  enumerated here as they belong to those variants' own design
  documents.

#### Shared modules (`blyt.*`; aliased as `blyt32.*` etc.)

| Lua module | C prefix(es) | Scope |
|---|---|---|
| `blyt.audio.*` | `blyt_audio_*` | Master volume; sub-modules below |
| `blyt.audio.sfx.*` | `blyt_audio_sfx_*` | SFX voice playback |
| `blyt.audio.music.*` | `blyt_audio_music_*` | Music track playback |
| `blyt.audio.stream.*` | `blyt_audio_stream_*` | Streamed audio (Large/Flagship only) |
| `blyt.audio.group.*` | `blyt_audio_group_*` | Voice group control |
| `blyt.speech.*` | `blyt_speech_*` | Voice-acted speech and lip sync (timing data; rendering of mouth shapes is the cart's job) |
| `blyt.state.*` | `blyt_buffer_*` | State buffers and slot management |
| `blyt.resource.*` | `blyt_resource_*` | Resource loading and unloading |
| `blyt.save.*` | `blyt_save_*` | Cart save slots |
| `blyt.prefs.*` | `blyt_prefs_*` | Persistent preferences |
| `blyt.time.*` | `blyt_time_*` | Frame count and tick queries |
| `blyt.rng.*` | `blyt_rng_*` | Named seedable RNG streams |
| `blyt.math.*` | `blyt_math_*`, `blyt_ease_*`, `blyt_noise_*` | Deterministic math, easing functions, procedural noise |
| `blyt.loc.*` | `blyt_loc_*`, `blyt_locale_*` | Localisation key lookup, locale get/set |
| `blyt.log.*` | `blyt_log_*` | Logging (debug/info/warn/error) |
| `blyt.dev.*` | `blyt_dev_*` | Dev-mode instrumentation; no-op in release (ADR-0065) |
| `blyt.mem.*` | `blyt_mem_*` | Memory usage introspection |
| `blyt.achievements.*` | `blyt_achievement_*` | Achievements (deferred; ADR-0014) |
| `blyt.speedrun.*` | `blyt_speedrun_*` | Speedrun tooling (ADR-0015) |
| `blyt.info` | — | Variant introspection (`blyt.info.console`, `blyt.info.api_version`) |

#### Blyt32-specific modules (`blyt32.*` only; no `blyt.*` alias)

| Lua module | C prefix(es) | Scope | Notes |
|---|---|---|---|
| `blyt32.gfx.*` | `blyt_gfx_*`, `blyt_image_*`, `blyt_tilemap_*` | Framebuffer, primitives, images, tilemaps, palette, screen shake | Paletted 2D model is Blyt32-specific; not present on BlyTTY (text grid) or Blyt3D (3D scene) |
| `blyt32.input.*` | `blyt_input_*` | Buttons, pointer, device info, text input | Dpad+4face+2shoulder layout per ADR-0017; BlyTTY has its own keyboard input model |
| `blyt32.color.*` | `blyt_color_*` | Color math utilities over palette indices | Tied to Blyt32's palette model; BlyTTY would have its own variant of this; Blyt3D operates in RGB |
| `blyt32.spatial.*` | `blyt_spatial_*`, `blyt_collision_*` | Spatial queries and collision primitives over tilemaps and Blyt32 state structures | Operates over Blyt32-specific structures (tilemaps); abstract math primitives like AABB intersect live in `blyt.math.*` |

**Easing functions** (`blyt_ease`, `blyt_ease_lerp`, `blyt_ease_lerp_i`)
live in `blyt.math.*` (shared). They are mathematical functions.
`blyt_tween.lua` is a separate cart-side SDK library providing
tween-object ergonomics built on top of these functions; it is not a
runtime-exposed module and authors include it optionally.

**Procedural noise** (Perlin, simplex, value noise, FBM) lives in
`blyt.math.*` (shared).

**Collision and spatial queries** live in `blyt32.spatial.*`
(Blyt32-only). This covers AABB checks, circle overlap, point-in-polygon,
swept collision, and buffer spatial queries (nearest-neighbour, rect
query, raycast over typed state buffers). The exact API for this module
is a follow-on design task once the state buffer and tilemap APIs are
finalised, since spatial queries operate over them. Pure-math primitives
that have no Blyt32 dependency (e.g. AABB intersection of two boxes
given only their coordinates) belong in `blyt.math.*` so they are
available to cross-variant code.

### `blyt32.color.*` scope

Color math utilities that operate on palette indices or color values without
requiring a draw call:

- Nearest palette index to a given RGB triple
- Perceptual luminance of a palette entry
- Palette-step operations (lighten or darken by N steps within the palette)
- Color interpolation between two palette indices

Palette manipulation (loading a palette, remapping entries during draw) stays
in `blyt32.gfx.*`. `blyt32.color.*` is for arithmetic on color values, not
for rendering.

### C-flat / Lua-nested duality

The C API is intentionally flat. All audio operations are at the same level,
distinguished by subsystem infix: `blyt_audio_music_*`, `blyt_audio_sfx_*`,
`blyt_audio_stream_*`. The Lua API nests these under `audio.music.*`,
`audio.sfx.*`, `audio.stream.*` for ergonomics and tab-completion. The shim
maps between them. Both representations are correct; C is navigable by grep,
Lua is navigable by module structure. This pattern applies throughout: the
C namespace and the Lua module hierarchy are parallel views of the same ECALL
surface, not required to be structurally identical.

## Consequences

- The `_get`/`_set` rule eliminates ambiguity about whether a bare verb is a
  query or a write. All existing API design document instances of the implicit
  pattern (`blyt_audio_music_volume`, `blyt_audio_sfx_volume`, etc.) are
  superseded and must use the explicit form in implementation.
- Every Lua module now has a defined scope. The shim has an unambiguous home
  for each function.
- `blyt.math.*` (shared) absorbs easing and noise without adding new
  top-level modules — both are straightforwardly mathematical and
  variant-portable.
- `blyt32.spatial.*` is a new module requiring its own follow-on API
  design. Pure spatial-math primitives that have no Blyt32 dependency
  belong in `blyt.math.*`.
- `blyt32.color.*` has a defined scope; its specific functions are a
  follow-on design task. Tied to Blyt32's palette model; not aliased on
  `blyt.*`.
- The shared/variant split means cross-variant Lua libraries written
  against `blyt.*` automatically work on BlyTTY and Blyt3D as those
  variants come online; only variant-specific code needs porting.
- The audio subsystem gains getters it was missing. Carts can query current
  volume levels — necessary for save/restore of audio state and for UI sliders
  that display current values.
