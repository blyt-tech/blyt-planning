# ADR-0054: Audio — voice groups vs voice tags

## Status
Accepted

## Context

Carts need to control volume and stop behavior across categories of sounds
(UI sounds, ambient sounds, gameplay sounds). Two distinct needs emerge:
persistent mixer categories with volume control, and scene-local batch
stopping of a set of sounds when exiting a context (e.g., stopping all
footsteps when the player enters a vehicle). Using one mechanism for both
conflates concerns with different lifetimes.

## Decision

**Two orthogonal categorization mechanisms: groups and tags.**

### Voice groups — manifest-declared, persistent mixer categories

**Three standard groups are provided by the runtime and always available
without declaration:** `BLYT_VG_MUSIC`, `BLYT_VG_SFX`, `BLYT_VG_VOICE`. These
correspond to the three categories most carts need, and the runtime
automatically provides matching volume preference handles (`BLYT_PREF_MUSIC_VOLUME`,
`BLYT_PREF_SFX_VOLUME`, `BLYT_PREF_VOICE_VOLUME`) for use in options menus.

Carts that need additional groups declare them in `cart.config.yaml`:

```yaml
# cart.config.yaml
voice_groups: [ambient, ui]  # extends the three runtime defaults
```

The packer generates constants for declared groups (`VG_AMBIENT`, `VG_UI`).
All groups — runtime-provided and cart-declared — are assigned at
`blyt_voice_play()` time and persist for the voice's lifetime. Groups support:

- Per-group volume control (scales with the three-layer volume model, ADR-0055).
- Per-group mute/unmute.
- `blyt_voice_stop_group(BLYT_VG_SFX)` — stop all voices in a group.

Groups are intended for persistent, meaningful mixer categories that span
many frames (music layer, sfx layer, dialogue layer, etc.).

### Voice tags — per-voice runtime labels for scene-local batch stopping

Tags are arbitrary `uint32_t` values assigned when a voice starts:

```c
blyt_voice_play(R_FOOTSTEP, VG_GAMEPLAY, MY_TAG_FOOTSTEPS, &voice);
```

`blyt_voice_stop_tag(MY_TAG_FOOTSTEPS)` stops all currently active voices
with that tag. Tags are not declared in any manifest; they are defined as
constants in cart code. Tags have no volume control — they are purely a
batch-stop mechanism for scene-local sound sets.

## Consequences

- Groups handle the persistent mixer hierarchy (volume sliders, muting in
  options menus, accessibility settings).
- Tags handle ephemeral batch operations ("stop all footstep sounds when
  the player teleports") without requiring a separate group for every
  transient category.
- The two mechanisms compose: a voice has both a group and an optional tag.
- Manifest-declared groups are type-safe via packer-generated constants;
  tags are cart-defined integers (no manifest declaration needed, reducing
  friction for ephemeral use).
