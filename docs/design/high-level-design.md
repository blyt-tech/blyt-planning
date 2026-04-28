# Fantasy Console Design Document

A fantasy console aimed at the sweet spot for small-team game development:
fidelity high enough to make genuinely nice-looking games, constrained
enough that a solo creator or a small team can realistically ship one.
Runs everywhere via libretro; runs natively on low-cost RISC-V hardware
for those who want a physical console experience. Lua for approachable
authoring, native code for performance-critical paths.

**Document status:** Design specification, not yet implemented. All
architectural decisions have rationales inline. Sections numbered for
cross-reference. Open decisions and explicit deferrals are collected
in §17. The phased build plan is §18.

**Project has no chosen name yet.** The console is referred to
throughout as "the console" or "this console." Naming is a deliberate
open question, set aside until foundational design is stable.

---

## 1. Core Identity

### Target audience

- **Solo creators and small teams** building real games with scoped ambition.
  The console's constraints are chosen so that one artist plus one programmer
  (or one person wearing both hats) can realistically produce a complete game
  in weeks-to-months, not years.
- **Modern authoring workflows**, including AI-assisted art and off-the-shelf
  asset pipelines. The palette-constrained pixel art format is friendly to
  generative tools and tileset libraries.
- **Players** who want nice-looking indie games with modern features
  (save states, rewind, netplay) available through familiar distribution
  channels (RetroArch, standalone builds, browser).

### Positioning vs. existing fantasy consoles

- **More capable than PICO-8 / TIC-80.** Those are deliberately tiny,
  education-focused, with in-console editors. This console targets real
  indie-game production with best-of-breed external tools.
- **More constrained than Löve / Godot.** Those scale to arbitrary fidelity
  and are general-purpose engines. This console's tight constraints are
  the feature — they make small-team production tractable and produce
  visually coherent games by construction.

### Why the specific constraints

- **320×240 paletted graphics**, 256 colors: the fidelity sweet spot where
  a solo artist can produce a full game's worth of art, where AI-generated
  or off-the-shelf sprite sheets fit the visual budget, and where the shared
  palette naturally gives games visual coherence.
- **32-bit numerics everywhere**: one consistent numeric model across Lua,
  native code, and saved state. No precision boundaries, no conversion
  surprises, no subtle cross-platform determinism bugs.
- **RISC-V ISA for native carts**: license-clean, restrictable to a minimal
  subset, with cheap hardware available ($9-13 SBCs). Enables native
  execution on low-cost physical hardware for anyone who wants to build
  a cabinet, handheld, or embedded product.
- **Deterministic fixed-timestep runtime**: save states, rewind, and netplay
  work as structural properties rather than bolt-on features. Modern
  players expect these; giving them away for free is cheap because the
  architecture is built for it.
- **Cross-platform via libretro**: distribution reach on every platform
  RetroArch supports (Switch homebrew, every retro handheld, macOS via
  OpenEmu, Android, iOS, desktop, browser) without per-platform porting.

### Design principle: scripting is optional

The native API is the primary contract. Lua is one consumer of the API;
C, Rust, Zig, and other RISC-V-targeting languages are equally first-class.
Approachable entry (Lua), professional ceiling (native). Same cart format,
same API, same distribution regardless of language.

### A note on era aesthetics

The constraints overlap with late-DOS / early-Pentium era game hardware,
and authors who want that specific aesthetic can lean into it (the runtime
ships VGA/EGA/CGA palettes and an IBM-style font as options). But the
constraints are chosen for production realism and audience reach, not
for era authenticity. Games on this console can and should feel modern;
the era references are available for authors who want them, not baked
in as identity.

---

## 2. Hardware Target

### Decision: RV32IMFC, little-endian, no D extension.

**Rationale:**
- RISC-V chosen for license cleanliness (vs. ARM's ambiguity — consequential
  for an open-source project and for anyone wanting to build physical
  hardware products around the console), first-class ISA restrictability,
  growing ecosystem with cheap available hardware, and momentum going
  into the late 2020s.
- 32-bit integer base (I) plus multiply (M), single-precision FP (F), and
  compressed instructions (C). Explicitly no D (double FP), no A (atomics
  not needed for single-threaded carts), no V (vector — not in hardware floor).
- F included (not no-FP as originally considered) because the MCU-class
  hardware that motivated no-FP was dropped from the support matrix. At the
  SBC floor, FP is universally available; fixed-point-only would impose
  author ergonomic cost for no remaining benefit.
- f32 (not f64) because it covers every realistic use case at this
  fidelity, matches game-industry convention, enables better SIMD in
  emulators, and keeps state buffer layouts compact.
- Little-endian matches every shipping RISC-V implementation and aligns with
  WASM, x86, and ARM.

### Reference hardware: Milk-V Duo / Duo S class.

**Floor:** Milk-V Duo (C906 RISC-V64 at 1GHz, 64MB RAM, $9).
**Reference target:** Milk-V Duo S (SG2000, 512MB RAM, $13).
**Minimum emulation host:** Raspberry Pi Zero 2 W class (Cortex-A53 quad-core,
1GHz, ARMv8 with NEON).

### Explicitly not supported:

- MCU-class hardware (ESP32-C3, CH32V series, etc.). RAM budgets are
  100-1000x smaller than the SBC target; forcing carts to accommodate
  both profiles would compromise the full-fidelity experience.
- Pi 1 / Pi Zero W (original). Too slow for the emulator at ambitious cart
  workloads; not worth constraining the design.

---

## 3. Visual and Audio Spec

### Graphics: 320×240 paletted, 256 colors, double-buffered.

**Rationale:**
- **Production realism:** a solo artist can produce a full game's worth
  of art at this resolution in weeks to months. At 640×480 truecolor,
  the same scope is a team-years effort.
- **AI / off-the-shelf asset compatibility:** the constrained resolution
  and palette make generative and library-sourced art pipelines practical.
  Generated sprite sheets fit the budget; a shared palette ties disparate
  sources together visually.
- **Distribution reach:** fits every target (memory footprint <200KB for
  framebuffer subsystem), runs on every platform without compromise.
- **Palette-indexed effects:** palette cycling, fades, damage flashes,
  time-of-day shifts — effectively free at paletted depths, expensive
  at truecolor.
- **Mobile-friendly aspect ratio (4:3):** deliberate choice over the more
  modern 16:9. In portrait orientation on a phone (roughly 9:19), a 4:3
  game at the top of the screen spans the full width with the lower 50%
  free for touch controls — a natural Game Boy-style layout. In landscape
  orientation (roughly 19:9 or 20:9), a 4:3 game filling the vertical
  space occupies about 63% of the width, leaving meaningful strips on
  both sides for side-controls without occluding gameplay. 16:9 would
  fit desktop fullscreen better but force touch controls to overlay the
  game. 4:3 prioritizes mobile ergonomics — consistent with the
  console's broad-audience goals.

**Not decided:** Whether to offer a 400×300 "widescreen" mode as alternate.
Lean toward locking to 320×240 for identity strength and mobile
ergonomic consistency.

### Audio: 44.1/48kHz PCM output, software mixer, 16-32 voices.

**Primary formats (blessed, available to all cart classes):**
- **Trackers (XM/IT via libxmp)** for music. Tiny files, negligible CPU,
  mature modern tooling (OpenMPT, Renoise). Strongly preferred for
  most carts.
- **QOA or ADPCM** for SFX. Near-zero decode cost, simple decoders.
- **Raw WAV (PCM)** as universal fallback.

**Streamed compressed audio (Large and Flagship classes only):**
- **Opus** for streamed music, ambient soundtracks, long-form voice
  acting. Best-in-class compression quality — 32 kbps Opus sounds
  comparable to 128 kbps MP3, handles both speech and music well,
  open standard with no patent concerns.

**Rationale for including Opus only in Large/Flagship:**

Streamed audio has real CPU cost — ~5-15% of one core continuously
while playing, depending on platform. This is acceptable but not free:

| Platform | Opus decode cost |
|----------|-----------------|
| Milk-V Duo native | ~5-10% of one core |
| Desktop (native runtime) | negligible |
| Browser via WASM | ~10-15% on weak mobile hardware |
| Pi Zero 2 W | ~8-15% of one core |

Unlike tracker music (zero CPU) or SFX (tiny one-shots), streamed audio
uses CPU the entire time it plays. For most carts, tracker music is
the better engineering choice; streamed audio is for cases where
tracker can't deliver (long voice-acted RPGs, specific composer
intent, ambient soundscapes).

**Why Opus and not MP3 / Ogg Vorbis:**
- Better quality per byte (matters even at Flagship scale).
- Excellent at low bitrates for voice (32 kbps Opus > 64 kbps MP3).
- Open standard, no patent concerns.
- If shipping one streamed format, pick the technically best one. The
  packer accepts MP3 / OGG / AAC inputs and transcodes to Opus at pack
  time, so authors can work with whatever their tools produce.

**Why not AAC (despite hardware offload advantages):**

Modern phones can hardware-decode AAC via browser audio APIs, essentially
eliminating decode CPU cost. Opus has growing but less universal
hardware support. AAC would give better battery life on mobile.

Rejected because:
- Hardware offload paths require two decode implementations (software
  for native, browser-API for web) and double the testing surface.
- Browser audio element routing has synchronization and latency
  concerns that matter for games. Direct PCM-buffer mixing (current
  design) provides tighter control.
- CPU cost of software Opus decode is manageable on all platforms
  (5-15% of one core) — noticeable but not prohibitive.
- Hardware-accelerated decode paths can be added in v2 as an
  optimization without changing the cart format or author-facing API.

**Runtime-implemented decode:** streamed audio decode happens in the
runtime's native code, not in cart code. This means decode cost doesn't
compound with the RISC-V interpreter's overhead on emulated platforms.
Cart API is `console.audio.play_stream(resource_id)`; runtime handles
the decode loop.

**Future optimization (v2):** on mobile browser platforms, runtime could
optionally route Opus decode through Web Audio / HTMLAudioElement APIs
to leverage hardware offload. Would reduce battery drain for
long-playing streams. Non-breaking addition; cart format unchanged.

**Packer enforcement:** Mini and Standard carts with Opus resources
are rejected at pack time. This preserves the "constraint encourages
tracker music" pressure for smaller classes while enabling ambitious
audio for Large/Flagship scope.

### Recorded speech

Small-to-medium games can include recorded voice acting within cart
size constraints, using format options appropriate to cart class:

| Format | Rate | Storage per minute | 10MB fits |
|--------|------|--------------------|-----------| 
| Raw PCM 8-bit mono | 22kHz | ~1.3MB | ~7.5 min |
| Raw PCM 8-bit mono | 11kHz | ~650KB | ~15 min |
| ADPCM 4-bit | 8kHz | ~235KB | ~42 min |
| ADPCM 4-bit | 11kHz | ~325KB | ~31 min |
| QOA | 22kHz stereo | ~1.3MB | ~7.5 min |

**Recommendations:**
- **ADPCM at 8-11kHz** is the sweet spot for speech. Good quality for
  dialog, excellent compression ratio, minimal decode cost.
- **11kHz is acceptable for speech** (unlike music, where it sounds
  bad). Halves storage with minimal perceptual cost for voice.
- **QOA or higher-rate PCM** for narrator / high-priority content where
  quality matters most.

**Streaming playback** (a modest runtime feature, ~200 lines) enables
much longer speech by reading from the cart file as playback progresses,
using a small playback buffer rather than loading clips entirely into
RAM. With streaming, speech length is limited by cart size budget, not
runtime memory budget — a 10MB speech section becomes 42 minutes at
8kHz ADPCM regardless of clip boundaries.

**Game scope considerations by cart class:**
- **Mini (2 MB):** combat barks, UI voice lines. Not much room for
  dialogue; fine for character grunts and short confirmations.
- **Standard (16 MB):** 15-30 minute games with full voice acting at
  moderate quality. Short narrative experiences. Voice-acted intros
  / cinematics (2-3 minutes at good quality).
- **Large (64 MB):** Day of the Tentacle-scale talkies — ~3.5 hours of
  voice acting at 8 kHz ADPCM (~49 MB for voice) plus music, art, code.
  Full voice-acted adventure games, mid-length RPGs with extensive
  dialog. The primary target for talkie-style games.
- **Flagship (256 MB):** Grim Fandango-scale — 7+ hours of voice acting.
  Can combine ADPCM for bulk dialog with Opus streams for premium
  cutscenes or narrator. Full commercial-indie-scope talkies.

Speech uses ADPCM (all cart classes) or Opus (Large/Flagship only).
Recording and editing workflow is covered by Audacity in the standard
zero-budget toolchain.

---

## 4. Numeric Model: 32-bit Everywhere

### Decision: i32 and f32 throughout the system.

- **Native (RV32IMFC):** i32 and f32 first-class in the ISA.
- **Lua:** Built with `LUA_32BITS` (or equivalent: `LUA_INT_TYPE=LUA_INT_INT`,
  `LUA_FLOAT_TYPE=LUA_FLOAT_FLOAT`). Lua numbers are i32 and f32.
- **State buffers:** Field types i8/u8/i16/u16/i32/u32/f32/bool plus
  fixed-size arrays.
- **64-bit types:** Available via userdata library (`i64.new()`, arithmetic
  via metamethods) for rare cases. Not first-class in language or state.

**Rationale:**
- Eliminates precision-loss conversion at Lua ↔ state buffer boundary.
- Save state bit-identical across Lua and native carts.
- Consistent mental model: "this is a 32-bit console."
- f32 integer-exact range up to 2^24 is plenty for game values at this
  fidelity; i32 range of ±2.1 billion covers any realistic score, counter,
  or index.
- Precedent: Defold uses 32-bit Lua in a commercial engine without issue.

**Trade-off accepted:** Time accumulation as f32 seconds loses precision
after hours of play. Mitigation: store time as i32 frames or milliseconds;
`console.time.frame()` API provides this.

---

## 5. Determinism

### Decision: The design is fully deterministic given identical inputs and starting state.

A cart running on any platform (native RISC-V, interpreted desktop,
WASM browser, libretro) with identical input sequences produces
bit-identical frame-by-frame results. This is the contract that makes
save states, rewind, replay, and netplay work.

**Determinism requires controlling every source of variation. Each
is handled as follows:**

### Floating-point operations

- **Basic FP ops** (add/sub/mul/div/sqrt) are bit-identical per IEEE 754
  when strict mode is respected.
- **Compiler flags enforced for cart builds:** `-ffp-contract=off`,
  `-fno-fast-math`, `-fno-unsafe-math-optimizations`, `-fwrapv` for
  signed overflow, `-frounding-math`, `-fsignaling-nans`. No `-Ofast`.
- **Runtime sets host FP state** (MXCSR on x86, FPCR on ARM) on emulator
  entry to ensure default rounding, no flush-to-zero, no default-NaN mode.
- **Transcendentals (sin/cos/tan/exp/log/etc.) provided by console API**
  using a deterministic implementation (musl libm as reference). Carts
  do not use the host's libm. Critical — IEEE 754 does NOT mandate
  bit-identical transcendentals across implementations.
- **NaN canonicalization** on write to state buffers (or reject NaN writes).
- **RISC-V interpreter** uses SoftFloat for guest FP ops, or host FP
  with careful state management. SoftFloat is bulletproof at modest
  performance cost.
- **WASM build** is deterministic by spec.

### Integer operations

- **Wraparound semantics** defined in both Lua (by spec) and native
  carts (by `-fwrapv`). No undefined behavior on overflow.

### Random number generation

- **Runtime-owned, seedable RNG streams.** State lives in tracked
  regions; save state preserves and restores it.
- **No uninitialized sources.** RNG must be seeded explicitly before
  use; default seed is documented and stable.

### Time

- **Only deterministic time is exposed to carts.** The time API:
  - `console.time.frame()` — i32 frame count since cart start.
    Increments once per logical update. Deterministic.
  - Cart-start-relative durations derived from frame count
    (at 60 Hz, 3600 frames = 60 seconds of logical play).
- **Wall-clock time is NOT exposed to carts.** No API provides "real"
  time, system time, Unix timestamps, or time-since-boot. Carts that
  need to represent "time of day" or "session duration" use frame count.
- **`dt` in update is always 1/60.** Fixed logical timestep per §15.

### Input

- **Snapshotted per logical update.** Runtime polls input state once
  per update tick; within an update, input is frozen.
- **Input events are associated with frame numbers, not wall-clock
  times.** For replay / rewind / netplay, inputs are recorded as
  `(frame_n, button_state)` tuples, not timestamped events.
- **Sub-frame timing is invisible to carts.** Two button presses
  happening within the same 16 ms frame both appear on that frame's
  input snapshot, regardless of exact sub-frame timing on different
  platforms.

### Audio

- **Audio mixing is a one-way cart-to-mixer data flow.** No audio state
  flows back into gameplay.
- **Audio playback queries are derived from cart state, not mixer state.**
  If a cart needs to know "is this clip still playing?", the answer is
  computed from `(frame_started, clip_duration, current_frame)`, not
  from the real mixer. A clip "started" at frame N of a given length
  is "still playing" at any frame less than N + length, regardless of
  whether the physical mixer has actually finished producing samples.

### Resource loading

- **Synchronous.** A cart calling `load_resource("foo")` blocks until
  the resource is available. Decompression latency varies across hosts,
  but the cart's observable behavior — "I called load, got a result" —
  is identical. No async completion semantics that could observe host
  speed.

### Coroutines

- **Cooperative scheduling only.** Lua coroutines yield and resume
  under explicit cart control. The runtime never preempts or schedules
  them autonomously.
- **Save state handles coroutine state** via hooked coroutines with
  explicit save/restore points (§7). Raw coroutines don't survive save,
  which is documented.

### GC timing

- **GC is invisible to carts.** Lua's incremental GC runs based on
  allocation pressure, which is a function of cart behavior. Two
  identical runs make identical allocations and trigger GC identically.
- **GC doesn't affect cart-observable state.** No API exposes "is a
  GC happening right now?" — the cart only sees values before and after
  its own operations.

### RISC-V instruction execution

- **The interpreter and hardware must agree on all instruction behavior.**
  Validated via the `riscv-tests` conformance suite.
- **Implementation-defined behavior is avoided.** The spec allows
  implementation freedom on a handful of edge cases (FP exception flag
  handling, some rounding modes); the runtime picks deterministic
  options and documents them.
- **No illegal instruction execution.** Bugs in the interpreter that
  execute RV64 or forbidden extension instructions would diverge from
  hardware; testing catches this.

### Platform differences

- **Hidden behind the API.** Cart code does not see byte order,
  pointer size, OS, hardware model, or any platform-specific detail.
  The abstract machine (RV32IMFC + console API) is identical everywhere.

### Save state / hot reload

- **Save/restore cycles preserve full cart state bit-identically.**
  A cart state snapshotted, then immediately restored, continues
  executing identically to what would have happened without the
  snapshot. Validated by CI tests.

### Draw output

- **Deterministic.** Drawing APIs have no randomness, no platform
  variation. Two identical runs produce identical framebuffer contents
  at identical frames.
- **Presentation timing is irrelevant to determinism.** The framebuffer
  is visible to the player at different wall-clock times on different
  hosts, but the *contents* of the framebuffer at frame N are identical
  on all hosts.

### Sources of non-determinism that are explicitly acknowledged

- **Wall-clock duration to reach a given frame differs across hosts.**
  A fast host reaches frame 3600 in 60 seconds; a slow host takes
  longer. The cart-state at frame 3600 is identical on both; the
  wall-clock time to get there is not. This is a feature — replays
  preserve game behavior regardless of host speed.
- **Update-overrun watchdog termination.** A cart that genuinely hangs
  or enters an infinite loop gets killed by the watchdog. Which frame
  the kill happens on depends on host speed. This only affects buggy
  carts; well-behaved carts never see it.
- **Memory pressure eviction.** If the runtime evicts a decompressed
  resource under memory pressure, re-accessing it has a brief latency
  spike while it re-decompresses. This affects wall-clock timing but
  not cart-visible state.

### Validation

- **CI bit-identity tests.** Known cart workloads are run on multiple
  platforms; frame-by-frame state snapshots are compared for
  bit-identity. Regressions surface as failed tests.
- **Cross-platform replay tests.** Record inputs on platform A,
  replay on platform B, verify end state matches.
- **RISC-V conformance suite.** Interpreter passes all relevant
  `riscv-tests` for RV32IMFC.

**Result:** save states, rewind, deterministic replay, and netplay
all work correctly across platforms as a structural property.

---

## 6. Memory Model

### Decision: API-based, not memory-mapped.

**Budgets:**
- Cart size (on disk): 16MB max.
- Runtime memory: 32MB total.
- Cart-visible working memory: 16MB (other 16MB is runtime overhead —
  decoded resources, framebuffer, mixer, VM state).

**Access patterns:**
- **Framebuffer:** `acquire_framebuffer()` returns a writable buffer for
  the current frame; `present_framebuffer()` hands it back. Direct pixel
  writes in hot loops without per-pixel API overhead.
- **Palette, input, mixer state:** Bulk API calls (`set_palette(table)`,
  `get_input_state()`, etc.). Infrequent enough that per-call overhead
  is irrelevant.
- **Typed buffers (entities, particles):** First-class for hot data.
  `alloc_state(layout, count)` returns a typed buffer with SOA storage
  and direct-index access from Lua.

**Rationale for rejecting a flat memory region:** The benefits
(fast framebuffer access, cheap palette swaps, tactile feel, trivial save
states) can all be recovered via targeted APIs without freezing layout
details into a cart-visible memory map. Keeps the runtime free to evolve.

**Visible accounting:** Carts can query memory usage. Allocation failures
at the cap return nil/null gracefully rather than crashing. Dev tools
display "X of 16MB used" continuously.

---

## 7. State and Save System

### Decision: Persistent state lives in tracked typed buffers.

**Model:**
- Carts allocate persistent state via `alloc_state(layout, count)`.
- Layouts are **POD**: primitive fields (i8/u8/i16/u16/i32/u32/f32/bool),
  fixed-size arrays of primitives, no pointers, no dynamic structures.
- **References between buffers use integer indices**, not pointers.
- Runtime tracks all state allocations plus its own state (RNG, timers,
  input buffer, etc.).
- `save_state()` walks tracked regions, emits self-describing binary
  snapshot with type info and endianness markers.
- `load_state(snapshot)` validates types, converts endianness if needed,
  restores.

**Rationale:**
- Save/load is essentially memcpy of tracked regions — simple, fast,
  cross-platform portable.
- POD discipline forces data-oriented design, which happens to be what
  games at this fidelity want anyway.
- Typed layouts (vs. untyped bytes) enable cross-platform save portability
  (desktop save loads on RISC-V hardware) and debugger introspection.
- Rewind (ring buffer of snapshots), replay (snapshot + recorded inputs),
  and netplay (lockstep sim) all fall out naturally.

### Decision: Lua ergonomic sugar via metatables.

**Approach:** SOA typed buffers exposed as `buffer.field[index]`,
no row proxies, no `ref<T>` machinery in v1.

```lua
declare_layout("Enemy", {
  x = "f32", y = "f32",
  hp = "i32",
  active = "bool",
  target = "i32",  -- raw index, -1 means none
})
enemies = alloc_state("Enemy", 200)

for i = 1, enemies.count do
  if enemies.active[i] then
    enemies.x[i] = enemies.x[i] + enemies.vx[i] * dt
  end
end
```

**Rationale:** ~90% of the ergonomic benefit of full row proxies and
`ref<T>` types at ~20% of the implementation cost (300-400 lines vs.
1500-2000). Row proxies can be added later as opt-in sugar without
breaking existing carts.

**Trade-off accepted:** Reference dereferencing is explicit
(`target_idx = enemies.target[i]`; check valid; use). Authors can build
helper functions; can add `ref<T>` machinery in a later version.

### Decision: Lua coroutines supported with save/restore hooks.

Raw Lua coroutines (`coroutine.create`) are available but do not round-trip
through save states. Authors who need persistent coroutines use a blessed
console API (`console.coroutine.create{ start, save, restore }`) with
structured yield points and explicit save/restore hooks.

**Rationale:** Full coroutine serialization (Eris-style) is heavyweight
and version-locked to specific Lua versions. Structured coroutines cover
the main use cases (cutscenes, sequenced animation, state machines) with
save compatibility, while raw coroutines remain available for authors who
don't care about saves.

### Decision: Four distinct save mechanisms, each for its natural purpose.

"Save" is ambiguous — several different concepts get conflated. The
runtime provides four separate mechanisms, each with different
semantics and different ownership:

**1. Cart-managed in-game save (cart decides).**

The player's ongoing progress — story position, inventory, party
state, level completion. Cart controls when and what to save.

```lua
console.save.write("slot1", state_table, {label = "Chapter 5 - 80%"})
local state, metadata = console.save.read("slot1")
local slots = console.save.list()   -- {slot_name, label, timestamp, size}
console.save.delete("slot2")
console.save.quota_remaining()      -- bytes
```

Runtime handles:
- Per-cart save directories on disk (platform-specific path).
- Serialization via the state buffer format (self-describing, cross-platform bit-identical).
- Atomicity (write-then-rename to prevent corruption mid-save).
- Per-cart quota (default 10 MB; cart sees errors if exceeded).
- Slot metadata storage (cart-provided label, timestamp, size) for UI.

Cart identity for save-directory naming comes from the cart's declared
ID in the manifest — stable across cart updates, so saves persist when
the cart is updated.

**2. Save state (platform-managed).**

A snapshot of the entire runtime state at a moment. Restoring resumes
execution from that exact moment. Cart has no knowledge that save
state is happening.

- Implemented via `retro_serialize` / `retro_unserialize` (libretro).
- Uses the runtime's save/restore machinery (same as hot reload).
- Frontend (RetroArch or custom libretro frontend) provides UI:
  typically quick-save / quick-load hotkeys plus a small number of
  numbered slots.
- Tagged with cart binary hash; loading a save state from a different
  cart version warns the player.

Save states are **orthogonal to cart saves.** A player can save via
cart save points, continue playing, quick-save-state before a tough
boss, die, quick-load — their cart save is unaffected.

**3. Rewind (platform-managed).**

Continuous save states in a ring buffer; player can rewind recent
gameplay.

- Implemented via libretro's rewind infrastructure.
- Uses the same serialize/unserialize the core already provides.
- Cart has no knowledge rewind is happening.
- Frontend provides UI (typically a hotkey held down to rewind).

Nearly free given the save state infrastructure. Valuable player
feature, especially for difficulty accommodation.

**4. Cart preferences (cart-managed, separate from save).**

Small persistent key-value store for player configuration —
volume, controller layout, accessibility settings. Not gameplay state.

```lua
console.prefs.set("volume", 0.8)
local volume = console.prefs.get("volume")
console.prefs.delete("legacy_option")
```

Separate API because:
- Available immediately at cart start, before any save is loaded.
- Persists across save slot boundaries (configuration isn't per-save).
- Survives cart updates (new cart binary uses existing preferences).
- Simpler semantics (key-value, not structured state).

Per-cart quota (default 64 KB; preferences are small by nature).

**Save directory structure:**

```
<platform-data-dir>/console/
├── carts/
│   ├── CART_ID/                  (one dir per cart; CART_ID from manifest)
│   │   ├── saves/
│   │   │   ├── slot1.save        (cart-managed saves)
│   │   │   ├── slot2.save
│   │   │   └── metadata.json     (slot metadata for UI)
│   │   ├── prefs.json            (cart preferences)
│   │   └── state/
│   │       └── quick.state       (save state, managed by libretro)
```

**Integration with libretro SAVE_RAM:**

Libretro's `RETRO_MEMORY_SAVE_RAM` model (flat memory region
automatically persisted to disk) is a poor fit for structured
multi-slot cart saves. Instead, cart saves use the runtime's own
disk handling. Libretro SAVE_RAM is not used by the runtime.

This means cart saves don't automatically get libretro frontend
cloud sync features for now. Acceptable tradeoff — structured
multi-slot saves are the right abstraction; cloud sync can be
added later as a v2+ feature using either libretro infrastructure
or a custom sync layer.

**Cross-platform saves:**

Save format is bit-identical across platforms (per §5 determinism
work and the state buffer design). A save written on desktop can be
read on hardware if the file is transferred. V1 ships with local-only
saves; cross-device sync is v2+.

**Versioning:**

- **Cart saves** are versioned at the runtime format level (self-describing
  binary). Cart handles its own migration when loading saves from
  older cart versions (new fields added: default; fields removed:
  drop; renamed: cart-specific logic).
- **Save states** are tagged with cart binary hash; loading across
  versions warns the player.
- **Preferences** are unversioned key-value data; cart handles missing
  keys via defaults.

### Decision: Manifest-declared achievements with runtime-provided UI.

Carts can optionally define achievements; the runtime handles
display, notification, persistence, and browsing. Cart code just
signals unlock.

**Cart manifest:**

```yaml
# cart.config.yaml
achievements:
  final_boss:
    name_key:    ach.final_boss.name
    desc_key:    ach.final_boss.desc
    icon:        crown            # references cart resource
  flawless:
    name_key:    ach.flawless.name
    desc_key:    ach.flawless.desc
    hidden:      true             # shown as ??? until unlocked
  collector:
    name_key:    ach.collector.name
    desc_key:    ach.collector.desc
    # optional progress metadata for runtime browser display
    progress:    { current: 0, total: 100 }
```

**Cart API:**

```lua
console.achievements.unlock("final_boss")
console.achievements.progress("collector", current, total)  -- optional
local unlocked = console.achievements.is_unlocked("final_boss")
```

**Runtime behavior:**

- Shows notification banner on unlock (standard visual, cart-provided icon).
- Maintains achievement browser UI (pause menu item) listing all
  achievements with status and descriptions.
- Persists unlocks in `achievements.json` per cart (see save directory
  structure above).
- Never "re-locks" achievements — save state / rewind don't undo unlocks.
  Once unlocked, always unlocked.
- Hidden achievements display as ??? until unlocked; name and
  description revealed on unlock.

**Rationale for runtime-managed vs. cart-managed:**

- Consistent UI across carts — players get the same achievement
  experience regardless of cart.
- Metadata in one place (manifest), discoverable by external tooling.
- Cart achievement definitions are part of cart metadata, not cart
  code — browseable without running the cart.
- Cart code stays simple: just announce unlocks.

**Libretro integration:**

For v1, achievements are managed by the custom libretro frontend
independently. RetroArch players would not see these achievements
(RetroArch uses RetroAchievements.org, which has a different model —
memory-inspection-based achievement detection, designed for
reverse-engineering classic games).

Later (v2+), mirroring cart-declared achievements into
RetroAchievements format could give RetroArch users achievement
support. Not essential for v1.

### Decision: Speedrun tooling built on deterministic replay.

The runtime's determinism guarantee (§5) provides a strong foundation
for speedrun infrastructure. Several features build on it.

**V1 scope:**

*Input log recording / replay.* The runtime records all player
inputs during a session and exports as a replay file. Another instance
of the runtime (same cart version, same runtime version) replays
the file and reaches bit-identical state.

Replay format includes: cart ID, cart version, runtime version,
initial RNG seeds, per-frame input state. Verifying a run means
replaying the file and comparing final state (or any intermediate
state) to the original.

Replay files are tiny (input state per frame is a few bytes) and the
infrastructure is nearly free given the existing determinism work.

*Frame-accurate timing.* `console.time.frame()` already provides the
deterministic frame counter; carts can compute run times from this
natively if they want to display a timer in-game.

*Speedrun mode.* Runtime setting (player-selectable via UI) that:
- **Pre-decompresses all cart resources to disk or locked memory** so
  no mid-run decompression latency occurs. Critical for run-time
  consistency — a 10-30 ms decompression pause during gameplay would
  add floor noise to timing measurements. If disk space is
  insufficient for full pre-decompression, falls back to lazy
  decompression with a warning that times may vary.
- Disables save states and rewind (would invalidate runs).
- Shows a timer overlay.
- Records input log automatically.
- Uses a fixed starting state (no save-scummed starting conditions).

Setup time: typically 5-30 seconds for Large-class carts. Player
sees a "preparing run..." progress indicator.

*Replay-as-demo.* The runtime supports loading "cart + input log"
as a single playable unit. When a replay file is associated with
a cart, the runtime plays back the log with inputs injected over
the deterministic simulation — identical to live play but following
the recorded inputs.

This is how classic game demos worked (Quake `.dem` files, StarCraft
replays). Use cases:
- Author-provided "watch the intended solution" for puzzle games.
- Community-shared speedrun replays.
- Ambient playback (attract mode on kiosk devices, screensaver-style
  playback of replays alongside Demo-class carts).
- Tutorial content (see the sequence of inputs for a technique).

Bundle format can be either a directory (cart + replay side by side)
or an extended cart format that includes an embedded replay. V1
supports at minimum side-by-side files; embedded-replay format may
follow.

**Deferred to v2:**

*Cart-declared split points.* Carts declare segment markers in the
manifest; cart code announces `console.speedrun.split("id")` as it
happens; runtime overlays split times. Polish feature — the
foundation (deterministic timing + input recording) is v1; split
tooling is v2. Community can build external split tooling (LiveSplit
integration) on top of cart-declared APIs when available.

*Community leaderboards and verified run submission.* Ecosystem work,
requires infrastructure (service to host, verification of submitted
replays, leaderboard UI), out of v1 scope.

### Decision: Design for game-service integration without v1 implementation.

"Game services" (Steam, GOG Galaxy, Epic Online Services, Xbox Live,
etc.) provide features like cloud saves, achievement sync, leaderboards,
friends lists, rich presence, and storefront integration. V1 does not
implement any of these, but the design avoids decisions that would
close doors to future integration.

**Integration layer: distribution wrapper, not cart or runtime.**

Services integrate at the **frontend/distribution wrapper layer**, not
at the cart or runtime layer. A Steam-distributed version of the console
would be a Steam-integrated frontend hosting the standard runtime;
the same cart runs identically whether launched via Steam, GOG Galaxy,
itch.io, or standalone.

This means:
- Carts don't use service-specific APIs.
- The runtime doesn't embed service SDKs or know about specific services.
- Service features (cloud save sync, achievement sync to Steam,
  storefront-managed DLC, rich presence) live in a distribution
  wrapper that subscribes to runtime events and translates to
  service-specific calls.

**v1 design requirements to keep doors open:**

*1. Event-based cart-to-runtime communication.*

Cart actions that services might care about are exposed as runtime
events. Frontends (including service-integrated distribution wrappers)
can subscribe to these events via the runtime's C API.

Relevant events already in v1:
- Achievement unlocked (already event-driven).
- Cart save written.
- Cart session started / ended.
- Cart loaded / unloaded.

A Steam wrapper subscribes to "achievement unlocked" and forwards to
Steam's achievement API; subscribes to "save written" and triggers
Steam Cloud sync for that cart's save directory; etc. Cart and
runtime know nothing about Steam.

*2. Frontend-owned cart identity.*

Carts carry no `cart_id` field. Each distribution channel keys saves
and service entitlements with the identifier native to that channel —
Steam app ID, GOG product ID, itch.io game ID, etc. Standalone
frontends pick a scheme appropriate for how they store carts (a
path-derived key, a user-named slot, or a content hash). Avoiding a
global cart-id namespace removes the coordination/collision problems
that come with one and lets each service use its existing identity
model unchanged.

*3. Cart save directories are filesystem-visible.*

Per-cart save directories on disk (not in libretro's flat SAVE_RAM)
make cloud-save integration trivial: a service wrapper syncs the
directory contents, just like Steam Cloud syncs directory-based saves
for most games.

*4. Achievement metadata in manifest.*

Cart achievement definitions (name, description, icon, hidden flag)
already live in the manifest per §7. Services typically require
achievement metadata on their back-end (Steamworks, for instance);
cart achievements map one-to-one to service achievements if the cart
author registers them on the service's back-end.

*5. Runtime doesn't assume online connectivity.*

Core features (cart save, achievement unlock, session management) work
entirely offline. Service integration *augments* these features; it
doesn't replace them. A player on an unauthenticated runtime still
saves, still unlocks achievements locally; service sync is a
distribution-layer add-on.

**Explicitly not v1 scope:**

- Steam SDK, GOG Galaxy SDK, or other service SDK integration in the
  runtime.
- Generic service-abstraction API (e.g., `console.services.*`).
- Authentication, account identity, or cloud storage infrastructure.
- DLC / entitlement management.
- Service-mediated multiplayer or matchmaking.
- Service overlay UI.
- Cross-service cloud save.

These are ecosystem work appropriate to each distribution wrapper;
not relevant to the core runtime or cart format.

**Example: how a Steam distribution would work (v2+):**

- Standalone runtime and carts are identical to non-Steam versions.
- Steam distribution ships as a Steam app that wraps the runtime.
- On launch, the wrapper authenticates with Steam, hosts the runtime,
  opens the default cart (or cart picker).
- Wrapper subscribes to achievement events, forwards to Steam.
- Wrapper subscribes to save events, triggers Steam Cloud sync.
- Wrapper sets rich presence from cart identity + manifest data.
- Cart developers register their cart as a Steam app, define matching
  achievement IDs in Steamworks, upload cart + runtime as the Steam
  depot.
- Cart code is unchanged — same cart works on standalone, Steam, GOG,
  itch, etc.

Same pattern for GOG (via GOG Galaxy SDK), Epic (via EOS SDK),
console storefronts if the console ever targets them. Each wrapper
is a relatively small project by someone motivated to do that
integration; the core runtime stays service-agnostic.

---

## 8. Input Model

### Decision: D-pad + 4 face + 2 shoulders + Start/Select, up to 4 players.

**Mapping is runtime preference, not cart-visible.** Carts see abstract
button IDs; runtime translates from whatever physical input (keyboard,
gamepad) the player is using.

**Three keyboard presets (player 1):**
1. **Gamepad-style:** WASD (d-pad) + HJKL (face) + U/O (shoulders) +
   Enter/Right-Shift (Start/Select).
2. **Arrow-style (default):** Arrows (d-pad) + ZXCV (face) + A/S
   (shoulders) + Enter/Right-Shift.
3. **Arrow-ASDF variant:** Arrows (d-pad) + ASDF (face) + Q/W (shoulders)
   + Enter/Right-Shift.

**Shoulders placed above the action group** (not split across hands like
a physical gamepad). Rationale: most shoulder uses are action-modifier
chords (hold L to aim, press A to shoot); keyboard authors find same-hand
modifier chords natural. Gives up the rare direction-modifier case.

**Analog sticks excluded from v1.** Keyboard emulation of analog is poor;
most games targeting this fidelity and scope don't need analog input.

**Multi-player input mapping:**

**One device, one player.** Each physical input device supports
exactly one player. Multi-player requires multiple devices:

- **Keyboard:** player 1 only. No keyboard-splitting fallback for
  player 2.
- **Touchscreen:** player 1 only. Touchscreens don't support multi-player
  on one screen.
- **Gamepads:** each connected gamepad is one player. First gamepad
  to connect or produce input is player 1; subsequent gamepads are
  players 2, 3, 4 in connection order.

For local 2-4 player games, every player needs their own gamepad.
Multi-keyboard support (two keyboards on one machine, each a separate
player) is possible technically but out of v1 scope; deferred unless
demand emerges.

Controller assignment UI in runtime settings lets players manually
reassign slots ("press any button to claim player 3" etc.) or shuffle
an existing assignment.

**Controller hot-plug handling:**

Controllers (especially Bluetooth) can connect, disconnect, sleep/wake,
run out of battery, and reconnect at any time during a session.
Libretro and SDL2 surface these as events; the frontend handles them
at the UX level. The cart doesn't see hardware events — just "player N
is pressing some button" or "player N is not pressing anything."

*Connection:* new controller appears via SDL_CONTROLLERDEVICEADDED.
Frontend:
- Recognizes returning controllers by GUID (SDL2 provides stable device
  identifiers). Restores previous slot assignment without prompting.
- For new controllers, auto-assigns to first empty slot.
- Shows brief "Player N controller connected" notification.

*Disconnection:* SDL_CONTROLLERDEVICEREMOVED event. Frontend:
- Marks slot as disconnected; inputs go to zero for that player.
- **Single-player local session:** pauses automatically (prevents
  accidental progress loss if controller dies mid-play).
- **Local multiplayer or netplay session:** session continues; cart
  sees player as "not pressing anything" and handles via
  `is_player_connected()`.

*Reconnection:* same controller (matched by GUID) reconnects →
restored to its previous slot, inputs resume. Handles the common
"controller went to sleep and woke up" or "controller's battery died,
replaced it" scenarios gracefully.

*Battery monitoring:* Bluetooth controllers typically report battery
level via HID. Frontend shows battery-low warnings in the controller
status UI. Not cart-visible.

**Mid-session controller connections:**

Different rules for different session types:

- **Single-player local session:** a new controller connecting during
  play can claim an empty player slot, enabling couch multiplayer
  drop-in (classic retro pattern — friend sits down, picks up a
  controller, joins the game). Cart sees player N become active; can
  handle via checking `is_player_connected()` in update.
- **Local multiplayer session:** same as single-player — empty slots
  can be claimed mid-session.
- **Netplay session:** player count is fixed at session start (§8
  netplay decision). Controllers connecting on a player's local
  machine affect their local input routing only, not the session's
  player count. Controllers connecting at the frontend level don't
  create new netplay peers — that requires a new machine joining the
  session, which is locked once the session starts.

This distinction matters: couch multiplayer is naturally drop-in
(someone walks up and joins); netplay requires pre-coordination
(everyone in the session is committed). The session-type distinction
makes both work without conflict.

**Why 4 players:**

Classic local multiplayer consoles (SNES with multi-tap, N64, Dreamcast)
supported 4-player simultaneous play, enabling genres that are core
fantasy-console fare: party games, 4-player beat-'em-ups, Micro
Machines-style racers, Bomberman-likes, asymmetric-role games. A fantasy
console that stops at 2 players forfeits this entire genre space.

Architectural cost over 2 players is small: input state storage gains
negligible bytes, input API takes player index 1-4 instead of 1-2
(same function signatures), gamepad assignment loops to 4 slots, small
frontend UI for controller-to-player assignment. Netplay handles 4
players automatically via libretro infrastructure.

**Cart state buffer convention:** carts storing per-player state should
use fixed 4-element arrays from v1, even for single-player or
2-player-only carts. Consistency across the ecosystem; avoids later
migration headaches when a cart adds 3-4 player support.

### Decision: Single-pointer API alongside button inputs.

In addition to the button-oriented API (d-pad + face + shoulders +
Start/Select), the runtime exposes a **single pointer abstraction** for
mouse and touch input:

```lua
console.input.pointer_is_held()
console.input.pointer_position()    -- x, y in game pixels
console.input.pointer_pressed()     -- edge-triggered on press
console.input.pointer_released()    -- edge-triggered on release
```

Pointer source depends on platform:
- **Desktop:** mouse.
- **Mobile:** first active touch.
- **Libretro frontends:** frontend-provided pointer (touchscreen, mouse,
  or cursor).

Pointer and button input coexist — a cart can use both. Carts designed
for touch-first schemes (`tap_to_select`, `direct_manipulation`) use
pointer exclusively and work identically on desktop mouse and mobile
touch.

**Multi-touch (raw touch events)** is available as an advanced API for
carts that genuinely need it (multi-finger puzzle games, etc.), but
most carts use the single-pointer abstraction.

**Principle:** cart code never branches on "am I on mobile?" It queries
what input is available and uses it. The runtime's touch control
schemes (see §14) translate touch gestures to button events for
button-based carts, making most carts portable to mobile with no
changes.

### Decision: Expose cosmetic device info to carts; withhold capability info.

Carts can query attached input devices to display correct prompts and icons
("Press Cross to jump" on PlayStation, "Press A to jump" on Xbox, "Press Z"
on keyboard). This is purely cosmetic — the abstract button IDs and the
spec itself (d-pad + 4 face + 2 shoulders + Start/Select) remain fixed and
unconditional.

**Exposed:**
- Device kind (`gamepad` / `keyboard` / `other`).
- Device family (`xbox` / `playstation` / `nintendo` / `keyboard` /
  `generic`) — primarily for icon sprite selection.
- Device name (full string from SDL's gamepad DB or equivalent).
- Per-button display labels (short form, e.g. "A" / "Cross" / "B" /
  "Z"; symbols like "↵" for Enter where appropriate).
- Connection/hot-plug events so carts can re-query on device changes.

**Not exposed:**
- Presence of extra buttons the spec doesn't use (L2/R2, stick clicks,
  home/share buttons).
- Analog stick values (spec is digital-only).
- Touchpad, gyro, LEDs.
- Rumble/haptics — deferred; if added later, as an opt-in feature flag
  that degrades gracefully when absent.

**Nintendo button-layout handling:** Abstract button IDs follow *positional*
convention (SNES-style: `a` is right face, `b` is bottom face). Runtime
maps to physical device's convention so the button in the correct position
is pressed regardless of the label the manufacturer put on it. Cart asks
for "a button label" and gets the right string for the device in front of
the player; cart doesn't branch on family unless it specifically wants
to.

**Sketch:**
```lua
local info = input.device_info(1)
-- { kind, family, name, labels = { a, b, x, y, l, r, start, select, ... } }

local jump_key = input.button_label(1, "a")
draw_text("Press " .. jump_key .. " to jump", 10, 10)

local family = input.device_family(1)   -- for icon sprite selection
draw_sprite(icons[family].button_a, 20, 20)
```

**Rationale:** Correct on-screen prompts are a quality-of-life feature that
costs very little to implement (SDL's gamepad database provides most of
the data) and that fantasy consoles usually skip. Exposing labels without
exposing capabilities keeps the console's input spec fixed — carts cannot
meaningfully branch on "this controller has more buttons," only on "how
should I name this button to the player."

**Libretro synergy:** Libretro's input descriptor mechanism (core tells
frontend "button A means Jump") is orthogonal and can be populated from
cart metadata if desired, giving RetroArch its normal remapping UI.

### Decision: Netplay via libretro, enabled by default determinism.

RetroArch has mature netplay infrastructure built on deterministic
rollback. Because the runtime is deterministic by design (§5) and
ships as a libretro core, multiplayer netplay works essentially for
free — no netplay engineering by the console project itself.

**Netplay UI is frontend-owned, not cart-owned.**

All netplay session management lives in the frontend, not in cart
code:
- Starting a netplay session (host).
- Browsing for available sessions (LAN or internet lobby).
- Joining a session.
- Configuring input delay / rollback parameters.
- Assigning player slots to connected clients.
- Disconnection handling.

The cart has no netplay awareness. It starts up normally, reads
inputs normally, runs its simulation. The only netplay-related API
is `console.input.local_player()` — returns which player slot this
machine represents — and that's for per-player rendering (see below),
not for any netplay session management.

**Why this model:**

- Cart authors don't become network programmers. Netplay is notoriously
  hard to get right; centralizing it in the runtime/frontend means
  every deterministic cart supports netplay automatically without
  cart code changes.
- Consistent netplay UX across all carts — one host/join UI the
  player learns once.
- Clean separation of concerns: cart owns the simulation; frontend
  owns the player's interaction with the runtime (including netplay
  session management).

**The cart's view of the world is constant regardless of netplay
state.** A cart cannot tell whether it's running single-player, local
multiplayer, or netplay multiplayer. Inputs arrive on the correct frame
for each player regardless of source. Everything else runs identically.

**Cart metadata for netplay UI (in manifest, not code):**

The frontend uses cart manifest data to present netplay UI — cart name,
icon, supported player count, etc.

```yaml
# cart.info.yaml
title:       Party Brawler
min_players: 2
max_players: 4
```

The frontend reads this before loading to populate lobby displays
("4-player party game") and validate connection requests (don't let
player 3 join a cart declared max_players = 2). Cart runtime code
doesn't serve this — it's declarative metadata, separate from
simulation behavior.

**How libretro netplay works:**

Deterministic lockstep: both players run the same cart locally; each
machine runs the full simulation; only inputs are exchanged over the
network. Both simulations advance in lockstep, reaching bit-identical
states. Each player's screen shows their own machine's rendered output,
but both machines render the same game state.

Bandwidth is minimal — input state per frame (a few bytes per player)
at 60 Hz is ~1 KB/s. Works on poor connections where video streaming
would fail.

Two modes supported by libretro:
- **Input delay:** both players delay their own inputs by N frames,
  letting the other player's inputs arrive in time. Feels laggy but
  always synchronized. Simpler; fine for turn-based or slower games.
- **Rollback:** each machine predicts the other's inputs, simulates
  forward, and rolls back + re-simulates when actual inputs arrive.
  Feels responsive; requires fast save state + restore (which the
  runtime already provides). Better for action games.

**What libretro / RetroArch provides:**
- Peer discovery, lobby, and connection (TCP/UDP, relay servers).
- Input exchange protocol.
- Rollback state management (using the core's serialize/unserialize).
- Latency measurement and configuration.
- Spectator mode.

**What the console provides to enable it:**
- Deterministic simulation (already specified).
- Fast serialize/deserialize via tracked state regions (already
  specified).
- Libretro input API compliance — per-player input indexing, exposing
  the supported player count.

**Cart-level API addition:**

```lua
-- Returns which player slot the local machine represents (1-4).
-- For single-player or local-only multiplayer, always returns 1.
local me = console.input.local_player()

-- Read any player's input normally:
for p = 1, 4 do
    if console.input.button_pressed(p, BUTTON_A) then ... end
end
```

The `local_player()` function lets carts render the right view for
asymmetric games (first-person, per-player information). Most games
are symmetric and ignore it — both players see the same game world.

**Per-player views (competitive multiplayer support):**

The netplay architecture supports competitive games where each player
sees their own perspective — shooters with first-person cameras,
platformers with per-player camera following, racing games with
behind-the-car views, sports games centered on the local team, RTSes
with per-player fog of war.

How it works: both machines run the *same simulation* (so all game
state is identical on both machines); each machine renders *from its
local player's perspective* based on `local_player()`.

```lua
function draw()
    local me = console.input.local_player()
    draw_world_from_camera(players[me].camera)
    draw_hud_for_player(me)
end
```

For split-screen local play on a single machine, the cart renders
both views — same cart code, called twice with different viewports.
For netplay, each machine renders only its own player's view. The
cart doesn't branch on "netplay vs. local"; it just renders based
on `local_player()`.

**Hidden information / fog of war — partial support.**

The lockstep netplay model runs *identical simulation state* on both
machines. For games with genuinely hidden information (RTS fog of war,
hidden card hands, stealth-based positioning), both machines know the
full state — the hidden-from-opponent data is in memory on their
machine too.

Two implications:

1. **Cart-side view filtering works for honest players.** The cart
   can choose not to render hidden information. For casual / friendly
   competitive play, this is enough — the opposing player sees
   appropriate fog of war, hidden card hands display as face-down,
   etc.

2. **Determined cheaters can defeat this.** A player running a
   modified runtime can render the hidden state anyway. The lockstep
   architecture doesn't prevent this.

This is a known limitation of peer-to-peer deterministic lockstep
netplay (Age of Empires, StarCraft, fighting games all have this
property). Client-server architectures with authoritative servers
are needed for robust anti-cheat; that's a different architecture
entirely and out of v1 scope.

**Guidance for cart authors:**

- Games with *perspective-based* differences (camera, view, HUD) work
  perfectly — both players see the same world, just from their own
  vantage point.
- Games with *information-based* differences (fog of war, hidden
  hands) work for friendly play via client-side filtering. If the
  game is seriously competitive and anti-cheat matters, a different
  architecture than libretro netplay is needed.
- Games with purely symmetric information (racing, fighting,
  sports with full-visibility) have no concerns — both players
  legitimately see the same state.

**Player count:**

V1 supports up to 4 players — local multiplayer on one device (up to
4 connected gamepads) or netplay (1v1, 2v2, free-for-all up to 4).
Libretro netplay handles the network coordination for higher player
counts; carts use the per-player input API with player indices 1-4.

```lua
-- Returns which player slot the local machine represents (1-4).
-- For single-player or local multiplayer, always returns 1.
local me = console.input.local_player()

-- Read any player's input:
for p = 1, 4 do
    if console.input.button_pressed(p, BUTTON_A) then ... end
end
```

**Cart design considerations for netplay:**

- **Pointer input** is associated with the local player. Cart should
  treat `console.input.pointer_*()` as player `local_player()`'s
  pointer, not a shared pointer.
- **Per-machine local state** (volume, accessibility settings) must
  not affect the simulation. Already a deterministic design principle;
  called out explicitly for netplay.
- **Vibration / toast-style notifications** (if added in future) should
  fire only on the relevant player's machine, not both. Runtime handles
  this by not including them in the deterministic simulation path.

**Discovery via RetroArch's lobby:**

RetroArch has a public lobby server at `lobby.libretro.com`. Hosting
a netplay session with "Publicly Announce" enabled registers the
room on this central lobby; clients refresh their in-app lobby list
to see and join rooms with one button press. The lobby is purely a
discovery mechanism — actual game traffic is peer-to-peer.

Rooms are identified by core + game. For the console, the game
identifier is the BLAKE3-256 hash of the loaded cart binary. Players
need byte-identical carts to join a room. The cart's display title
(taken from `cart.info`) is also broadcast for UI; only the hash is
used for matching.

NAT traversal: RetroArch uses UPnP to open the host's port (TCP 55435
default). If UPnP fails, a "Relay Server" option proxies traffic
through public relays at the cost of some latency. Handles the
typical "can't port-forward" case without requiring user effort.

Private rooms: host disables public announce or sets a password;
clients connect via IP directly. For friend-group play.

Discord (operated by the RetroArch community) is the informal
social layer — "anyone want to play X in 10 minutes?" — with the
lobby handling the technical connection.

**LAN discovery (same-network play):**

Separately from the internet lobby, RetroArch broadcasts netplay
sessions on the local network via UDP. Clients on the same LAN
doing "Refresh Netplay Host List" see LAN hosts at the bottom of
the list (typically shown as "Local: Anonymous") alongside internet
lobby rooms.

This handles the classic "friends together in a room, each with their
own phone, all running RetroArch" scenario: no internet required, no
manual IP entry, no lobby server, automatic discovery. Each device
on the same wifi sees the others' hosted sessions when refreshing
the lobby.

**Because the console ships as a libretro core, both of these work
automatically** for players running the core through RetroArch. No
netplay discovery infrastructure to build.

**Custom libretro frontend netplay support:**

The custom frontend is a libretro frontend, not RetroArch, so it
doesn't automatically inherit RetroArch's lobby UI. Three options:

- **Option A: Defer netplay to RetroArch in v1.** Custom frontend
  doesn't expose netplay; players wanting netplay launch the core
  through RetroArch. Simple; both internet lobby and LAN discovery
  already work there.
- **Option B: Implement LAN discovery only in the custom frontend.**
  LAN discovery is a small amount of code (UDP broadcast + listen)
  and arguably the highest-value netplay feature — "friends in the
  same room want to play together" is the common case. Internet
  lobby support stays deferred to RetroArch. Interesting v1 middle
  ground.
- **Option C: Use `lobby.libretro.com` from the custom frontend.**
  The lobby server has an open protocol; the custom frontend can
  query it, filter for the console's core, show matching rooms in
  its own branded UI. Combined with LAN discovery, gives full netplay
  parity with RetroArch through branded UX.

**v1 decision: Option B — LAN-only netplay in the custom frontend.**

The custom libretro frontend implements LAN discovery and
host/join UI for same-network multiplayer. Internet lobby
(`lobby.libretro.com`) is not integrated in v1; players who want
internet netplay use RetroArch.

Rationale: LAN netplay covers the highest-value use case — friends
together in the same room, each with their own device, wanting to
play together. This is the natural multiplayer scenario for a
fantasy console and deserves first-class support in the custom
frontend. Internet lobby is more complex (NAT traversal, relay
fallbacks, lobby protocol integration) and its users are enthusiasts
happy to use RetroArch.

**What v1 builds:**

*LAN discovery:*
- UDP broadcast on a chosen port, periodic (~1-2 second intervals)
  while hosting.
- UDP listener on the same port, receiving broadcasts while browsing.
- Broadcast payload: cart binary hash (BLAKE3-256), cart title (display
  only, taken from `cart.info`), host name, player count (current/max),
  host IP.
- Listener maintains a list of discovered hosts with last-seen
  timestamps; hosts aged out after ~5 seconds without broadcasts.

*Host/join UI:*
- "Host multiplayer session" option in the frontend menu. Once a
  cart is loaded, starts broadcasting and listening for join requests.
- "Join LAN session" option. Scans for broadcasts; shows available
  sessions (cart name, host name, current player count); select to
  join.
- Player connection screen. Host sees "Player N connected"
  notifications as clients join; can start the session when ready.
- In-session status indicator showing number of connected players
  and connection quality.
- Disconnection handling. Client disconnect mid-session: pause,
  show rejoin option. Host disconnect: clients see "session ended."

*Cart compatibility check:*
- Join attempt includes the joining client's cart hash.
- Host accepts only when hashes match exactly. Mismatch returns a clear
  error ("your cart does not match the host's cart"). Hashing the loaded
  binary is stricter than a developer-declared id+version — there is no
  way for two builds to claim the same identity, and authors do not need
  to remember to bump a version field.

*Connection protocol:*
- Uses libretro-common's netplay infrastructure for input exchange
  and rollback — not reimplementing that.
- The LAN discovery + join UI layer is custom; the network protocol
  underneath is libretro-common.

**Scope estimate:** ~1000-1500 lines of frontend code (~2-3 weeks).

**Explicitly not v1:**
- Internet lobby integration (Option C from above).
- Relay server fallback for NAT traversal.
- Discord or other social integration.
- Spectator mode UI (libretro supports it; exposing in frontend UI
  is deferred).

### Decision: Join during lobby phase; player count fixed once session starts.

Libretro architecturally supports drop-in / drop-out of non-host
players mid-session (new players get caught up via save state
synchronization; leaving players' slots open up). In practice, though,
libretro has long-standing bugs specifically around 3+ player
drop-in/drop-out (issues #4955, #7767, #6287 in libretro/RetroArch),
and — more importantly — mid-session joining creates cart-design
complexity (where does a new player spawn? what's their starting
state?) that most carts don't naturally handle.

**v1 scope:**

*During lobby phase (before cart starts):*
- Any number of players (up to 4) can join.
- Host can start the session when ready.
- Players can leave the lobby freely.

*During play:*
- Player count is fixed at what it was when the session started.
- If a player drops out (disconnect, quit), their input stream goes
  to zero — cart sees them as "not pressing anything."
- If the host drops out, session ends.
- No new joiners mid-session.

This matches how most couch multiplayer naturally works ("everyone
ready? let's go!") and avoids both libretro's rough edges and cart
authoring complexity.

**Cart API for connection status:**

Carts that want to handle drop-out gracefully check connection state:

```lua
if not console.input.is_player_connected(3) then
    -- handle player 3 drop-out: AI takeover, pause, remove from match
end
```

Carts that don't check get default behavior (disconnected player
reads as no input — safe, unremarkable). Consistent with "cart cannot
tell it's running netplay" — a disconnected player is indistinguishable
from a local player who isn't pressing anything.

**Deferred to v2+:**
- Mid-session joining (with cart opt-in via manifest declaration).
- Spectator mode UI.
- Natural-break-point joining (cart signals "it's safe to add/remove
  players now" during match transitions).
- Host handoff if host leaves (rather than ending the session).

### Decision: Netplay sessions start fresh; saves are cart-author's responsibility.

Netplay sessions don't automatically load or save cart-managed saves.
Each netplay session starts the cart fresh; session ends when the
cart exits; nothing persists to individual cart save slots.

**Rationale:**
- Simplest model; avoids edge cases around "whose save does the
  session use?" or "what happens if players have different progress?"
- Matches how typical couch-multiplayer works (nobody saves a
  Bomberman session; you just play and stop).
- Per-session save/load is cart-author territory — carts that want
  to support multiplayer persistence can do it themselves via the
  cart save API, with whatever rules they want (host's save wins,
  vote to save, etc.).

Save states and rewind still work normally during netplay — these
are runtime state snapshots, not cart saves, and libretro handles
them across the netplay session (all participants rewind together).

**Interaction with existing multiplayer decisions:**

- **Per-player views:** works normally via `local_player()` — each
  machine renders its own view in LAN netplay exactly as in RetroArch
  netplay (§8).
- **Hidden information limits:** same caveats apply (§8) — client-side
  filtering is the only protection; determined cheaters bypass. LAN
  play among friends is the target use case; robust anti-cheat is
  out of scope.
- **4-player support:** LAN discovery handles multiple clients
  broadcast-joining a single host; up to 4 players naturally.
- **One device, one player:** still applies on each participating
  machine. A single device with 4 gamepads hosts 4-player local play;
  across LAN, each device hosts 1-2 players depending on its own
  attached controllers.

**Browser netplay is a separate problem.**

Browsers can't use TCP directly or do LAN broadcast (security
restrictions on mDNS/UDP broadcast from web contexts). Browser
netplay needs WebRTC with a signaling server for connection setup.
Deferred to v2+.

Discovery options for browser netplay when built:
- **Link sharing with signaling:** host gets a URL with a short code;
  others enter the code or follow the link; lightweight signaling
  service matches them. Works on LAN or internet.
- **QR code joining:** host displays QR code with session info;
  nearby devices scan to join. Particularly nice for "same room,
  each has their own phone" — visual, no typing, no prior setup.
- **Explicit IP entry:** fallback for enthusiasts who know what
  they're doing.

QR code joining is an especially appealing UX for mobile browser
netplay; worth flagging as the preferred path when browser netplay
is built.

**Mobile netplay:**

Mobile via RetroArch works normally (RetroArch's mobile apps support
netplay including both the internet lobby and LAN discovery). Mobile
browser via WebRTC is v2+, same as desktop browser.

---

## 9. Cart Format

### Decision: Unified RISC-V ELF format for all carts.

All carts are RISC-V ELF binaries, regardless of whether they are "native"
carts (written in C/Rust/Zig/etc.) or "Lua" carts. One format, one loader,
one distribution story.

**Properties:**
- Single file (`.cart`).
- Standard ELF structure (RV32IMFC, little-endian, statically linked).
- Resources embedded in ELF sections under the `.cart.*` namespace.
- Metadata distributed across `.cart.info` (frontend-facing — title, author,
  API version, size class, etc.) and `.cart.config` (runtime-facing — state
  buffer schemas, voice groups, achievements, etc.); see ADR-0073.
- Size-class-based caps (see "Size-class-based cart caps" decision below).

**ELF section conventions:**
- `.text`, `.data`, `.bss`, `.rodata` — standard code and data.
- `.cart.info` — frontend-readable cart metadata, FlatBuffers compiled from
  `cart.info.yaml` (ADR-0073).
- `.cart.config` — runtime-readable configuration (state buffer schemas,
  voice groups, achievements, etc.), FlatBuffers compiled from
  `cart.config.yaml` (ADR-0073).
- `.cart.lua` — Lua-specific runtime configuration, present only in Lua
  carts; read by `liblua.rv32` (ADR-0073).
- `.cart.resources` — single section containing a directory of named
  resources (sprites, tilemaps, tracker modules, sample sets, Lua
  source, etc.), accessed by name through the resource API.

**How the runtime loads a cart:**
- On hardware: mmap the cart file; standard ELF loader sets up memory
  regions per program headers; resources are pointers into the mmap'd
  region (zero-copy access).
- In emulator (desktop/browser): ELF loader parses; cart code and data
  are copied into the RISC-V interpreter's guest memory; resource
  sections are mapped to known guest addresses.

**Rationale:**
- ELF is battle-tested, widely understood, with mature tooling (binutils,
  objcopy, linker scripts). Debuggers (GDB, LLDB) understand ELF and
  DWARF debug info natively, which is load-bearing for the debugging
  story.
- One format simplifies the runtime's loader, the packer, and authoring
  documentation.
- Resources in ELF sections enable zero-copy mmap on hardware — a real
  performance and simplicity win over a custom container format.
- Language extensibility is natural: any language that targets RV32IMFC
  ELF can be a cart; there's no "Lua is special" in the format itself.

### Decision: Lua carts are ELF carts that use the runtime's Lua-host API.

The runtime exposes a minimal Lua-host API via ECALLs — shaped like the
Lua C API, but routed to the runtime-owned Lua interpreter. The SDK
provides a cart-side shim library (`libconsolelua`) that wraps these
ECALLs so authors see normal-looking Lua C API calls.

A Lua cart's native side is small boilerplate:
1. Read embedded Lua source from a resource section.
2. Create a `lua_State` handle via the Lua-host API (ECALL).
3. Load and execute the source (ECALL).
4. Forward the standard frame callbacks (`init`/`update`/`draw`) into
   Lua via the Lua-host API (ECALL).

The SDK ships this boilerplate as a default cart template. Authors
writing pure Lua never see the native shim; they write their `.lua`
files, the packer produces the final cart ELF.

**Why this matters for debugging:** the `lua_State` lives in the
runtime's native address space, not in guest memory. This means the
runtime can use the full Lua C API (`lua_sethook`, `lua_getinfo`,
`lua_getlocal`, etc.) to implement a first-class debugger. See section 22.

**Rationale:**
- Single cart format preserved; no two-format dispatch.
- Runtime-owned Lua means one blessed version, centrally updateable,
  fully debuggable.
- Lua carts stay small: just the shim (a few KB), the Lua source
  (typically a few KB to 100KB), and resources.
- ECALL overhead for Lua operations is amortized — the shim is called
  by the cart's boilerplate a handful of times per frame, and everything
  within Lua (which is most of the execution) runs at full speed in the
  runtime's native Lua VM.

**Trade-off:** Carts that heavily interleave Lua and cart-side native
code pay per-call ECALL overhead for every Lua C API operation. This
affects a narrow slice of carts (those optimizing hot functions in
native while keeping game logic in Lua); the common case (all-Lua carts)
is unaffected.

### Decision: Lua-host API is a minimal subset of the Lua C API.

The runtime exposes roughly 10-15 core functions shaped like the Lua C
API — enough to create states, load source, call functions, pass basic
values, get return values. Authors who need more reach for escape hatches
(e.g., "evaluate this Lua source string and return results"). Full Lua
C API compatibility is not a goal; common-case Lua carts are.

### Decision: Per-resource compression (zstd) with packer-driven choice.

Assets in `.cart.resources` may be individually compressed to fit more
content in the 16MB cart budget. Typical compression: 2-4x across a
typical cart, meaning a 16MB cart can hold 32-60MB of effective content.

**Structure:**
- The `.cart.resources` directory (resource metadata, indices, names)
  is always uncompressed and mmap-friendly. Resource discovery is fast
  and allocation-free.
- Individual resource bodies have a per-resource compression flag:
  `none` or `zstd`.
- The **packer decides per-resource** based on measured compressibility.
  Already-compressed content (tracker modules, ADPCM/QOA audio, most
  PNG-sourced sprites) ships uncompressed. Text (Lua source, manifests),
  tilemaps, sprite sheets produced from uncompressed sources, and
  native code ship zstd-compressed.
- Authors don't configure per-resource; the packer evaluates
  compressibility and picks the best option. Threshold: ship compressed
  if compression saves >5% size.

**Runtime behavior:**
- Uncompressed resources: returned as pointers directly into the mmap'd
  (or WASM-imported) cart bytes. Zero-copy, instant.
- Compressed resources: decompressed into runtime-allocated memory on
  first access; cached for subsequent accesses.
- Decompressed resource memory counts against the cart-visible 16MB
  working memory budget. Memory accounting API reports "X MB of
  decompressed resources in cache."
- Optional cache eviction in v2: if cart's decompressed cache approaches
  the budget cap, runtime evicts least-recently-used resources. V1 keeps
  decompressed resources resident.

**Algorithm choice: zstd at medium compression level.**

Why zstd:
- 3-5x compression ratios typical for game content (better than LZ4's
  2-3x).
- Fast decompression (~500MB/s+ on modern CPUs, adequate on Pi-class
  hardware at ~100-200MB/s).
- Mature, stable format. Library is battle-tested.
- Decoder is ~50KB of code — negligible against a runtime binary
  already containing Lua VM, RISC-V interpreter, SDL, etc.
- Widely supported toolchain and library availability across all
  target platforms.

**Why not LZ4:** simpler and faster decode, but compression ratios are
meaningfully worse (2-3x vs. 3-5x). For storage-constrained carts, the
ratio matters more than the marginal decode speed difference. Also,
zstd's decode speed is already plenty fast for the use case.

**Why not whole-cart compression:**
- Would break mmap — cart index needs to be readable without
  decompressing the whole file.
- Memory blow-up: a 16MB compressed cart with 50MB expanded content
  can't fit whole-expanded in the 32MB runtime budget. Per-resource
  compression with lazy decompression handles this naturally (only
  active working set is expanded).
- Can't skip decompression for already-compressed content.

**Rationale:**
- Typical cart shrinks 30-50% on disk. Meaningful distribution benefit.
- Browser carts benefit especially (network bandwidth matters). Cart
  loads faster, "time to playable" drops noticeably.
- Cart index stays mmap-friendly — resource discovery and metadata
  access are fast.
- Pay-as-you-go: decompression cost only for resources actually used.
- Invisible to cart authors; packer handles all compression decisions.
- Distribution size (cart file) is decoupled from runtime working set
  (decompressed cache), so cart authors get more content budget without
  runtime memory pressure.

### Decision: Carts can explicitly release loaded resources.

Long-running carts with phase-specific resources (cutscenes, level
intros, boss fights with unique assets) should be able to reclaim
memory when those resources are no longer needed. The resource API
supports explicit release alongside GC-driven cleanup.

**API:**
```lua
local hero_sprites = console.resource.load("hero_sprites")
-- ... use it ...
console.resource.release(hero_sprites)
-- handle is now invalid; re-load returns a new handle
```

- `load` is idempotent: loading an already-cached resource returns
  quickly without re-decompressing.
- `release` is advisory: tells the runtime "cart no longer needs
  this." Runtime may or may not immediately free the decompressed
  bytes — keeping recently-released resources cached enables fast
  re-load if the cart changes its mind.
- Actual eviction policy is the runtime's decision. V1: evict on
  memory pressure only. V2: consider LRU with explicit hints.

**Lua GC integration:** Resource handles are userdata with `__gc`
metamethods that call `release` on collection. Carts that don't
manage lifecycles explicitly still get reasonable behavior — handles
dropped from Lua scope eventually release. Authors who want precise
memory control call `release` explicitly (recommended for large
resources like cutscene art).

**Handle stability across save/restore:** Resource handles encode
logical identity (resource name + load generation), not physical
address. Save state preserves which resources were loaded; restore
re-establishes the physical decompression mapping. This makes
handles safe to store in state buffers and Lua tables — they survive
save/load, hot reload, and cross-platform transport.

**Uncompressed resources:** `release` on an uncompressed mmap'd
resource is a no-op — no memory is allocated to free. The API
behaves the same; the cart doesn't need to know whether a resource
is compressed.

### Decision: Persistent resources declared in cart manifest.

Resources the cart always needs (primary font, main palette,
player sprites, UI elements) can be declared persistent in the
cart manifest:

```yaml
# cart.config.yaml
persistent_resources: [font_ui, palette_main, player_sprites]
```

Persistent resources load automatically at cart init and remain in
cache for the cart's lifetime. They don't need explicit load/release
management, and the runtime won't evict them under memory pressure
(allocation failures elsewhere happen first).

**Rationale:** Most cart resources are "always loaded" in practice.
Making authors explicitly `load` every resource at init, and not
forget to keep them loaded, is error-prone. Persistent resources
cover the common case declaratively, leaving explicit load/release
for the genuinely transient cases.

### Decision: Memory introspection API.

```lua
local mem = console.mem.stats()
-- mem.resource_cache_used  -- bytes of decompressed resources in cache
-- mem.cart_allocations     -- bytes of cart-allocated memory (state buffers, etc.)
-- mem.total_used           -- total cart-visible memory in use
-- mem.budget_cap           -- 16MB working memory cap
-- mem.resources_loaded     -- table of currently-loaded resources with sizes
```

Exposed to carts so they can make informed decisions about releasing
resources under pressure. Also shown prominently in the dev-mode
debug overlay so authors see memory usage during development.

**Deferred to v2:** Memory pressure callbacks (`on_memory_pressure(severity)`)
— notifies the cart when budget usage crosses warning / critical
thresholds, giving it a chance to release non-essential resources
before allocations fail. Nice pattern but not required for v1; authors
can poll `console.mem.stats()` in their update loop.

### Decision: Size-class-based cart caps.

Rather than a single cart size cap, carts declare a size class in
their manifest; the packer enforces the cap for that class. The
runtime treats all classes identically at runtime (same memory budget,
same API, same format) — the class just determines the maximum cart
file size on disk.

| Class | Cap | Typical use |
|-------|-----|-------------|
| **Demo** | 256 KB | Demoscene-style audiovisual intros, generative art, tiny experiments, coding exercises. Tight enough to be in the demoscene tradition; loose enough to be practically workable. |
| **Mini** | 2 MB | Short jam games, minimal art, chiptune/tracker only, Lua-only carts |
| **Standard** | 16 MB | Typical indie-scope game — full platformer, puzzle game, short RPG, adventure with text-only dialog |
| **Large** | 64 MB | Ambitious games with voice acting, rich music — Day of the Tentacle-scale talkies, voice-acted adventures, mid-length RPGs |
| **Flagship** | 256 MB | Full commercial-indie-scope — Grim Fandango-scale projects, RPGs with hours of voice, adventure games with extensive content |

**Cart manifest declaration:**

```yaml
# cart.info.yaml
size_class: large
```

**Rationale:**

- **Different games need different budgets.** A puzzle game fits in 2 MB;
  a full voice-acted adventure needs 60+ MB. A single 16 MB cap forces
  both into the same bucket — too loose for small games (no pressure
  to optimize), too tight for talkies.
- **Declared scope signals design intent.** A cart committing to Mini
  has made a deliberate discipline choice. A Flagship cart is declaring
  ambitious scope. Both are valid positions on the same console.
- **Discovery and cataloging.** Players can filter ("show me Mini carts
  for quick play" or "flagship-scope work"). Stores can organize by
  class. Reviews can contextualize ("a remarkable amount packed into
  Mini").
- **Cultural positioning.** Each class has a different cultural feel —
  Demo for demoscene and coding exploration; Mini for game-jam output;
  Standard for typical indie work; Large for voice-acted talkies; Flagship
  for commercial-indie-tier projects.

**The Demo size class in context:**

Demo at 256 KB sits deliberately close to demoscene-tradition tight-size
categories (4 KB, 64 KB "intro" tiers). After baseline overhead
(~20-30 KB for ELF + Lua shim), a Demo cart has ~200 KB of content
budget — enough for compressed Lua code, a small sprite atlas, and a
tracker module, while still requiring real discipline. The tightness
is the point: the constraint encourages procedural generation, compact
code, and creative use of the runtime's primitives.

If demand emerges for tighter demoscene-style sub-tiers
(`demo-64k`, `demo-4k`), they slot in without breaking existing carts.

### Decision: Interactivity is orthogonal to size class.

Carts separately declare whether they're interactive. Combined with
size class, this captures the cart's nature on two orthogonal axes.

**Manifest declaration:**

```yaml
# cart.info.yaml
size_class:  demo
interactive: false
```

**Two settings:**

- `interactive = true` (default): cart receives input per its
  `inputs_used` declaration (§8). Runtime displays appropriate control
  UI for the declared inputs (touch overlay, etc.).
- `interactive = false`: cart receives no input. Runtime captures
  Start for pause/unpause; ignores everything else. Touch platforms
  display no control overlay — full screen for the cart, small pause
  indicator when paused.

**Additional non-interactive-only manifest fields:**

```yaml
# cart.info.yaml — when interactive: false
duration_hint: 2m30s    # optional: approximate duration
loops:         true     # default true; false means defined ending
```

Loop behavior:
- `loops = true` (default): runtime restarts the cart when it completes
  (either by reaching a declared duration or cart calling an "end" API).
- `loops = false`: runtime displays a "complete" screen with option
  to restart when the cart finishes.

**Why orthogonal:**

Size class and interactivity capture genuinely different things:
- **Demo + non-interactive** = classic demoscene intro (tight size,
  runs to music, no input).
- **Demo + interactive** = tiny arcade game or micro-experience
  (1K/4K intro-style game).
- **Mini + non-interactive** = ambient music player, screensaver with
  more content than Demo affords.
- **Standard + non-interactive** = generative art piece, longer-form
  visual experience.
- **Flagship + non-interactive** = long-form art installation (unusual
  but valid).
- **Mini/Standard/Large/Flagship + interactive** = games as standard.

Coupling "non-interactive" to the Demo class would be overly restrictive:
separating the axes lets authors pick size and interaction model
independently. A non-interactive ambient piece with rich music naturally
wants Mini size; forcing it into Demo (256 KB) would be artificially
restrictive.

**Screensaver / ambient integration:** screensaver plugins, kiosk
attract modes, and curated ambient-display playlists look for
`interactive = false` carts regardless of size class. Makes the
screensaver pipeline format-agnostic across size classes (§18 deferred).

**Capacity calibration check — Day of the Tentacle-style talkie:**

A DOTT-scale voice-acted adventure game on this console:
- ~3.5 hours of dialog at 8 kHz ADPCM 4-bit: ~49 MB.
- At 11 kHz ADPCM (higher quality): ~68 MB.
- Music (tracker modules): ~4 MB.
- Cutscene-replacement art (no video format; use scripted sprite scenes): ~3 MB.
- Base game (code, sprites, tilemaps): ~6 MB.
- **Total: ~62-81 MB.**

Fits comfortably in **Large** at 8 kHz; slightly over at 11 kHz. Voice
compression choice is the author's design lever. Larger-than-DOTT
projects (Grim Fandango-scale at 7+ hours of voice) need Flagship.

**Runtime behavior is identical across classes.** The cart-visible
working memory budget (16 MB) is independent of cart file size. A
2 MB Mini cart and a 256 MB Flagship cart both run within the same
32 MB runtime budget; the class determines what fits on disk, not what
can run at once. The per-resource compression and release APIs (§9)
handle the streaming/working-set management for larger carts.

**Distribution implications:**

- **Browser load time** scales with cart size. Mini carts load near-
  instantly. Standard carts in a few seconds. Large carts take 10-30
  seconds on typical mobile connections. Flagship carts take minutes.
  Worth surfacing in cart metadata so players can set expectations.
- **Hardware storage.** A Milk-V Duo with a 32 GB SD card holds
  thousands of Mini carts, hundreds of Standard, dozens of Large, or
  ~100 Flagship. All reasonable.

### Decision: Major.minor API versioning.

Cart declares target API version in `cart.info.yaml` (compiled to the
`.cart.info` ELF section). Runtime refuses carts requiring a newer major
version. Minor bumps are backward-compatible feature additions.

---

## 10. Runtime Architecture

### Decision: Runtime as a library, multiple frontends.

```
libfantasyconsole (core)
├── Cart loading (ELF) and parsing
├── RISC-V interpreter (for emulated native cart execution)
├── Lua interpreter (runtime-owned; service to carts via Lua-host ECALLs)
├── Graphics (paletted framebuffer, palette application)
├── Audio (software mixer, format decoders)
├── Input (abstract button state)
├── State management and save/restore
├── Debug servers (DAP for Lua, GDB remote for native)
└── Public C API (~15-20 functions, frontend-agnostic)

Frontends (thin adapters, each ~300-500 lines):
├── SDL2 native app (desktop)
├── Libretro core (ships as .so/.dylib/.dll)
├── Emscripten/WASM + HTML (browser)
└── Hardware image (runtime as PID-1 under minimal Linux)
```

**Rationale:** Single core, multiple frontends is the right factoring and
enables libretro distribution essentially for free. Libretro compatibility
unlocks RetroArch's entire platform matrix (Switch homebrew, retro
handhelds, macOS via OpenEmu, Android, iOS, every Linux, every BSD) plus
netplay, save states, shaders, recording — all for free.

### Decision: Standalone and hardware frontends are custom libretro frontends.

The runtime is a libretro core. There are two distinct categories of
consumer for that core:

1. **RetroArch** — players who want the full RetroArch experience with
   all its features, system-wide settings, shader library, etc.
2. **Custom libretro frontends** — programs built by the console project
   that load the libretro core, use libretro's infrastructure (shaders,
   rewind, save states, input abstraction), and present their own
   branded UX.

The project ships its own custom libretro frontend for standalone and
hardware distribution. This is *not* RetroArch — it's a separate
program that happens to use the libretro API the same way RetroArch
does. Examples of this pattern in the wild: Phoenix, KoDi's
RetroPlayer, Lakka's launcher, Batocera's launcher, OpenEmu on macOS.

**What the custom frontend provides:**
- Branded boot experience (startup chime, splash, console identity).
- Cart picker UI (browse local carts, launch with single action).
- Settings menu (display scaling, shader selection, input config).
- Pause menu with save state and rewind access.
- Platform-specific display handling (window, fullscreen, hardware
  panel).

**What the custom frontend inherits from libretro:**
- **Shader support** — GLSL / Slang shader chains, including the
  common CRT / scanline / LCD shaders from the libretro shader
  library. Players who want retro visual treatment get it without
  duplicating shader infrastructure in the runtime.
- **Rewind** — libretro's rewind buffer handles state history;
  frontend just provides the UI for triggering it.
- **Save state management** — libretro's serialize/unserialize
  patterns cover the on-disk format; frontend provides the UI.
- **Input abstraction** — libretro's device API handles gamepad /
  keyboard / touch mapping uniformly, including rebinding.
- **Frame pacing** — libretro's timing infrastructure.

**Licensing: clean.**

libretro-common (the frontend-facing library) is MIT-licensed. RetroArch
itself is GPL-3.0, but a custom libretro frontend is not a RetroArch
user — it just links against libretro-common, which is MIT. The custom
frontend can be licensed however the project wants (MIT, proprietary,
etc.). Hardware distribution has no GPL entanglement. This is
significantly better than shipping RetroArch itself, which would trigger
GPL-3.0 distribution obligations.

**Architecture:**

```
libfantasyconsole (core library, shared across all frontends)
└── [as specified above]

Libretro core adapter (wraps core as libretro-compatible .so/.dylib/.dll)
└── Implements retro_* functions; used by RetroArch AND custom frontend

Custom libretro frontend (the console's own launcher)
├── Links libretro-common (MIT)
├── Loads libretro core via libretro API
├── Provides branded UX (cart picker, settings, pause menu)
├── Delegates shaders, rewind, save state to libretro infrastructure
└── ~2000-5000 lines (mostly UI code)

Distributions using the custom frontend:
├── Desktop native (SDL-based display backend)
├── Hardware image (framebuffer / DRM display backend)
└── Browser (WASM; libretro-in-WASM is feasible though less common)
```

**Trade-off accepted:** slightly larger binary (a few MB for
libretro-common) and slight complexity of conforming to libretro's
frontend model. In exchange: shaders, rewind, and input abstraction
for free, plus consistency with the RetroArch distribution path
(same core code runs in both contexts).

**Why not a simpler custom frontend with no libretro dependency:**

- Would need to build shader pipeline from scratch (or skip shaders).
- Would need to build rewind UI infrastructure from scratch (save state
  system exists in core, but rewind adds ring-buffer management and
  UI).
- Input abstraction would be custom; no reuse of libretro device
  remapping infrastructure.
- Browser frontend stays custom regardless (libretro-in-WASM is less
  ergonomic than direct Emscripten compilation of a minimal frontend).

On balance, libretro infrastructure is worth the small added complexity
for the feature set it provides.

### Decision: On hardware, minimal Linux (buildroot-based), runtime as PID-1-equivalent.

**Rationale:**
- Leverages Linux driver support (USB gamepads, audio codecs, display
  interfaces, SD card filesystem). Avoids writing per-board drivers.
- Portable across SBCs: "does it run Linux?" is usually yes.
- Linux invisible to end user — boot goes directly to runtime,
  no shell, no desktop. Users perceive a fantasy console.
- Boot time 3-5 seconds with tuning; acceptable for home-console feel.
- Developer access via SSH / remote debugging available when enabled.

**Explicitly not choosing:**
- Full desktop Linux distribution (breaks fantasy).
- Bare-metal (driver work is weeks per board; not v1 scope; future
  option for dedicated hardware product).

### Decision: Main loop is fixed-timestep with accumulator.

**Model:**
- `update(dt)` always receives `dt = 1/60`, called at logical 60Hz.
- Runtime maintains logical-time accumulator.
- Per iteration: add elapsed real time (capped at 250ms to prevent
  death spiral); while accumulator ≥ 1/60 and updates-this-frame < 3,
  call `update`; then `draw`; then present with vsync.
- Cap of 3 catch-up updates per render: mild overrun absorbed invisibly,
  severe overrun degrades to slow-motion gameplay.
- Watchdog: single `update` call exceeding hard limit (e.g. 2 seconds)
  terminates cart with diagnostic. Enforced via Lua instruction-count
  hook; emulator-level for native; acknowledged limitation on bare RISC-V
  hardware.

**Rationale:** Deterministic (needed for save states, rewind, replay,
netplay), graceful degradation under load, survivable handling of buggy
carts. Libretro frontend delegates frame pacing to RetroArch; the fixed
timestep logic remains in the core for other frontends.

---

## 11. Sandboxing

Carts are sandboxed at two distinct layers, both enforced by the runtime.

### Layer 1: RISC-V ABI sandbox (applies to all carts).

Every cart is a RISC-V ELF running inside a controlled execution
environment. The cart can only affect the outside world via ECALL
(invoking the console API). Memory is bounds-checked by the RISC-V
interpreter on emulated platforms; on real RISC-V hardware the runtime
arranges memory layout and relies on Linux userspace isolation for the
cart process.

The cart cannot:
- Access host filesystem (no syscall for it).
- Open network sockets (no syscall for it).
- Read memory outside its allocated regions.
- Execute privileged instructions.
- Escape via side channels (any API call with external effects goes
  through the console ECALL surface, which is the full audit boundary).

This sandbox applies uniformly to Lua carts (whose cart-side native
shim is still a RISC-V ELF) and to carts written in any other language.

### Layer 2: Lua environment sandbox (applies to Lua carts' script).

The runtime-owned Lua interpreter runs with a stripped standard library:

**Removed:** `os`, `io`, `package`, `require`, `debug` (mostly),
`loadfile`, `dofile`, anything that reaches the host filesystem or OS.

**Kept:** `string`, `table`, `math` (with console's deterministic
versions of transcendentals), `coroutine`, basic globals (`ipairs`,
`pairs`, `type`, `tostring`, `tonumber`, etc.).

**Console-provided:** Graphics, audio, input, state, resource loading,
time, RNG, deterministic math.

Since the Lua interpreter is in the runtime's address space (not in
guest memory), sandboxing is enforced directly at VM configuration —
`package` is simply not loaded, dangerous globals are not defined.
Authors cannot escape by reaching into C via the debug library because
it is not exposed.

### Combined effect:

A Lua cart is sandboxed both at the Lua level (what the script can
access in its VM) and at the RISC-V level (what the cart's native shim
can do). A native cart is sandboxed only at the RISC-V level, which is
sufficient — it has no VM-level concerns.

---

## 11a. Lua Performance Strategy

### The hybrid question

Authors writing Lua carts will inevitably want to drop to native code for
performance-critical paths (particle updates, pathfinding, noise generation,
collision broad-phase, etc.) without rewriting the entire cart.

### Decision: two-layer approach, with v1 leaning on rich APIs and v2
optionally adding inline native code.

**Layer 1 (v1): Rich native-implemented primitives in the console API.**

The runtime exposes a deliberately generous set of primitives, all
implemented in native C in the runtime itself:
- Particle systems (create/update/render thousands with simple data
  descriptors, not per-particle scripts).
- Tilemap rendering (scrolling, layers, parallax).
- Sprite batch submission.
- Spatial queries over typed buffers (rect/circle queries, raycasts,
  nearest-neighbor).
- Collision primitives (AABB, circle, point-in-polygon, swept).
- Tween/ease library.
- Pathfinding (A* over grids).
- Procedural noise (Perlin, simplex, value noise, FBM).
- Bulk vector math over typed buffers (add, scale, map-with-predefined-op).

Together these cover ~80-90% of performance-sensitive hot paths in typical
games. Lua carts rarely need to leave Lua when these primitives are
aggressive enough. Matches PICO-8's "use the built-in primitives"
philosophy successfully applied at a larger scope.

**Layer 2 (v2+, if real usage justifies): Inline native code in Lua carts.**

The unified ELF format makes this natural rather than a new mechanism:
a Lua cart's ELF already contains native code (the boilerplate shim).
Additional native functions in the same ELF, callable from Lua via the
existing Lua-host API, is a natural extension.

The packer would accept `.c` / `.rs` / `.zig` files alongside `.lua` in
a project directory, compile them to RISC-V, link them into the cart ELF
with the shim, and generate Lua bindings so the Lua side sees them as
callable functions.

**API shape rules that enable both layers efficiently:**

- **Bulk operations on typed buffers, not per-element.** A call like
  `buffer_update_positions(pos_buf, vel_buf, dt)` amortizes both Lua stack
  overhead and ECALL overhead across N elements. Per-element callbacks
  from Lua force per-element overhead and kill performance.
- **Closed set of operations for bulk ops.** Don't accept arbitrary Lua
  functions as per-element callbacks. Provide parameterized operations
  with known signatures the runtime can SIMD-vectorize.
- **State buffers accessible by raw pointer from native code.** Layout
  descriptors produce predictable memory layouts; native functions in a
  cart's ELF can cast buffer handles to typed pointers and iterate
  directly, without going through the metatable layer that Lua uses.

These rules are v1 design decisions even though the v2 inline-native
feature isn't built. They ensure the v2 extension slots in naturally.

### Why not a full LuaJIT-style FFI

LuaJIT's FFI makes hybrid near-free because the JIT compiles Lua-to-C
calls to near-native overhead. Vanilla Lua (your choice, due to WASM
portability) doesn't have this — Lua-to-C calls go through the standard
Lua C API with real per-call overhead.

This is fine, *provided the API shape encourages bulk operations*. A
single call doing 10,000 elements of work has trivial relative overhead;
10,000 calls doing 1 element each have prohibitive overhead. Design the
APIs accordingly and the hybrid model works.

### Recommendation: design for it, implement later

For v1: do not build inline native code for Lua carts. Ship the rich
primitive library and let that carry performance-sensitive carts.

For the API header work in Phase 1: shape the Lua-host API and the state
buffer access patterns with Layer 2 in mind, so v2 can add inline native
without restructuring anything.

---

## 12. Resource Addressing

### Decision: Names in source, IDs at runtime, names preserved in debug builds.

Carts reference resources by stable string names in source code
(`load_sprite("hero_idle")`). The packer resolves names to integer IDs
at build time. Runtime sees IDs (fast lookups, compact). Debug builds
preserve the name table for error messages and introspection.

**Rationale:** Authoring ergonomics (names) plus runtime speed (IDs)
plus debuggability. Matches commercial engine convention.

---

## 13. RNG

### Decision: Multiple named seedable streams.

Runtime provides ~16 named RNG streams, each seedable independently.
Use case: separate gameplay randomness from visual-effects randomness
so cosmetic jitter doesn't affect game state or break replays.

**Implementation:** xoshiro128** or similar 32-bit PRNG. State per
stream lives in a tracked region, so RNG state saves/restores with
the rest of the cart state automatically.

---

## 13a. Default Assets

### Decision: Runtime ships with a bundle of default assets, always available.

Shipping good defaults does two jobs: it lowers the barrier for new
authors (first `console.text.draw(...)` call just works without supplying
a font), and it gives the console a distinct identity via signature
visual and auditory touches.

### Built into the runtime binary (cost: <70KB total)

**Fonts:**
- **`font_builtin` (8x8):** Primary text font. See "Font choice" below —
  the current plan is to ship the public-domain IBM CGA/VGA CP437 font
  as a reasonable default, with the option to replace it with a custom-
  designed font as a console-identity investment. 2KB either way.
- **`font_small` (4x6 or 3x5):** Compact ASCII micro-font for dense UI,
  debug overlays, crowded HUDs at 320×240. Sourced from a permissively-
  licensed public-domain pixel font (Cozette, Tamzen, or similar). <1KB.
- **`font_icons`:** Companion icon font with ~120-150 glyphs — see
  previous subsections for detail on glyph coverage, prompt-device
  awareness, and trademark considerations.

**Palettes:**
- **`palette_default` (the boot default):** A well-curated modern 256-color
  palette optimized for pixel art production — good tonal ramps, full
  hue coverage, comfortable neutrals, strong primary colors. Chosen so
  that "hello world" carts look good out of the box and authors using
  AI-generated or off-the-shelf sprite sheets get coherent results.
  Sourced from a permissively-licensed palette (AAP-256, Blk Neo 256, or
  similar) or designed custom for the console.
- **`palette_vga`:** IBM VGA default 256-color palette (EGA 16 + grayscales
  + 6×6×6 cube). Available for authors who want that specific look.
- **`palette_ega`:** EGA 16-color palette in standard slots, expanded
  across 256 entries with grayscale and utility colors.
- **`palette_cga`:** CGA 4-color palette expanded to 256 entries, for
  deliberate 4-color aesthetic work.

All palettes are 768 bytes each.

**System sounds:**
- **Startup chime:** Brief "console powered on" sound played by runtime
  on boot (authors can suppress via manifest flag). Defines a signature
  brand moment — this is a console-identity investment worth some care
  in its design. ~20KB.
- **Crash sound:** Played when a cart panics / hits a watchdog / fails
  to load. <1KB.

### Bundled with the SDK (optional, authors opt in per cart)

**SFX library:** ~200-500KB of general-purpose sound effects (pickup,
jump, hit, explosion, step, menu beep/blip, power-up) in QOA/ADPCM.
Lets new authors have audible prototypes before commissioning their
own sounds. Commissioned original set or sourced from CC0 libraries;
attribution documented.

**Example tracker modules:** A handful of "house style" pieces (title
theme, menu music, ambient) for prototyping. Demonstrates the console's
audio capabilities and gives prototypes a musical baseline. A few
hundred KB total.

**Placeholder sprite/tile sets:** Generic placeholder art for in-progress
carts — labeled untextured sprites, numbered tile atlases. Helps carts
look like *something* before the artist has drawn anything.

### Font choice: IBM default vs. custom identity font

The IBM CGA/VGA font is a reasonable default: public domain, widely
recognized, functional, free. It ships with the console as the current
plan.

However, a **custom-designed font** is a significant console-identity
investment worth considering:
- Gives the console a distinctive voice visible in every cart.
- Can be designed specifically for the console's visual language (e.g.,
  slightly friendlier letterforms, subtle quirks that distinguish from
  generic pixel fonts).
- Cost: a few hundred dollars with a pixel-font designer, or a week of
  focused work to draw one.
- Risk: without care, looks generic or amateur.

**Recommendation:** ship IBM font in early versions; commission or design
a custom font before v1.0 release if the project is progressing toward
a public launch. The investment differentiates the console meaningfully.

### Palette choice rationale

The default palette is the palette every new cart starts with. It's the
single decision that shapes the "hello world" first impression more than
any other.

- **Modern curated palette as default** (recommended): new carts look
  good out of the box, AI-generated or off-the-shelf pixel art fits the
  visual budget, authors aren't forced to design a palette before making
  a game. This is a **production-support** decision — it makes the
  console more useful for small teams.
- **VGA palette as default** (alternative): anchors the console visually
  to the Mode X / DOS era. This is an **era-reference** decision — it
  gives the console a specific identity at the cost of production-
  friendliness.

The current recommendation is **production-support** (modern curated
palette default), with era palettes available for authors who want them.
This aligns with the console's actual goals of supporting small-team
production rather than recreating a specific past era.

Authors who want the full VGA aesthetic swap palettes in a single line:
```lua
console.gfx.set_palette("palette_vga")
```

### Identity investments worth prioritizing

Beyond the defaults listed, a few investments would sharpen console
identity meaningfully:
- **Signature boot experience:** startup chime + brief visual moment
  when the runtime launches. PICO-8's rainbow is the reference example.
  Worth designing carefully; users see it on every session.
- **Custom primary font** (as noted above).
- **Default palette designed for the console** rather than adopted from
  an existing pixel-art palette. Gives the console a coloristic
  signature. Requires a color designer's time but is a one-shot
  investment.
- **Cart selector / menu UX** when the runtime launches without a cart.
  This is another "always seen" surface worth polishing.
- **Tonal voice in error messages and docs.** Small but real — PICO-8's
  error messages are part of its charm. Yours get to have a personality.

### Licensing notes

- IBM VGA/CGA fonts and palette values are public domain.
- Modern curated palettes require CC0 / CC-BY / permissive licenses
  with attribution captured in runtime documentation.
- Custom commissioned assets (fonts, SFX, music, palette) have
  documented licensing from commission agreements.
- All default assets are themselves documented — authors can see exactly
  what they're using and under what terms.

### Icon API and prompt-device awareness

**Drawing icons:**
```lua
console.text.draw_icon(x, y, "btn_a", color)
console.text.draw(x, y, "Press {btn_a} to jump", color)  -- inline substitution
```

**Abstract prompt API:** since the input model is abstract (carts see
"button A," not physical device), the runtime tracks current input device
and resolves abstract prompts to the right icon:

```lua
-- Cart writes device-agnostic prompts:
console.text.draw(x, y, "Press {prompt_action_a} to jump", color)

-- Runtime substitutes based on active input:
-- Keyboard:      shows keycap "Z" glyph
-- Generic pad:   shows button "A" glyph
-- Numeric pad:   shows button "1" glyph
```

Authors querying current style:
```lua
local style = console.input.current_prompt_style()  -- "keyboard" | "gamepad" | ...
local icon = console.input.prompt_for(BUTTON_A)     -- current icon name for abstract A
```

This elevates the console's feel significantly — seeing the actual key
you're holding rather than a generic indicator is a polish touch modern
games do, achievable here because input abstraction is already in place.

### Why ship icons as a companion font rather than extending `font_builtin`

- Preserves CP437 authenticity in the text font. Authors wanting pure
  DOS aesthetic get it unmodified.
- Clean separation of concerns: text fonts are for text, icon fonts for
  icons. Each is complete on its own.
- Companion font isn't bound to 8x8 if some icons benefit from different
  metrics later.
- Licensing stays clean — public-domain CGA font isn't mixed with
  icon creative work that has its own provenance.
- Authors can ship alternative icon fonts as cart resources using the
  same API; `font_icons` is just the default.

### Trademark note

Real console button shapes (PlayStation ○✕□△, Xbox colored A/B/X/Y,
Nintendo-style) are trademarked *as used for button identification on
gaming products*. The abstract geometric shapes themselves (cross,
triangle, square, circle) are not trademarkable — Sony cannot own the
letter X or a triangle — but specific stylizations in gaming-button
contexts are protectable trade dress.

**Safe path: ship geometric shape glyphs, not PS-style buttons.**

`font_icons` includes four neutrally-named "geometric primitive" glyphs:
- `geom_cross`, `geom_triangle`, `geom_square`, `geom_circle`.

Design rules that keep these clearly distinct from Sony's trade dress:
- **Visually stylized differently** (e.g., hollow outlined rather than
  solid, or asymmetric proportions, or internal texture). A consistent
  house style for all four — hollow with single-pixel border is a
  clean choice — makes them coherent and clearly not PS imitations.
- **Not using Sony's color associations** (pink/teal/green/blue). In
  monochrome UI text this is trivial; in colored contexts, use the
  console's own palette.
- **Named neutrally** — `geom_cross`, not `ps_x` or `playstation_button_x`.
- **Used for multiple purposes** beyond button prompts (list markers,
  warning indicators, dialog continuation, directional glyphs).
  Diverse use weakens any "identifies PlayStation button" reading.
- **Documented as general-purpose geometric glyphs**, not as PlayStation
  prompts.

The font-glyph-vs-button-image distinction matters: font glyphs in
running text are typographic symbols (like dingbats); button-framed
images with "PRESS" text are button-identification trade dress.

Authors who want PS-style prompts in their carts can opt into a prompt
style that maps abstract buttons to these geometric glyphs:
```lua
console.input.set_prompt_style("geometric")
```

Authors who want strictly-branded prompts ship their own icons as cart
resources, accepting their own licensing posture.

Xbox and Nintendo prompt conventions are handled similarly — the default
`font_icons` uses generic neutral A/B/X/Y and 1/2/3/4 designs that
establish the console's own visual identity. Brand-specific styles are
author-shipped content.

### Bundled with the SDK (optional, authors opt in per cart)

**SFX library:** ~200-500KB of classic game sound effects (pickup, jump,
hit, explosion, step, menu beep/blip, power-up) in QOA/ADPCM. Lets new
authors have audible prototypes before commissioning their own sounds.

**Example tracker modules:** A handful of "console style" tracker pieces
(title theme, menu music, boss theme, ambient) for prototyping. A few
hundred KB total.

**Placeholder sprite/tile sets:** Generic placeholder art for in-progress
carts — untextured sprites labeled with letters, tile atlases with
numbered tiles. Helps carts look like *something* before the artist
has drawn anything.

### Rationale

- The 50KB runtime overhead is negligible.
- Lowers friction for new authors — first cart can display text and
  draw colors without supplying any resources.
- Gives the console a consistent first-impression identity across all
  carts (signature boot experience, default palette, default font).
- All first-party defaults are public domain or permissively licensed;
  no legal encumbrance to redistribution.

### Licensing notes

- IBM VGA/CGA fonts and palette values are public domain (just bitmaps
  and RGB triples, no copyright).
- Modern curated palettes require CC0 / CC-BY / similar permissive
  licenses with attribution captured in runtime documentation.
- SFX bank either commissioned original or sourced from CC0 libraries
  (Freesound.org, OpenGameArt); attribution documented.
- All default assets are themselves documented — authors can see exactly
  what they're using and what the licensing says.

---

## 14. Platform Targets

| Platform | Execution model | Cart execution |
|----------|----------------|----------------|
| Native RISC-V (Milk-V Duo class) | Runtime runs on Linux; RISC-V cart binaries execute natively | Native |
| Desktop (x86-64, ARM64) | Runtime runs as SDL app | Lua: native; RISC-V carts: interpreted |
| Browser (desktop) | Runtime compiled to WASM via Emscripten | Lua: interpreted in Lua VM (itself in WASM); RISC-V carts: RISC-V interpreter (in WASM) |
| **Browser (mobile — iOS Safari, Chrome)** | Same WASM build + touch-control overlay | Same as desktop browser |
| RetroArch (all platforms including mobile) | Runtime as libretro core | Same as desktop |

**Cart format identical across all platforms.** One artifact runs everywhere.

### Smartphone support

Mobile is reachable via three independent paths, all using the same
cart format:

1. **WASM in mobile browser (primary v1 path):** The browser runtime
   already targets desktop browsers; mobile browsers use the same
   engine. Players visit a URL, the cart runs. Trivial distribution,
   no app store gatekeeping.
2. **RetroArch on iOS / Android (automatic bonus):** The libretro
   frontend ships as a core; RetroArch's mobile apps already handle
   touch, gamepad, storage, save states. Zero additional work.
3. **Native mobile apps (post-v1):** Purpose-built iOS / Android apps
   wrapping the runtime. Real engineering per platform but enables
   native app store distribution and per-cart UX polish. Community
   or future work, not v1.

The WASM-in-mobile-browser path is the interesting one because it's
both accessible and delivers a good experience — and it composes
naturally with hot reload (edit on desktop, test live on phone).

### Touch input design

The input challenge: your button model (d-pad + 4 face + 2 shoulders
+ Start/Select) doesn't translate directly to touch, particularly the
shoulder buttons. The runtime handles this via a library of declarative
touch control schemes plus graceful shoulder-button handling.

**Aspect ratio supports this design:** 4:3 was chosen partly because
it leaves natural room for touch controls in both orientations (see §3)
— controls live outside the game area rather than overlaying it.

**Touch control scheme library (runtime-provided):**

- **`virtual_gamepad_classic`** (default): overlay d-pad lower-left,
  face buttons lower-right. Portrait puts controls below the game
  (Game Boy-style); landscape puts them on left and right sides of
  the game. Universal fallback — works for any button-input cart.
- **`virtual_gamepad_modern`**: floating virtual stick (d-pad follows
  thumb) plus face buttons. Better for games with fluid directional
  input.
- **`tap_to_select`**: touches map to pointer events. For menus,
  point-and-click adventures, turn-based strategy. Requires cart
  cooperation (must read pointer input, not button input).
- **`vertical_scroller`**: tilt or swipe movement + tap for action.
  For shmups, endless runners, casual action.
- **`direct_manipulation`**: raw touch events exposed to cart. For
  puzzle games, point-and-click, games with custom touch behavior.

**Shoulders are always available, via the best mechanism each platform
provides.** Every cart has full access to all 10 abstract buttons
(d-pad + 4 face + 2 shoulders + Start/Select) on every platform. The
runtime provides the best shoulder input mechanism the platform supports:

- **Gamepad:** physical shoulder buttons (L1 / R1 or equivalent).
- **Keyboard:** the shoulder-mapped keys in the chosen preset (see §8).
- **Touch without external controller:** a runtime-provided touch
  mechanism for shoulder access — by default, small labeled L and R
  buttons in corner positions of the control area (not the game area).
  Thumbs can reach them with a small stretch; not as fast as physical
  shoulders but always functional.
- **Libretro frontend:** whatever the frontend maps to L1 / R1.

**Design contract:** cart authors design with all 10 buttons freely.
The runtime ensures every button has *some* input mechanism on every
platform. On touch, shoulder access is slightly clunkier than physical
buttons, but always available.

**Rationale for this framing:** treating shoulders as "optional on
touch" produces a predictable failure mode — authors treat shoulders
as not existing (since designing for optional inputs is less valuable
than designing for guaranteed inputs), and shoulders become vestigial.
Guaranteeing shoulders exist on every platform — even if awkward on
touch — lets authors design weapon cycling, camera control, run
modifiers, aim modes, and other standard uses without penalty.
Mobile-only players with shoulder-heavy games get a slightly worse
experience but can still play.

**Cart-declared touch shoulder style (manifest):**

```yaml
# cart.config.yaml
touch_scheme:         virtual_gamepad_classic
touch_shoulder_style: corner_buttons   # or long_press or custom
```

- `corner_buttons` (default): small L and R buttons in the corners of
  the control area. Discoverable, always visible, always clickable.
- `long_press`: no visible L/R buttons; long-press on face buttons
  invokes corresponding shoulders (hold A ≈ L, hold B ≈ R). Less
  screen clutter, more latency, less discoverable.
- `custom`: cart implements its own touch UI for shoulder access. The
  cart reads raw pointer events and programmatically dispatches
  shoulder button events for player-visible UI elements (e.g., a
  pie menu, context-sensitive buttons, gesture recognition).

Most authors use the default. Authors polishing for mobile first-party
experience choose `long_press` or `custom`.

The cart doesn't implement touch handling (for the default schemes).
The runtime implements the scheme, translating touches into abstract
input events the cart reads via its normal input API.

**Layout for mobile:**
- **Landscape:** 320×240 game fills vertical space (~63% of screen
  width); touch controls occupy side strips. No game occlusion; thumbs
  rest naturally on sides. Shoulder corner buttons at upper edges of
  side strips.
- **Portrait:** game spans full width at the top; touch controls
  occupy the lower ~50% of the screen. Game Boy-style layout.
  Shoulder corner buttons at upper corners of the control strip
  (nearest the game area).
- Cart author can declare portrait or landscape unsupported in the
  manifest if a particular orientation doesn't suit the game.

### Decision: Carts declare which inputs they use.

Cart manifest optionally declares the set of inputs the cart actually
reads. The runtime uses this to optimize touch overlay layout, hiding
unused controls entirely.

**Manifest syntax:**

```yaml
# cart.config.yaml
inputs_used: [dpad, button_a, button_b, start]
```

Or with labels for tooltips / prompts:

```yaml
# cart.config.yaml
inputs_used:
  dpad:     Move
  button_a: Jump
  button_b: Attack
  start:    Pause
```

**Group keywords** expand to their members: `dpad` = 4 directions,
`face_buttons` = all 4 face, `shoulders` = L and R, `system` = Start
and Select. Individual button names (`dpad_up`, `button_x`, etc.) work
too. Hybrid declarations combine both.

**Runtime behavior:**

- **Touch scheme:** hides unused controls entirely. A puzzle game
  using only d-pad + A gets a clean 5-button layout instead of the
  full 10-button overlay. Used buttons render larger and better-placed
  thanks to the freed screen real estate.
- **Gamepad / keyboard:** declaration is informational. Physical
  hardware isn't hidden, but prompt icons, input tutorials, and
  dev-mode overlays filter to the used set.
- **Labels** (if provided) appear in touch button labels, input prompt
  substitutions (`{action_a}` → "Jump"), and runtime-rendered tutorials.

**Shoulder handling interaction:** if `shoulders` isn't in `inputs_used`,
the runtime doesn't show shoulder buttons on touch at all. The
`touch_shoulder_style` manifest field becomes irrelevant for that cart.

**Start / Select special handling:** if a cart doesn't declare `start`,
the runtime captures Start for its own purposes (pause menu, save state
UI, input config). If the cart declares `start`, the runtime uses an
alternative gesture (three-finger tap, long-press elsewhere, or a
small always-visible system button) for its overlay.

**Pointer input declaration:** carts using pointer input declare it:
```
inputs_used = ["pointer", "start"]
```
The runtime can then adapt — a pointer-only cart might get no virtual
gamepad at all, with the full screen as pointer target.

**Declaration is a contract.** The cart receives events only for
buttons in its effective input set; buttons outside the set do nothing
(filtered by the runtime before reaching the cart). The effective
input set is:

- **If `inputs_used` is declared:** exactly the declared buttons.
- **If not declared:** the system default for the platform — currently
  the full 10-button set (d-pad + 4 face + 2 shoulders + Start/Select)
  on all platforms.

This means a cart without `inputs_used` receives events for all 10
buttons. A cart with `inputs_used` receives only its declared subset.
Either way, the set is well-defined and the runtime filters accordingly.

Dev mode warns if cart code reads a button not in its effective input
set — the declaration (if any) and the code are out of sync, meaning
either the declaration should be updated or the cart shouldn't be
reading the button.

**Declaration is optional but encouraged.** A cart without `inputs_used`
gets the safe default: full 10-button set, full virtual gamepad overlay
on touch. Carts that declare get a cleaner touch UI (unused buttons
hidden) and an explicit input contract. Authors who see their cart's
touch overlay with inert buttons visible will naturally be prompted to
declare.

**Design guidance:** declare accurately — list every button the cart
reads anywhere, not just the most-used ones. The declaration is a
union across all game states, so players see the same controls
throughout. If a cart uses different buttons in different phases and
the visual consistency matters, use a `custom` touch scheme to
implement phase-specific UI.

### Physical controllers on mobile (MFi, Bluetooth HID)

Mobile devices support standard physical controllers:
- **iOS/iPadOS:** MFi-certified controllers (Xbox Series X/S, DualSense,
  Backbone, Razer Kishi, 8BitDo, GameSir) pair via Bluetooth or
  USB-C / Lightning.
- **Android:** any Bluetooth HID or USB-C controller recognized by the
  OS (functionally the same set as iOS plus some additional budget
  options).
- **Phone-gripper controllers** (Backbone, Kishi, GameSir X2) turn a
  phone into a Switch-like handheld with full physical buttons
  including shoulders.

**These are available to WASM apps via the Gamepad API** — a W3C
standard supported by mobile Safari (iOS 14.5+) and mobile Chrome
(Android). The runtime's SDL2-based input handling transparently
exposes gamepads via the Gamepad API when compiled to WASM.

**Input precedence on mobile:**
- Physical controller connected and active → use it. All 10 buttons
  available including shoulders via physical buttons. Touch overlay
  hidden or minimized.
- No controller active → touch mode. Touch scheme overlay shown. All
  10 buttons available; shoulders accessed via the cart-declared touch
  shoulder style (default: corner buttons).
- Controller disconnects mid-play → touch overlay fades back in,
  runtime pauses briefly for orientation.

This state machine is entirely runtime-managed; cart code reads
abstract buttons and doesn't know or care what the input source is.

**Implication for cart design:** a cart author can design with shoulders
as full first-class inputs without worrying that mobile players will
be excluded. Mobile players without external controllers get a slightly
clunkier shoulder experience (corner buttons or long-press); mobile
players with Backbone-class controllers get the same experience as
desktop gamepad players.

### Pointer input API

For touch-first schemes and for desktop mouse support, the input API
exposes a single pointer abstraction:

```lua
console.input.pointer_is_held()       -- true if pointer is pressed
console.input.pointer_position()      -- x, y in screen pixels (320×240)
console.input.pointer_pressed()       -- fires once on press edge
console.input.pointer_released()      -- fires once on release edge
```

Single pointer — a unified abstraction for mouse (desktop), first touch
(mobile), and whatever the frontend maps to pointer (libretro). Most
carts that want touch support use pointer input; multi-touch is
available as raw events for carts that need it but is rarely necessary.

**Principle:** cart code never asks "what platform am I on?" It asks
"what input is available?" A well-designed cart reading pointer events
works identically on desktop mouse, mobile touch, and any frontend
that provides pointer input.

### Mobile-specific runtime features

- **30fps mode option** for battery-friendly play.
- **Background pause:** runtime pauses automatically when the app is
  backgrounded; resumes from state when foregrounded. Uses the same
  save/restore mechanism that powers hot reload.
- **Thermal throttle awareness:** if the device signals thermal
  pressure, runtime can optionally reduce framerate or complexity.
- **Viewport and orientation handling:** responds to device rotation
  per the cart's manifest preferences.

### Hot reload on mobile

Particularly nice for WASM-in-browser: developer edits cart on desktop,
packer rebuilds, mobile browser auto-reloads via the dev-mode reload
signal. Test on your phone while editing on your laptop without USB
cables or platform-specific tooling. Hot reload works identically to
desktop browser because the platform IS desktop browser from the
runtime's perspective.

### Display scaling and presentation

Carts render to a 320×240 paletted framebuffer. Displaying that on
real screens requires scaling, which is the runtime's responsibility
(not the cart's). Different platforms handle this differently:

**RetroArch (libretro frontend):** RetroArch handles everything —
scaling, filters, shaders, bezels, aspect ratio, fullscreen. The core
just submits the framebuffer; players configure visual treatment
through RetroArch's UI (which they may already have set up for all
their retro systems). This is one of the significant benefits of
shipping a libretro core: decades of display-polish work comes for
free, and players get consistent visual treatment across all their
retro gaming.

**Standalone desktop runtime (SDL2-based):** The runtime provides
direct display handling:
- **Default:** fullscreen with integer-multiple upscaling, nearest-
  neighbor scaling (no blurring — pixel art convention), black
  letterbox/pillarbox for unused screen area.
- **Typical scaling factors:** 4x (1280×960) on 1080p displays, 5x
  (1600×1200) fits 1440p+, 3x (960×720) on smaller displays.
- **Windowed mode** available with integer-multiple defaults; player
  can resize for non-integer scaling if desired.
- **Fullscreen toggle** via standard hotkey (e.g., Alt+Enter).
- **Shader library:** small curated set (~5 shaders) — clean
  pixelated (default), CRT, scanlines, LCD grid, color bleed. Player
  picks via runtime settings. Not a libretro-scale shader ecosystem;
  a minimal selection covering common retro-aesthetic preferences.

**Browser runtime (WASM):** Similar principles, different implementation:
- **Canvas sized at integer multiple** of 320×240 matching viewport.
- **`image-rendering: pixelated`** CSS property applied to canvas —
  critical to prevent browser interpolation blurring. Without this,
  default browser scaling ruins pixel art.
- **Fullscreen via Fullscreen API** when player requests.
- **Responsive to browser resize** — recalculates largest integer
  multiple that fits.
- **Shader support:** can use WebGL for shaders if the runtime opts in;
  probably defer to v2 unless player demand is clear.

**Mobile browser:** Browser handling plus mobile-specific concerns:
- **Orientation-aware layout** (landscape vs. portrait per §14).
- **High-DPI awareness:** canvas renders at logical resolution;
  device pixel ratio handled by the browser.
- **Safe areas and notches:** viewport meta tag configured for device
  safe areas; game viewport stays within safe zones.
- **Touch overlay** occupies space outside the game area (§14), never
  overlays the game itself.

**Hardware (Milk-V Duo + HDMI or LCD):** Runtime handles display
directly via DRM/KMS or framebuffer device:
- **Direct output** if the panel is 320×240 or a simple multiple
  (trivial small LCDs connected via SPI).
- **Scaled output** for larger displays, preferring integer multiples.
  Hardware DMA scaling where available; software scaling fallback.
- **Boot configuration** sets the display mode; player generally
  doesn't need to adjust.

**Cart's perspective: the full 320×240 is always visible.**

Carts render to the full framebuffer; the runtime guarantees no
occlusion by UI elements, touch overlays, safe areas, or notches. The
game area is preserved on every platform. Touch overlays on mobile,
dev-mode overlays, and system UI live outside the 320×240 region
(in letterbox space on desktop; in side/bottom control strips on
mobile; never on top of the game).

**User-configurable via runtime settings (not cart-controlled):**
- Integer vs. non-integer scaling (default: integer).
- Scaling filter / shader (default: nearest-neighbor clean).
- Background in letterbox areas (default: black; could be bezel images
  in v2).
- Fullscreen vs. windowed.

Carts don't see or influence any of this — the abstraction is clean
"you render to 320×240, runtime handles display."

---

## 15. Authoring and Dev Loop

### Decision: CLI tool packs project directory to cart; VS Code dev loop.

**Packer:** `console pack ./myproject` → `myproject.cart`. Takes a project
directory with a manifest, source files, and resources; produces the cart
container. `console run myproject.cart` for local testing.

**VS Code integration:** Extension or task-based setup that runs packer
on save and reloads a webview panel containing the browser build of the
runtime. Hot-reload for Lua carts (and optionally native carts via
rebuild). Dev loop iteration time: seconds.

**Asset pipeline (open for detailed design):** Accepted input formats:
PNG (palette-quantized or pre-paletted) for sprites, WAV for SFX,
XM/IT for music. Packer handles format conversion and ID resolution.

### Decision: Hot reload via the save/restore mechanism.

In dev mode, the runtime watches cart source and asset files. On an
explicit reload signal (IDE save triggering packer rebuild, or manual
hotkey), the runtime:

1. Calls `save_state()` to snapshot all tracked state regions.
2. Waits for the packer to finish rebuilding the cart artifact.
3. Loads the new cart (fresh code, fresh assets, fresh runtime-owned
   state like Lua VM).
4. Calls `load_state(snapshot)` to restore the cart to its previous
   state, applying layout migration if state schemas changed.
5. Resumes execution on the next frame.

The cart continues from the exact game-state it was in, with new
code/assets in place.

**Why this approach is right:**

- **Reuses existing mechanism.** Save/restore is already built, tested,
  and works across platforms. Hot reload is the trivial same-process
  same-platform case — essentially free.
- **Forces dogfooding.** Every reload exercises `save_state` / `load_state`.
  Bugs in the save contract (missing state regions, migration issues,
  `on_save` hooks) surface during development instead of when a player
  loses save data.
- **No partial-reload pitfalls.** Unlike function-level hot reload
  (which struggles with orphaned closures and stale references), this
  reloads all code fresh and restores state via POD serialization.
  No orphans possible.
- **Consistent mental model.** "Game state survives; everything else
  is rebuilt." Clean and predictable.

**Layout migration on reload:**

When state buffer layouts change between reloads (author adds a field,
removes a field, reorders fields), the snapshot's self-describing layout
descriptors let the runtime migrate:

- **Matching fields** (same name, same type) copied over.
- **New fields** default-initialized (zero or author-specified default).
- **Removed fields** dropped.
- **Type-changed fields** require author-provided migration hook or
  are dropped with a warning.
- **Complex restructuring** handled via optional `on_migrate(old, new)`
  callback the cart can provide.

Migration is silent for simple cases (new field added), warned for
ambiguous cases, and fails safe (offer full reset) when unresolvable.

**Performance targets:**

- Snapshot + restore: <2ms for typical cart state (2-4MB of tracked
  regions).
- Rebuild (Lua-only change): <500ms.
- Rebuild (asset-only change): <100ms.
- Rebuild (native code change): <3s (RISC-V compile + link + repack).
- Total user-visible reload time: <1s for common cases (Lua edits,
  asset edits), <4s for native code edits.

**Signal protocol:**

Packer runs in watch mode (`console watch ./project`), detects file
changes, rebuilds, signals runtime. Signal channel is the DAP
connection (already present in dev mode) via a custom `hot_reload`
command. Alternative channels (Unix socket, filesystem marker) available
for non-DAP setups.

**Runtime dev mode:**

Dev mode is a runtime startup flag (`--dev` or env var). Same binary,
different config. Dev mode enables:
- File watching and reload handling.
- DAP debug server.
- Debug overlay (memory usage, frame timing, state inspector).
- Relaxed error reporting (stack traces, verbose diagnostics).
- Permissive memory limits (tracked but not enforced, so rapid
  iteration isn't blocked by temporary leaks).

Shipping mode is the default; dev mode is opted into.

### Authoring patterns encouraged by hot reload

Hot reload via save/restore surfaces certain patterns as necessary:

- **Handlers by name/ID, not by function reference.** Event handlers,
  callbacks, coroutines-by-label — identify them by string or enum in
  state, reconstruct the actual function references in `init`. Storing
  Lua function references in state buffers doesn't work across reload
  (or across save/load in the shipping game).
- **Hooked coroutines for persistent async work.** Cutscenes, sequenced
  animations, AI state machines: use the hooked coroutine API with
  save/restore points. Raw Lua coroutines are fine for transient work
  but don't survive reload.
- **Derived state recomputable in `init`.** Caches, lookup tables,
  precomputed spatial indices — recompute these in `init` from the
  primary state rather than storing them. Avoids migration headaches
  and reduces snapshot size.

These are also the patterns needed for proper save games, so hot reload
makes authors learn them during development instead of shipping buggy
save-game code.

---

## 15a. Authoring Ecosystem

The console commits to "bring your own tools" as a core design principle —
but that commitment only works if authors can actually find and use those
tools at accessible cost. This section maps the tooling ecosystem to
make the path explicit.

### The gap vs. PICO-8

PICO-8's main "all-in-one" advantage isn't that its built-in tools are
better — they're deliberately more limited than free alternatives. The
advantage is **pre-integration**: zero-setup, instant-start authoring.

This console's advantage is the opposite: tools have **no ceiling**
(professional-grade pixel art, professional tracker music, any code
editor you prefer), but initial setup is real (install multiple tools,
configure them, learn them).

Closing that on-ramp gap is a design concern, not an afterthought.

### Recommended zero-budget toolchain

Every category has at least one free, production-grade option. This is
a complete toolchain with zero cash outlay:

| Category | Tool | Notes |
|----------|------|-------|
| Pixel art | **LibreSprite** or **Pixelorama** | Free Aseprite-class tools |
| Tilemaps / levels | **Tiled** or **LDtk** | Both excellent, open source |
| Music | **OpenMPT** or **MilkyTracker** | Professional-grade trackers |
| SFX | **Bfxr** (browser) + **Audacity** | Generator + editor |
| Sound library | **Freesound.org** | Huge CC-licensed library |
| Code | **VS Code** or **VSCodium** | Dev loop integration ships for these |
| Version control | **Git** + **GitHub/Codeberg** | Free private repos |
| Image editing | **GIMP** or **Krita** | When pixel-specific tools aren't enough |
| Font creation | **FontForge** or **BMFont** | If making custom fonts |

A solo creator can ship a full game on this toolchain with no cash outlay.

### Recommended low-budget toolchain (~$100 total one-time)

The sweet-spot upgrades for authors willing to spend small amounts:

- **Aseprite** ($20): the single best paid upgrade in indie game dev;
  industry-standard pixel art tool.
- **Renoise** ($85): professional tracker DAW (free demo is fully
  functional for export, so this upgrade is mostly for convenience).

Everything else is already excellent in free form; don't spend money
elsewhere without specific need.

### Light-footprint toolchain (low-end Chromebook / RPi 400 class)

For authors on constrained hardware (4GB RAM, modest CPU — a common
"first computer" profile), favor lightweight and browser-based tools:

| Category | Light-footprint choice | Notes |
|----------|------------------------|-------|
| Pixel art | **LibreSprite** | Lighter than Pixelorama (Godot-based); runs well on 4GB |
| Tilemaps | **Tiled** | Much lighter than LDtk (Electron); native and fast |
| Music | **MilkyTracker** | Native cross-platform; no Wine overhead |
| SFX | **Bfxr** (browser-only) | Zero install, zero footprint |
| Code | **VS Code** | Workable on 4GB but tight; close other apps when running heavy tools |
| Runtime dev loop | **Browser WASM build** | No desktop emulator process; runs in already-open browser |

**Verified workable configurations:**
- **RPi 500 (8GB):** Comfortable — VS Code + any single tool + browser
  runtime concurrently. Legitimate primary dev machine.
- **RPi 400 (4GB):** Workable — swap heavy tools rather than keeping
  all open. VS Code + one tool at a time + browser runtime.
- **Low-end Chromebook (4GB, Celeron-class) via Crostini:** Tight but
  possible. Favor browser-only tools (Bfxr, browser runtime) and close
  VS Code when using Aseprite/LibreSprite.
- **Mid Chromebook (8GB):** Fine, similar to RPi 500.

This matters because it reinforces accessibility — a $100 secondhand
Chromebook or $90 Pi 400 is enough to make games for this console.
No $2000 development rig required.

### Authoring with AI assistance

### Authoring with AI assistance

The console's constraints — paletted 320×240 pixel art with a defined
palette — are a strong fit for AI-generated art in 2026. Tools like
pixellab.ai, Scenario, various Stable Diffusion pixel-art fine-tunes,
and bespoke pixel-art generators produce usable output at this resolution
range.

**Design position: document and enable, don't build integration in v1.**

Reasoning:
- The AI tooling space moves faster than the console can reasonably
  track. Anything integrated today is stale in 12 months.
- Authors have strong preferences about AI tools and providers. Locking
  in one choice creates friction.
- Third-party tools exist, work well, and are improving. Duplicating
  their work is wasteful.
- API costs, rate limits, and provider relationships are a real
  maintenance burden.

**What to provide instead:**

1. **Publish the console's palette in AI-friendly formats** (`.gpl`,
   `.hex`, `.pal`, plus a reference image). Many AI tools accept palette
   constraints as input; being importable into workflows matters.
2. **Document effective AI workflows** in a "Making Pixel Art with AI"
   docs page. Example prompts that produce good results at 320×240,
   post-processing techniques, tool recommendations, ethical and
   licensing considerations.
3. **Make the packer AI-output-tolerant.** Accept PNG from any tool
   (auto-quantize to palette with good dithering). If AI outputs are
   at higher resolutions (common), accept those and downsample cleanly.
4. **Include AI-generated content in the CC0 starter asset pack.** This
   dogfoods the workflow and produces useful bundled assets.

**What to defer to v2 or community:**

- Direct in-IDE generation UI. If a clear winner emerges in AI pixel
  tools and community demand is strong, consider a pluggable integration
  (user configures backend via shell command) rather than a hard-coded
  provider.
- Project-scaffolding AI ("describe your game, get a project skeleton").
  This is a different problem — uses general LLM APIs rather than
  image generation — and might be worth a lightweight v2 feature. Fits
  cleanly into the VS Code extension as a command.

**Ethical posture:** transparent documentation that AI-assisted art is
welcome here; CC0 or clearly-licensed training data in bundled assets;
no restrictions on authors' choices but clear guidance about licensing
implications of AI-generated work in their shipping games.

### Packer format compatibility commitments

The packer must accept common formats without friction. Authors
should rarely need to convert outputs from their tools:

- **Sprite sheets:** PNG (auto-palette-quantized if not already paletted).
- **Tilemaps:** Tiled's TMX and JSON; LDtk's native format.
- **Music:** XM, IT, MOD, S3M (tracker formats).
- **SFX source:** WAV (primary), OGG, MP3 (converted to QOA/ADPCM
  internally).
- **Fonts:** BMFont's FNT format; direct PNG grid for pixel fonts.
- **Aseprite source files (.ase/.aseprite):** direct, with frame /
  animation metadata preserved.

Adding format support is cheaper than making authors convert. If a tool
outputs something common and the packer can't read it, that's a gap
to fix.

### Starter kit and examples

`console new myproject` creates a project with:
- Pre-configured manifest, VS Code workspace, recommended extensions.
- Starter Lua or native-cart code with inline comments explaining the API.
- Example sprite sheet, tilemap, tracker module in correct formats —
  a runnable "hello world" game the author modifies.
- Readme linking to recommended tools and quick-start guides.

This turns "start from blank files" into "start from a working game
and modify," which is meaningfully less intimidating.

### CC0 starter asset pack (bundled with SDK, opt-in per cart)

Ship an optional asset library of public-domain / CC0 content:
- Tilesets in common settings (town, dungeon, forest, sci-fi interior).
- Character sprite sheets across styles (hero, villain, NPC).
- UI element packs (buttons, frames, cursors, dialog boxes).
- SFX library (general-purpose game sounds).
- Example tracker modules (title themes, ambient, boss themes).

This dramatically lowers the "getting to something playable" barrier:
an author can put together a prototype game entirely from bundled
assets while they work on their own, without blocking on the art
pipeline.

AI-generated pixel art is a viable source for this pack — curated
AI-generated assets that fit the console's palette and resolution can
produce a substantial library at modest creation cost, all CC0-licensed
to avoid downstream licensing concerns.

### Browser-based quickstart tools (stretch goal)

Consider hosting browser-based tools alongside the console docs for
authors who want to start with zero installation:
- Sprite editor (exports to cart-compatible format).
- Simple tilemap editor.
- Bfxr-style SFX generator (already exists; link to it).

These don't compete with Aseprite / Tiled; they're on-ramp tools for
beginners and for quick work. The browser-based console emulator
itself, running in the same page, lets someone go from "nothing" to
"playing a game they made" without installing anything.

This is a real engineering investment (months, not days) but the
leverage for onboarding is significant. Worth planning for, not
necessarily shipping in v1.

### Documentation as ecosystem infrastructure

A dedicated "Making Your First Game" page on the docs site:
- Lists exactly which tools to install.
- Per-platform install instructions.
- Links to brief "enough to get started" tutorials for each tool.
- Explains how each tool's output flows through the packer.
- Walkthroughs of complete small games with all assets included.

This is documentation work, not engineering work, but it's what makes
"bring your own tools" feel welcoming rather than daunting.

---

## 16. Testing Strategy

### Layers

1. **Desktop runtime:** Fastest iteration loop. Runtime as native SDL app,
   carts load and run directly. ~90% of development happens here.
2. **Browser build:** Emscripten build, test in actual browsers.
3. **Hardware image in QEMU:** `qemu-system-riscv64 -machine virt` with
   the buildroot image. Tests kernel boot, userspace init, runtime launch,
   cart execution end-to-end.
4. **Real hardware:** Final validation on Milk-V Duo with real peripherals
   (USB gamepad, display, audio). Catches driver quirks, timing, peripheral
   bugs that QEMU misses.

### CI

Automated QEMU-based tests: boot image, run test carts headless, verify
outputs (frame buffer hashes, audio buffer hashes) bit-identical across
CI runs. Integration tests validate save state cross-platform portability.

### Conformance

RISC-V interpreter validated against upstream `riscv-tests` conformance
suite for RV32IMFC. Determinism validated via cross-platform bit-identity
tests for representative cart workloads.

---

## 17. Remaining Design Decisions

### To resolve before freezing the API header

- **API surface shape:** Module organization (`gfx.*`, `snd.*`,
  `input.*`, etc.), naming conventions, verb-object ordering, getter/setter
  pairs vs. modal functions. Strong lean: modules, verbose-ish names,
  consistent verb ordering.
- **Error handling convention:** Fallible ops return nil + retrievable
  last-error; unrecoverable errors trap with diagnostic. (Needs final ratification.)
- **Cart lifecycle entry points:** Exact signatures of `init`, `update`,
  `draw`, `on_save`, `on_load`, `cleanup`. All carts (Lua and native)
  share the same ELF-level contract; the SDK's Lua-cart template handles
  forwarding these into Lua automatically.
- **Exact Lua-host API surface:** The ~10-15 ECALLs that the cart-side
  Lua shim wraps. Minimum sufficient set for typical Lua carts without
  bloating the ECALL surface unnecessarily.
- **Lua standard library allowlist:** Final explicit list.
- **Asset pipeline input formats:** Exact formats accepted, conversion
  rules, manifest format.
- **Cart crash display:** Visual style of the runtime's crash screen
  (thematic; e.g., PICO-8's red error text is iconic).

### To resolve after first prototype contact

- **Performance targets per host:** Empirical measurement of guest
  instructions/second on each platform.
- **Hot-reload details:** Exactly which changes reload in-place vs.
  require cart restart.
- **Debug overlay features:** Frame timer, memory usage graph, audio
  waveform, state-buffer inspector.

### Deliberately deferred

- Multi-cart interactions (cart-loads-cart), networking APIs, mod support,
  cart distribution platform / store, monetization / licensing story,
  in-engine content editors (sprite, tilemap, level).
- `ref<T>` state-buffer reference types with auto-deref and generation
  counters. Can be added in a later version as opt-in sugar without
  breaking existing carts.
- **Inline native code in Lua carts (Layer 2 of hybrid strategy, §11a).**
  Architectural fit is natural given unified ELF format; packer changes
  (compile `.c`/`.rs`/`.zig` alongside `.lua`, generate bindings) are
  the main v2 work. v1 API design should anticipate this to avoid retrofit.
- **Screensaver integration for non-interactive carts.** Desktop
  screensaver plugins, embedded/kiosk attract modes, and curated
  ambient-display playlists built on any `interactive = false` cart
  (typically Demo or Mini size class, but works for any size). Requires
  a screensaver wrapper (per platform: XScreenSaver, macOS
  ScreenSaverView, Windows .scr) that hosts the runtime, cycles through
  a library of non-interactive carts, and respects system timer/activity
  events. Natural v2+ project once a library exists to motivate it.
- **Hardware-accelerated audio decode paths.** Browser platforms can
  route Opus decode through Web Audio / HTMLAudioElement APIs to
  leverage device audio DSPs, reducing battery drain. Non-breaking
  optimization; cart format and API stay the same.
- **Demo sub-tiers** (`demo-64k`, `demo-4k`) for demoscene-style tight
  size coding. If community interest emerges, sub-tiers slot in without
  breaking existing Demo carts.
- **Framework layer for genre-specific authoring.** Opinionated layers
  on top of the base console that provide genre-appropriate abstractions,
  libraries, and editor tooling. Analogous to AGS (Adventure Game Studio),
  GB Studio, or Inform 7. Examples of possible frameworks:
  - **Adventure game framework** (AGS-style): rooms, inventory, dialog
    trees, verbs. Custom room editor + dialog graph editor + Lua runtime
    library that handles the genre's machinery.
  - **JRPG framework**: party, grid combat, tilemap world, NPCs, quests.
    Character/encounter/map editors.
  - **Shmup framework**: bullet pattern design, wave composition, boss
    phases. Pattern editor + wave composer.
  - **Visual novel framework**: scene graph, branching dialog, character
    portraits. Scene flow editor.
  - **Puzzle game framework**: grid rules, level sequencing. Level editor
    with constraint visualization.

  Frameworks don't modify the runtime. They provide:
  - A Lua library (or native library) that the cart bundles, handling
    genre-specific machinery.
  - Custom input formats for the packer (e.g., AGS-style room files,
    scene-graph definitions) that compile to cart-compatible resources.
  - VS Code extension providing genre-specific editors, project
    templates, and workflow tooling.

  **v1 design implications:** the base architecture should accommodate
  frameworks without modification:
  - Packer should have a plugin architecture for custom input formats
    (design in v1; leave extension points even if unused). This is
    structural — hard to retrofit.
  - VS Code extension should be extensible by other extensions (standard
    VS Code pattern). Also structural.
  - Resource namespace (`.cart.*` ELF sections) should be open-ended,
    letting frameworks add their own resource types later. Already
    the case.

  Manifest fields identifying a cart's framework can be added in v2
  when frameworks actually exist — the manifest format is already
  extensible via minor API version bumps, so new fields are
  backward-compatible by construction.

  This is primarily a community/ecosystem opportunity rather than core
  console work. The console ships with a few blessed frameworks (perhaps
  shmup + visual novel + one more) as reference examples; the community
  builds others. Dramatically expands the authoring audience —
  non-programmers can make genre games using framework-provided editors
  without writing the genre's machinery from scratch.
- **Extract a console-agnostic library from the codebase.** Much of the
  design is independent of this console's specific fidelity choices
  (resolution, palette, button layout, audio format, memory budgets).
  The deterministic simulation infrastructure, state serialization,
  cart format, packer architecture, Lua integration, input filtering
  and recording, replay, achievements, and speedrun tooling are all
  console-agnostic — they'd apply equally to a hypothetical sibling
  console at different fidelity (480×270 truecolor with OpenGL ES, say)
  or with different input hardware (analog sticks, different button
  layout).

  Cleanly separating "console-agnostic library" from "this console's
  concrete specification" positions the architecture for future reuse
  and forces useful discipline about what's genuinely general vs.
  this-console-specific.

  **Not a v1 goal** — v1 is still learning which decisions are right;
  drawing library/console boundaries prematurely would commit to
  abstractions before the real seams are clear. But v1 development
  should practice the discipline:

  - Don't hard-code console constants (320×240, specific palettes, button
    counts) in modules that are logically general (RNG, serialization,
    Lua integration).
  - Keep the C API factoring clean: cart-facing vs. frontend-facing,
    with console-specific choices separable from general infrastructure.
  - Categorize each Lua API function mentally: genuinely
    console-specific (draw APIs, which care about 320×240) vs.
    console-agnostic (coroutine helpers, time API, save/restore).
  - Document what's console-specific vs. general as the code is
    written, so later extraction is easier.

  V2 can then extract the library cleanly with less guesswork. Library
  could be released as its own project ("fantasy console construction
  kit") enabling others to build consoles with different fidelity
  targets, different renderer architectures, or different input models
  while sharing the hard-won infrastructure (determinism, serialization,
  cart format, etc.).

  Examples of hypothetical sibling consoles this would enable:
  - Higher-fidelity console: 640×360 truecolor with OpenGL ES renderer.
  - Lower-fidelity console: 160×144 monochrome with 4-color palettes
    (Game Boy-class).
  - Different input model: console with dual analog sticks for twin-stick
    games.
  - Different language: console using a different scripting language
    in place of Lua (Python, JavaScript, Wren).
  - Platform variation: console targeting different hardware class
    (phones-as-primary-target with different input specifics).

  None of these need to exist; the library abstraction just makes them
  possible without rebuilding the infrastructure.
- Bare-metal runtime on dedicated hardware. Future option if the project
  matures into a hardware product; Linux-on-SBC is the right v1 approach.
- 400×300 "widescreen" resolution option. Lean toward no, for identity
  sharpness and mobile ergonomic consistency.

---

## 18. Suggested Early Work

### Phase 1: Foundations (weeks 1-4)

1. **Core library C API header** (`fantasyconsole.h`). This is the primary
   contract; everything else follows from it. Iterate on this until it
   reads well — write example cart code against it (on paper) to validate
   the shape before implementing. Two headers: one for *cart* authors
   (cart-facing API) and one for *frontend* authors (driving the core).
2. **Skeleton core library** (`libfantasyconsole`). Stubs for all API
   functions, returns placeholder data. Validates that the API factoring
   is realizable.
3. **SDL2 frontend.** Thin wrapper around the core, window + input + audio
   output. Native desktop player. First end-to-end integration test.

### Phase 2: Running Carts (weeks 4-10)

4. **Cart format tooling: ELF loader in runtime** plus packer CLI that
   produces ELF carts from a project directory. Minimal first: no
   compression, single ELF output.
5. **Embedded Lua interpreter in runtime**, with sandbox applied
   (stripped stdlib, deterministic math).
6. **Lua-host API (ECALLs)** plus cart-side `libconsolelua` shim.
   Default Lua-cart template that forwards `init`/`update`/`draw` into
   Lua automatically. First working Lua cart (Snake or Pong).
7. **State buffer system.** Typed layouts (stored in `.cart.layouts`
   ELF section), metatable sugar on the Lua side, SOA storage.
8. **Save state.** Walk tracked regions; serialize; deserialize; verify
   round-trip. First end-to-end test: save mid-game, restore, continue.

### Phase 3: Native Carts and Debugging (weeks 10-18)

9. **RISC-V interpreter** embedded in core. Start simple (decode/dispatch
    loop), validate against `riscv-tests`. Target RV32IMFC.
10. **Native cart SDK.** Compiler toolchain config, linker script with
    `.cart.*` section conventions, API header for C/Rust/Zig cart
    authors, layout-declaration helpers (macros emitting `.cart.layouts`
    entries).
11. **First pure-native cart.** A simple game exercising the full
    native path — no Lua involvement.
12. **Debug servers in runtime.** DAP server for Lua-level debugging;
    GDB remote serial for native-level debugging. VS Code integration
    via standard extensions.

### Phase 4: Platforms (weeks 18-24)

13. **Emscripten build** of the core. Browser frontend (HTML + canvas +
    Emscripten glue, `image-rendering: pixelated` CSS, touch control
    overlay system). Same carts run in-browser.
14. **Libretro core adapter.** Adapter shim, ~400 lines. Test in
    RetroArch on desktop; then on a retro handheld if available.
15. **Custom libretro frontend** (standalone desktop build). Links
    against libretro-common (MIT); provides branded UX (cart picker,
    settings, pause menu) while inheriting libretro's shader/rewind/
    save-state infrastructure. Display scaling via integer-multiple
    upscaling with nearest-neighbor default. See §10 and §14.
16. **VS Code integration.** Extension or workspace setup, runs packer
    on save, reloads browser frontend in webview, wires DAP/GDB
    debuggers to the runtime's debug servers.

### Phase 5: Player Features (weeks 24-30)

17. **Cart save / preferences APIs.** Runtime-managed per-cart save
    directories, multi-slot `console.save.*`, `console.prefs.*`. See §7.
18. **Save state via libretro** (`retro_serialize` / `retro_unserialize`)
    using the runtime's serialize machinery. Enables rewind in the
    libretro frontends for free.
19. **Achievements.** Manifest-declared; runtime-provided notification
    banner, browser UI, persistence. See §7.
20. **Input recording / replay + replay-as-demo.** Runtime records
    inputs per-frame; replay file format; frontend UI for loading
    "cart + input log" as a playable unit. Essentially free given
    determinism + save-state infrastructure.
21. **Speedrun mode.** Runtime setting: disables save states / rewind,
    pre-decompresses all resources to avoid mid-run latency spikes,
    shows timer overlay, records input log automatically.
22. **LAN netplay in custom frontend.** UDP broadcast discovery,
    host/join UI, player assignment. Connection protocol via
    libretro-common. See §8.

### Phase 6: Hardware (weeks 30-36)

23. **Buildroot image for QEMU virt.** Minimal Linux, runtime as
    PID-1-equivalent. Full boot-to-runtime flow in QEMU.
24. **Milk-V Duo adaptation.** Port buildroot config to real hardware.
    Hardware image uses custom libretro frontend (same as standalone
    desktop; framebuffer/DRM display backend). Validate on physical
    device with gamepad/display/audio.
25. **Cross-platform save state portability test.** Save on one
    platform, load on another. Validates determinism end-to-end.

### Phase 7: Polish and Validation (weeks 36+)

26. Audio format support: QOA, ADPCM, libxmp integration for tracker
    music, Opus decode (runtime-hosted) for Large/Flagship carts.
27. Documentation: reference generated from API headers, quick-reference
    cheat sheet, sample carts, "Making Your First Game" guide.
28. Performance tuning: measure, optimize hot paths (likely RISC-V
    interpreter dispatch and software mixer).
29. **Doom port as showpiece and API stress test.** Port Chocolate Doom
    (or similarly clean Doom source port) as a native cart. Validates:
    - Framebuffer API is adequate for real game workloads.
    - Input API handles complex binding (movement, strafing, weapons,
      menus).
    - Audio API handles simultaneous SFX channels plus music.
    - Native cart SDK (C toolchain, linker script, layout declarations)
      works for a nontrivial codebase.
    - Performance claims hold up: full-speed native on Milk-V, full-speed
      interpreted on desktop, playable (20-30fps) in browser via WASM,
      via libretro in RetroArch.

    Serves as marketing touchstone ("runs Doom on a $9 RISC-V board")
    and forces discovery of API gaps that pure test carts wouldn't
    reveal.
30. Community bootstrap: example carts, tutorials, starter kit
    (`console new myproject`), CC0 starter asset pack, initial tooling
    polish.

**Capability reference points:** The console is **Doom-class** in raw
capability — a native cart can run software-rendered 2.5D games with
complex AI and level geometry at full framerate on the reference hardware.
Games of comparable or lower technical complexity are comfortably within
reach: tile-based action, platformers, RPGs, strategy games, adventure
games, shmups, puzzle games. Quake-class (true 3D with perspective-correct
texturing and dynamic lighting) is a stretch goal achievable with careful
native ports on fast hosts.

These are **technical capability** reference points, not aesthetic targets.
The console supports making games that look and play modern, that reference
specific past eras, or that do something entirely new — the constraints
shape production tractability, not aesthetic direction.

Lua carts comfortably cover the non-rendering-intensive portion of this
range. Rendering-heavy titles are native-cart territory — the designed-for
case of the "scripting is optional" principle.

---

## 19. Key Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| Fixed-point Lua / f32 precision trips up authors | Clear docs on patterns (time as frame count, etc.); add i64 userdata library; revisit if real usage shows D extension is needed |
| RISC-V interpreter too slow on low-end hosts | API design keeps expensive ops host-side; measure early and optimize dispatch; SoftFloat as fallback for determinism over speed |
| State buffer ergonomics insufficient in practice | Can add `ref<T>` sugar and row proxies in v1.x without breaking existing carts |
| Libretro API constraints conflict with core design | Build libretro frontend early (before browser even) to catch assumptions; core API designed frontend-agnostic from day one |
| Buildroot / Linux image proves fragile on new SBCs | Start with Milk-V Duo only; add others as community demand emerges |
| Save state / rewind / netplay determinism bugs | CI tests for bit-identity across platforms; controlled math library; strict FP mode enforcement |
| Cart authoring friction from 32-bit-only numerics | Ship good patterns / libraries; f64 can be added later if genuine need emerges (non-breaking) |
| Hot-reload state migration surprises authors | Default migration (copy matching fields, zero new) handles most cases; author migration hooks for complex cases; clear docs on "state stays, closures don't" boundary |
| Packer rebuild too slow to feel "live" | Target <500ms for Lua edits, <100ms for asset-only; profile and optimize the packer if it regresses; native rebuild is acceptably slower (<3s) |

---

## 20. Design Principles (Summary)

1. **API-first.** The C API is the contract; everything is a consumer.
2. **One core, many frontends.** Runtime is a library; native app, libretro
   core, browser, hardware image are thin adapters.
3. **Same cart everywhere.** One `.cart` file runs natively on RISC-V,
   interpreted on desktop/browser, via libretro wherever RetroArch runs.
4. **32-bit everywhere.** Unified numeric model; no precision boundaries
   inside the system.
5. **Determinism is structural, not optional.** Strict FP, controlled
   math library, fixed timestep, tracked state. Save/rewind/replay/netplay
   all work as a consequence.
6. **Constraints serve production.** 320×240, 256 colors, size classes
   from 256 KB (Demo) to 256 MB (Flagship) — tight enough to be
   tractable for a solo artist or small team at each tier, loose enough
   to build genuinely nice-looking games. Compatible with AI-assisted
   and library-sourced art pipelines.
7. **Scripting is optional.** Native carts (C/Rust/Zig/etc.) are first-class;
   Lua is the approachable default, not the only path.
8. **POD state, index references.** Data-oriented layouts enable save
   state by memcpy, migration on reload, cross-platform portability.
9. **Leverage existing infrastructure.** Linux for drivers on hardware,
   libretro for distribution, SDL for desktop, Emscripten for browser,
   QEMU for testing. Don't reinvent solved problems.
10. **Design for decision reversal.** Start strict (no FP → added F;
    no row proxies → can add later; single resolution → can add alternate);
    relaxing is non-breaking, tightening would be painful.
11. **Dev experience is a feature.** Hot reload via save/restore, first-class
    debugging via DAP and GDB, fast rebuilds, external-tool-friendly
    asset pipeline. The authoring experience is a competitive
    differentiator, not an afterthought.

---

## 21. Debugging

Both Lua and native carts get first-class debugging via standard protocols.
This is a primary authoring feature, not an afterthought.

### Lua cart debugging: DAP via runtime-owned Lua.

Because the Lua interpreter lives in the runtime's native address space
(not in guest memory), the runtime has full access to Lua's C debug API:
`lua_sethook`, `lua_getinfo`, `lua_getstack`, `lua_getlocal`,
`lua_getupvalue`. The runtime exposes these as a Debug Adapter Protocol
server. VS Code's built-in DAP support connects to it.

Features available:
- Source-level breakpoints (by file and line).
- Single-step, step-over, step-into, step-out.
- Call stack inspection with function names and source locations.
- Local variable and upvalue inspection.
- Expression evaluation in the current scope.
- Conditional breakpoints.

This is the standard Lua debugger experience. The key enabler is that the
`lua_State` is runtime-native, not guest — see section 9 for the unified
ELF format design that makes this work.

### Native cart debugging: GDB remote serial protocol.

The runtime implements the GDB remote serial protocol (approximately 500
lines of code, standard implementation). The cart's ELF carries DWARF
debug info when built in debug mode. GDB (or VS Code's GDB integration)
connects to the runtime and drives debugging at the source level for
whatever language the cart was written in.

Features available:
- Source-level breakpoints (any language with DWARF output: C, C++, Rust,
  Zig, Odin, etc.).
- Single-step at source or instruction level.
- Full register inspection.
- Memory dumps within the cart's address space.
- Call stack walking via DWARF unwinding.
- Expression evaluation.

Works uniformly in the RISC-V interpreter (desktop/browser/emulator
contexts) and on real RISC-V hardware (via TCP connection to the
runtime's debug server, enabled in development-mode device images).

### VS Code integration.

Both debug paths plug into VS Code via standard extensions:
- DAP: built-in VS Code debug infrastructure, launch configurations point
  at the runtime's DAP port.
- GDB: any GDB extension (e.g., native-debug, CodeLLDB with GDB backend).

Launch configurations in the VS Code dev-loop setup automatically pick
the right debugger for the cart type. Authors set breakpoints, hit F5,
debug.

### Runtime introspection APIs (orthogonal to language-level debugging).

In addition to source-level debuggers, the runtime exposes introspection
for cart-authored state regardless of language:
- Memory usage (current / cap, breakdown by state region).
- State buffer contents (dump-readable format, respects typed layouts).
- Input state history (for reproducing bugs).
- Frame timing and perf counters.
- RNG stream states.

Accessible via:
- An in-runtime debug overlay (toggle with a keybind; rendered over the
  cart without affecting its execution).
- The DAP/GDB connections (as custom variable scopes).
- Command-line tools (dump snapshot → inspect offline).

### Hardware debugging note.

On real RISC-V hardware, debugging requires a development-mode build of
the device image (enables the DAP and GDB servers on a TCP port reachable
from the dev machine). Release/shipping images omit the debug servers.
The development-mode image is the default for anyone flashing a Milk-V
Duo to develop carts.
