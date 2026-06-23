# ADR-0104: VS Code dev shell — renderer-side runtime with Node bridge

## Status
Proposed

## Context

The VS Code extension dev shell needs to execute a cart, render its
320×240 paletted framebuffer (ADR-0003), mix and play audio (ADR-0004),
sample input, and additionally provide dev-only operations the
production runtime doesn't need: filesystem-backed asset hot reload,
save-state inspection, profiling/debug hooks, and Pi-parity feedback
(ADR-0103).

VS Code extensions run in two distinct contexts:

- The **extension host** is a Node.js process. It has filesystem,
  child_process, native modules, and `process.hrtime.bigint()`
  nanosecond timing. It cannot render or play audio.
- The **web view** (renderer) is a Chromium context. It has
  `<canvas>`, Web Audio, gamepad and keyboard events. By default it has
  the 100 µs `performance.now()` floor (no cross-origin isolation;
  `--enable-coi` reduces to 5 µs but is host-controlled, not
  extension-controlled).

The cart runtime — Lua VM, mixer, framebuffer, save state, scheduler
— is the same code regardless of where it runs. The question is which
context it lives in, and what crosses the IPC boundary.

Three architectures are viable:

### Option A — All-renderer

Cart and runtime both in the web view. No IPC in the hot path. Subject
to the 100 µs timer floor. G.3's accumulated-debt throttle is required
for Pi-parity feedback. This is also the architecture for portable
web-shell distributions (itch.io, hosted dev demo, vscode.dev) where
no Node host is available.

### Option B — Cart Node-side, runtime renderer-side

Cart logic and Lua VM in the Node extension host; canvas, audio mixer,
input in the web view. Per frame: ship framebuffer (~75 KB), audio
samples (~1.5 KB), input requests across `webview.postMessage` IPC.
Node-side gets `process.hrtime.bigint()` nanosecond timing — the 100 µs
floor doesn't apply, so per-line busy-wait or accumulated-debt both
work at full configured precision.

Costs: ~5 MB/s through the IPC boundary at 60 Hz; copy-only (no
`Transferable` for webviews); no `SharedArrayBuffer` without COI.
Splits the runtime between two processes — same logical runtime, two
different host environments to maintain.

### Option C — All-renderer, plus library-shaped Node IPC

Cart and runtime renderer-side as in A — no per-frame IPC, no 75 KB
copy. The Node side exposes a small surface that the cart's library
calls into for operations Node is uniquely positioned for. The
renderer-side library has two implementations of those calls: a local
fast path for production-equivalent behavior, and an IPC stub that
routes to Node-side handlers in dev. The cart sees the same API
regardless.

Operations that route through the Node bridge:

- **Asset hot reload** — Node watches the cart's source/asset
  directory; on change, ships a reload command back to the renderer.
- **Save-state writes** — to `workspaceStorage` via Node-side
  `vscode.workspace.fs`.
- **Native rv32emu** *(optional)* — if dev mode wants to run the cart
  through the actual emulator rather than WASM Lua-direct, Node spawns
  the binary and IPCs at the cart-library boundary. The renderer-side
  surface is unchanged.
- **Profiling/debug hooks** — Node-side tools that need filesystem
  and process access.

Pi-parity throttling stays renderer-side using G.3's mechanism — IPC
round-trip latency (~1–2 ms) is too coarse to drive a per-line throttle
hook anyway, so the ns-timing advantage of moving cart-side into Node
is moot for that purpose.

## Decision

**Option C — all-renderer cart and runtime, with a library-shaped IPC
bridge for specific Node-side operations.** Filed Proposed; subsequent
work either accepts this with concrete API surface or supersedes.

## Consequences

- **G.3's accumulated-debt throttle is the primary mechanism**, not a
  fallback for portable shells only. The 100 µs floor applies; the
  spike's ±2.5 % accuracy against configured D is the working
  precision. ADR-0103's Open Question 5 collapses into a reference to
  this ADR.
- **No per-frame IPC.** Hot path is local to the renderer. The 75 KB
  framebuffer ship described in Option B doesn't happen.
- **Library API has a dual surface in dev builds.** Most calls
  resolve locally (fast); a small set route to Node via IPC. The
  cart-author-visible API is identical; the routing is a build-time
  decision in the runtime shim.
- **Same renderer-side runtime for VS Code and portable web shells.**
  Web-only distribution builds simply ship without the Node bridge —
  hot reload and dev-only operations are absent, but the cart's normal
  execution path is identical. One runtime to test, not two.
- **Node-side surface is small and well-bounded.** It is *not* a
  general "anything can route to Node" mechanism; it is a defined set
  of dev-only operations that each have a clear motivation (filesystem,
  process spawn, host APIs).
- **Hot path stays bound by the renderer's constraints.** No
  `SharedArrayBuffer`, 100 µs timer floor, no native modules, etc.
  Anything that wants to escape those constraints would need to
  motivate either re-architecting onto Option B or accepting an
  IPC-per-call cost.

## Open questions

1. **Concrete API surface for the Node bridge.** What set of cart
   library calls (or sub-API namespaces) route across IPC? Asset
   reload and save state are obvious; debug/profiling and rv32emu
   passthrough are open. Decide alongside the dev-UI design.
2. **IPC contract.** `webview.postMessage` with structured-clone
   payloads, request/reply matched by ID, async on the renderer side.
   Standard pattern; needs writing down once.
3. **Web extension (vscode.dev) compatibility.** Web extensions don't
   have a Node host; the bridge is unavailable. Either gate dev-only
   features off in that environment, or fall back to renderer-side
   shims for the bridge operations that have viable browser
   equivalents.
4. **Production web build sharing.** The renderer-side runtime in this
   architecture *is* the production web build, just with the bridge
   stub absent. Worth confirming in a later ADR that this is the
   intended overlap.

## References

- ADR-0103: Dev-mode Pi-parity feedback — Open Question 5 (renderer
  vs extension host) is resolved by this ADR.
- ADR-0044: CLI packer and VS Code dev loop.
- ADR-0045: Hot reload via the save/restore mechanism.
- ADR-0003: Display — 320×240 paletted, 256 colors, double-buffered.
- ADR-0004: Audio format tiers.
- Spike G.3 results — `docs/design/spike-g.3-results.md`
  (accumulated-debt mechanism PASS under the 100 µs renderer floor).
- Spike W — `docs/design/spike-w-cart-module-swap.md`: seamless native
  reload-while-debugging requires the lldb-dap `program` to be a **stub ELF**
  with the cart loaded **as a shared library** (never the executable), so it is
  cleanly unloadable/reloadable across reloads. A debug-session-structure
  consequence for this dev-shell architecture; filed as a follow-up to #90.
