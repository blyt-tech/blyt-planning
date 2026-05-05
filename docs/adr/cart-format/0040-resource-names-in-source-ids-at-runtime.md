# ADR-0040: Resource addressing — names in source, compile-time constants at build, IDs at runtime

## Status
Accepted

## Context

Resources (sprites, tilemaps, audio, fonts) need to be referenced from cart
code. Using raw integer IDs in source is fast but opaque and error-prone.
Using string names everywhere is ergonomic but adds hash lookup overhead on
every access in a hot loop. Debug builds need names for error messages.

The state buffer decision (ADR-0009) established the pattern of packer-
generated compile-time constants for buffer and field handles. The same
pattern applies to resource handles.

## Decision

**Names in source, packer-generated compile-time constants for access,
IDs at runtime.**

### Auto-scan is the default

The packer scans the `assets/` directory and automatically generates
resource entries for every file whose extension maps to a known resource
type (`.png` → sprite, `.ase`/`.aseprite` → sprite, `.xm`/`.it`/`.mod` →
music, `.wav`/`.ogg`/`.mp3` → sfx, `.tmx`/`.ldtk` → tilemap, `.fnt` →
font, etc. — the full list matches the asset pipeline in ADR-0044). No
manifest declaration is needed for the common case.

Files with unknown extensions (READMEs, source PSDs, work-in-progress
notes) are silently ignored. Specific files or directories can be
explicitly excluded in `cart.build.yaml`:

```yaml
# cart.build.yaml
assets:
  exclude:
    - assets/wip/**
    - assets/README.md
    - assets/reference/**
```

Explicit asset declarations in `cart.build.yaml` (for non-standard paths,
glob patterns, or assets outside the `assets/` tree) follow the same
auto-naming rule and are additive to the default scan.

### Constant name derivation

The resource name — and therefore the generated constant — is derived from
the file's path relative to `assets/`, with the extension stripped and
directory separators replaced by `_`:

| File | Resource name | C constant | Lua |
|------|--------------|------------|-----|
| `assets/hero.png` | `hero` | `R_HERO` | `R.HERO` |
| `assets/sprites/hero.png` | `sprites_hero` | `R_SPRITES_HERO` | `R.SPRITES_HERO` |
| `assets/ui/buttons/play.png` | `ui_buttons_play` | `R_UI_BUTTONS_PLAY` | `R.UI_BUTTONS_PLAY` |
| `assets/music/theme.xm` | `music_theme` | `R_MUSIC_THEME` | `R.MUSIC_THEME` |

The full transformation pipeline applied to each path component:

1. Strip the file extension.
2. Replace directory separators with `_`.
3. Replace any character that is not a letter, digit, or `_` with `_`.
   This covers dashes, spaces, dots, and any other non-identifier
   characters.
4. Collapse consecutive `_` characters to a single `_`.
5. Trim leading and trailing `_` characters.
6. Lowercase the result for the resource name; uppercase (with `R_`
   prefix) for the generated C constant.

Examples with non-trivial filenames:

| File | Resource name | C constant |
|------|--------------|------------|
| `assets/hero-idle.png` | `hero_idle` | `R_HERO_IDLE` |
| `assets/sfx/jump 1.wav` | `sfx_jump_1` | `R_SFX_JUMP_1` |
| `assets/--draft--.png` | `draft` | `R_DRAFT` |
| `assets/ui/btn.play.png` | `ui_btn_play` | `R_UI_BTN_PLAY` |

The packer errors on name collisions (two files that derive the same
name after transformation).

References to resources in `cart.config.yaml` (e.g., `persistent_resources`,
`mutable_tilemaps`) use the same derived names:

```yaml
persistent_resources: [sprites_hero, ui_buttons_play, music_theme]
```

### Manual declaration

For resources that need options the auto-scan cannot infer (TTF fonts
requiring explicit size lists, audio with non-default encoding settings),
`cart.build.yaml` accepts explicit declarations that supplement or override
the auto-scan result for that file:

```yaml
assets:
  fonts:
    - file:  assets/ui/body.ttf
      sizes: [8, 12, 16]   # required for TTF — no default
```

The packer generates a resource entry and constant for these just as it
would for an auto-scanned file, using the same name derivation rule.

### Generated constants

The packer emits a header (native carts) or Lua module (Lua carts) with
`blyt_resource_h` constants for every resource, auto-scanned or explicit:

```c
// Generated: cart_resources.h
#define R_HERO_SPRITES   ((blyt_resource_h)1)
#define R_TILEMAP_WORLD  ((blyt_resource_h)2)
#define R_THEME_MUSIC    ((blyt_resource_h)3)
```

```lua
-- Generated: cart_resources.lua
local R = require("cart_resources")
R.HERO_SPRITES   -- integer constant
R.TILEMAP_WORLD
R.THEME_MUSIC
```

Built-in runtime resources (`BLYT_FONT_BUILTIN`, `BLYT_PALETTE_DEFAULT`, etc.)
set bit 31 to guarantee no collision with cart-generated IDs; see ADR-0059
for the full convention.

Resource IDs are stable within a development session but not guaranteed
across packer rebuilds that add or remove resources. Carts must not
serialize resource IDs to disk (use names for persistence, or load by
constant at init).

Generated files are gitignored; the packer regenerates them on every build.
The packer enforces that all declared resource names resolve to actual files
in the project directory — a class of runtime errors becomes a build-time
error.

## Consequences

- Authoring ergonomics: resource names are readable and refactorable in the
  manifest; access in cart code uses named constants, not magic integers.
- Runtime speed: ID-based lookup after the initial load call is O(1) with no
  string comparison in hot paths.
- Packer validates all resource references at build time.
- Debug error messages use the debug name table (preserved in debug builds,
  stripped in release); release error messages reference IDs, which is
  acceptable since debug builds are used during development.
- The generated-header pattern is consistent with state buffer field constants
  (ADR-0009), RNG stream constants, localisation key constants, and
  palette cycle constants — one authoring mental model for all packer-
  generated access.
- Matches commercial engine convention (Unity asset GUIDs, Unreal FNames).
