# ADR-0063: Locale stored as a preference, not game state

## Status
Accepted

## Context

Locale selection (language choice) could live in the cart's persistent game
state (a language field in the player's save data) or in `blyt_prefs` (the
runtime's preference store that is separate from the save game). The two
approaches have different consequences for save-file portability and for when
locale changes take effect.

## Decision

**Locale is stored in `blyt_prefs`, not in cart game state.**

```c
blyt_result_t blyt_prefs_set_locale(const char *locale_tag);  // e.g., "en", "ja"
const char *blyt_prefs_get_locale(void);
```

`blyt_prefs` is a per-player, per-console store that persists independently of
save games (ADR-0013). Locale therefore survives a "new game / erase save"
operation: the player's language preference is not tied to their save file.

**Immediate effect:** a locale change takes effect on the next call to
`blyt_locale_get()`. There is no deferred-until-restart behavior. Carts that
display text in `draw()` see the new locale on the next frame.

**Cart interaction:** carts can read the current locale (to select locale-
appropriate assets or fonts) but are not required to manage it. The standard
options-menu locale picker calls `blyt_prefs_set_locale()` and the runtime
handles the rest.

**RTL (right-to-left) layout:** deferred to v2. In v1, all text is rendered
left-to-right regardless of locale. This is a known limitation for Arabic
and Hebrew support.

## Consequences

- Language preference survives save-file deletion; players don't have to
  re-select their language after starting a new game.
- Locale is set once per player setup and changes rarely; storing it in
  prefs is a better fit than bloating every save-game schema with a locale
  field.
- Immediate-effect locale changes allow live preview in options menus without
  a "restart required" message.
- The RTL deferral is a known gap; v2 will require text rendering changes
  and is explicitly acknowledged rather than silently ignored.
