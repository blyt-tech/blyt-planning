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
and packed into the ELF cart file. This phase runs only for `blyt pack`;
it is skipped entirely in `blyt watch` / dev mode.

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
| Lua source → bytecode | `luac` (Lua 5.4, matching runtime version) run inside the fc32 emulator | Standard luac cannot cross-compile for RV32IMAFC; running in-emulator guarantees compatibility (ADR-0109) |

**Forked (user-installed):**

| Transform | Tool |
|---|---|
| C/Rust/Zig → RV32IMAFC | Platform compiler toolchain |

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

## Amendment (2026-06-26): raw resource type, explicit asset declaration, and as-built corrections (#162)

The thin-slice asset pipeline (#91) shipped text (`.txt`) only. Issue #162 adds
the **`raw`/opaque** resource type — the one type addable with zero new runtime
or ECALL API, because the runtime already serves uninterpreted bytes and the
hot-reload path is type-agnostic. Implementing it settled several questions this
ADR left under-specified and corrected several places where the original prose
no longer matches the code. This amendment supersedes the conflicting parts
above.

### Membership is explicit; type is by extension

The original "by file extension" table (above) doubled as both a *type* map and
an *auto-inclusion* rule, and listed `.bin` → raw / `.json` → raw as
auto-recognized. That conflation is removed:

- **There are no default passthrough types.** Nothing is auto-scanned for
  passthrough — *not even `.txt`*. Every asset a cart ships must be declared
  explicitly via an `include:` glob (see schema below). The `.bin` → raw and
  `.json` → raw rows are withdrawn; those extensions are raw only because, when
  included, anything without a dedicated processor is raw.
- **Auto-scan is reserved for *processed* types.** When a real transform exists
  for an extension (sprite/audio/font/tilemap — all still deferred until their
  subsystem APIs land), that extension is auto-scanned across the asset dirs as
  if `**/*.<ext>` were implicitly declared, because the packer knows how to
  process it. Until such a processor lands, the auto-scan set is empty.
- **A resource's *type* is decided by its extension, independent of how it
  became a member.** `.txt` → `text`; every other extension → `raw`. `include:`
  controls membership only — it never forces a type. Declaring `**/*.txt` in
  `include:` yields `text` resources, not raw. When an extension later gains a
  processor, the same already-included files upgrade from `raw` to the typed
  form with no manifest change.

`text` and `raw` are both passthrough transforms today (identity copy); the
distinction is the `.meta` `type=` field (`type=text` vs `type=raw`), which is
descriptive only — the runtime never reads `.meta` (it serves `.data` bytes via
`resource-id-index`). `blyt_resource_text_get` is a guest-side convenience that
NUL-terminates a copy; its `len` out-param is authoritative, so it works for any
resource, `raw` included.

### `assets:` manifest schema (in `blyt.build.yaml`)

The asset-declaration block lives in **`blyt.build.yaml`** (the name
`cart.build.yaml` used throughout this ADR is the old name for the same file).
The `additional_dirs:` shape sketched earlier is replaced by a uniform
`assets.dirs` list — there is nothing special about `assets/` except that it is
the default directory, so adding globs to it and to any other directory look
identical:

```yaml
assets:
  dirs:
    - dir: assets/            # the default dir; entry optional unless you need
                              # include/exclude/constant_prefix on it
      include:
        - "**/*.txt"          # ship these as text resources
        - "**/*.lvl"          # custom format → raw
      exclude:
        - "wip/**"            # drop matches from the membership set
    - dir: other_assets/
      constant_prefix: DATA   # REQUIRED for any non-assets/ dir (see below)
      include:
        - "**/*.dat"
```

- `assets:` is a map (a `dirs:` list plus room for future cross-cutting asset
  config). `dirs` is a list of `{ dir, include?, exclude?, constant_prefix? }`.
- `assets/` is the implicit default directory: it is a member dir even when
  `dirs` is omitted or does not list it. Listing it lets you attach
  `include`/`exclude`/`constant_prefix`.
- `include`/`exclude` globs are **relative to that dir's root** (`**/*.foo`
  matches `<dir>/**/*.foo`), keeping every dir symmetric. This supersedes the
  earlier project-root-relative examples (`assets/wip/**`).
- Membership of a dir = (auto-scanned processed-type files) + (files matching
  any `include` glob) − (files matching any `exclude` glob). `exclude` applies
  to the whole set, so it can drop an auto-scanned file too, not only includes.
- Globs are matched with the `globset` crate (supports `**`); add it to the
  packer crate table above.

### `constant_prefix` (folded in from the directory-prefix rules above)

The directory-prefix rules from §"Resource constant naming" apply per `dirs`
entry as the optional `constant_prefix` field: empty default for `assets/`,
**required** for any other directory (omission is a build error), must already
be `[A-Z0-9_]*` (empty permitted) and is not transformed by the packer. It is
prepended to the derived name, and the existing duplicate-name → build-error
check spans all declared dirs (so `assets/level.bin` and `other/level.bin`
without distinct prefixes are a build error, as specified).

### Dev-mode watch

`blyt run` / dev mode watches **every declared asset directory** (recursively),
not just `assets/`, so edits to assets in additional dirs hot-swap via
`update_assets` like assets in `assets/` (#88/#91/#122).

### As-built corrections to the staging description

The staging/​tracking prose above predates the implemented engine and is stale;
the implementation is authoritative:

- **Input tracking is not `build/packer-state.json`.** There is no such file.
  Staging is **content-addressed** — each asset is written to
  `resources/<name>-<fingerprint>.data` (xxh3 of the bytes) so a changed asset
  produces a new file alongside the old; up-to-date checking is handled by the
  devtool build engine's per-task state under `build/.blyt-tasks/`.
- **Staging is flat and fingerprinted, not a mirror of the source subtree.**
  Files are `resources/<name>-<fp>.data` (+ `.meta`), not
  `resources/sprites/npcs/gnome_king.data`.
- **`resource-id-index` lines carry the fingerprinted filename**, e.g.
  `1 resources/greeting-<fp>.data` — not the extension-less,
  fingerprint-less `42 → resources/sprites/npcs/gnome_king` shown above.

### Known deviation: constant-name derivation (#164)

The implemented `resource_name_from_rel` does not yet perform the NFD /
diacritic-stripping of step 3, nor the "leading digit" / "residual non-ASCII"
build-error checks of the build-errors table; only the empty-stem error is
enforced. This pre-existing gap is tracked in #164 and is out of scope for #162.

### Still deferred (unchanged by #162)

All typed transforms (sprite/image, audio + path-keyword subtyping, font,
tilemap), ADR-0068 typed handles, Phase 2 bundling (`index.fb`, compression),
single-palette mode, and `console freeze` remain deferred until their subsystem
APIs exist. #162 is **text + raw only**.

## Amendment (2026-06-27): typed text/raw handles, declared text extensions, text-only NUL termination (#162→#166)

Issue #166 turns the build-side `text`/`raw` tag (#162, descriptive only) into a
typed handle enforced through codegen, and hardens text for C-string safety. It
does **not** change the runtime's resource model: the runtime remains byte-blind
— it serves `.data` bytes and never reads `.meta` or interprets a resource's
type. The cross-language typed-handle scheme lives in ADR-0068's 2026-06-27
amendment; the build-pipeline parts are here.

### Text-extension declaration (replaces the hardcoded `.txt` mapping)

The `.txt`→text / everything-else→raw mapping becomes declarative via a new
global `assets.text_extensions` list, default `["txt"]`:

```yaml
assets:
  text_extensions: [txt]   # default ["txt"] when omitted
  dirs: [...]
```

Type is still **by extension** (the 2026-06-26 amendment's principle): an
extension in `text_extensions` is `text`, every other (without a dedicated
processor) is `raw`. `include:`/`exclude:` still control membership only.
Extension-granularity is intentional; a per-glob/per-file `type:` override is
deferred until a concrete need appears.

### Build-time text validation

A `text` resource must be valid UTF-8 with **no embedded NUL** (`0x00`) byte;
otherwise the build fails with the file path. (`0x00` is itself valid UTF-8 —
U+0000 — but never appears inside a multi-byte sequence, so the two checks are
orthogonal: `valid_utf8 && !contains(0x00)`.) This is honest-cart correctness,
not a security boundary — see the threat-model note below.

### Text-only NUL termination (C-string safety), runtime stays byte-blind

The devtool appends exactly one `\0` byte to the staged `.data` of **text**
resources only. **Raw resources stay byte-exact** — an unexpected trailing byte
can break a binary parser (length-prefixed, checksummed, or EOF-delimited
formats). Consequences:

- The **reported length includes the NUL.** The emulated `blyt_resource_pin`
  ECALL copies `len` bytes into guest scratch, so the terminator only reaches
  the guest if it is inside `len`. The host serves stored bytes + size verbatim
  and stays fully type-blind (`len` = content+1 for text, content for raw — it
  neither knows nor cares which). The packed `.cart.resource.<id>` section
  inherits the NUL from `.data`; dev-mode `read_whole_file` reports the file
  size — both legs agree with **zero host change**.
- Only the **guest text accessors** are text-aware. `blyt_resource_text_get`,
  Rust `LoadedText`/`PinnedText`, and Lua `:text()` (in both the guest runtime
  and the WASM host-Lua fast path) assert `len>=1 && data[len-1]==0`, then report
  content length `len-1` (NUL stripped) so Rust `String` / Lua string lengths are
  correct; the buffer is already NUL-terminated for C consumers. Because
  build-time validation guarantees the trailing NUL is the *only* NUL,
  `len-1 == strlen == content_len` holds exactly. This assertion doubles as the
  runtime "is this really a text resource" check (a raw id fed to a text accessor
  fails it).

### Threat model (why no runtime resource-type validation)

The earlier #166 proposal called for runtime hostile-cart text validation; this
is **struck**. The real threat is a malicious cart breaking out of its sandbox by
feeding a crafted payload (embedded NUL → C `strlen`/length disagreement;
malformed UTF-8 → decoder bug) into a place where the *host* parses it. Such a
payload can be constructed anywhere (a `raw` resource, a C array, computed
bytes), so validating *text resources* specifically defends nothing. Defense
belongs at string-consuming API boundaries, which are already `(ptr,len)`-based
and reject embedded NUL where they parse (e.g. `BLYT_ECALL_CONSOLE_DEBUG`,
`bridge_read_guest_str`). As long as the runtime treats every resource as an
opaque bag of bits and never parses it, runtime resource-type validation buys
nothing.
