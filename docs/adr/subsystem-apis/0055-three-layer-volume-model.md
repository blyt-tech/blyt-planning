# ADR-0055: Three-layer volume model

## Status
Accepted

## Context

Audio volume is controlled at multiple levels of the system: the platform
master volume (controlled by the frontend or OS), player preferences (stored
across sessions), and in-game transient state (cinematic ducking, fade-out
effects). These three levels have different owners and different lifetimes and
must compose correctly.

## Decision

**Three independent volume layers that multiply to produce the final output
level:**

1. **Frontend master volume** — owned by the frontend; set by the player at
   the OS or frontend level (e.g., system volume knob, RetroArch master
   volume slider). The runtime reads this via a frontend callback; it does
   not control it.

2. **Cart preferences volume** — per-group volume preferences stored in
   `fc_prefs` (master, music, SFX, voice — up to the groups declared in
   `cart.config`). Persisted across sessions. The player sets these in the
   in-game options menu. Range 0.0–1.0.

3. **Transient in-game volume** — per-group volume set by cart code each
   frame. Default 1.0 (no attenuation). Used for cinematic ducking, gradual
   fade-out, muffled-underwater effects, etc. Not saved to preferences.

Final sample amplitude = frontend_master × prefs_group_volume × transient_volume × voice_volume.

The runtime exposes the transient layer to carts:

```c
fc_result_t fc_audio_set_group_volume(fc_group_h group, float volume);
float       fc_audio_get_group_volume(fc_group_h group);
```

Preferences volume is managed via `fc_prefs` (ADR-0013).

## Consequences

- Players control global and per-category volume through familiar UI
  (options menu persists, system volume is always respected).
- Cart code controls transient ducking without overwriting player preferences.
- The three layers compose multiplicatively: any layer can silence output
  without requiring coordination with other layers.
- The frontend master volume layer means the runtime never assumes it controls
  final hardware output — it is always just one contributor.
