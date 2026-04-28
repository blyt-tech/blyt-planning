# ADR-0083: Lua cart crash diagnostics — runtime-owned dump

## Status
Accepted

## Context

When a cart panics, the runtime kills the RISC-V VM externally (ADR-0084).
For native carts, this still allows a brief window: the runtime can call the
cart's `fc_cart_panic` function pointer before terminating, and the cart's
native code can execute diagnostic logic within that window.

For Lua carts, no such window is available. The VM is terminated from the
outside; no further Lua code can run. The `fc_cart_panic` callback model
does not apply.

A separate question is what diagnostic information the runtime should present
when any cart crashes, and whether the debug overlay data (frame timer,
memory graph, state-buffer inspector) should be available at the moment of
the crash.

## Decision

### `fc_cart_panic` is restricted to native carts

The `fc_cart_panic` callback (called by the runtime with a brief window
before termination) is defined only for native carts. It is not part of the
Lua cart contract. The SDK's Lua cart template does not define or reference
it.

For native carts, the existing model holds: the cart gets a brief execution
window; only `fc_log_*` calls are valid inside it; the runtime kills the cart
after a hard deadline regardless.

### Lua cart panics: runtime-owned diagnostic dump

When a Lua cart panics, the runtime collects and displays everything it owns
without invoking any cart code. The runtime always has access to this data
because it owns the relevant structures independently of the cart.

**Always dumped (dev and release):**
- Panic reason (`WATCHDOG` / `OOM` / `FATAL_API_VIOLATION` / `ILLEGAL_INSN`)
- Frame count at time of panic
- Last `fc_last_error` string

**Dev mode additionally:**
- Full contents of all state buffers, formatted by field name using the
  layout schema (the runtime owns the schema; it can render any buffer
  without cart cooperation)
- Lua call stack, if obtainable (see below)
- Last N frames of debug overlay data: frame timer, per-subsystem CPU
  breakdown, memory usage, audio voice activity
- Memory usage breakdown at time of panic

**Release mode:**
- Crash screen with reason and frame count only. State buffer contents are
  not shown (they may contain game data the author considers private).

### Lua call stack availability

Whether the runtime can extract a Lua call stack depends on the panic reason:

- **Fatal API violation**: the panic is triggered from within an ECALL
  handler, before the VM is terminated. At this point the VM is in a coherent
  state. The runtime can read the Lua call stack via the VM's debug interface
  before issuing the kill.
- **Watchdog / OOM**: the VM is terminated mid-execution in an unknown state.
  Attempting to run further code or read VM internals is unsafe. No stack
  trace is produced; the panic reason and frame count are the primary signal.

### Debug overlay data is runtime-owned

For the overlay data to be available at crash time — when the cart can no
longer push updates — the runtime must maintain the overlay data structures
as continuously updated ring buffers, independent of the cart. The cart does
not push data to the overlay; the runtime samples what it needs each tick
(frame timing from its own loop, memory from its allocator, audio from the
mixer). This data is therefore always current and always accessible, even
after cart death.

This has the additional benefit that the debug overlay is always accurate
and cannot be spoofed or suppressed by cart code.

## Consequences

- Lua cart authors have no crash callback to implement; the runtime handles
  everything. This is simpler for authors and eliminates a category of
  "my panic handler panicked" failure modes.
- Native cart authors retain `fc_cart_panic` for use cases like flushing
  an in-progress save or logging subsystem-specific state that the runtime
  cannot know about.
- The debug overlay data structures must be implemented as runtime-owned ring
  buffers sampled each tick, not as cart-push data. This is a design
  constraint on the overlay implementation.
- Dev-mode crash screens are information-rich without requiring any cart
  cooperation: field-named state buffer dumps give authors the equivalent of
  a structured core dump.
- The Lua stack trace is available for the most actionable panic class (API
  violations, where the author made a specific wrong call) and absent for
  the less actionable classes (watchdog, OOM, where the stack trace is less
  useful than the state dump anyway).
