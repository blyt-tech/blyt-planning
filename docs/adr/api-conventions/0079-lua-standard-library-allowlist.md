# ADR-0079: Lua standard library — allowlist, removals, and aliases

## Status
Accepted

## Context

Lua 5.4's standard library includes modules that are incompatible with
the console's constraints:

- **Determinism (ADR-0007):** `math` transcendentals (`sin`, `cos`, `log`,
  etc.) delegate to the host's `libm`, which is not bit-identical across
  platforms. `os.time()`, `os.date()`, `os.clock()` expose wall-clock time.
  `math.random` / `math.randomseed` use the C runtime's RNG, which is
  neither seeded nor tracked.
- **Sandboxing (ADR-0038):** `io`, `file`, and filesystem-related `os`
  functions give carts direct access to the host filesystem. The console
  provides `console.resource.*` as the only sanctioned I/O path.

At the same time, third-party Lua libraries and authors' intuitions assume
the standard library names (`math`, `string`, `require`, etc.) are present
and behave as documented. Removing or renaming them without replacements
breaks code reuse and surprises authors.

## Decision

### Kept without modification

These modules are safe and useful as-is:

| Module | Notes |
|--------|-------|
| `string` | Byte-oriented; no locale-sensitive behaviour. |
| `table` | `table.sort` is deterministic within a fixed Lua version. |
| `coroutine` | Standard library provided; save-state caveats are documented separately (ADR-0012). |
| `utf8` | Read-only iteration utilities; deterministic. |
| `pcall`, `xpcall`, `error`, `tostring`, `tonumber`, `type`, `ipairs`, `pairs`, `next`, `select`, `unpack`, `rawget`, `rawset`, `rawequal`, `rawlen`, `setmetatable`, `getmetatable` | Core Lua globals; safe and expected. |

### Replaced: `math` → `console.math`, aliased as `math`

The standard `math` library is replaced by `console.math`, which provides
deterministic implementations of all transcendentals using a controlled
math library (musl libm). `console.math` is also installed as `math` in
the cart's global environment, so code written against the standard `math`
API works without modification.

Functions that are safe and identical in both:
`abs`, `ceil`, `floor`, `max`, `min`, `fmod`, `modf`, `sqrt`,
`huge`, `pi`, `maxinteger`, `mininteger`, `tointeger`, `type`.

Functions replaced with deterministic equivalents:
`sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `exp`, `log`.

**`math.random` / `math.randomseed`:** These are present and route to a
hidden default RNG stream managed by the runtime. The stream is
deterministic (seeded at cart start from the cart's declared initial seed),
advances normally, and participates in save state (ADR-0041). Third-party
code using `math.random` works and is deterministic. Authors who need
explicit seeds or multiple independent streams use `console.rng` directly.

### Replaced: `require`

The standard filesystem `require` loader is replaced with a custom loader
that resolves module names against Lua source files bundled into the cart
at pack time. The call signature is identical; modules the author has
included in the cart load normally. Modules not bundled cause a clear error
at load time rather than a filesystem error.

The packer collects all `require`'d modules transitively and bundles them
into the cart (similar to how a bundler handles JavaScript imports). Authors
do not need to enumerate dependencies; the packer resolves them.

Filesystem access via `require` is not available. Carts cannot load Lua
files from arbitrary paths at runtime; all Lua source is fixed at pack time.

### Replaced: `print`

`print` writes to the runtime's dev-mode console output. In release builds
it is a no-op. This is the standard convention for embedded Lua and matches
what most Lua authors expect. `console.log()` is the explicit dev API
(ADR-0065 pattern — no-op in release).

### Removed: `io`

The `io` module is not present. All resource access goes through
`console.resource.*`. Attempts to use `io.*` produce a clear "not
available" error in dev mode rather than a silent nil.

### Removed: `os` (except `os.exit`)

The `os` module is not present with the following exception:
`os.exit()` is available as an alias for orderly cart shutdown (equivalent
to the cart returning from its main loop). All other `os` functions
(`os.time`, `os.date`, `os.clock`, `os.getenv`, `os.execute`,
`os.tmpname`, etc.) are absent.

### Removed: `package`

The `package` module (underlying `require`) is not directly accessible
to carts. The custom `require` loader handles all module resolution; the
`package` table internals are not exposed, preventing carts from
manipulating the loader chain or accessing `package.loadlib`.

### Removed: `debug`

The `debug` module is not available to carts. The runtime uses `debug`
internally for its DAP server implementation (ADR-0044), but exposing it
to cart code would break the sandbox. Dev-mode introspection goes through
`console.dev.*` APIs.

### Not present: `load`, `loadfile`, `dofile`

`load` (compiling arbitrary Lua strings at runtime) is not available.
All Lua code is fixed at pack time; runtime compilation would both
circumvent the bytecode model (ADR-0067) and introduce a potential sandbox
escape vector. `loadfile` and `dofile` are absent for the same reasons.

## Consequences

- Third-party Lua libraries using `math.*` and `require` work without
  modification, as long as they don't rely on removed modules (`io`, `os`,
  `debug`, `load`).
- `math.random` in third-party code is deterministic and save-state-safe,
  routing to a hidden console-managed stream.
- Authors familiar with standard Lua find the environment recognisable;
  surprises are at the edges (no `io`, no `load`) rather than in the
  common paths.
- The absence of `load`/`loadstring` is the most likely friction point for
  authors porting code that uses runtime code generation. This is a
  deliberate constraint, not an oversight.
- Dev mode surfaces "not available" errors with console-specific
  guidance ("use `console.resource.load` instead of `io.open`") rather
  than silent nils.
