# ADR-0065: Dev instrumentation — blyt_dev_* as no-ops in release builds

## Status
Accepted

## Context

Profiling overlays, watch variables, and timing sections are essential during
development but must not affect release performance or binary size. The
standard approach (preprocessor guards) is verbose and error-prone. A
compile-time no-op approach lets authors freely call dev APIs without
`#ifdef` clutter.

## Decision

**All dev instrumentation functions are prefixed `blyt_dev_*` and compile to
no-ops in release builds.**

```c
// Draw a colored rectangle overlay (RGBA, not palette index — dev only)
void blyt_dev_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgba);

// Watch a value: display name=value in the dev overlay
void blyt_dev_watch_f32(const char *name, float value);
void blyt_dev_watch_i32(const char *name, int32_t value);

// Named timing sections
void blyt_dev_start(const char *section_name);  // begin timing
void blyt_dev_finish(const char *section_name); // end timing, record to overlay

// Log a message to the dev console (not visible in release)
void blyt_dev_log(const char *fmt, ...);
```

The packer sets the `debug` flag in `cart.info` (see ADR-0073); the runtime
reads this at cart load to determine whether dev features are active. The SDK
defines `BLYT_RELEASE` when the packer invokes the compiler for a release build.

In release builds (`BLYT_RELEASE` defined), these functions are either:
- Empty inline stubs (zero code generated for the call site), or
- Stripped by the linker (if the compiler cannot prove the call is pure).

Dev overlay items (watch variables, timing sections) are visible in the dev
mode overlay (enabled by `--dev` flag or env var). They reset each frame.

**RGBA vs palette:** `blyt_dev_rect` uses RGBA because dev overlays need to be
visible regardless of what palette the cart is using. This is a dev-only
escape hatch from the paletted model.

## Consequences

- Authors instrument freely without `#ifdef DEBUG` guards.
- Release builds incur zero overhead from dev instrumentation calls.
- The RGBA escape hatch in dev overlays ensures visibility; it is explicitly
  dev-only and does not pollute the cart API's palette-only model.
- `blyt_dev_start`/`blyt_dev_finish` (not `blyt_dev_end`) is chosen to distinguish
  from common "end" conventions and to make start/finish pairing visually
  obvious in code.
