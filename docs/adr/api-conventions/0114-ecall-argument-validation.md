# ADR-0114: ECALL argument validation policy

## Status

Accepted

## Context

ADR-0085 specifies the ECALL calling convention used internally by the
emulator-side `libblyt32.so`: ECALL number in `a7`, up to six arguments
in `a0`–`a5`, return value in `a0`. Every value that crosses this
boundary from the guest is adversarial input — the cart may be
hand-assembled by a hostile actor who can place arbitrary bit patterns in
any register.

This ADR specifies the validation rules that every ECALL handler must
apply before performing any operation.

## Decision

### Integers

Every integer argument is validated against the API-defined valid range
for that parameter before use. The valid range for each parameter is part
of the ECALL specification for that number.

Where unsigned non-negative values are expected, the handler explicitly
rejects negative values (i.e. values with the sign bit set when
interpreted as `int32_t`). Do not rely on unsigned truncation or silent
modular arithmetic to make a negative value harmless.

### Overflow-safe combined checks

When two or more arguments combine to form a range (e.g. `x` and `w`
forming a horizontal span on a surface of width `surface_width`), use
overflow-safe forms:

```c
// Correct:
if (x > surface_width || w > surface_width - x) { reject; }

// Incorrect — can overflow:
if (x + w > surface_width) { reject; }
```

### Flags arguments

`uint32_t flags` parameters are validated against the bitmask of defined
flag bits for that call. Any bit set that is not in the defined mask
returns `BLYT_ERR_INVALID_FLAGS`. This prevents future flag additions from
silently having undefined effects on older runtimes that do not know about
them.

### Enum-like arguments

Arguments that represent a choice from a fixed set (e.g. blend modes,
audio formats, filter types) are validated with an explicit allowlist.
The default case in any switch or lookup rejects the value and returns an
error. There is no silent fallthrough to a default behaviour for unknown
values.

### String arguments

When an API function accepts a string as a `(guest_ptr, length)` pair
(per ADR-0085 string-passing convention):

- Validate `guest_ptr + length <= guest_memory_end` with
  overflow-safe arithmetic before accessing any byte.
- Use a single shared helper function `copy_from_guest(dst, guest_ptr, len)`
  so that the bounds check exists in exactly one place. Do not inline
  ad-hoc bounds checks at call sites.
- Reject `length` values that exceed a per-call reasonable maximum.

### Out-parameter pointers

Where an ECALL writes a result through a guest pointer (per ADR-0085
out-parameter convention), validate that the guest address is non-null
and lies within a writable region of guest memory before writing.
An invalid out-pointer returns `BLYT_ERR_INVALID_ARG`.

### Return value

Every ECALL handler returns `blyt_result_t`. Validation failures return
specific negative error codes (per ADR-0046 error model). The handler
never returns an error silently by producing a "safe" garbage result; the
caller must see the error.

## Consequences

- Every ECALL entry point is hardened against the full range of possible
  input values. No integer argument can be assumed to be within its
  expected range unless explicitly checked.
- The single `copy_from_guest` helper for string bounds-checking ensures
  this check cannot be omitted at individual call sites.
- Overflow-safe combined-range checks eliminate a class of integer-wrap
  bugs that would otherwise require careful per-site audit.
- The flags-rejection policy means that future flag additions produce
  clean errors on old runtimes rather than silently ignored bits; this
  improves forward-compatibility diagnostics.
