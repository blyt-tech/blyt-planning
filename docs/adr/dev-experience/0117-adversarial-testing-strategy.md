# ADR-0117: Adversarial testing strategy

## Status

Accepted

## Context

The runtime must protect the host from hostile cart binaries. Standard
spec-conformance tests and CI smoke tests exercise correct-input paths;
they do not cover adversarial input. Upstream rv32emu's test suite is
oriented toward correctness and performance, not security hardening, and
cannot be relied upon for this purpose.

Dedicated testing against adversarial inputs is required before the
runtime can be considered ready for untrusted cart distribution.

## Decision

### Fuzzing harnesses

Three separate harnesses, each compiled with ASan and UBSan enabled:

**Harness 1 — load-time parsers.** AFL++ or libFuzzer entry point that
feeds arbitrary bytes to the cart loader. Exercises the ELF parser, the
FlatBuffers parser for `.cart.info` and `.cart.config`, the resource
bundle parser, and the `.lua_exports` parser (ADR-0112). Gate: no crash,
no sanitiser trip, no hang on any input that fits within the cart size-
class memory limit.

**Harness 2 — interpreter decode and memory-access path.** Random bytes
placed in the `.text` section of an otherwise structurally valid cart
ELF (bypassing the static opcode scan, which is exercised separately).
Run for a bounded instruction count. Exercises the instruction decoder,
memory-access bounds checks, and integer-type-widening paths in the
interpreter's execution core. Gate: no sanitiser trip, no OOB write,
no uncontrolled branch to host code.

**Harness 3 — API surface.** Structured fuzzer driving sequences of ECALL
calls with fuzzed integer arguments and fuzzed handle values (in-range,
out-of-range, zero, stale-generation, wrong-type). Exercises ADR-0113
handle validation, ADR-0114 argument validation, and the per-subsystem
bounds checks. Gate: no crash, all invalid inputs return
`BLYT_ERR_*` codes, no sanitiser trip.

Run all three harnesses continuously in CI. A CPU-week minimum on each
harness is required before a release can be claimed adversarially tested.

### Adversarial test corpus

A hand-curated set of test cases for known patterns. Each case must be
rejected at load time (where appropriate) or handled cleanly at run time:

- ELF with a `PF_W | PF_X` LOAD segment.
- ELF with overlapping LOAD segments.
- ELF with a LOAD segment extending past EOF.
- ELF with `e_entry` outside any `PF_X` segment.
- ELF with `ecall` at every aligned and unaligned offset within `.text`.
- ELF with `ebreak` at every aligned and unaligned offset.
- ELF with every reserved/illegal opcode encoding in the RV32IMAFC
  subset at 4-byte and 2-byte alignment.
- ELF with a malformed FlatBuffers root in `.cart.info`.
- ELF with `api_version` below the runtime minimum and above the
  runtime maximum.
- ELF with a `.cart.resources` entry whose `offset + size` overflows.
- ELF with a `.lua_exports` section where `sym_addr` is zero, out of
  guest-address-space bounds, and equal to `FC32_SENTINEL_ADDR`.
- ELF with `DT_NEEDED` for a library not on the permitted list.
- ELF importing a symbol not present in the public exports of any
  permitted library (expect load rejection).
- ELF explicitly importing `blytc_arena_init` or another known internal
  symbol (expect load rejection by the symbol import allowlist).
- ELF without a `PT_GNU_RELRO` segment (expect load rejection).
- ELF linked with lazy binding rather than `BIND_NOW` / `-z now`
  (expect load rejection).
- Cart that repeatedly calls `malloc` until it reaches the 16 MB budget
  limit, then calls `malloc` once more — must return NULL without
  crashing or corrupting internal state.
- Cart that loads resources to the 16 MB limit, then attempts one further
  resource-load ECALL — must return a `BLYT_ERR_*` error code, not crash.
- Cart that interleaves heap allocations and resource loads to exhaust the
  combined 16 MB budget — same gate as above.
- ECALL with handle value 0 on every API function.
- ECALL with handle value `max_valid + 1` on every API function.
- ECALL with a stale-generation handle on every API function.
- ECALL with out-of-range integer on every integer-argument API function.
- ECALL with an unknown flags bit set on every flags-argument API function.

### ASan and UBSan in CI

All three fuzzing harnesses and the adversarial corpus test suite are
built and run under AddressSanitizer and UndefinedBehaviorSanitizer.
These are also applied to the loader and interpreter in a dedicated CI
job that runs on every merge. A sanitiser trip in any CI run is treated
as a release-blocking defect.

### Upstream rv32emu monitoring

Subscribe to commits on the rv32emu upstream repository, particularly
in files covering instruction decode (`src/decode.c`, `src/riscv.c`),
the ELF loader, and any syscall or memory-access handlers. Security
fixes in emulators are frequently committed without explicit security
labelling. Diff against the pinned version on a cadence of at least
once per major release cycle.

## Consequences

- Security properties of the runtime are verified by adversarial input,
  not just by reasoning about the design.
- The three-harness structure exercises the three distinct attack
  surfaces: load-time parsers, interpreter internals, and the API
  boundary.
- A CPU-week fuzzing minimum before release sets a concrete threshold
  rather than an open-ended aspiration.
- The adversarial corpus provides regression coverage for known-bad
  patterns; each new vulnerability discovered becomes a new corpus entry.
- The symbol import allowlist, RELRO+BIND_NOW, and 16 MB memory budget
  tests verify the boundaries introduced in ADR-0112 and ADR-0120. The
  load-time cases (missing RELRO, unlisted imports) are fast deterministic
  checks; the memory budget cases exercise graceful failure paths that
  fuzzing alone is unlikely to hit.
- Upstream monitoring is a low-cost continuous activity that catches
  security-relevant changes before they accumulate into a large diff.
