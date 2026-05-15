# ADR-0105: Project naming and console variants

## Status
Accepted.
Amended by ADR-0119 (EI_OSABI changed from 0x42 to ELFOSABI_NONE (0);
the Linux kernel rejects non-standard OSABI values at exec time).

## Context

The project was developed under placeholder names, tracked in the
now-removed `docs/pending-name.md`. Placeholders included:

| Placeholder           | Replaced by                                  |
|-----------------------|----------------------------------------------|
| `fc_` / `FC_` prefix  | `blyt_` / `BLYT_`                            |
| `console.*` Lua ns    | `blyt32.*` / `blytty.*` / `blyt3d.*` (variant top-level, full surface) plus `blyt.*` (shared portable subset) |
| `libfantasyconsole`   | `libblyt` (host runtime library)             |
| `libconsole.so`       | `libblyt32.so` / `libblytty.so` / `libblyt3d.so` (cart-facing, per variant) |
| `libconsolelua.so`    | `libblyt32lua.so` / `libblyttylua.so` / `libblyt3dlua.so` (per variant) |
| `console` CLI binary  | `blytrun` (runner) and `blyt` (dev tool)       |
| `.cart` extension     | `.blyt`, with `.blyt.demo` for demo carts    |
| `fc32` codename       | `blyt` (project) / `Blyt32` (initial variant)|
| ELF OSABI (unassigned)| `ELFOSABI_NONE` (0) — see ADR-0119           |

The deferral was deliberate: naming would be settled once the foundational
design was stable.

Two facts shaped the resolution.

**The project's name is Blyt.** Repo, runtime, libretro core, and
distributed native player are all presented to users as "Blyt".

**The console is not one console.** During design, three sibling consoles
emerged that share most of the runtime (RV32IMAFC ISA, Lua, state
management, audio, fixed-timestep update/draw lifecycle — i.e. everything
that preserves rewind / save / netplay) and diverge only in graphics and
input. Rather than picking one and treating the others as future work that
might never happen, the project commits to a *variant* model up front:
the runtime is variant-agnostic; carts link to a variant-specific dynamic
library; one native player or libretro core can host any variant of cart.

This ADR records the variant model and the concrete names that flow from
it, replacing every placeholder named in `docs/pending-name.md`.

## Decision

### Project identity

- **Project / runtime / repo / libretro core / native player:** **Blyt**
  (repo identifier: `blyt`, lowercase).
- **Distributed native player binary:** `blytrun`. End users run
  `blytrun mygame.blyt`.
- **Developer tooling binary:** `blyt`. Subcommands: `blyt pack`,
  `blyt new`, `blyt run`, `blyt watch`, `blyt info`. Includes
  the ELF toolchain, asset pipeline, and project scaffolding; not shipped
  to end users.
- **Host runtime library** (embedded by frontends — SDL, libretro adapter,
  hardware image): `libblyt`, with frontend-facing API in
  `blyt_runtime.h`. Variant-agnostic.

### Variants (internal term) / Consoles (user-facing term)

The architecture refers to the multi-target functionality as **variants**
internally — in code, ADR rationale, and architecture discussion ("variant
boundary", "cross-variant library", "variant-specific subsystem"). In
user-facing surfaces — manifest fields, Lua introspection, error messages,
and authoring documentation — the same concept is called **console**,
because that's what end users see ("this is a Blyt32 console game"). The
two words refer to the same thing; the distinction is purely about
audience.

Three variants share the runtime infrastructure (RV32IMAFC, Lua, state,
audio, lifecycle). They differ in graphics and input:

| Variant  | Display              | Input                          | Status        |
|----------|----------------------|--------------------------------|---------------|
| Blyt32   | 320×240 paletted, 256 colours, 2D Mode-X-style, 2D scene-graph | dpad + 4 face + 2 shoulder, 4 players (per ADR-0017) | Initial focus |
| BlyTTY   | 640×480×256, 80×30 8×16 text grid (CP437) | Keyboard-primary; gamepad fallback for purpose-built carts | Planned (post-v1) |
| Blyt3D   | 640×480 OpenGLES-style 3D | Same as Blyt32 | Far future |

Each variant gets its own design treatment when its work begins. ADR-0081
is the seed for BlyTTY's eventual design ADRs. Blyt3D has no design yet.

### Cart-facing API per variant

- **Cart-facing dynamic library:** `lib<variant>.so` — `libblyt32.so`,
  `libblytty.so`, `libblyt3d.so`. Carts declare exactly one variant
  library in `DT_NEEDED` matching the console declared in `.cart.info`
  (manifest field `console:`, see below). See ADR-0024.
- **Cart-facing Lua library:** `lib<variant>lua.so` — `libblyt32lua.so`,
  `libblyttylua.so`, `libblyt3dlua.so`. Each is a thin variant-specific
  binding shim over a shared engine core (see below). See ADR-0025.
- **Internal common libraries** (implementation detail; not cart-facing):
  - `libblytcommon.so` — shared C runtime (audio, state, RNG,
    lifecycle, resources, etc.) holding logic common to every variant.
    Re-exported behind each variant library.
  - `libblytcommonlua.so` — shared Lua engine core (VM lifecycle,
    sandbox, bytecode loading, Lua C API). Includes `blyt.h`; touches
    only shared subsystems. Re-exported behind each variant Lua
    library, which adds variant-specific Lua-name bindings (e.g.
    `libblyt32lua.so` registers `blyt32.gfx.blit` against
    `blyt_gfx_blit` from `libblyt32.so`).
  - Carts never link these directly. The split keeps the engine core
    compiled once across all variants and the cart-facing variant
    libraries lean.

### C API conventions

- **Symbol prefix:** `blyt_` (functions, types) and `BLYT_` (constants).
  *Single shared prefix across all variants.* Variant identity is carried
  by which header is included and which library is linked, not by symbol
  names. A symbol like `blyt_audio_voice_play` is implemented in shared
  internal code and re-exported from every variant library; a symbol like
  `blyt_gfx_blit` is exported only from `libblyt32.so`; a symbol like
  `blyt_term_put` is exported only from `libblytty.so`.
- **Header layout:** shared umbrella, per-variant umbrellas, and
  per-subsystem headers.
  - `blyt.h` — **shared umbrella**. Pulls in only the subsystem headers
    present in every variant (audio, state, RNG, save, time, lifecycle,
    resources, math). Variant-portable code — scripting language engine
    cores, cross-variant libraries, anything that uses only shared
    subsystems — includes this. The `blyt.h` surface is what the
    `blyt_*` ABI guarantees across all variants.
  - `blyt32.h`, `blytty.h`, `blyt3d.h` — **variant umbrella** headers.
    Each `#include`s `blyt.h` plus the subsystem headers specific to
    that variant (e.g. `blyt32.h` adds `blyt/gfx.h`, `blyt/input.h`,
    `blyt/stage.h`; `blytty.h` adds `blyt/term.h`, `blyt/keyboard.h`).
  - `blyt/<subsystem>.h` — subsystem headers (e.g. `blyt/audio.h`,
    `blyt/state.h`, `blyt/gfx.h`, `blyt/term.h`). Directly includable
    for code that wants tight imports.
  - `blyt_runtime.h` — frontend-facing host API (variant-agnostic).
  - Cart authors typically write `#include <blyt32.h>` (or `<blytty.h>`)
    and call `blyt_*` symbols. Variant-portable library authors write
    `#include <blyt.h>` and rely on the shared symbol set.
- **Naming convention** within the prefix: `blyt_<subsystem>_<verb>`,
  e.g. `blyt_audio_voice_play`, `blyt_gfx_blit`, `blyt_state_alloc`.

### Lua API conventions

The runtime exposes **two coexisting top-level tables** so that
cross-variant Lua library code is possible (mirroring how C library code
that uses only shared `blyt_*` symbols is variant-portable):

- **Variant top-level — full surface.** The runtime exposes
  `blyt32.*` / `blytty.*` / `blyt3d.*` containing every Lua-accessible
  subsystem available to that variant. Cart authors targeting a specific
  variant write code here:
  ```lua
  blyt32.audio.voice_play(...)
  blyt32.gfx.blit(img, x, y)
  blyt32.state.alloc(...)
  blyt32.input.pressed(0, BUTTON_A)
  ```
- **Shared top-level — portable subset.** The runtime additionally
  exposes `blyt.*` containing only the subsystems present in every
  variant: audio, state, RNG, save, time, lifecycle, resources, math
  (and `blyt.info` for variant introspection). Cross-variant Lua
  libraries write code here:
  ```lua
  blyt.audio.voice_play(...)    -- works in any variant
  blyt.state.alloc(...)         -- works in any variant
  blyt.gfx                      -- nil; graphics is variant-specific
  ```

For shared subsystems the two tables are the **same Lua table by
identity**: `blyt.audio == blyt32.audio` is true. One implementation,
two access paths. Variant-specific subsystems (graphics, input, term,
keyboard, stage) live only on the variant table.

**Console introspection.** `blyt.info.console` returns the running
console identifier as a string (`"blyt32"` / `"blytty"` / `"blyt3d"`).
`blyt.info.api_version` returns the runtime's major.minor API version
(per ADR-0032). Hybrid libraries that mostly use shared APIs but want a
console-specific fast path branch on these.

**Conventions for cart and library authors:**
- A cart that does not aim to be variant-portable uses the variant
  top-level (`blyt32.*` for a Blyt32 cart) for everything. Default
  cart-template scaffolding follows this pattern.
- A Lua library intended to be reusable across variants uses `blyt.*`
  exclusively. The package's auto-completion surface is naturally
  constrained: there is no `blyt.gfx`, so the library cannot
  accidentally bind to a variant-specific surface.
- Mixing both within one cart is allowed but discouraged for style; pick
  one and stick with it within a project.

**Asymmetry with C.** C code achieves cross-variant portability through
shared symbol names (`blyt_audio_voice_play` is in every variant
library). Lua achieves it through the parallel `blyt.*` table. The
mechanisms differ because C namespaces are implicit in headers/libraries
while Lua tables are explicit at every call site, but the resulting
contract — shared APIs are portable, variant APIs are not — is the same
in both languages.

### Cart format

- **File extension:** `.blyt` for all consoles. The manifest field
  `console: blyt32 | blytty | blyt3d` in `.cart.info` discriminates.
  Per-console extensions buy nothing — libretro frontends don't use
  per-extension metadata, so console icons would be per-core anyway.
- **Manifest field default.** `console:` is **optional and defaults to
  `blyt32`**. Since 99% of carts will be Blyt32 carts (BlyTTY is planned
  but post-v1; Blyt3D is far future), the default keeps the common case
  noise-free — a Blyt32 cart's manifest needs no `console:` line at all.
  When BlyTTY ships, BlyTTY carts must declare `console: blytty`
  explicitly. Future versions may make the field mandatory; that would
  be a backwards-incompatible change but acceptable, and `blyt`
  tooling can migrate older carts automatically by inserting the
  default.
- **Non-interactive extension (placeholder):** `.blyt.demo` for
  non-interactive carts (screensaver-class, museum installation, attract
  reels, demoscene intros, generative art). The manifest field
  `interactive: false` (per ADR-0031) is the source of truth; this
  extension is purely a user-visible affordance for file managers and
  humans browsing directories. **The `.demo` suffix is a placeholder**:
  it conflates with "demo version of a paid game" and a better term
  (e.g. `.attract`, `.reel`, `.idle`, `.art`) may replace it. Frontends
  filter on `interactive: false`, not on the suffix, so renaming the
  suffix later is a cosmetic change.
- **Manifest filenames:** `cart.info.yaml`, `cart.config.yaml`,
  `cart.build.yaml` — unchanged. The `cart.*` prefix is a stable
  convention regardless of project name.
- **ELF section names:** `.cart.info`, `.cart.config`, `.cart.lua`,
  `.cart.resources` — unchanged. Internal convention, not user-facing.
- **FlatBuffers preamble tags:** `CINF` (cart info), `CCFG` (cart config)
  — unchanged. Stable identifiers derived from "cart info / cart config".
- **ELF OSABI value:** `ELFOSABI_NONE` (0). The original placeholder was
  `0x42` ('B' for Blyt), but spike testing showed the Linux kernel rejects
  non-standard OSABI values when exec-ing an ELF natively. Cart identity
  is carried exclusively by the `console:` field in `.cart.info`; the
  OSABI field carries no Blyt-specific information. See ADR-0119.

### Runtime / variant relationship

One runtime can host any cart variant. The native `blytrun` reads the
`console:` field from `.cart.info` (defaulting to `blyt32` if absent),
then provides the matching variant library (and Lua library, for Lua
carts) at load time. The libretro core follows the same model. There is
no separate "Blyt32 libretro core" and "BlyTTY libretro core" — one core,
three variants.

## Consequences

- The project ships under one identity (Blyt) with one runner binary
  (`blytrun`), one libretro core, and one set of distribution channels.
  Per-variant artefacts are libraries, not products.
- Carts are portable across runtime distributions but locked to their
  declared variant. A Blyt32 cart cannot be played on a runtime that only
  ships `libblytty.so`; the mainstream `blytrun` ships all variants.
- The variant boundary is a real architectural seam, not a hypothetical
  one. Drawing it correctly during Blyt32 implementation makes BlyTTY
  cheap to add later. Mistakes (Blyt32 assumptions leaking into shared
  code) are costly.
- The single `blyt_` C prefix avoids variant-renaming churn for shared
  code. The variant top-level Lua namespace makes variant identity
  obvious at every cart-side call site.
- The `.blyt` extension is one ecosystem-level identifier. Carts don't
  rename when a variant boundary moves; only the manifest field changes.
- `docs/pending-name.md` is removed; this ADR is the forward-looking
  reference. Earlier ADRs that referenced `pending-name.md` are revised
  to reference this ADR (notably ADR-0024 for cart extension,
  ADR-0025 for Lua library naming, ADR-0081 for the BlyTTY sibling).
- SDK package registrations (cargo crate, npm, VS Code publisher ID)
  are unblocked but deferred until those artefacts actually exist.

## Alternatives considered

- **Two-tier C prefix (`blyt_` shared, `blyt32_` variant).** Rejected:
  added churn for shared code if a symbol moved between tiers, and the
  variant boundary is already explicit at the header and library level.
- **All-variant C prefix (`blyt32_*` everywhere, even for shared
  semantics).** Rejected: forces shared logic to be re-named per variant
  with no compensating gain; cart code becomes visually noisy.
- **Single shared Lua namespace (`blyt.*` only, no variant top-level).**
  Rejected: variant-specific subsystems (graphics, input, term) belong
  on a variant-named table because they're not portable, and folding
  them into `blyt.*` would invite accidental cross-variant code that
  silently breaks at load time on the wrong variant. The dual model —
  `blyt.*` for portable code, `blyt32.*` for the full surface — keeps
  portability errors visible at the call site.
- **Variant-only Lua top-level (`blyt32.*` / `blytty.*` only, no shared
  `blyt.*`).** Initially chosen and then refined: the asymmetry with C
  (where shared symbols are portable across variants by design) created
  a real gap for Lua library authors. Restoring `blyt.*` as a shared
  portable table closes the gap without giving up the variant top-level
  for cart authors.
- **Per-variant cart extensions (`.blyt32`, `.blytty`).** Rejected:
  libretro frontends don't use per-extension metadata for icons or
  per-content-type display, so the only effect would be more extensions
  to register and more confusion for users with mixed libraries.
- **Single binary for both runner and dev tooling.** Rejected:
  `blytrun` is lean (runtime + frontend only, no toolchain or debug
  scaffolding); `blyt` is the heavier dev tool with a different footprint
  and audience. Splitting cleanly is closer to rust/cargo or node/npm
  than to pico8/love.
