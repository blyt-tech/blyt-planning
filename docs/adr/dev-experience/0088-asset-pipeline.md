# ADR-0088: Asset pipeline — packer architecture, resource types, and constant naming

## Status
Accepted

## Context

The §17 open item on asset pipeline input formats covers the packer's build
model, accepted input formats, conversion rules, and how source files become
addressable runtime resources. ADR-0044 established the CLI commands and
high-level toolchain; this ADR specifies the mechanics.

## Decision

### Packer build model

The packer operates in two phases.

**Phase 1 — Transform.** Each source asset is processed into its internal
format and written to the staging directory (`build/`). Transforms are
cached: only assets whose inputs have changed since the last build are
re-processed. All staged resources are uncompressed; compression is deferred
to Phase 2.

**Phase 2 — Bundle.** Staged files are compressed where beneficial (ADR-0026)
and packed into the ELF cart file. This phase runs only for `blytbuild pack`;
it is skipped entirely in `blytbuild watch` / dev mode.

**Dev mode** runs Phase 1 only. The runtime accepts a `build/` directory
path in place of a `.blyt` file and reads resources directly from the staged,
uncompressed files. Hot reload (ADR-0045) signals the runtime to re-read
changed files from the staging directory — no ELF step required.

### Incremental build — input tracking

Each transform's inputs are tracked using the same strategy as Gradle's local
up-to-date checks:

- **Fast path:** if stored size and mtime match the current `fstat` result,
  the input is assumed unchanged and the transform is skipped.
- **Slow path:** if size or mtime differs, the file is hashed. If the hash
  matches the stored hash, the stored size/mtime are updated but the
  transform is still skipped (handles VCS checkouts, `touch`, etc.). If the
  hash differs, the transform runs.

The tracking state is stored in `build/packer-state.json`, which maps each
input path to its last-known size, mtime, hash, and the list of output paths
it produced.

### Staging directory layout

The staging directory mirrors the source asset directory structure. The source
extension is dropped; each asset produces a `.data` file (transformed bytes)
and a `.meta` file (type, dimensions, format, and other metadata):

```
build/
  packer-state.json
  resource-id-index       ← maps resource IDs to relative paths
  cart.info.fb            ← compiled from cart.info.yaml
  cart.config.fb          ← compiled from cart.config.yaml
  resources/
    sprites/
      npcs/
        gnome_king.data   ← palette-indexed pixel data
        gnome_king.meta   ← type: image, width: 64, height: 48, ...
    audio/
      sfx/
        jump.data
        jump.meta
      music/
        theme.data
        theme.meta
    tilemaps/
      level1.data
      level1.meta
```

**`resource-id-index`** maps integer resource IDs to staging paths (e.g.,
`42 → resources/sprites/npcs/gnome_king`). The runtime reads this single
file at dev-mode startup to build its in-memory ID→path table. It is updated
only when the resource *set* changes (asset added, removed, or renamed) — not
on content edits, which is the common case during development.

**`index.fb`** (the full resource directory, including all metadata) is
assembled from the individual `.meta` files only during Phase 2 (cart bundle).
It is not present in the staging directory during development.

### Packer implementation language

The packer is implemented in Rust. This shapes the library choices below:
several dependencies that would otherwise require C library FFI or forked
processes are available as native Rust crates.

### External tools and embedded libraries

The packer forks external processes only where no suitable Rust crate exists.
Everything else is handled by Rust crates or inline packer code, keeping the
tool dependency surface small.

**Forked (bundled with SDK):**

| Transform | Tool | Reason not a crate |
|---|---|---|
| TTF/OTF → BMFont | `fontbm` | No mature Rust BMFont generator |
| WAV → lip sync JSON | `rhubarb` | C++ tool, no Rust equivalent |
| Lua source → bytecode | `luac` (Lua 5.4, matching runtime version) run inside the fc32 emulator | Standard luac cannot cross-compile for RV32IMFC; running in-emulator guarantees compatibility (ADR-0109) |

**Forked (user-installed):**

| Transform | Tool |
|---|---|
| C/Rust/Zig → RV32IMFC | Platform compiler toolchain |

**Rust crates (no fork):**

| Transform | Crate |
|---|---|
| PNG → palette-indexed | `imagequant` (pngquant's own library, rewritten in Rust) |
| PNG load / image handling | `image` |
| Aseprite → sprite frames | `aseprite` crate (open format, no CLI fork needed) |
| OGG / MP3 / FLAC / WAV decode | `symphonia` (pure Rust, single crate for all formats) |
| QOA encode | `qoa` crate (or FFI to C reference encoder if crate is immature) |
| ADPCM encode | Inline Rust (~100 lines, trivial to own) |
| Opus encode | `opus` crate (Rust bindings to libopus) |
| Tiled TMX/JSON parse | `tiled` crate |
| LDtk parse | `serde_json` + custom deserialization |
| YAML parse | `serde_yaml` |
| FlatBuffers | `flatbuffers` crate |
| zstd compression | `zstd` crate |

**Internal (no library):**
- Cart ELF assembly
- CSV → locale binary
- Staging directory management and packer-state tracking

### Resource type identification

**By file extension (authoritative for format):**

| Extension | Type | Transform |
|---|---|---|
| `.png` | sprite | Palette quantize |
| `.ase`, `.aseprite` | sprite | Aseprite CLI → quantize |
| `.xm`, `.it`, `.mod`, `.s3m` | music | Passthrough |
| `.wav`, `.ogg`, `.mp3`, `.flac` | audio (subtype by path, see below) | Encode |
| `.ttf`, `.otf` | font | `fontbm` rasterize |
| `.fnt` | font | BMFont repackage |
| `.tmx` | tilemap | Tiled XML |
| `.tmj` | tilemap | Tiled JSON |
| `.ldtk` | tilemap | LDtk |
| `.txt` | text | Raw UTF-8 |
| `.bin` | raw | Passthrough |

`.json` is ambiguous and treated as raw data; Tiled JSON exports should use
`.tmj`. Unknown extensions are ignored unless explicitly declared in
`cart.build.yaml`.

**PCM audio subtype by path keyword:**

Tracker formats (`.xm`, `.it`, `.mod`, `.s3m`) are always `music` regardless
of path. For PCM formats, the subtype is resolved from keywords anywhere in
the path:

| Path keyword | Audio subtype | API |
|---|---|---|
| `sfx` | sfx | `audio.sfx.play()` |
| `speech` | speech | `speech.play()` |
| `stream`, `streams`, `music` | stream | `audio.stream.play()` |

Resolution rules:
- Single keyword found → use that subtype
- Multiple keywords found → build error; require explicit declaration
- No keyword found → default `sfx`; dev-mode warning issued

Stream resources require Large or Flagship size class; declaring a stream
resource in a smaller cart is a build error.

### Audio encoding

| Subtype | Default encoding | Default sample rate |
|---|---|---|
| sfx | QOA | Preserve source rate |
| speech | ADPCM | Downsample to 11,025 Hz |
| stream | Opus | Preserve source rate |

ADPCM as the speech default applies ADR-0056's recommendation automatically.
Developers override per-file or per-pattern in `cart.build.yaml`:

```yaml
assets:
  overrides:
    - file: assets/speech/narrator.wav
      sample_rate: 22050     # higher quality for narrator
    - pattern: assets/sfx/ui/**
      encode: adpcm          # save space for UI sounds
```

**Warnings for suboptimal source formats.** If a source file requires more
work than necessary at pack or runtime (e.g., a 44.1 kHz stereo SFX that will
be resampled by the mixer), the packer issues a warning:

```
WARNING: assets/sfx/crowd.wav is 44.1kHz stereo — will be resampled to mono 
at runtime. Consider providing at 22050Hz mono to reduce mixer overhead.
```

Warnings are suppressed for files with an explicit `encode:` or `sample_rate:`
override (the developer has consciously made the choice). Other cases:

```yaml
assets:
  overrides:
    - file: assets/sfx/crowd.wav
      suppress_warnings: [sample_rate]
```

### Image quantization

Images are quantized to the cart's 256-color palette at pack time. Two
quantization modes:

- **`nearest`** (default) — clean nearest-colour mapping. Dev-mode warnings
  issued for pixels whose source colour has no close match in the palette.
- **`dither`** — Floyd-Steinberg error-diffusion dithering. No warnings
  issued; remapping is intentional.

```yaml
# globally
assets:
  quantize: dither

# per asset
assets:
  overrides:
    - file: assets/backgrounds/cave.png
      quantize: dither
    - file: assets/sprites/gnome_king.png
      warn: false          # nearest, but suppress off-palette warnings
```

### Single-palette mode

Carts that use one palette throughout declare it in `cart.config.yaml`:

```yaml
palette: assets/palette.png
```

Effects:
- The runtime loads this palette automatically before `init`; the cart never
  calls `blyt_gfx_palette_set` for the base palette.
- All images are quantized to this palette at pack time using nearest-colour.
  Off-palette pixels produce dev-mode log warnings identifying the affected
  file and the colour delta, suppressable via `warn: false` or `quantize: dither`.
- Save thumbnails always use the declared palette; `thumbnail_palette` in save
  metadata (ADR-0087) is redundant and omitted in this mode.

### Resource constant naming

The packer generates a constant for each resource. The transformation:

1. Strip the directory prefix (see below).
2. Strip the file extension.
3. Apply Unicode NFD decomposition and strip combining diacritical marks
   (U+0300–U+036F), mapping accented characters to their ASCII base
   (`é`→`e`, `ü`→`u`, `ñ`→`n`).
4. Replace `/`, `-`, spaces, and any remaining non-alphanumeric characters
   (except `_`) with `_`.
5. Collapse consecutive `_`.
6. Trim leading and trailing `_`.
7. Convert to uppercase.
8. Prepend `R_` (C) or use as a key in the `R.` table (Lua).

```
assets/sprites/npcs/gnome_king.png
→ strip 'assets/':         sprites/npcs/gnome_king.png
→ strip extension:         sprites/npcs/gnome_king
→ remove diacritics:       sprites/npcs/gnome_king      (no change here)
→ replace non-alnum:       sprites_npcs_gnome_king
→ collapse _:              sprites_npcs_gnome_king      (no change here)
→ trim leading/trailing _: sprites_npcs_gnome_king      (no change here)
→ uppercase:               SPRITES_NPCS_GNOME_KING
→ prefix:                  R_SPRITES_NPCS_GNOME_KING
```

**Directory prefix.** `assets/` is stripped by default with no constant
prefix added. Every other declared directory must specify `constant_prefix`
explicitly in `cart.build.yaml` — omitting it is a build error. The prefix
must be valid identifier characters already (`[A-Z0-9_]*`; empty string
permitted); the packer does not transform it.

```yaml
assets:
  additional_dirs:
    - path: data/
      constant_prefix: DATA      # data/config.bin → R_DATA_CONFIG
    - path: sounds/
      constant_prefix: ""        # sounds/sfx/jump.wav → R_SFX_JUMP
```

`assets/` can also declare an explicit `constant_prefix` if the default
(empty) is not wanted.

**Build errors — all silent handling is rejected:**

| Condition | Result |
|---|---|
| Two resources normalize to the same constant | Build error |
| `constant_prefix` contains invalid or lowercase characters | Build error |
| `constant_prefix` omitted for a non-`assets/` directory | Build error |
| File stem is empty after extension strip | Build error |
| Non-ASCII character remains after diacritic removal | Build error |
| Normalized identifier begins with a digit | Build error |

Collision detection spans all declared directories — a constant produced from
`assets/` can collide with one from an `additional_dirs` entry.

### Freeze command

`console freeze` (exact name TBD) converts source assets to their target
encoded formats in-place, so subsequent builds treat them as pre-converted and
skip re-encoding. This is appropriate at release time or when a batch of
assets has been signed off and will no longer be edited.

The command is intentionally separate from the normal build loop: developers
need the original source files (full-quality audio, unquantized images) for
editing. Overwriting sources during normal builds would destroy the originals.
Exact UX (in-place vs. separate frozen directory, selective freeze) is deferred.

## Consequences

- The two-phase model keeps dev-mode iteration fast (Phase 1 only, no ELF,
  no compression) while producing correct optimised carts in Phase 2.
- Incremental builds skip unchanged assets; the common case (editing one
  sprite or Lua file) rebuilds only that asset.
- `resource-id-index` stability means the hot path (content edits to existing
  assets) never touches the index — only structural changes (add/remove/rename)
  do.
- Embedded audio codec libraries eliminate the ffmpeg distribution and
  licensing problem; the SDK is self-contained for audio conversion.
- Path-keyword audio subtyping requires no manifest declarations for
  conventionally-structured projects; explicit overrides handle exceptions.
- Constant naming is deterministic, auditable, and fails loudly on any
  ambiguity — no silent transforms or guessed intent.
- Single-palette mode eliminates palette management code for the common case
  and simplifies save thumbnail display.
