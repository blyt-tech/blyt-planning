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
(no handle retained, never polled), so they do not need to participate
in `is_playing` at all. Voices the cart polls — music, speech,
long-running loops, ambient layers — are the ones whose end matters,
and they are bounded by the voice budget rather than the SFX trigger
rate.

## Decision

**Voice-end and music-end events are captured per frame and threaded
through the same input record as player inputs. `is_playing` reads
from a logical mixer view maintained from those events, not from the
live audible mixer.**

### The logical mixer view

The runtime maintains a per-frame "logical mixer view" that mirrors
which voices the cart still considers playing:

- A voice enters the view when `blyt_voice_play` succeeds.
- A voice leaves the view when the runtime processes a `voice_end`
  event for that handle in the next frame's input record.
- `blyt_voice_is_playing(handle)` reads the logical view, not the
  audible mixer. Same for `blyt_music_is_playing()`.

The audible mixer is unchanged — it still streams samples to hardware
and may steal voices under pressure. The point is that the cart no
longer reads the audible mixer directly; it reads a deterministic
projection of it.

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

`is_playing` is meaningful for voices the cart tracks: music, speech,
long-running loops, tagged ambient layers, anything the cart kept a
handle for. Fire-and-forget SFX never poll, so their lifecycle never
exercises the recording path beyond a single end event when the voice
finishes — the per-frame event volume is bounded by the **voice
budget**, not by how aggressively the cart triggers SFX. Even the
worst case (voice budget × frame rate × replay length, fully saturated)
is tens of KB across a multi-minute capture.

A voice that starts and ends within the same frame contributes one
end event. The recording does not encode start events — those are
already recorded as cart API calls in the deterministic call sequence
(or, equivalently, they are inferable from `voice_play` succeeding).

### Cross-platform record-time behaviour

Two peers running the same cart with identical inputs may still see
their audible mixers steal voices differently if the mixer
implementations diverge. The logical view recorded at one peer is
canonical for replay of that peer's session; netplay still relies on
mixer behaviour being deterministic from the cart-call sequence
(ADR-0006's original argument), or, if peers must share `is_playing`
answers, on the host peer's voice-end events being broadcast as part
of the netplay frame envelope. Cross-peer determinism is a separate
problem from replay determinism; this ADR fixes the latter
unconditionally.

## Consequences

- ADR-0006 is superseded. The "bounded determinism exception" no
  longer applies — audio joins the rest of the system under ADR-0007's
  full-determinism guarantee.
- Debug captures (ADR-0074) replay `is_playing` answers bit-identically
  to the original session, so audio-driven branching reproduces.
- Speedrun replays (ADR-0015) likewise become faithful for carts whose
  logic depends on `is_playing`.
- The cart-facing API is unchanged: `blyt_voice_is_playing` and
  `blyt_music_is_playing` still return live-feeling answers. The change
  is in what backs them.
- Recording cost is bounded by the voice budget per frame, not by SFX
  call rate. Carts with heavy fire-and-forget SFX use pay the same
  per-frame ceiling as carts with sparse SFX.
- The audible mixer remains free to steal voices, drift on
  resume-from-save, and behave differently across hosts. The cart
  cannot observe those differences, so they cannot affect simulation.
- ADR-0007's audio bullet, ADR-0076's parenthetical about
  `is_playing`, and ADR-0074's input-log description are updated to
  reference this mechanism.
