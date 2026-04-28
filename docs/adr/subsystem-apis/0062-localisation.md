# ADR-0062: Localisation — packer-driven, compile-time integer key constants

## Status
Accepted

## Context

Localisation requires mapping string keys to translated strings at runtime.
String key lookups have the same tradeoff as resource name lookups (ergonomic
but slow). The same packer-generated constant pattern used for resources and
state fields applies here.

## Decision

**Localisation keys are declared in the manifest (or a separate locale source
file); the packer generates compile-time integer constants; the runtime
performs ID-based lookup.**

```yaml
# cart.config.yaml
locale_keys:
  - menu.start
  - menu.options
  - menu.quit
  - hud.score
  - dialog.hero.greeting
```

The packer generates:

```c
// cart_locale.h (generated, gitignored)
#define L_MENU_START          ((fc_locale_key_h)1)
#define L_MENU_OPTIONS        ((fc_locale_key_h)2)
#define L_HUD_SCORE           ((fc_locale_key_h)5)
#define L_DIALOG_HERO_GREETING ((fc_locale_key_h)6)
```

Locale string data is stored in cart resources, one resource per locale
(`locale_en.dat`, `locale_ja.dat`, etc.), loaded on demand.

```c
const char *fc_locale_get(fc_locale_key_h key);
// Returns: pointer to string for current locale; falls back to base locale
// if key not found; pointer valid until next locale change.
```

**Fallback chain:** if a key is missing in the active locale, the runtime
falls back to the declared base locale (usually English).

**Dev tools:**
- F8 in dev mode cycles through installed locales for live preview.
- F9 highlights untranslated strings (those falling through to base locale).
- `console loc-report` packer command generates a missing-translations report.

**Overflow detection:** `fc_locale_get()` in dev builds warns if the returned
string exceeds the expected display width (useful for detecting layout-breaking
translations before they ship).

## Consequences

- Locale key access is a compile-time integer lookup — no string hashing.
- Missing translation keys are caught at pack time (packer validates all
  referenced key constants exist in the locale data).
- The dev F8/F9 tools make localisation testing part of the normal dev loop.
- Locale fallback is handled transparently; untranslated strings degrade
  gracefully to the base locale rather than showing raw key names.
