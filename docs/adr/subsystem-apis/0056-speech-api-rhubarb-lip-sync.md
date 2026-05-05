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
blyt_result_t  blyt_speech_play(blyt_resource_h res, blyt_voice_h *out_voice);
blyt_result_t  blyt_speech_stop(blyt_voice_h voice);
bool         blyt_speech_is_playing(blyt_voice_h voice);
blyt_phoneme_t blyt_speech_mouth_shape(blyt_voice_h voice);
// Returns: BLYT_PHONEME_A .. BLYT_PHONEME_X (Preston Blair set)
bool         blyt_subtitles_active(void);
```

`blyt_speech_mouth_shape()` returns the phoneme for the current playback
position, or `BLYT_PHONEME_X` (rest) if the voice is not playing. Cart code
maps phonemes to sprite frames.

`blyt_subtitles_active()` returns true if subtitles should currently be shown.
The result is the logical OR of two independent settings (see below); cart
code should call this rather than reading `BLYT_PREF_SUBTITLES` directly.

### Subtitle activation

Subtitles are shown when **either** of the following is true:

- **Per-cart preference** `BLYT_PREF_SUBTITLES` (ADR-0013) — the player has
  enabled subtitles in this cart's options menu.
- **Global runtime preference** `BLYT_RUNTIME_PREF_SUBTITLES_ALWAYS` (ADR-0013)
  — the player has set "always show subtitles" at the device level. This
  overrides any cart's per-cart setting; if the global is on, subtitles are
  always shown regardless of what the cart's options say.

**System hotkey.** The runtime provides a configurable system-level hotkey
(frontend-specific; not captured by cart code) that toggles subtitles from
anywhere without entering a menu. The toggle logic is:

- **If subtitles are currently active** (either `BLYT_RUNTIME_PREF_SUBTITLES_ALWAYS`
  or `BLYT_PREF_SUBTITLES` is true): turn both off — clear the global pref and,
  if the cart has declared `BLYT_PREF_SUBTITLES`, clear that too. This ensures
  the hotkey is a decisive "off" regardless of which setting was responsible.
- **If subtitles are currently inactive** (both are false/absent): set only
  `BLYT_RUNTIME_PREF_SUBTITLES_ALWAYS` to true. The cart pref is left untouched.

The runtime displays a brief "Subtitles ON" / "Subtitles OFF" overlay on
toggle. This allows players who always need subtitles to enable them
immediately without navigating each cart's options, and to turn them off
just as quickly regardless of what any individual cart has configured.

### Automatic subtitle rendering

Each speech resource is associated with a locale key at pack time. When a
speech line plays and subtitles are active, the runtime automatically
renders the locale-resolved subtitle text as an overlay — no cart code
required. Carts that want custom subtitle presentation (different position,
font, styling) can suppress the automatic overlay and render manually using
`blyt_subtitles_active()` as before.

This means subtitle support is an accessibility guarantee at the platform
level: any cart that uses the speech API gets subtitles for free. The cart
author only needs to provide the locale key text (which they must do anyway
for localisation); subtitle display follows automatically from the player's
preference.

Subtitles also work in languages for which no dubbed audio exists: a cart
dubbed only in English will still show subtitle text in French, Japanese, or
any other language present in the locale data, without additional audio
assets.

### Speech pacing preference (cart-cooperative)

An accessibility preference — "pause before next line" — lets players read
each subtitle at their own pace before continuing. When enabled, the
expected flow is: speech line plays and completes → subtitle remains
visible → player presses a confirm button → cart proceeds to the next line.

The runtime cannot implement this automatically because the cart controls
when it calls `blyt32.speech.play()` for the next line. The runtime
provides:

- A readable preference (`BLYT_PREF_SPEECH_WAIT_FOR_ACK` or equivalent).
- A query `blyt32.subtitles.waiting()` — true when subtitles are visible
  and the pacing preference is on, indicating the cart should hold before
  advancing.

Cart code checks `blyt32.subtitles.waiting()` in its dialogue sequencing
logic and waits for a button press before playing the next line. Carts that
do not check this preference are unaffected; the feature only works in
carts that opt in to honouring it.

### Lua

```lua
-- Subtitles render automatically when active — no cart code needed.
local voice = blyt32.speech.play(R_LINE_HELLO)

-- In update (lip sync only — subtitles handled by runtime):
local shape = voice:mouth_shape()  -- "A".."X"
mouth_sprite:blit(MOUTH_FRAMES[shape], mx, my)

-- Custom subtitle rendering (suppresses automatic overlay):
if blyt32.subtitles.active() then
  blyt32.text.draw(L.LINE_HELLO, 10, 220, { style = "my_subtitle_style" })
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
