# ADR-0106: Audio voice-end events recorded as frame inputs

## Status
Accepted (supersedes ADR-0006)

## Context

ADR-0006 carved out a determinism exception: `blyt_voice_is_playing()` and
`blyt_music_is_playing()` queried live mixer state, accepting that two
runs with identical inputs could diverge under voice-stealing pressure or
across mixer implementations. The reasoning was that frame-count
estimates were brittle, and divergence in `is_playing` was bounded to
"presentation-layer" branching.

That trade-off is uncomfortable in practice. ADR-0074's debug capture
replays a recorded input log against an anchor save state to reproduce
a misbehaving sequence. If `is_playing` answers can drift between
record and replay, the cart can take different branches during replay
than it did when the bug was captured — which is exactly the case
where the author needs the replay to be faithful.

ADR-0007 already establishes the right pattern for non-deterministic
inputs: button events are recorded as `(frame_n, player_index,
button_state)` tuples and fed to the cart deterministically each
frame. Voice and music completion are structurally similar — events
that originate outside the cart's control and that the cart observes
through a poll API.

The high-volume case is sound effects. Most SFX are fire-and-forget
and the cart never branches on their end; the few that the cart does
care about (looping ambient loops, charge-up cues) are better served
by a separate group anyway. ADR-0054 captures this by giving each
group a **tracked** flag: tracked groups participate in `is_playing`
and end-event recording; untracked groups do not. SFX is fixed
untracked; cart-declared groups are untracked unless opted in.

Cutting an in-flight SFX to match a host-authoritative end event
would sound terrible (clipped footsteps, truncated impacts). Cutting
an in-flight dialogue line is contextually fine (the cart will stop
the lip-sync animation on the same frame anyway). The tracked split
falls along this audible-cost line.

## Decision

**Voice-end and music-end events for voices in tracked groups
(ADR-0054) are captured per frame and threaded through the same
input record as player inputs. `is_playing` reads from a logical
mixer view maintained from those events, not from the live audible
mixer. SFX and other untracked groups do not participate: their
playback state is invisible to the cart, never recorded, never
broadcast.**

### The logical mixer view

The runtime maintains a per-frame "logical mixer view" that mirrors
which **tracked** voices the cart still considers playing:

- A voice enters the view when `blyt_voice_play` succeeds for a
  voice in a tracked group.
- A voice leaves the view when the runtime processes a `voice_end`
  event for that handle in the next frame's input record.
- `blyt_voice_is_playing(handle)` reads the logical view, not the
  audible mixer. Same for `blyt_music_is_playing()`.
- Voices in untracked groups never enter the view; their handles
  return `false` from `is_playing` (dev-mode warns).

The audible mixer is unchanged in shape — it still streams samples
to hardware. The point is that for tracked voices, the cart no
longer reads the audible mixer directly; it reads a deterministic
projection of it. Untracked voices are a pure cart→mixer one-way
flow with no return path.

### Recording

At the end of each audio frame, the mixer reports voice-end events
(natural completion or stealing) to the runtime. The runtime queues
those events and applies them at the start of the next logical update,
in handle order. The applied events become part of that frame's input
record, alongside player input tuples:

```
(frame_n, voice_end, voice_handle)
(frame_n, music_end)
```

Format and storage match ADR-0007's input event tuples and ADR-0015's
input log. ADR-0074's debug capture buffer includes them automatically.

### Save state

The logical mixer view is part of cart-observable state and is saved
with the rest of the world. A restore reinstates the set of handles
the cart considered playing at capture time. The audible mixer
re-seeds from the live save (whatever the platform supports for
mid-stream resumption); the cart's `is_playing` answers come from the
restored logical view, so they remain consistent regardless of how the
audible mixer behaves on resume.

### Scope and cost

The recording covers only voices in tracked groups: music, speech,
and any cart-declared groups opted into tracking via ADR-0054.
Tracked-voice budgets are typically very small in practice — one
music slot, a handful of dialogue voices, a few cart-declared
loops — so per-frame event volume is small in absolute terms (a
multi-minute replay buffer measures in single-digit KB rather than
tens of KB).

A voice that starts and ends within the same frame contributes one
end event. The recording does not encode start events — those are
already recorded as cart API calls in the deterministic call sequence
(or, equivalently, they are inferable from `voice_play` succeeding).
Untracked voices are absent from the record entirely; SFX call rate
does not affect replay-buffer size.

### Per-frame recorded inputs and netplay

Per-frame recorded inputs (this ADR's voice-end events, the
cosmetic-RNG post-draw state under ADR-0076, and any future
additions of the same shape) serve **replay determinism**
unconditionally: the recording captures whatever happened on the
recorder's machine, and replay re-applies it. Their **netplay**
implications depend on whether each peer can derive the same value
locally:

- **Locally derivable** (no wire traffic): if the value is
  deterministic from the cart-call sequence and prior tracked state
  on each peer, every peer computes the same value independently.
  Cosmetic RNG post-draw state lands here under deterministic
  `draw()`.
- **Authoritative broadcast** (a few wire bytes): if peer
  implementations cannot be relied on to agree, the host is
  authoritative and its events go out alongside the input envelope.
  Voice-end events land here, because the audible mixer is
  off-the-shelf and not bit-identical across hosts (see
  "Considered and rejected" below).

### Netplay: host-authoritative voice-end broadcast

Voice-end and music-end events for tracked voices are broadcast by
the host as part of the netplay frame envelope, alongside the
input record. Each receiving peer applies a broadcast event by:

1. Marking the voice ended in its logical mixer view (so
   `is_playing` returns false on this frame).
2. Issuing a stop on the corresponding **audible** voice if it is
   still playing locally on that peer.

Tracked voices are dialogue, music, and cart-opted custom groups
where end-state matters. Cutting an in-flight tracked voice on
broadcast keeps the cart's simulation, the player's animation, and
the player's audio in sync locally on every peer. A clipped final
syllable or a slightly-early music end is preferable to
mouth-closed-while-line-still-playing.

Wire cost is small: tracked-voice ends are sparse (a handful of
events per second during dialogue, zero most frames) and each
event is a few bytes. Carts sensitive to music transition timing
follow ADR-0053's guidance — use cart-driven `music_stop(FADE)` or
`music_play(new, CROSSFADE)` rather than relying on natural
completion. The crossfade window absorbs whatever small mixer
differences exist between peers, and the cart's transition runs
identically on every peer because it is a deterministic cart-call
sequence.

This ADR fixes replay determinism for audio unconditionally;
netplay parity for `is_playing` is achieved by the broadcast path
above.

## Considered and rejected: a deterministic audible mixer

An earlier draft specified that the audible mixer itself be
bit-identical across hosts — same sample rate, same voice budget,
deterministic voice-stealing policy, and integer or controlled-FP
DSP throughout. Under that policy, voice-end events would be
locally derivable on every peer and netplay would need no wire
traffic for them.

This was rejected. The runtime is expected to use an off-the-shelf
mixer (SDL_mixer or equivalent) for the audible path, which means
the mixer is *not* bit-identical across hosts: different host audio
APIs, different sample-rate-conversion implementations, format
decoders not written for cross-platform bit-identity, and voice
stealing under load that varies with host CPU.

Replacing that with a deterministic in-house mixer would require:

- Bit-identical decoders for every supported format (XM/IT/MOD,
  Opus, QOA, ADPCM, WAV — ADR-0004). Most existing implementations
  are not built for cross-platform bit-identity.
- Deterministic voice stealing and mixing math (integer or
  carefully-controlled FP) throughout.
- Integration with each platform's audio output API in a way that
  preserves the deterministic guarantee.
- Ongoing maintenance of all of the above.

The cost is large; the benefit is narrow. The rest of this ADR's
design — recorded events for replay, host-authoritative broadcast
for netplay — already absorbs mixer-level non-determinism cleanly.
Adding a deterministic-mixer requirement on top is double-defense
for a problem the design already solves. The remaining cost without
deterministic mixer is a few bytes per second of netplay wire
traffic and an audible tail clip on slower peers when a voice is
cut on broadcast — both small relative to the implementation cost
of a custom deterministic mixer.

## Consequences

- ADR-0006 is superseded. The "bounded determinism exception" no
  longer applies — tracked-voice audio joins the rest of the system
  under ADR-0007's full-determinism guarantee.
- Debug captures (ADR-0074) replay `is_playing` answers bit-identically
  to the original session, so audio-driven branching reproduces.
- Speedrun replays (ADR-0015) likewise become faithful for carts whose
  logic depends on `is_playing`.
- The cart-facing API is unchanged for tracked groups:
  `blyt_voice_is_playing` and `blyt_music_is_playing` still return
  live-feeling answers; the change is in what backs them. Untracked
  groups (SFX, cart-declared default) are explicitly opted out and
  return `false` from `is_playing` (ADR-0054, ADR-0068).
- Recording cost is bounded by the **tracked** voice budget per
  frame — typically a handful of voices — not by the full voice
  budget or SFX call rate. Replay buffers stay in single-digit KB
  per minute even pessimistically.
- The audible mixer is unconstrained — v1 uses an off-the-shelf
  mixer (SDL_mixer or equivalent). For tracked voices, the cart's
  view stays deterministic via recorded events and host-authoritative
  broadcast. For untracked voices, mixer drift between peers is
  unobservable to the cart and inaudible in practice.
- ADR-0007's audio bullet, ADR-0076's parenthetical about
  `is_playing`, and ADR-0074's input-log description are updated to
  reference this mechanism.
