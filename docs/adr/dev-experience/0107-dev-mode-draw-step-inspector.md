# ADR-0107: Dev-mode draw step inspector

## Status
Accepted

## Context

Authors regularly need to understand how a frame's pixels come together:
why the HUD is drawing under the player, which buffer's `draw_handler`
is producing a stray sprite, where a transparency mask is wrong, how
the declarative draw pipeline (ADR-0102) is ordering its passes. The
existing tools (rewind capture, save-state thumbnails) show finished
frames; they don't help with **mid-frame** inspection.

A few things make this affordable on Blyt where it would not be on a
3D engine:

- The framebuffer is software-rendered, paletted, 320×240 — a single
  75 KB byte array (ADR-0003). "Mirror the in-progress buffer to the
  display" is a memcpy plus present.
- `draw()` is read-only over tracked state (ADR-0076), so a recorded
  call log is purely a presentation-layer artifact and can be replayed
  without perturbing the simulation.
- Stage's draw orchestration (ADR-0102's declarative pipeline,
  ADR-0092's scene stack) already has well-defined boundaries that
  the runtime knows about and can label.
- The dev loop has DAP plumbing in place (ADR-0044) — adding draw
  inspection doesn't need a new transport.

## Decision

**Dev mode records every draw primitive emitted during a frame into a
per-frame call log structured as a tree of named sections. Carts
declare section names in the manifest and bracket their draw code
with section calls; Stage emits its own internal sections for the
parts of the draw orchestration it owns. The inspector lets the user
step through the tree at debugger-style granularities and presents
the in-progress framebuffer at every pause. Release builds compile
the machinery away entirely.**

### Section names: cart-declared in the manifest

Cart-emitted section names follow ADR-0059's universal pattern —
declared in the manifest, generated as packer constants, referenced
by integer ID at runtime:

```yaml
# cart.config.yaml
draw_sections: [hud, minimap, debug_overlay, particles_post]
```

```c
// Generated: cart_draw_sections.h
#define DRAW_SECTION_HUD             ((blyt_draw_section_h)1)
#define DRAW_SECTION_MINIMAP         ((blyt_draw_section_h)2)
#define DRAW_SECTION_DEBUG_OVERLAY   ((blyt_draw_section_h)3)
#define DRAW_SECTION_PARTICLES_POST  ((blyt_draw_section_h)4)
```

```lua
local DS = require("cart_draw_sections")
```

Tooling reads the manifest and offers the declared names as
breakpoint candidates **before any frame runs** — authors can set
inspector breakpoints by name in the IDE without trial-and-error
stepping. The packer validates that every section ID referenced in
cart code is declared (catching typos at build time).

The manifest declares **names only**, not nesting structure. Runtime
nesting is dynamic (the minimap section may be emitted only when the
minimap is enabled), so locking the structure into the manifest
would either over-constrain authors or invent restrictions that
runtime branches violate. The name list is enough for tooling — the
tree itself is discovered as `draw()` runs.

### Stage-emitted sections

Stage emits sections for the parts of the draw it orchestrates,
using runtime-internal IDs from a private namespace (no manifest
declaration required). Names are derived either from the runtime's
well-known set or from declarations the cart has already made
(scenes, buffers):

| Section | Source |
|---------|--------|
| `<scene:NAME>` | one per stack frame (ADR-0092); name from `scenes:` |
| `<phase:clear>` | declarative draw clear pass (ADR-0102) |
| `<phase:world>` | declarative world-space pass |
| `<entities:NAME>` | one per scene-scoped buffer drawn in the world pass; name from `state_buffers:` |
| `<phase:screen>` | declarative screen-space pass |
| `<entities:NAME>` | one per screen-space buffer drawn in the screen pass |
| `<phase:ui>` | declarative ui-handler invocation |
| `<post:screen_shake>` | runtime post-process (ADR-0051) |
| `<post:palette>` | palette cycling (ADR-0061) |

Tooling computes the full pre-frame Stage section list from the
existing manifest declarations plus the runtime's well-known
phases, so breakpoint candidates for Stage sections are also
available before any frame runs.

The cart's `on_draw` body sits inside the appropriate
`<scene:NAME>` section; cart-emitted sections nest inside it
naturally.

### Cart API

**Lua: closure-based.** A single call wraps the section's body;
pairing is structural:

```lua
local DS = require("cart_draw_sections")

blyt32.dev.draw_section(DS.HUD, function()
    -- draw HUD
    blyt32.dev.draw_section(DS.MINIMAP, function()
        -- draw minimap
    end)
end)
```

The runtime emits the begin event, invokes the closure, and emits
the end event in a finally-style cleanup that runs even if the
closure throws. There is no `draw_section_begin`/`draw_section_end`
pair in Lua — the closure form is the only API.

**C: paired calls.** C carts use explicit begin/end:

```c
blyt_dev_draw_section_begin(DRAW_SECTION_HUD);
    blyt_dev_draw_section_begin(DRAW_SECTION_MINIMAP);
    // draw minimap
    blyt_dev_draw_section_end(DRAW_SECTION_MINIMAP);
blyt_dev_draw_section_end(DRAW_SECTION_HUD);
```

C is the lower-level interface and Lua is the convenience layer
(ADR-0046's general API conventions); a C author can manage paired
calls. Dev mode validates the pairing: end-of-frame with an
unclosed cart section, or an end with the wrong ID, surfaces a
warning with source location.

**Unconditional break.** Independent of section structure, both
languages have a debugger-style breakpoint primitive that halts
inspection regardless of step mode:

```c
void blyt_dev_draw_break(void);
```
```lua
blyt32.dev.draw_break()
```

No-op in release builds (ADR-0065).

### Recording

While `draw()` runs, the runtime appends entries to the frame's
draw-call log:

- **Section begin** / **section end** for cart-emitted and
  Stage-emitted sections, carrying the section ID and source
  location.
- **Primitive** entries for every blit, line, fill, text, palette
  op, etc., carrying kind, arguments, and source location.

Entries form a tree by virtue of nested begin/end markers. A
typical frame log looks like:

```
<scene:gameplay>
  <phase:clear>
  <phase:world>
    <entities:enemies>
      blit(...) blit(...) ...
    <entities:projectiles>
      blit(...)
    <entities:particles>
      blit(...) ...
  <phase:screen>
    <entities:hud>
      blit(...)
  <phase:ui>
    [hud]                       ← cart's draw_section(DS.HUD, ...)
      [minimap]
        line(...) fill(...)
      blit(...) blit(...) text(...)
<scene:pause_menu>
  ...
<post:screen_shake>
<post:palette>
```

Worst-case log size is a few KB per busy frame; allocation is
dev-mode-only. The log is cleared at the start of each `draw()`.

### Step modes

Three modes selectable at runtime via dev UI or a DAP command:

- **Continuous** (release default; dev default unless overridden):
  no pausing. Recording still happens; the user can scrub the log
  after the frame ends without interrupting normal play.
- **Step on section boundaries**: pause at every section begin
  and end, cart-emitted or Stage-emitted. Optionally filtered to
  a specific set of sections via the inspector UI or DAP.
- **Step every primitive**: pause after every recorded entry —
  primitive or section boundary. Verbose; for fine-grained
  walkthrough.

`blyt_dev_draw_break` halts in any mode.

### Pause semantics

When execution is suspended inside `draw()`:

- The cart's `draw()` is suspended at the next instruction. Its
  stack frame is preserved; resuming continues.
- `update()` does not run while paused — the cart is held on this
  frame.
- The runtime presents the in-progress framebuffer (everything
  drawn up to the suspension point) to the host display.

When the suspension came from the inspector (section-step,
per-primitive step, `draw_break`, etc.), the inspector additionally
shows the call log tree with the current node highlighted, the
current section path, and the source location of the next entry.

### In-progress framebuffer mirroring is universal

Mirroring the in-progress framebuffer to the host display is a
property of **"draw is suspended"** — not of the section-step
machinery. Any pause during the draw phase triggers it, regardless
of source:

- The inspector pausing on a section boundary or per-primitive
  step.
- A `dev.draw_break()` call.
- An ordinary code breakpoint set via the DAP server or IDE on a
  line inside cart `draw()` source.
- A DAP "pause" command issued mid-frame.
- An unhandled error inside `draw()` — the framebuffer at the
  point of failure is often the most direct clue to what went
  wrong.

This means a cart with **no** `dev.draw_section` instrumentation
still gets useful mid-frame inspection: the author sets a regular
breakpoint on a line in the draw code, hits it, and immediately
sees the partial framebuffer. Section instrumentation enriches the
experience (named navigation, section-aware step semantics) but is
not a prerequisite for live mid-draw display.

### Step granularity (debugger-shaped)

The inspector exposes the standard debugger step set, with the call
log tree as the structure they navigate:

- **Step**: advance to the next entry (primitive or boundary).
- **Step into**: at a section begin, advance into its first child.
- **Step over**: skip to the entry immediately after the current
  section ends — useful to fast-forward past a section the user
  isn't interested in.
- **Step out**: skip to the entry immediately after the current
  enclosing section ends — useful to bail out of a section the
  user has dropped into.
- **Continue**: resume to the next breakpoint or end of frame.
- **Step back**: re-render frame N from primitive 0 through
  primitive K-1 using the recorded log. Cheap on paletted
  software rendering: clear the buffer and replay K primitives.
  No per-step framebuffer snapshots needed.

### VS Code / DAP integration

The DAP server (ADR-0044) gains draw-inspection commands:

- `setDrawSectionBreakpoint(id)` — pause on begin of this section
  in the next frame.
- `drawStep`, `drawStepInto`, `drawStepOver`, `drawStepOut`,
  `drawStepBack`, `drawContinue`.

The existing webview panel that hosts the runtime display
(ADR-0044) is the inspector view; pausing simply changes what the
runtime is presenting. Sidebar UI lists the call log with the
active node highlighted and section path breadcrumbs; clicking an
entry seeks the framebuffer to "after this primitive". For text-
mode debugging in the bare CLI runner, key binds drive the same
operations and the call log can be dumped to stdout.

### Validation

- **Build time.** The packer ensures that every cart-emitted
  section ID referenced in source code is declared in
  `draw_sections:`. Untyped string names are not accepted.
- **Runtime, dev mode (C carts only).** End-of-frame with an
  unclosed cart section, or an end with the wrong ID, surfaces a
  warning identifying the section and source location. Lua's
  closure form makes this check unnecessary — pairing is
  structural.
- **Runtime, dev mode (all carts).** A cart-emitted section
  attempting to use an ID Stage owns (or vice versa) is impossible
  by construction — they are separate namespaces with disjoint
  type tags.

### Hot-reload interaction

If the cart's draw code is reloaded (ADR-0045) while the inspector
is paused mid-frame, the partially-executed call log no longer
matches the live code. The simplest behavior — and the one chosen
here — is to abort the inspection session: clear the log, finish
the frame using the new code in continuous mode, and let the user
re-trigger the inspector on the next frame. Stepping into a frame
that started under stale code is more confusing than the lost
session.

Cart code reloads under `update()` are unaffected — inspection is
a draw-phase concern.

### Release behavior

All of the above is dev-mode-only. In release builds:

- The recording machinery is `#ifdef`-ed out (or the equivalent
  for the Lua binding).
- `blyt_dev_draw_section_begin`, `_end`, and `blyt_dev_draw_break`
  resolve to empty inlines.
- `blyt32.dev.draw_section(id, fn)` becomes `fn()` (Lua-side
  inlining of the closure invocation, no instrumentation).
- No memory allocation, no extra branches in the draw call path.

This follows ADR-0065's pattern; nothing in this ADR changes the
release-build cost of `draw()`.

## Consequences

- Authors can inspect the build-up of a frame without leaving the
  dev loop. The 320×240 paletted display makes "mirror the
  framebuffer mid-draw" a memcpy; no GPU frame-debugger plumbing
  needed.
- Live mid-draw display works for **any** pause source — including
  ordinary code breakpoints set in cart draw source — so an
  uninstrumented cart still benefits. Section instrumentation
  enriches navigation but is not required to see the framebuffer
  paused mid-frame.
- Cart-declared section names follow the established
  packer-generated-constant pattern (ADR-0059), so authors don't
  learn a new mental model and tooling has stable IDs and types.
- Tooling can offer breakpoint candidates and section
  autocompletion before any frame runs — the manifest plus the
  runtime's well-known Stage sections produce a complete pre-frame
  name list.
- Manifest declares names only; the runtime tree stays flexible.
  An author whose cart conditionally emits the minimap section
  isn't fighting a manifest-declared structure.
- The Lua closure API removes a class of bugs (mismatched
  begin/end, sections accidentally left open across early returns
  or errors). C carts trade that automatic safety for the
  lower-level API they already use elsewhere.
- Section instrumentation in cart source doubles as living
  documentation of the frame's intended phase structure — useful
  in code review and onboarding.
- The recording is independent of the rest of the dev capture
  (ADR-0074). A user investigating a layout bug doesn't need a
  rewind capture; they enable section-stepping and step the live
  frame. For postmortem analysis the rewind capture's replay
  drives the same recorder, so the inspector works on past frames
  too.
- Stepping is a presentation-layer affordance only — `update()`
  is suspended while paused, so the cart's simulation stays
  consistent and resuming continues normally.
- Backward stepping is cheap because draw is pure over the call
  log; no per-step framebuffer snapshots are stored.
- Adding new built-in primitives requires extending the recorder
  and the inspector's pretty-printer for that primitive — a
  bounded one-time cost per new draw API.
- Cart-emitted and Stage-emitted sections share the inspector UI
  but live in disjoint ID namespaces, so neither can collide with
  the other.
