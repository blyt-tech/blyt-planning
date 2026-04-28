# ADR-0013: Four distinct save mechanisms

## Status
Accepted

## Context

"Save" covers several different concepts that get conflated: saving progress
mid-game, taking a full runtime snapshot, rewinding recent gameplay, and
persisting player configuration. Each has different ownership, different
semantics, and different UI implications. Conflating them into a single
mechanism creates awkward edge cases (do config settings rewind with gameplay?
does save-state-loading lose save-game progress?).

## Decision

Four separate mechanisms, each for its natural purpose:

**1. Cart-managed in-game save** (cart decides when and what to save).
Player progress — story position, inventory, level completion. API:
`console.save.write(slot, data, metadata)` / `console.save.read(slot)`.
Runtime handles serialization, atomicity, quota (10 MB default), and slot
metadata (label, timestamp, size).

Per-cart isolation is the frontend's responsibility, not the cart's. The
frontend supplies a save-storage location to the runtime at cart load; the
cart neither declares nor sees its own identity. There is no `cart_id`
field in the manifest. This avoids a global cart-id namespace — no central
registry, no collisions between independently developed carts — and lets
each distribution channel pick whatever identity scheme already exists in
its world: Steam uses the app ID under `userdata/<userid>/<appid>/`, itch
uses the game ID, a standalone frontend can use a path-derived key or a
user-named slot. So long as a given frontend keeps its identity stable
across patched cart binaries, saves persist across cart updates.

**2. Save state** (platform-managed, via libretro `retro_serialize`).
Snapshot of entire runtime state. Restoring resumes from that exact moment.
Cart has no knowledge this is happening. Managed by the frontend (RetroArch
or custom). Tagged with cart binary hash; loading across cart versions warns
the player.

**3. Rewind** (platform-managed, via libretro rewind infrastructure).
Continuous save states in a ring buffer; player rewinds recent gameplay.
Cart has no knowledge rewind is happening. Free given save state
infrastructure.

**4. Cart preferences** (cart-managed, separate from save).
Small persistent store for player configuration — per-group audio balance,
control layout, accessibility settings. Preference keys are declared in
`cart.config.yaml`; the packer generates `PREF_*` compile-time constants
following the universal name→ID pattern (ADR-0059):

```yaml
# cart.config.yaml
prefs:
  sfx_volume:   { type: f32,  default: 1.0 }
  music_volume: { type: f32,  default: 1.0 }
  subtitles:    { type: bool, default: false }
```

```c
// Generated: cart_prefs.h
#define PREF_SFX_VOLUME   ((fc_pref_h)1)
#define PREF_MUSIC_VOLUME ((fc_pref_h)2)
#define PREF_SUBTITLES    ((fc_pref_h)3)
```

```c
fc_prefs_set_f32(PREF_SFX_VOLUME, 0.8f);
float vol = fc_prefs_get_f32(PREF_SFX_VOLUME);
```

Available immediately at cart start, before any save is loaded. Quota: 64 KB
default. The declared `default` value is used when no stored preference exists
(first launch, or after a preference reset).

**Master volume is not a cart preference.** It is owned by the frontend or
OS (layer 1 of the three-layer volume model, ADR-0055); the runtime reads it
via a frontend callback and never exposes it as a settable pref.

**Runtime-provided preference handles.** The runtime pre-populates a set of
standard preference handles that carts can use without declaring them. Carts
may override the defaults by declaring a pref with the same name:

| Handle | Type | Default | Notes |
|--------|------|---------|-------|
| `FC_PREF_MUSIC_VOLUME` | f32 | 1.0 | Volume for the `FC_VG_MUSIC` group (ADR-0054) |
| `FC_PREF_SFX_VOLUME` | f32 | 1.0 | Volume for the `FC_VG_SFX` group |
| `FC_PREF_VOICE_VOLUME` | f32 | 1.0 | Volume for the `FC_VG_VOICE` group |
| `FC_PREF_SUBTITLES` | bool | false | Whether this cart shows subtitles by default |

**Global runtime preferences** (distinct from per-cart preferences).
Some player settings apply across all carts and are owned by the runtime,
not by any individual cart. Carts can read but not write these:

```c
bool fc_runtime_pref_get_bool(fc_runtime_pref_t key);
```

| Handle | Type | Default | Notes |
|--------|------|---------|-------|
| `FC_RUNTIME_PREF_SUBTITLES_ALWAYS` | bool | false | If true, overrides per-cart subtitle setting |

Global preferences are persisted by the runtime in a device-wide store
separate from per-cart preference files. The locale preference (ADR-0063)
is also stored here.

## Consequences

- Save states and rewind are **orthogonal to cart saves**. A player can
  save-state before a boss, die, rewind — their cart save slot is untouched.
- Preferences persist across save slot boundaries (volume is not per-save).
- Cart save directories use the filesystem (not libretro's flat SAVE_RAM)
  making cloud sync integration trivial for service wrappers (ADR-0016).
- The libretro SAVE_RAM model (flat persisted memory region) is not used.
  Cart saves are not automatically cloud-synced via libretro infrastructure
  in v1; this is an acceptable trade-off for a better abstraction.
