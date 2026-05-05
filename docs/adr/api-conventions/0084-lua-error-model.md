# ADR-0084: Lua error model — three tiers, VM-external panic termination

## Status
Accepted

## Context

The C API uses `blyt_result_t` return codes throughout (ADR-0046). The Lua
API needs a corresponding error model that is idiomatic for Lua authors,
consistent with the C model, and that handles the distinction between
recoverable errors and unrecoverable panics.

Lua has a built-in error mechanism: `error()` raises a value that propagates
up the call stack until caught by `pcall` or `xpcall`. If nothing catches it,
it reaches the runtime's own `lua_pcall` backstop (the boilerplate that calls
`init`/`update`/`draw`, per ADR-0025). The question is which errors should be
catchable by cart code and which should not.

An initial approach considered replacing `pcall` with a wrapper that
re-raises special "panic sentinel" values — analogous to Python's
`BaseException` vs `Exception`. This is rejected (see below).

## Decision

**Three tiers of error handling apply to the Lua API.**

### Tier 1: Silent failure

Operations where failure has no useful recovery path and checking would
clutter every call site. The call is a no-op on failure.

```lua
gfx.pixel(x, y, color)    -- out-of-bounds: ignored
voice:volume(0.5)          -- voice finished: silent no-op
voice:is_playing()         -- voice finished: returns false, not an error
```

These are all operations where the worst outcome is a missing pixel or a
missed audio nudge — not a corrupted game state. The corresponding C
functions return `blyt_result_t` but Lua does not surface it.

### Tier 2: nil + last_error_code

Operations that can fail in ways the cart may legitimately want to handle.
The function returns `nil` on failure; `blyt32.last_error_code()` returns
the specific code.

```lua
local data, meta = save.read("slot1")
if not data then
    local code = blyt32.last_error_code()
    if code == ERR.SAVE_NOT_FOUND then
        -- first run: start fresh
    end
end
```

This is the standard recoverable-error path. `pcall` can catch any Lua error
raised during these calls. The full set of error codes (grouped by subsystem,
ADR-0046) is available.

### Tier 3: VM termination (panic)

Unrecoverable conditions that the cart cannot and should not handle. The
runtime terminates the RISC-V VM externally — no Lua error is raised, so
`pcall` is never involved. Panic reasons:

- **Watchdog**: `update` or `draw` exceeded the hard time budget (ADR-0037).
- **OOM**: cart exhausted its memory quota. The runtime's custom allocator
  terminates the VM directly instead of returning `NULL` to Lua (which would
  enter Lua's error system as a catchable memory error).
- **Fatal API violation**: the ECALL handler detects misuse that indicates
  corrupted runtime state, not a cart logic error. The handler terminates the
  VM before returning.
- **Illegal instruction**: the emulator detects an invalid guest instruction.

In all cases, termination is external to the Lua VM. The panic callback and
crash diagnostics are handled by the runtime (ADR-0083).

### Why not a pcall sentinel

Replacing `pcall` with a wrapper that re-raises special sentinel values was
considered and rejected:

- It requires both `pcall` and `xpcall` to be replaced.
- The sentinel must be unforgeable from Lua (requiring C-side identity
  checks), adding complexity.
- It is unnecessary: because Lua runs inside the RISC-V emulator (ADR-0025),
  all panic conditions can be enforced at the VM level, outside Lua's error
  system entirely. The sentinel approach solves a problem that the
  architecture does not have.

Standard `pcall` and `xpcall` are kept as-is (ADR-0079).

### The boundary: recoverable vs. fatal API violations

Most API misuse (bad handle, wrong argument type, quota exceeded) is tier 2:
the ECALL returns an error code, Lua sees `nil`, cart code can branch on it.

A violation is fatal (tier 3) only when continuing would corrupt the
runtime's own internal state — not merely the cart's. In practice this means
a very small set of cases (e.g., calling an ECALL with a stack pointer the
runtime cannot safely read). Most "this is wrong" misuse is tier 2.

## Consequences

- Lua authors use idiomatic nil-check error handling for recoverable errors
  and `pcall` freely without runtime interaction concerns.
- `pcall` behaves as documented in standard Lua with no surprises.
- Panics are genuinely uncatchable — a cart cannot swallow a watchdog
  timeout or OOM by wrapping its update loop in `pcall`.
- The custom allocator must terminate the VM externally on quota exhaustion
  rather than returning `NULL`, so that OOM never enters the Lua error path.
- Fatal API violations require the ECALL handler to trigger external VM
  termination rather than raising a Lua error — these cases must be
  explicitly identified in the ECALL surface documentation.
