# ADR-0075: Gameplay capture — GIF and screenshot

## Status
Accepted

## Context

Short clips and screenshots are the primary way fantasy console games are
discovered and shared in community spaces. PICO-8 established this pattern:
its built-in GIF recording produces exactly the kind of short animated clips
that circulate on Discord, Twitter/X, Reddit, and itch.io. This console
targets the same community dynamics.

**GIF is a structurally natural format for this console.** The display is
already 256-colour indexed (ADR-0003). A GIF frame has a 256-colour indexed
palette; GIF capture of this console's output is therefore lossless —
no dithering, no colour reduction, no quality loss. This property is unique
to paletted systems; it does not hold for truecolour consoles.

Two capture use cases have distinct characteristics:

- **User-triggered capture**: player or author presses a chord to save
  what just happened. Duration is fixed and brief. No advance planning
  required — the capture window is always rolling.
- **Cart-triggered capture**: the cart author deliberately fires a capture
  at a moment of interest ("here's a GIF of your ridiculous death"). Duration
  is cart-specified. This is a player-facing feature in release builds.

### Considered: replay-from-state as the capture source

The dev-mode history buffer (ADR-0074) stores save-state anchors and a
full input log, and could in principle generate GIF frames by re-simulating
the clip window. This approach uses less memory than storing raw frames
(a handful of save states plus a tiny input log, vs. ~17 MB of pixel data).

It is the right approach for the debugging use case, where full state
fidelity and frame-by-frame stepping are needed and a brief seek delay
is acceptable.

It is the wrong approach for capture. Generating a GIF requires running
the cart simulation forward through the full clip duration. A Lua cart on
a fast machine might simulate 5–10× real-time; a native cart running near
the hardware performance floor on a K230D-class device will simulate
at or below real-time. Hitting "share my death clip" and waiting 15 seconds
for the GIF to render is unacceptable UX. The framebuffer ring buffer
eliminates this: GIF generation encodes already-rendered frames and is fast
on any device.

## Decision

### Framebuffer ring buffer

The runtime maintains a **framebuffer ring buffer** at a fixed capture
framerate (default 15 fps). Each entry stores:
- The 320×240 palette-index framebuffer (76,800 bytes).
- The 256-colour RGBA palette for that frame (1,024 bytes).

At 15 fps for 15 seconds: 225 frames × ~78 KB ≈ 17 MB. This buffer is
always-on in both dev and release builds; its memory cost is constant.

The capture framerate (15 fps) is sufficient for social sharing and halves
the storage versus capturing at the full 60 fps. The buffer duration (15
seconds) is a runtime constant; frontends may expose it as a user setting.

The ring buffer is written entirely from the render path — no simulation
cost. It records exactly what appeared on screen, including any rendering
edge cases that replay-from-state might not reproduce identically.

---

### Screenshot

**Format:** PNG.

**Trigger:** A configurable button chord, defaulting to **L+R+B**. The
runtime intercepts the chord before it reaches the cart.

**Destination:** frontend-determined. Desktop frontends save to disk
(configurable path, defaulting to the platform pictures directory).
Browser frontends trigger a file download. Hardware frontends save to
the SD card.

**Cart API (release builds):**
```lua
blyt32.capture.screenshot()
```

---

### Short video capture (GIF)

**Format:** Animated GIF.

**Trigger:** A configurable button chord, defaulting to **L+R+A**. The
runtime intercepts the chord before it reaches the cart.

On trigger, the runtime encodes the ring buffer contents — the last
configured duration of frames — as an animated GIF and writes it to the
frontend-determined destination. No simulation is involved; encoding
proceeds directly from stored pixel data and is fast on any device.

**Destination:** same frontend-determined behaviour as screenshots.

**Cart API (release builds):**
```lua
-- Capture the last N seconds (clamped to the available buffer window)
blyt32.capture.gif(seconds)
blyt32.capture.gif(seconds, "label")  -- optional label for the filename
```

The GIF is written asynchronously; the cart does not block. This is the
mechanism for socially-shareable moments authored into the cart: a death
animation, a high-score beat, a lucky escape. The cart fires the capture
at the dramatically correct moment; the ring buffer ensures the preceding
seconds are already available without the player having needed to press
anything.

**Chord conflict note.** L+R+A and L+R+B involve face buttons that some
carts may use in gameplay combinations. The runtime intercepts these chords
unconditionally while they remain the configured defaults. In dev mode, the
runtime warns if a cart's `inputs_used` declaration includes all three
buttons of a default capture chord, indicating a likely conflict. Frontends
distributing carts with conflicts can reconfigure the chord (e.g., to
L+R+Start or L+R+Select) or disable chord capture entirely.

---

### What is not provided

- **Long-form video** (MP4, WebM): deferred. The 15-second GIF window
  covers social sharing; longer recording is either OS-level screen capture
  or a v2 addition.
- **Audio in clips**: GIF is video-only. If video-with-audio is added in
  v2, the framebuffer ring buffer extends naturally (a parallel audio ring
  buffer feeds the audio track).
- **Live streaming**: out of scope.
- **Replay-file sharing**: the speedrun replay format (ADR-0015) handles
  this separately; replay files are tiny and complementary to GIF clips.

## Consequences

- The framebuffer ring buffer adds ~17 MB of always-on memory overhead.
  This is a fixed cost independent of cart size, platform speed, or
  simulation complexity.
- GIF generation is uniformly fast on all devices — including hardware at
  the performance floor — because it encodes stored frames rather than
  re-simulating gameplay.
- GIF capture is lossless by construction: the console's paletted output
  maps exactly to GIF's indexed-colour model.
- Cart authors can trigger captures at precisely the right dramatic moment
  rather than relying on players to hit a chord in time.
- The framebuffer ring buffer and the dev-mode state-history buffer
  (ADR-0074) serve complementary purposes and use different storage
  strategies: pixel data for fast capture; save states for deep debugging.
  Both can be active simultaneously in dev mode.
- Chord defaults can conflict with some cart input designs. Conflicts are
  detectable at dev time and configurable at the frontend level.
