# ADR-0121: C++ as a supported cart language — conventions and codegen

## Status

Accepted.
Amended by ADR-0127 (libc++ is a modified fork of LLVM libc++, not stock
upstream; sourcing, modifications, and SDK distribution are specified there).

## Context

C++ is widely used in game development and the broader ecosystem of
libraries a cart author may wish to integrate (physics engines, data
structure libraries, audio DSP, etc.). It is not the primary target
language — that position is shared by Lua (scripting) and Rust (native) —
but it is important enough that the console should bless a specific way
to use it rather than leaving cart authors to discover the constraints
themselves.

The goals are:

- Support C++ as a native cart language for authors who need it, and for
  carts that want to include C++ ecosystem libraries such as Box2D.
- Avoid ABI coupling between the runtime and cart code: the runtime must
  not expose a C++ API surface.
- Keep the SDK small: no bundled C++ standard library in the runtime;
  static linking from an SDK-supplied libc++ instead.
- Produce a documented, opinionated set of conventions rather than leaving
  each cart author to reinvent the wheel.

## Decision

### Declaring a C++ cart

`c++` must be quoted in YAML — the `+` characters confuse many parsers
when the value appears as a mapping key. Use double quotes:

```yaml
# Single-language C++ cart
language: "c++"
```

For carts that mix C++ game logic with vendored C libraries:

```yaml
languages:
  "c++":
    codegen: true
  c:
    codegen: false
    sources:
      - vendor/box2d/src/box2d.c
```

The `codegen: true` field tells the packer to emit C++ headers (`.hpp`)
in addition to C headers (`.h`) for this cart's declared resources, RNGs,
and constants.

### Compilation constraints

All C++ cart code is compiled with:

```
-fno-exceptions  -fno-rtti
```

Both flags are mandatory. They are enforced by the SDK's default compiler
flags and must not be disabled.

**No exceptions.** On Linux and RISC-V, C++ uses the "Itanium C++ ABI"
— a specification named after the processor where it was first developed,
but used universally on all non-Windows platforms regardless of
architecture. This ABI's exception-handling mechanism requires DWARF-based
unwind tables (`.eh_frame` sections) to be emitted for every function that
could be part of a `throw`/`catch` path. These tables add meaningful binary
size. With `-fno-exceptions`, no unwind tables are emitted, and `throw`
becomes `std::terminate()`. Standard library features that unconditionally
throw (e.g. `std::vector::at`) are available but terminate on out-of-range
access rather than throwing.

**No RTTI.** `typeid` and `dynamic_cast` are disabled. `std::any` and
any other standard library feature that requires RTTI is unavailable.
This is consistent with the `-fno-exceptions` stance and reduces binary
size.

These constraints are standard practice in console and embedded game
development. They are not novel restrictions.

### Static libc++

The SDK ships a pre-built `libc++.a` and `libc++abi.a` targeting the
console's ABI (RV32 ILP32). Cart C++ code links these statically. The
runtime does not expose libc++ symbols and does not depend on or control
the version of libc++ the cart uses.

The SDK-supplied libc++ is a **modified fork** of LLVM libc++, not the
stock upstream library. It removes facilities unavailable in the cart
execution environment (`<filesystem>`, `<fstream>`, `<thread>`, `<locale>`)
and stubs `std::random_device` to terminate. The full list of modifications,
sourcing, and build process are specified in ADR-0127.

**Why static, not dynamic.** Dynamic libc++ would require the runtime to
provide a versioned libc++ ABI boundary and ship the library as part of
the platform. That introduces ABI lock-in, increases the runtime's surface
area, and creates version skew problems over the platform's lifetime.
Static linking means each cart carries its own copy; the runtime is
unaffected.

**Size.** Static libc++ with LTO and `-fno-exceptions -fno-rtti` strips
down significantly. LTO is mandatory in the SDK's default release build
flags (see below). The expected overhead for a typical cart using
`<algorithm>`, `<vector>`, `<optional>`, and `<variant>` is well within
the cart binary budget.

**LTO is mandatory in release builds.** The SDK's release toolchain
configuration enables LTO by default. Cart authors must not disable it.
LTO is what makes the static-libc++ model viable; without it, dead
standard-library code is not eliminated. The form libc++ is shipped in
(machine-code archive vs LLVM bitcode vs fat LTO objects) governs how far LTO
can reach and whether it ties authors to the bundled toolchain — see ADR-0127.
Note that `-ffunction-sections` + `--gc-sections` already eliminates dead
standard-library code at function granularity; whole-program LTO is an
additional optimisation beyond that (ADR-0127 records the current status).

### The API boundary is C

The runtime API is `extern "C"`. Cart C++ code calls the runtime through
the same C headers used by C and Rust carts. No C++ types — not even
standard ones — cross the boundary between cart code and the runtime.

This is the load-bearing constraint that makes static libc++ safe. Two
copies of libc++ (one in a cart, one hypothetically in the runtime) cause
ODR violations and ABI problems only if C++ types cross the boundary.
Since the boundary is C-only, there are no such problems.

The same rule applies at any language boundary within a cart. If Lua or
Rust code in a hybrid cart calls into C++ (e.g. a C++ physics library
called from Lua), the interface must be `extern "C"`. Lua's C API and
Rust's FFI both require C-linkage functions; neither can call C++ directly.
Cart authors who write libraries for use by other carts should also expose
a C API at the library boundary for the same reason.

### Error handling — `std::expected`

Without exceptions, error propagation requires an explicit mechanism. The
idiomatic C++ error-handling pattern for blyt carts is `std::expected<T, E>`
(C++23) or `tl::expected` (the C++17 backport, included in the SDK).

```cpp
std::expected<Sprite, LoadError> load_sprite(ResourceHandle r);
```

`std::optional` remains appropriate for nullable returns where there is no
error to report. `std::variant` (with `-fno-exceptions`: bad access
terminates) is appropriate for discriminated unions.

Boost error-handling facilities that are compatible with `-fno-exceptions`
(e.g. `boost::outcome`, `boost::variant2`) are usable but not part of the
SDK. Cart authors who vendor them are responsible for the added dependency.

### `std::random_device` is unavailable

`std::random_device` reads from `/dev/urandom`, which is not accessible
within the console's seccomp sandbox. The SDK stubs it out; any attempt
to construct `std::random_device` terminates.

### RNG — `blyt32::Rng<QueryFn>`

The console provides a controlled RNG (and cart-declared additional RNGs)
whose state is part of the serialised game state. Cart authors who want
randomness that participates in deterministic replay and save/load must
use these RNGs, not independently seeded standard engines.

The SDK provides a fixed template that satisfies `UniformRandomBitGenerator`:

```cpp
// blyt32/rng.hpp  (SDK — written once, not generated)
namespace blyt32 {
template<auto QueryFn>
struct Rng {
    using result_type = uint32_t;
    static constexpr result_type min() { return 0; }
    static constexpr result_type max() { return UINT32_MAX; }
    result_type operator()() const { return QueryFn(); }
};
} // namespace blyt32
```

This composes with all standard distributions:

```cpp
std::uniform_int_distribution<int> d(1, 6);
int roll = d(blyt32::Rng<my_rng_next>{});
```

**Packer-generated wrappers.** For each cart-declared RNG, the packer
generates a C header with `extern "C"` declarations and a C++ header with
a type alias:

```c
/* my_rng.h — generated C */
#ifdef __cplusplus
extern "C" {
#endif
uint32_t my_rng_next(void);
#ifdef __cplusplus
}
#endif
```

```cpp
// my_rng.hpp — generated C++
#include "my_rng.h"
#include <blyt32/rng.hpp>
using MyRng = blyt32::Rng<my_rng_next>;
```

The template machinery lives in the SDK once. The generator emits only
the type alias per RNG. The generated C header is included in the C++
header so cart authors include only the `.hpp`.

**Independent standard engines.** `std::mt19937` and other standard
engines are not prohibited. They are appropriate for randomness that is
intentionally outside the game state — for example, a one-shot procedural
level generator seeded from a level hash, where the seed is the state and
the generated output is reconstructed deterministically from it on load.
If such an engine is seeded from a live game-state value and its output
affects game state, its own state must be included in `on_save_state` /
`on_load_state`. The standard engines support serialisation via
`operator<<` / `operator>>` for this purpose. This is the cart author's
responsibility; the runtime does not track standard engine state.

### C++ coroutines and state serialisation

**Background: how C++ coroutines work.**
A C++ coroutine is a function that can pause mid-execution and resume
later. Where a normal function's local variables live on the call stack
and disappear when the function returns, a coroutine's locals are moved
by the compiler into a heap-allocated *coroutine frame* at each suspension
point (`co_await` / `co_yield`). The frame also stores a pointer to the
compiled machine code that resumes the coroutine from that suspension
point. Calling `handle.resume()` on the coroutine's handle jumps back
into that machine code and restores the locals from the frame.

**Why frames are not serialisable.**
The resume pointer inside a coroutine frame is a raw address into the
cart's compiled machine code. It changes with every build and is
meaningless across save/load boundaries. There is no supported way to
snapshot a suspended coroutine frame and restore it, in the same way
that a suspended Lua coroutine can be. (Lua coroutines are data
structures inside the Lua VM; the runtime has full visibility into them.)

**Consequence.** The runtime does not attempt to capture suspended C++
coroutines as part of `on_save_state`. A coroutine that is mid-flight
when a save occurs simply ceases to exist after a load.

**Patterns for handling this:**

*Transient coroutines (safe to abandon).* Visual effects, animations, and
other work where the result is derived from the saved state buffers anyway
need no special handling. On load, the state buffers are restored and the
coroutine is just not recreated:

```cpp
// Animate a pickup bobbing. Abandoning this on load is fine —
// on_load_state will restart it from the saved entity position.
Task animate_pickup(blyt_handle_t entity) {
    for (int f = 0; f < 30; ++f) {
        set_y_offset(entity, std::sin(f * 0.2f) * 4.0f);
        co_await next_frame();
    }
}
```

*Checkpointed coroutines.* For coroutines that drive game-meaningful
progress (a scripted dialogue, a boss phase transition), store the logical
step index in a POD state buffer field. `on_save_state` captures it
automatically; `on_load_state` relaunches the coroutine from that step:

```cpp
// on_save_state captures s_dialogue_step automatically.
// on_load_state calls:
void on_load_state(blyt_load_reason_t) {
    int step = state_get_u8(S_DIALOGUE_STEP);
    if (step > 0)
        launch_dialogue_from_step(step);
}

Task run_dialogue() {
    state_set_u8(S_DIALOGUE_STEP, 1); co_await show_line("Hello.");
    state_set_u8(S_DIALOGUE_STEP, 2); co_await show_line("Goodbye.");
    state_set_u8(S_DIALOGUE_STEP, 0);
}
```

*Drain before save.* For short-lived coroutines that must complete
atomically, run them to `co_return` within a single frame so no suspension
crosses a save boundary. A coroutine that never yields between two save
points is never mid-flight when a save fires.

C++ coroutines are appropriate for transient async work where the above
patterns apply. They are not a substitute for the Lua coroutine / Stage
sequence mechanism for long-running scripted work that must survive
save/load transparently.

This is the same constraint that applies to Rust `async`/`await`
(ADR-0108); the underlying reason is identical.

### SDK structure

```
sdk/
  include/
    blyt32/
      rng.hpp          # fixed template — not generated
  lib/
    libc++.a
    libc++abi.a
```

Packer-generated C++ headers follow the same gitignored-output convention
as generated C headers and Rust modules: they are produced at build time
into the SDK output directory, not checked into the cart source tree.

## Consequences

- C++ carts may freely use `<algorithm>`, `<optional>`, `<variant>`,
  `<expected>`, `<memory>`, `<string_view>`, and the rest of the standard
  library subject to the `-fno-exceptions -fno-rtti` constraints.
  `std::any`, `typeid`, `dynamic_cast`, and `std::random_device` are
  unavailable.
- Ecosystem C++ libraries that respect `-fno-exceptions -fno-rtti`
  (Box2D, most embedded/game-oriented libraries) are usable with no
  special SDK support. Libraries that require exceptions or RTTI are not
  usable.
- The libc++ build for the RV32 ILP32 target should be validated early
  (a build-and-hello-world check is sufficient) before the SDK ships
  documentation that promises it.
- RNG state is automatically managed by the runtime for `blyt32::Rng`
  wrappers. Cart authors who use standard engines for non-state randomness
  must manage serialisation themselves if they later change their mind.
- The no-RTTI, no-exceptions, static-libc++ stance is standard practice
  across major console platforms and is not expected to surprise experienced
  game developers.
