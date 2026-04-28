# ADR-0004: Audio — format tiers by cart class

## Status
Accepted

## Context

The console needs an audio system that serves a wide range of carts — from
tiny jam games to full voice-acted adventures — while keeping CPU cost
manageable on the weakest supported hardware (Milk-V Duo, Pi Zero 2 W) and
in the browser (WASM on weak mobile hardware).

Streamed compressed audio (Opus, MP3, OGG Vorbis) costs 5–15% of one core
continuously while playing. This is acceptable for ambitious carts but not
negligible. Tracker music is effectively zero CPU once loaded; one-shot SFX
are tiny. The key insight: different formats have very different cost profiles
and belong at different cart-class tiers.

## Decision

**Output:** 44.1/48 kHz PCM, software mixer, 16–32 simultaneous voices.

**Blessed formats (all cart classes):**
- **XM/IT via libxmp** for music. Tiny files, negligible CPU, mature modern
  tooling (OpenMPT, Renoise, MilkyTracker). Strongly preferred for most carts.
- **QOA or ADPCM** for SFX. Near-zero decode cost, simple decoders.
- **Raw WAV (PCM)** as universal fallback.

**Streamed compressed audio (Large and Flagship classes only):**
- **Opus** for streamed music, ambient soundtracks, long-form voice acting.
  Best-in-class compression quality (32 kbps Opus ≈ 128 kbps MP3), handles
  both speech and music well, open standard, no patent concerns.

The packer accepts MP3/OGG/AAC inputs for streamed audio and transcodes to
Opus at pack time. Mini and Standard carts with Opus resources are rejected
by the packer at build time.

AAC was considered due to hardware decode offload in mobile browsers, but
rejected: it requires two decode implementations (software + browser API),
browser audio element routing has synchronization concerns, and software Opus
decode is manageable (5–15% of one core) on all platforms.

**Streaming playback** (for long clips): runtime reads from the cart file as
playback progresses using a small buffer. Speech/music length is bounded by
cart size, not runtime RAM.

## Consequences

- Tracker music encourages the primary use case for Mini/Standard carts;
  Opus is available where tracker genuinely can't deliver (long voice-acted
  RPGs, specific composer intent).
- Decode runs in the runtime's native code, not in cart code — decode cost
  doesn't compound with RISC-V interpreter overhead on emulated platforms.
- Per-format storage budgets (approximate):
  - XM/IT tracker module: 50–500 KB, negligible CPU
  - ADPCM 4-bit at 8 kHz: ~235 KB/minute (good for speech)
  - Opus streamed: ~240 KB/minute at 32 kbps
- A Day of the Tentacle-scale talkie (~3.5 hours of dialog) fits in the
  Large cart class (64 MB) using ADPCM at 8 kHz.
- Browser v2 optimization path: Opus decode routed through Web Audio API
  for hardware offload — non-breaking, cart format unchanged.
