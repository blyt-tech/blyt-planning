# ADR-0056: Speech API and Rhubarb lip sync pipeline

## Status
Accepted

## Context

Visual novels and adventure games frequently synchronize character mouth
animations to voice-over audio. Implementing this from scratch requires
either manual frame-by-frame annotation (time-consuming) or a runtime
speech recognition pipeline (too heavy for the target hardware). Rhubarb
Lip Sync is an open-source tool that analyses audio and produces phoneme
timings offline at pack time.

## Decision

**The packer drives Rhubarb Lip Sync at build time; the runtime provides a
simple playback and query API.**

### Packer behavior

If a voice-over audio file has a companion `.json` or `.txt` script file,
the packer:
1. Runs Rhubarb on the audio file (recognizer: `phonetic` mode, no external
   model required).
2. Rhubarb emits a frame-by-frame phoneme sequence using the Preston Blair
   set (A, B, C, D, E, F, G, H, X — rest/silence).
3. The packer binarizes this as a compact frame table and embeds it in the
   cart alongside the audio resource.

Rhubarb processing is opt-in: only audio files with a companion script file
trigger it. Audio without a script file is packed normally with no lip sync
data.

### Runtime API

```c
fc_result_t  fc_speech_play(fc_resource_h res, fc_voice_h *out_voice);
fc_result_t  fc_speech_stop(fc_voice_h voice);
bool         fc_speech_is_playing(fc_voice_h voice);
fc_phoneme_t fc_speech_mouth_shape(fc_voice_h voice);
// Returns: FC_PHONEME_A .. FC_PHONEME_X (Preston Blair set)
bool         fc_subtitles_active(void);
```

`fc_speech_mouth_shape()` returns the phoneme for the current playback
position, or `FC_PHONEME_X` (rest) if the voice is not playing. Cart code
maps phonemes to sprite frames.

`fc_subtitles_active()` returns true if subtitles should currently be shown.
The result is the logical OR of two independent settings (see below); cart
code should call this rather than reading `FC_PREF_SUBTITLES` directly.

### Subtitle activation

Subtitles are shown when **either** of the following is true:

- **Per-cart preference** `FC_PREF_SUBTITLES` (ADR-0013) — the player has
  enabled subtitles in this cart's options menu.
- **Global runtime preference** `FC_RUNTIME_PREF_SUBTITLES_ALWAYS` (ADR-0013)
  — the player has set "always show subtitles" at the device level. This
  overrides any cart's per-cart setting; if the global is on, subtitles are
  always shown regardless of what the cart's options say.

**System hotkey.** The runtime provides a configurable system-level hotkey
(frontend-specific; not captured by cart code) that toggles subtitles from
anywhere without entering a menu. The toggle logic is:

- **If subtitles are currently active** (either `FC_RUNTIME_PREF_SUBTITLES_ALWAYS`
  or `FC_PREF_SUBTITLES` is true): turn both off — clear the global pref and,
  if the cart has declared `FC_PREF_SUBTITLES`, clear that too. This ensures
  the hotkey is a decisive "off" regardless of which setting was responsible.
- **If subtitles are currently inactive** (both are false/absent): set only
  `FC_RUNTIME_PREF_SUBTITLES_ALWAYS` to true. The cart pref is left untouched.

The runtime displays a brief "Subtitles ON" / "Subtitles OFF" overlay on
toggle. This allows players who always need subtitles to enable them
immediately without navigating each cart's options, and to turn them off
just as quickly regardless of what any individual cart has configured.

### Lua

```lua
local voice = console.speech.play(R_LINE_HELLO)

-- In update:
local shape = voice:mouth_shape()  -- "A".."X"
mouth_sprite:blit(MOUTH_FRAMES[shape], mx, my)

if console.subtitles.active() then
  console.text.draw(subtitle_text, 10, 220)
end
```

## Consequences

- Lip sync analysis is free at runtime — all heavy processing happens at
  pack time.
- Authors need no external pipeline beyond having Rhubarb available (the
  packer invokes it automatically when a script file is present).
- The Preston Blair phoneme set (9 shapes) is the industry-standard minimum;
  mapping to sprite frames is trivial.
- Carts that do not use speech incur zero size or runtime cost.
- The packer can warn when a referenced audio file has no companion script
  (helping authors catch forgotten annotations).
