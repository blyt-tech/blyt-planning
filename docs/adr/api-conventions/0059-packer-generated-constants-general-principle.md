# ADR-0059: Packer-generated compile-time constants as the universal name→ID bridge

## Status
Accepted

## Context

Throughout the API, cart code needs to reference named entities declared in
the manifest: state buffer fields, resource handles, RNG streams, locale
keys, palette cycle handles, voice group handles, pause menu items. Each
reference needs to be ergonomic in source (human-readable names), fast at
runtime (no string lookup), and validated at build time (missing references
are compile errors, not runtime crashes).

Two naive approaches both fail:
- **String keys at runtime:** readable, but O(n) or O(1)-with-hash lookup on
  every access, and errors surface at runtime.
- **Raw integer literals in cart source:** fast, but opaque, fragile to
  schema changes, and validated nowhere.

A third benefit beyond correctness and performance: **IDE experience**. When
cart code references a constant (`R_HERO_SPRITES`), the editor can provide
autocomplete from the generated header, jump-to-definition, and inline
documentation. A string literal (`"hero_sprites"`) gives the editor nothing
to work with — no completion, no type information, no "find all references."
Errors that would be caught at edit time with constants are invisible until
the packer runs.

## Decision

**All named manifest entities that are referenced from cart code use the same
pattern: string names in the manifest, packer-generated compile-time integer
constants in cart source, opaque integer IDs in the runtime.**

```
manifest (strings) → packer → generated header (compile-time constants)
                                      ↓
                             cart source uses constants
                                      ↓
                             runtime receives integer IDs
```

### Naming convention for generated constants

Packer-generated constants use short domain prefixes with **no `FC_` prefix**.
`FC_` is reserved exclusively for SDK-defined constants in `fc_cart.h`
(built-in assets, error codes, flag values, button identifiers). Cart-specific
constants such as `R_HERO_SPRITES` are never `FC_R_HERO_SPRITES` — the
absence of `FC_` is the signal that a constant was generated for this
particular cart, not defined by the SDK.

| Namespace | C prefix | C example | Lua module | Lua example |
|-----------|----------|-----------|------------|-------------|
| Resource handles | `R_` | `R_HERO_SPRITES` | `R` | `R.HERO_SPRITES` |
| State buffer handles | `S_` | `S_PLAYERS` | `S` | `S.PLAYERS` |
| State field handles | `S_<TYPE>_` | `S_PLAYER_X` | `S` | `S.PLAYER_X` |
| RNG stream handles | `RNG_` | `RNG_GAMEPLAY` | `RNG` | `RNG.GAMEPLAY` |
| Locale key handles | `L_` | `L_MENU_START` | `L` | `L.MENU_START` |
| Voice group handles | `VG_` | `VG_UI` | `VG` | `VG.UI` |
| Palette cycle handles | `CYCLE_` | `CYCLE_WATER` | `CYCLE` | `CYCLE.WATER` |
| Pause item handles | `PAUSE_ITEM_` | `PAUSE_ITEM_RESUME` | `PAUSE_ITEM` | `PAUSE_ITEM.RESUME` |
| Preference keys | `PREF_` | `PREF_MASTER_VOLUME` | `PREF` | `PREF.MASTER_VOLUME` |

### Runtime-provided constants — high bit set

Built-in runtime entities (fonts, palettes, and other assets bundled in the
runtime binary) need handles that can never collide with cart-generated
constants. Packer-generated constants start at 1 and increment; a cart with
100 resources will have IDs in the range 1–100. Runtime constants therefore
set bit 31:

```c
// fc_cart.h — static, not generated
#define FC_FONT_BUILTIN   ((fc_resource_h)0x80000001)
#define FC_FONT_SMALL     ((fc_resource_h)0x80000002)
#define FC_FONT_ICONS     ((fc_resource_h)0x80000003)
#define FC_PALETTE_DEFAULT ((fc_resource_h)0x80000010)
#define FC_PALETTE_VGA    ((fc_resource_h)0x80000011)
#define FC_PALETTE_EGA    ((fc_resource_h)0x80000012)
#define FC_PALETTE_CGA    ((fc_resource_h)0x80000013)
```

This means:
- Cart constants are always in the range `[1, 0x7FFFFFFF]`.
- Runtime constants are always in the range `[0x80000001, 0xFFFFFFFF]`.
- The runtime can branch on bit 31 to distinguish "look up in cart resource
  table" from "look up in built-in asset table" with a single mask check.
- Zero remains the universal invalid/null sentinel across all handle types.

In C, the `FC_` prefix on runtime constants (vs. `R_`, `A_`, `PREF_`, etc.
on cart constants) is a naming-level signal of the same distinction.

**In Lua, runtime-provided constants are merged into the same per-namespace
module as cart-generated constants.** The high-bit distinction is an
implementation detail invisible to Lua authors:

```lua
local R = require("R")
R.HERO_SPRITES   -- cart-declared resource (packer-generated)
R.FONT_BUILTIN   -- runtime built-in (pre-populated by the runtime module)
R.PALETTE_VGA    -- runtime built-in
```

The `R` module is pre-populated with all runtime built-ins for its namespace
before the cart runs; the packer appends cart-declared constants on top.
Authors always `require("R")` for any resource handle regardless of origin.
The `FC_` prefix appears only in C (`fc_cart.h`); in Lua the module name
itself provides sufficient namespacing.

### Cart override of built-in resources

If a cart declares a resource whose name matches a known built-in name
(e.g., a resource named `font_builtin`), the cart's version silently replaces
the built-in for all code in that cart. This is deliberate: a shared library
or framework can reference `FC_FONT_BUILTIN` / `R.FONT_BUILTIN` without
knowing or caring whether the cart has substituted its own version.

**In C**, the generated header redefines the colliding `FC_` constant to the
cart's resource ID. Library code compiled with the cart's generated headers
(`#include "cart_resources.h"` after `#include "fc_cart.h"`) transparently
gets the override:

```c
// cart_resources.h (generated — excerpt when cart overrides font_builtin)
#undef  FC_FONT_BUILTIN
#define FC_FONT_BUILTIN  ((fc_resource_h)3)   // cart's resource ID
```

**In Lua**, the `R` module entry for the colliding name is replaced with the
cart's ID during packer generation; library code using `R.FONT_BUILTIN`
requires no changes.

**Non-overridden built-ins** retain their high-bit IDs unchanged. The
override is per-name and only applies to names the cart explicitly declares.
The packer emits a notice (not a warning) for each override so the
substitution is visible in build output.

### Properties of the pattern

- **Build-time validation:** the packer errors if a constant is referenced
  in cart source that is not declared in the manifest, and warns if a
  manifest entry has no corresponding usage.
- **Zero runtime overhead:** constants are integers; no hash lookup or string
  comparison occurs at any call site.
- **Type safety in C:** each namespace uses a distinct `uint32_t` typedef
  (`fc_resource_h`, `fc_field_h`, `fc_rng_h`, `fc_locale_key_h`, etc.),
  so passing a locale key handle where a resource handle is expected is a
  compile error.
- **Gitignored generated files:** all generated headers (`cart_resources.h`,
  `cart_state.h`, `cart_locale.h`, etc.) are gitignored; the packer
  regenerates them on every build.
- **Lua:** constants are integer values in per-namespace generated Lua modules,
  required by their short module name: `local R = require("R")`,
  `local RNG = require("RNG")`, `local L = require("L")`, etc.

### What is NOT covered by this pattern

- **Button identifiers** in label queries: abstract button names (`FC_BUTTON_A`
  etc.) are compile-time constants defined in `fc_cart.h`, not generated by
  the packer.
- **Manifest-internal strings:** the manifest files themselves use string
  names for clarity and yaml-language-server schema validation (ADR-0073) —
  the packer is what converts these to integers.

## Consequences

- One mental model applies to every entity type: declare in manifest, access
  via generated constant, runtime receives integer. Authors learn it once.
- The generated files are the single source of truth for what the packer
  resolved; reviewing them answers "what ID does `R_HERO_SPRITES` have?"
- Adding a new entity type (new subsystem with named entities) follows the
  same recipe with zero API design ambiguity.
- ADRs for individual subsystems (0009, 0040, 0041, 0057, 0061, 0062, 0064)
  each instantiate this pattern for their domain; this ADR is the canonical
  reference they defer to for rationale.
