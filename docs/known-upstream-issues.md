# Known upstream issues

Bugs in upstream dependencies that affect blyt development.  Each entry
records: the symptom, the root cause, the current workaround in blyt code,
and the upstream fix when known.

---

## rv32emu

### `CSR_FRM` (0x002) not handled in `csr_get_ptr`

**Affected file:** `src/emulate.c` — `csr_get_ptr()`

**Symptom:** Writes to the FP rounding-mode sub-register (`csrwi frm, N` or
`csrw frm, rs1`) are silently ignored on the emulated path.  The `csr_fcsr`
field is not updated, so any code that reads FRM back (or that blyt's
frame-boundary FCSR check reads) sees the default value 0 regardless of what
the cart wrote.

**Root cause:** `csr_get_ptr()` maps `CSR_FFLAGS` (0x001) and `CSR_FCSR`
(0x003) to `&rv->csr_fcsr`, but the `CSR_FRM` (0x002) case is absent.
The function falls through to `default: return NULL`, and all three CSR
instruction handlers (`csr_csrrw`, `csr_csrrs`, `csr_csrrc`) treat a NULL
return as a no-op.

Note: `CSR_FFLAGS` writes also have a masking bug — `*c = val` writes all
32 bits of `csr_fcsr` when only the low 5 bits should change, clobbering
any FRM value.  The read side is correctly masked (`out &= FFLAG_MASK`) but
the write side is not.

**Upstream fix (not yet submitted):**

Three changes are needed — they are separate concerns:

**1. `csr_get_ptr`: expose CSR_FRM**
```c
case CSR_FRM:
    return (uint32_t *) (&rv->csr_fcsr);
```

**2. `csr_csrrw` / `csr_csrrs` / `csr_csrrc`: add read masking for FRM**

Currently only FFLAGS reads are masked (`out &= FFLAG_MASK`).  FRM reads
must also be shifted and masked, otherwise `csrr t0, frm` returns the full
`csr_fcsr` word instead of the 3-bit rounding-mode field:
```c
if (csr == CSR_FFLAGS)
    out &= FFLAG_MASK;
else if (csr == CSR_FRM)
    out = (out >> 5) & 0x7u;
```

**3. `csr_csrrw` / `csr_csrrs` / `csr_csrrc`: add write masking for FRM (and FFLAGS)**

Currently all CSR writes do `*c = val`, overwriting all 32 bits of
`csr_fcsr`.  `csrwi frm, 3` passes `val = 3`, which sets `csr_fcsr =
0x00000003` — putting `frm = (3 >> 5) & 7 = 0` in the FRM field (still
wrong) and dirtying the two low fflags bits.  Writes must be masked to their
respective sub-fields:
```c
if (csr == CSR_FFLAGS)
    *c = (*c & ~FFLAG_MASK) | (val & FFLAG_MASK);
else if (csr == CSR_FRM)
    *c = (*c & ~(0x7u << 5)) | ((val & 0x7u) << 5);
else
    *c = val;
```

**FP operations themselves are already correct:** `softfp.h` reads the
rounding mode as `(rv->csr_fcsr >> 5) & (~(1 << 3))` before each
floating-point operation, so once writes correctly update bits [7:5] of
`csr_fcsr`, arithmetic will immediately respect the new rounding mode with
no further changes.

**Blyt workaround:** Dirty-FRM test carts write `3u << 5 = 0x60` to
`CSR_FCSR` (0x003) directly rather than writing `3` to `CSR_FRM` (0x002).
This sets `FCSR.frm = 3` through the path that rv32emu does handle.
See `tests/integration/tests/e2e.rs` and `native_qemu.rs`.

**Discovered:** Session of 2026-05-21.  CI failure: `emulator_fcsr_dirty_frm_detected`.

---

### `syscall_read` host-side buffer overflow above 4 KiB

**Affected file:** `src/syscall.c` — `syscall_read()`

**Symptom:** When a cart issues a `read` syscall requesting more bytes than
the file contains AND the request size exceeds `PREALLOC_SIZE` (4 KiB),
rv32emu's read implementation writes past the end of its internal scratch
buffer, causing heap corruption on the host.

**Root cause:** `syscall_read` pre-allocates a fixed 4 KiB scratch buffer
(`PREALLOC_SIZE`) on the host side to ferry data into the guest address
space.  The final `fread` call uses the full requested `count` rather than
clamping to `PREALLOC_SIZE`, so an oversized request overflows the scratch.

**Upstream fix (one-liner, not yet submitted):**
```c
/* syscall.c::syscall_read — clamp the final fread to the scratch size */
size_t n = fread(buf, 1, MIN(count, PREALLOC_SIZE), stream);
```

**Blyt workaround:** Cart-side code that reads from files chunks its reads
to ≤ 4 KiB.  See `tests/native/` and spike-K save-state I/O helpers
(`save_io.c`).

**Discovered:** Spike K (save-state portability).  Documented in
`docs/design/spike-k-results.md`.
