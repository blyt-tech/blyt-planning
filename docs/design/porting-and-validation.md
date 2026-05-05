# Porting existing games — validation strategy and SDL compatibility

Porting existing open-source games is a crucial early validation step.
A working port exercises the full native cart path (RISC-V toolchain, ELF
loader, API surface, audio, input, save state) in a way that synthetic test
carts cannot. It surfaces API gaps, performance shortfalls, and toolchain
friction on a real, complex codebase.

This note covers the porting approach, subsystem mapping, and recommended
targets for early development validation.

---

## The Doom benchmark

The design document flags a Chocolate Doom port as the phase 7 showpiece.
It is also the right *first* port because it sidesteps the hardest porting
problem entirely.

Doom's native framebuffer is **320×200 paletted, 256 colours**. This
console's framebuffer is 320×240 paletted, 256 colours. The mapping is
nearly direct: letterbox 40 pixels vertically, hand the game's palette to
`blyt32.gfx.set_palette`, write the framebuffer. Chocolate Doom already
abstracts its video backend behind `i_video.c`; a console backend is a
drop-in replacement, not a general SDL compat layer.

Audio, input, and file I/O need attention (see below), but the graphics
path — normally the hardest part of a port — is essentially free. This
makes Doom the ideal first target: high validation value, low porting cost.

---

## The hard problem: truecolour vs. paletted

For games not originally palette-constrained, the 256-colour limit is the
fundamental mismatch. Two approaches:

**Quantize at pack time.** Run a colour quantizer (libimagequant or similar)
over all source textures when packing the cart. Build a shared palette;
bake per-texture colour-to-palette-index mappings into the cart resources.
Works well for pixel art games designed with limited colour budgets. Quality
degrades for photographic or heavily-gradient art.

**Select appropriate targets.** Only port games originally designed for
limited colour: DOS-era originals (which were paletted to begin with),
modern indie pixel art (Celeste, Shovel Knight, and their successors). These
are also the most culturally interesting ports for this console. Accept the
constraint as a filter rather than trying to solve it generically.

A general "convert any truecolour game" path is not a v1 goal.

---

## Event loop model

SDL games poll events in a `while (running)` loop. The console uses
`update()` / `draw()` callbacks.

**Option A — Restructure the game.** Split the main loop body into
`update()` and `draw()` functions. This is the right approach for any port
intended as a shippable cart: it respects the `draw()` read-only constraint
(ADR-0076), makes save state work correctly, and produces a clean codebase.
For a well-structured game this is rarely more than a few hours of work.

**Option B — Coroutine trick.** Run the SDL game loop in a coroutine that
yields back to the runtime each frame. Technically feasible. However: raw
coroutines do not survive save state (ADR-0012), so rewind, save games, and
hot reload all break. Acceptable for a one-off validation port; not
acceptable for a shippable cart.

Recommendation: use Option B to get a port running quickly for validation,
then restructure to Option A before treating the port as a real cart.

---

## Subsystem mapping

### Graphics

| SDL call | Console equivalent | Notes |
|---|---|---|
| `SDL_RenderPresent` | framebuffer present | Implicit at end of `draw()` |
| `SDL_RenderCopy` | `blyt32.gfx.blit` | Texture → resource handle |
| `SDL_RenderFillRect` | `blyt32.gfx.rect` | Map SDL_Color to palette index |
| `SDL_RenderDrawLine` | `blyt32.gfx.line` | |
| `SDL_SetRenderDrawColor` | palette index lookup | Build colour→index map during init |
| `SDL_CreateTextureFromSurface` | `blyt32.resource.load` | Source image pre-packed; not runtime-created |

Runtime texture creation (`SDL_CreateTexture` with dynamic pixel writes) has
no direct equivalent. Games that procedurally generate textures at runtime
need either pre-baking those textures at pack time or writing directly to
the framebuffer.

### Input

SDL keyboard and gamepad events map to the console's abstract button model.
Mapping is game-specific: identify which keys/buttons the game actually uses
and map them to the console's d-pad + 4 face + 2 shoulders + Start/Select.

Mouse input maps to `blyt32.input.pointer_*()`.

SDL has far more keys than the console exposes. Games with complex keyboard
schemes (RTS, simulation) may need UI redesign, not just remapping.

### Timer

| SDL call | Console equivalent | Notes |
|---|---|---|
| `SDL_GetTicks()` | `blyt32.time.frame() * (1000/fps)` | Approximate; deterministic |
| `SDL_Delay(ms)` | — | No blocking sleep in cart code; stub as no-op or restructure callers. Usually only in non-hot paths (loading screens, menu transitions). |

### File I/O

SDL games use `fopen` / `fread` / `SDL_RWops` for asset loading. All
assets must be pre-packed into the cart at build time and loaded via
`blyt32.resource.load`. The translation is mechanical:

- Identify every asset file the game loads at runtime.
- Add them to the cart project as resources.
- Replace `fopen("assets/hero.png")` with `blyt32.resource.load("hero")`.

Save data (`fopen` for writing) maps to `blyt32.save.*`.

### Audio

| SDL approach | Console equivalent | Difficulty |
|---|---|---|
| Tracker / module music | `blyt32.audio.play_music` | Low — tracker formats supported natively |
| Pre-baked WAV/OGG SFX | `blyt32.audio.play_voice` | Low |
| `SDL_mixer` channel mixing | voice groups (ADR-0054) | Medium |
| `SDL_AudioCallback` (custom mixing) | No direct equivalent | Hard |

Games using `SDL_AudioCallback` to do their own PCM mixing have no
direct equivalent. Options: pre-mix audio at pack time, restructure to
use the console's voice model, or (for validation only) silently stub the
audio callback and accept silence.

### Memory

Not a porting concern. Native RISC-V carts are statically linked ELF
binaries with their own heap (from the cart's bundled libc). malloc/free
work normally. The 16 MB working memory budget applies as a total cap;
most SDL games targeting this fidelity fit comfortably.

### Not available

- `SDL_net` — no networking API in carts.
- `SDL_Thread` — threading breaks determinism (ADR-0007). Most game logic
  does not genuinely require threads; audio mixing and asset loading happen
  in the runtime, not cart code.
- Runtime font rendering (`SDL_ttf` with arbitrary TTF files at runtime) —
  fonts are pre-rasterized at pack time (ADR-0072).

---

## Recommended validation targets

**Tier 1 — highest value, lowest cost**

- **Chocolate Doom** (or similar Doom source port): paletted framebuffer,
  clean backend abstraction, well-understood codebase. Validates native cart
  path end-to-end including audio and input. The primary showpiece target.

- **Commander Keen / Wolf3D source ports**: also natively paletted. Simpler
  audio than Doom. Good second validation data point.

**Tier 2 — broader API coverage**

- A modern open-source SDL2 pixel art indie game: validates the compat path
  on a contemporary codebase. Tests palette quantization pipeline and
  `SDL_RenderCopy`-heavy rendering.

- A game with non-trivial audio (multiple simultaneous voices, music +
  SFX): specifically targets the audio compat path, which is the second
  hardest subsystem after graphics.

**Avoid initially**

Games with truecolour rendering, custom audio mixing callbacks, heavy
threading, or complex keyboard schemes. These are solvable but represent
additional engineering work beyond pure porting.

---

## Scope of a compat layer

A general SDL2 compatibility layer covering all common usage is a large
project (5–10k lines). The right approach for early validation is not to
build a general layer but to write the minimal targeted shim each candidate
game needs — typically 1–2k lines — and let a general layer emerge naturally
from common patterns across several ports.

The priority for early development is *validation coverage* (does the full
native cart pipeline work?) not *compat completeness*. A working Doom port
with a thin targeted shim answers most of the important questions.
