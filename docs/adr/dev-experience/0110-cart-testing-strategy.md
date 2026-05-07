# ADR-0110: Cart testing strategy — frameworks, test tiers, and blyt API mock surface

## Status

Accepted — amends ADR-0109 (TAP protocol confirmed; two-tier test execution model added).

## Context

ADR-0109 established that test binaries compile to RV32IMFC ELF and run inside
the fc32 emulator, with TAP output left as "TBD". The testing frameworks for
each cart language, the blyt API mock surface, and the relationship between
emulator-based and host-based test execution were left unspecified.

Cart code is written in C, Lua, and Rust. Each language has its own testing
conventions, and the RV32IMFC target constrains which frameworks can run on
the device. The three languages have meaningfully different answers.

## Decision

### Two-tier test execution

Tests run in two modes:

**Emulator mode** (`blytbuild test`) — the authoritative tier. Test binaries
compile to RV32IMFC ELF and execute inside the fc32 emulator. Results reflect
the real target environment: ABI, integer sizes, emulator bugs, and blyt API
behaviour are all as-shipped. This is the mode CI runs.

**Host mode** (`blytbuild test --host`) — the fast-iteration tier. Test
binaries compile for the native host target (macOS ARM64). The blyt API
boundary is stubbed; no emulator is required. ASan and UBSan are available.
Tests that call real blyt functions fail to link on host and are excluded from
this mode automatically — a compile-time enforcement of the tier boundary.

Both modes use the same source set. There is no separate source directory for
host-only vs. emulator-only tests. The compilation target determines what runs
where.

### C: Unity + fff

**Unity** is the unit testing framework for C cart code:

- Single `.c` + `.h`, zero POSIX or heap dependencies — compiles to RV32IMFC
  bare-metal without modification.
- TAP output mode built in (confirms ADR-0109's protocol as TAP).
- No test registration boilerplate: `TEST_CASE` macros are sufficient.

**fff** (Fake Function Framework) provides mocking:

- Header-only, no code generation, no build tool dependency.
- Generates controllable per-call fakes for individual blyt SDK wrapper
  functions. Only the functions under test need fakes; the rest can be left
  unresolved in host mode or resolved to the real ECALL stubs in emulator mode.

**State reset between tests.** Blyt state buffers are globally accessible POD
memory. A test that leaves buffers dirty contaminates subsequent tests. The
SDK provides `blyt_test_reset()`, which zeroes all blyt state buffers to a
known default state. Cart test code calls it at the top of each test case (or
in a Unity `setUp` function) to guarantee a clean starting state regardless of
execution order.

Unity and fff are vendored into the SDK. No package manager is required; cart
projects receive them as part of the SDK distribution.

### Lua: busted on host Lua 5.4

Lua unit tests run against **standard host Lua 5.4** using **busted**
(BDD-style `describe`/`it`, built-in spies and stubs, TAP output). The
`blyt.*` and `blyt32.*` modules do not exist on host Lua; they are stubbed
before tests run as plain Lua tables:

```lua
blyt = { audio = {}, state = {}, time = {}, rng = {}, log = {} }
blyt32 = { gfx = {}, input = {}, color = {}, spatial = {} }
```

This is sufficient for testing game logic. No emulator is required for the
bulk of Lua unit tests. Emulator-based Lua tests remain available for
target-environment validation but are not the primary unit test path.

### Rust: #[test] on host, blyt_test for emulator

**Host**: standard `#[test]` and `#[cfg(test)]` against the native target.
`cargo test` works without modification. The blyt FFI boundary is stubbed via
`#[cfg(test)]` conditional implementations in the cart's own codebase.

**Emulator**: the SDK ships a `blyt_test` crate providing a custom test
harness for `no_std` RV32IMFC targets:

- `#[blyt_test::test]` proc macro attribute marks test functions.
- Uses linker-section collection (via `linkme` or equivalent) — no unstable
  `custom_test_frameworks` feature required, no nightly compiler required.
- The crate provides the `main` entry point that iterates collected tests,
  outputs TAP, and exits with pass/fail code.

`blyt_test` is not published to crates.io. `blytbuild` injects a
`[patch.crates-io]` entry via `cargo --config` (Cargo 1.63+) pointing to the
SDK installation path. Cart `Cargo.toml` declares a normal dev-dependency:

```toml
[dev-dependencies]
blyt_test = "1"
```

`blytbuild` resolves it to the SDK-local copy transparently, consistent with
its existing role as the build driver.

### Blyt API mock surface for C tests

The blyt API is a clean test seam: all cart code calls named SDK wrapper
functions (never raw ECALLs), so any function can be replaced with an fff
fake per test.

**Input** — not mocked per call. Input state does not change mid-test; it is
set up as a fixture before the test runs via the dev/instrumentation ECALL
range (900–999, ADR-0085). No per-call fake is needed.

**Time** — `blyt_time_frame()` is the one genuine fff stub. Tests that cover
frame-dependent logic (animation, cooldowns, timer countdowns) need a
controlled return value. A single fff fake covers this entirely.

**Audio** — small capturable surface with fff. Record which cues were
triggered with which arguments; assert on them. The audio subsystem has no
return values that game logic branches on, so capture-only fakes are
sufficient.

**Graphics** — three approaches in preference order:

1. **Logic extraction** (preferred): keep "what to draw and where" — position,
   sprite index, visibility — in pure functions that return data. Test those
   functions directly. The draw calls themselves are a thin, untested shell.
   This is the right structural discipline regardless of test tooling.

2. **Selective fff stubs**: fake only the specific `blyt_gfx_*` functions the
   code under test calls. Most systems call a small subset of the full graphics
   API. Use when logic-extraction refactoring is not yet in place.

3. **Framebuffer comparison**: run in emulator mode, capture pixel output, diff
   against a reference image. A separate visual regression tier, not a unit
   test. Complementary to the above; not a substitute.

**Not mocked**: state buffers (directly writable in C), RNG (seedable per
ADR-0041; already deterministic), math and easing functions (pure functions
in the SDK headers, always available).

### Test output

**TAP** is the unified internal protocol across all three languages (amending
ADR-0109's "TBD"). Each framework emits TAP to stdout; blytbuild reads it:

| Language | Framework | TAP mechanism |
|---|---|---|
| C | Unity | Unity TAP output mode |
| Lua | busted | busted `--output TAP` |
| Rust (emulator) | blyt_test | blyt_test runner |
| Rust (host) | cargo test | blytbuild translates or passes through |

**JUnit XML** is the CI artifact format. blytbuild converts TAP output to
JUnit XML and writes it to `build/test-results/<language>-<mode>.xml`
(e.g. `c-host.xml`, `rust-emulator.xml`). CI systems (GitHub Actions, etc.)
consume these files directly. No TAP tooling is required in the CI pipeline.

### Test filtering

`blytbuild test --filter <pattern>` runs only tests whose names match the
given pattern. The pattern is a substring match by default; prefix with `/`
for a regex. blytbuild threads the filter through to each framework's native
mechanism:

| Language / mode | Mechanism |
|---|---|
| C (Unity) | blytbuild compiles a filter shim that skips non-matching test names at registration time |
| Lua (busted) | passed as `--filter <pattern>` to busted |
| Rust (host) | passed as the test name argument to `cargo test` |
| Rust (emulator) | passed to the `blyt_test` runner which skips non-matching entries |

`--filter` applies to both `--host` and emulator modes and composes with
`--watch`.

### VS Code integration

`blytbuild new` ships a pre-configured VS Code workspace (ADR-0044) that
includes `tasks.json` and `launch.json` entries for test running and
debugging. Cart authors get working test IDE integration from the first
`blytbuild new`.

#### Running tests

Four tasks cover the two execution tiers, with and without watch mode. The
host task is the default test task (bound to the IDE's test shortcut):

```json
// .vscode/tasks.json
{ "tasks": [
  {
    "label": "test: host",
    "type": "shell",
    "command": "blytbuild test --host",
    "group": { "kind": "test", "isDefault": true }
  },
  {
    "label": "test: host (watch)",
    "type": "shell",
    "command": "blytbuild test --host --watch",
    "group": "test",
    "isBackground": true,
    "problemMatcher": []
  },
  {
    "label": "test: emulator",
    "type": "shell",
    "command": "blytbuild test",
    "group": "test"
  }
]}
```

`--watch` is only offered for the host tier; emulator startup overhead makes
watch mode impractical on the emulator tier. `--filter` composes with both:
`blytbuild test --host --watch --filter player` re-runs matching tests on
every save.

#### Debugging host C tests

Host test binaries are native executables. VS Code launches them directly via
CodeLLDB (or the C/C++ extension). A build-only task produces the binary
without running it:

```json
// .vscode/tasks.json (additional entry)
{
  "label": "build: c tests (host)",
  "type": "shell",
  "command": "blytbuild test --host --no-run"
}

// .vscode/launch.json
{
  "name": "Debug C tests (host)",
  "type": "lldb",
  "request": "launch",
  "program": "${workspaceFolder}/build/test/c/tests",
  "preLaunchTask": "build: c tests (host)"
}
```

#### Debugging emulator tests

Emulator-mode debugging uses the GDB remote stub, the same mechanism as game
debugging (ADR-0044/0045). The flow requires two steps: start the emulator
with the test binary and GDB stub listening, then attach. A background task
handles the emulator startup; VS Code waits for the stub-ready sentinel before
attaching:

```json
// .vscode/tasks.json (additional entry)
{
  "label": "test: start emulator (debug)",
  "type": "shell",
  "command": "blytbuild test --debug --port 3333",
  "isBackground": true,
  "problemMatcher": {
    "pattern": { "regexp": "." },
    "background": {
      "activeOnStart": true,
      "beginsPattern": "blytbuild: starting emulator",
      "endsPattern": "blytbuild: GDB stub listening on :3333"
    }
  }
}

// .vscode/launch.json
{
  "name": "Debug tests (emulator)",
  "type": "cppdbg",
  "request": "launch",
  "program": "${workspaceFolder}/build/test/c/tests.elf",
  "miDebuggerServerAddress": "localhost:3333",
  "miDebuggerPath": "/path/to/riscv32-gdb",
  "preLaunchTask": "test: start emulator (debug)"
}
```

`blytbuild test --debug --port <n>` builds the test binary, starts the
emulator with it loaded, opens the GDB stub on the specified port, and emits
the sentinel line to stdout when the stub is ready. The exact sentinel strings
(`blytbuild: starting emulator`, `blytbuild: GDB stub listening on :3333`) are
stable and part of the blytbuild output contract.

#### Debugging Rust host tests

`rust-analyzer` discovers `#[test]` functions and provides gutter run and
debug buttons automatically — no `launch.json` entry is needed. To extend
this to emulator-targeted tests, the `#[blyt_test::test]` proc macro also
emits `#[test]` when compiling for the host target, so those functions appear
in rust-analyzer's test lens and can be run or debugged via the gutter in the
same way.

## Consequences

- Most unit tests run without the emulator — fast TDD iteration for logic in
  all three languages.
- Emulator mode is authoritative; CI runs both tiers.
- The C blyt mock surface is minimal: one fff stub (`blyt_time_frame`), fff
  capture for audio, input via fixture injection. No full API mock layer.
- Graphics unit testing is deliberately minimal; logic extraction is the
  preferred structural discipline.
- `blyt_test` requires blytbuild to inject Cargo config at build time,
  consistent with blytbuild's existing role managing the RV32IMFC toolchain
  invocation.
- Host tests that call real blyt functions fail to link — compile-time
  enforcement of the tier boundary with no runtime machinery needed.
- Unity and fff are vendored by the SDK; cart authors have no framework
  selection or version management burden for C testing.
- `blyt_test_reset()` is a required SDK function; its contract (zeroes all
  blyt state buffers) must be maintained across SDK versions.
- `blytbuild new` ships pre-configured `tasks.json` and `launch.json` entries;
  test running, watch mode, and debugging work in the IDE from project creation
  with no manual setup.
- blytbuild writes JUnit XML to `build/test-results/` after every test run.
  The filenames (`<language>-<mode>.xml`) are stable and part of the blytbuild
  output contract.
- `blytbuild test --filter`, `--host`, `--watch`, `--no-run`, `--debug`, and
  `--port` are all required blytbuild subcommand flags. `--watch` is restricted
  to `--host` mode.
- `blytbuild test --debug --port <n>` must emit two stable sentinel lines
  (`blytbuild: starting emulator`, `blytbuild: GDB stub listening on :<n>`)
  that must not change without a corresponding update to the generated workspace
  templates.
- Test timeouts are a CI job responsibility; blytbuild does not implement an
  internal timeout.
- `#[blyt_test::test]` emitting `#[test]` on host targets is a requirement of
  the proc macro implementation, not an optional convenience.
