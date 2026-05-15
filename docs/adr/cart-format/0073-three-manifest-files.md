# ADR-0073: Manifest files — cart.info.yaml, cart.config.yaml, cart.build.yaml, and language-specific manifests

## Status
Accepted

## Context

The initial design referred to "the manifest" as a single file. API design
revealed that manifest data has three distinct consumers with different access
patterns: the frontend (reads before loading the runtime), the runtime (reads
at cart load time), and the packer (build-time only; never shipped). Serving
all three from one file is possible but wasteful — the frontend reads a small
subset; the packer reads build-only data that should never ship.

**Authoring format: YAML 1.2.** All source manifest files use YAML. YAML 1.2
reads naturally for the nested structures in `cart.config.yaml` (type
definitions, palette cycles, pause menu items) and has first-class IDE support
via `yaml-language-server` with JSON Schema. YAML 1.2 specifically is
required: it fixes the 1.1 boolean-coercion footguns (`no`, `yes`, `on`,
`off`) that would otherwise affect locale codes and other string values.

**Shipped format: FlatBuffers binary.** The packer compiles YAML manifests
to FlatBuffers and stores each binary in its own ELF section. Frontends and
the runtime never need a YAML parser — they access fields directly from the
mmap'd ELF bytes with no deserialization step. All YAML parse errors and
schema violations are caught at pack time on the developer's machine, not at
runtime on the player's device. `cart.build.yaml` is consumed only by the
packer and is never compiled into the cart.

**File naming: `cart.*.yaml`.** The `.yaml` extension ensures IDE YAML
tooling activates automatically. The `cart.*` identifiers remain
recognizable; the ELF section names (`.cart.info`, `.cart.config`, etc.)
are the binary compiled forms.

**Language-specific manifests.** Since each implementation language is
treated equally (ADR-0025), language-specific configuration does not belong
in `cart.config.yaml`. Each language that needs runtime configuration ships
its own manifest file compiled to its own ELF section, read by that
language's own runtime library. For v1, only Lua has a language-specific
manifest (`cart.lua.yaml` → `.cart.lua`), described below.

**Tooling.** The SDK ships JSON Schemas for all manifest files. `blyt new`
generates projects with a `# yaml-language-server: $schema=` header in each
manifest and a pre-configured VS Code workspace, giving schema-validated
completions and inline error highlighting out of the box.

## Decision

**FlatBuffers preamble convention.**
Every FlatBuffers buffer produced by the packer carries an 8-byte preamble
before the buffer itself. Readers check the preamble before passing the
remainder to the FlatBuffers deserializer.

```
offset 0:  4 bytes   ASCII type tag  e.g. "CINF", "CCFG"
offset 4:  uint16 LE format major version
offset 6:  uint16 LE format minor version
offset 8:  …         FlatBuffers buffer (no file_identifier; preamble replaces it)
```

The type tag identifies what the buffer contains; the version pair identifies
the schema revision independently of the cart's API version. A reader that
does not recognise the type tag or supports only an older major version rejects
the buffer with a clear error before any deserialization is attempted. Type
tags are name-pending (see `docs/pending-name.md`).

**Three manifest files with distinct roles:**

### cart.info.yaml — frontend-readable

Authored as YAML; compiled to FlatBuffers in the `.cart.info` ELF section.
Read by the frontend before the runtime is initialized.

Contains: title, author, version, size_class, api_version, icon path,
max_players, interactive flag, duration_hint (for non-interactive carts),
supported_locales, debug flag.

The frontend can display cart metadata, enforce size-class caps, and decide
whether to load the runtime — all without initializing the full runtime.
Framerate is not here: the frontend reads it via `blyt_cart_fps()` after cart
load (see ADR-0047); it is not needed before runtime initialization.

The `debug` field is set by the packer — `true` for debug builds, absent
(treated as `false`) for release builds. It is not authored in
`cart.info.yaml`; the packer derives it from the `--release` flag. Frontends
use it to display a visible "DEBUG BUILD" warning; the runtime uses it to
gate dev features (ADR-0065).

```yaml
# yaml-language-server: $schema=.console/schemas/cart.info.json
title:       My Game
author:      Author Name
version:     1.0.0
size_class:  standard
api_version: "1.2"
icon:        assets/icon.png
interactive: true
max_players: 2
supported_locales: [en, fr, de, ja]
# debug: true  — injected by packer in debug builds; absent in release
```

### cart.config.yaml — runtime-readable

Authored as YAML; compiled to FlatBuffers in the `.cart.config` ELF section.
Read by the runtime at cart load time. Not read by the frontend.

Contains: fps (ADR-0047), state buffer schemas (ADR-0009), voice groups
(ADR-0054), palette cycles (ADR-0061), mutable tilemaps (ADR-0060),
persistent resources (ADR-0028), inputs_used (ADR-0043), pause_items
(ADR-0064), credits_file, locale_keys (ADR-0062), font_charsets (ADR-0072).

```yaml
# yaml-language-server: $schema=.console/schemas/cart.config.json
fps: 30  # optional; default 60
voice_groups:         [ui, ambient, gameplay]
persistent_resources: [font_ui, palette_main, player_sprites]
credits_file: assets/credits.txt

inputs_used:
  dpad:     Move
  button_a: Jump
  button_b: Attack
  start:    Pause

types:
  Player:
    x:      f32
    y:      f32
    vx:     f32
    vy:     f32
    hp:     i32
    active: bool
  Enemy:
    x:      f32
    y:      f32
    hp:     i32
    active: bool
    target: i32

state_buffers:
  players: { type: Player, count: 4   }
  enemies: { type: Enemy,  count: 128 }

prefs:
  master_volume: { type: f32,  default: 1.0  }
  sfx_volume:    { type: f32,  default: 1.0  }
  subtitles:     { type: bool, default: false }

palette_cycles:
  - name: water
    start: 32
    length: 8
    rate: 4

mutable_tilemaps:
  - name:   level_main
    source: assets/level.tmx
```

### cart.build.yaml — packer-only

Read directly by the packer as YAML. Never compiled into the cart artifact.

**`cart.build.yaml` is optional for Lua carts.** When absent, the packer
assumes `language: lua` and applies the following defaults:
- Source files: all `*.lua` under `src/`.
- Assets: all files under `assets/`, resource type inferred from file
  extension (`.png` → sprite, `.xm`/`.it` → music, `.wav`/`.ogg` → sfx,
  `.tmx`/`.ldtk` → tilemap, `.fnt` → font, etc.).

When `cart.build.yaml` is present, a language declaration is required —
the packer does not infer language from file contents. Other settings
continue to use the defaults above unless overridden.

`cart.build.yaml` overrides these defaults and adds build-only settings:
compiler flags for native carts, Rhubarb configuration, font rasterization
sizes for TTF/OTF inputs, locale source files, build hooks. Assets can be
listed explicitly, matched by glob, or excluded — any of which override the
default scan for that category.

### Language declaration

The `language` key declares the primary implementation language and enables
packer-generated constants for it. The `languages` map is used when multiple
languages are present and per-language codegen control is needed.

**Single primary language (common case):**

```yaml
language: rust   # or: lua, c
```

The packer generates compile-time constants (ADR-0059) for the declared
language only. If `cart.build.yaml` is present but contains no language
declaration, the packer errors rather than guessing.

**Multiple languages with independent codegen control:**

```yaml
languages:
  rust:
    codegen: true    # generate Rust constant modules (resources.rs, state.rs, …)
  c:
    codegen: false   # build and link C code, but suppress constant generation
    sources:
      - vendor/physics.c
      - vendor/lz4.c
```

`codegen` must be stated explicitly for every entry in the `languages` map —
there is no default. This makes the intent unambiguous: a C library linked
into a Rust cart almost never needs packer-generated entity constants, and
requiring the author to say so prevents silent over-generation.

**What `codegen: false` suppresses:** only the packer's constant-file
emission step for that language (no `cart_resources.h`, no `cart_state.h`,
etc.). Building and linking of the declared sources is unaffected. A
`codegen: false` language's sources may be compiled independently and handed
to the packer as a pre-built object or archive — the common case for
third-party C libraries.

**`cart.build.yaml` full example:**

```yaml
# yaml-language-server: $schema=.console/schemas/cart.build.json
# cart.build.yaml is optional — only needed when overriding defaults

language: lua

# override default src/ scan:
sources:
  - src/main.lua
  - src/entities.lua
  - src/ui.lua

assets:
  # explicit files (override default scan for these):
  - assets/sprites/hero.png
  - assets/sprites/enemy.png
  # glob (override default scan for a category):
  sfx: assets/sfx/*.wav
  # exclude from default scan:
  exclude:
    - assets/wip/**

# TTF inputs require explicit size list (no default):
fonts:
  - file:  assets/font.ttf
    sizes: [8, 12, 16]

locale_sources:
  - lang: en
    file: assets/locales/en.csv
  - lang: fr
    file: assets/locales/fr.csv
```

### Language-specific config sections (Lua example)

For Lua carts the packer generates a `.cart.lua` ELF section (FlatBuffers)
containing Lua-specific runtime configuration. In v1 this is entirely
packer-derived — no authored source file exists. The section is read by
`liblua.rv32` during VM initialisation; the host runtime does not parse it.

When author-facing Lua settings are needed, a `cart.lua.yaml` source file
following the same `cart.*.yaml` convention will be introduced and compiled
to this section. Other implementation languages would follow the same
pattern with their own `cart.<lang>.yaml` and `.blyt.<lang>` ELF section.

## Consequences

- Frontend cold-start reads the compact `.cart.info` FlatBuffers section
  with zero-copy field access; no YAML parser, no runtime initialization.
- All schema errors surface at pack time, not at runtime on players' devices.
- Build-only configuration never bloats the shipped cart.
- Authors write YAML and get schema-validated IDE completions via
  `yaml-language-server` + JSON Schema; no bespoke tooling required.
- The `.yaml` extension means editors recognise the files automatically;
  the `cart.*` prefix keeps the three files visually grouped in a project
  directory.
- The three-file split is additive to existing ADRs: references to "the
  manifest" in other ADRs remain valid; this ADR clarifies which of the
  three files contains each declared item.
- Packer errors clearly attribute to the correct file (e.g.,
  "`cart.build.yaml` line 12: font file not found"), reducing authoring
  confusion.
