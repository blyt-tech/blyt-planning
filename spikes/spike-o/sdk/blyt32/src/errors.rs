// Two-tier error model (ADR-0108 §3.3).
//
// Tier 1 (Result<T, BlytError>): genuinely fallible operations — those that
// can fail at runtime due to resource exhaustion, invalid cart data, or
// OS-level errors.  The toy cart does not call any Tier-1 APIs; this type
// exists to confirm the no_std error type compiles.
//
// Tier 2 (infallible, panic on programming error): operations that can only
// fail due to caller bugs (wrong handle type, out-of-range index).  These
// use debug_assert! in debug builds and are UB-free in release.
//
// Spike simplification: BlytError holds a 'static str rather than a heap-
// allocated String (no allocator).  Production would use the blyt_last_error()
// heap-clone path from ADR-0108; flagged as a follow-up.

#[derive(Copy, Clone, PartialEq, Eq)]
pub struct BlytError {
    pub message: &'static str,
}
