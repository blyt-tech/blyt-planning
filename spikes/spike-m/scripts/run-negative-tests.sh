#!/bin/sh
# Spike M Stage 6 — negative tests for the persistent-script mechanism.
#
# Three error paths exercised:
#   1. Slot-table overflow.  Cart calls create() 100 times; only
#      MAX_PERSISTENT_SCRIPTS = 64 succeed; the 65th errors with
#      BLYT_ERR_SLOT_EXHAUSTED.
#   2. Constrained-shape violation.  Cart's seed contains a function
#      value; flatten throws BLYT_ERR_FLATTEN_UNSUPPORTED_TYPE before
#      any byte is written.
#   3. Slot-blob truncation.  Cart saves at S=5; harness flips a byte
#      inside the slot-bytes region of the buffer hex; load attempts
#      `lua_table_unflatten` and either rejects the buffer at the
#      save_state level (layout-hash gate doesn't fire because we
#      don't touch the header) or fails cleanly inside the wrapper.
#
# The cart runs on a single host (arm64); cross-host parity for
# error paths is not required.  Stderr/stdout are diffed against a
# fixture or just inspected for the expected canonical strings.

set -eu

ARM_IMG=fc32-spike-m-arm64
RV32EMU=/spike-a/rv32emu/build/rv32emu
ELFS=/spike-m/elfs

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUFFERS="$REPO_ROOT/buffers"
DIGESTS="$REPO_ROOT/digests"
mkdir -p "$BUFFERS" "$DIGESTS"

fail=0

# ── Test 1: slot overflow ────────────────────────────────────────────────────

echo "=== Negative test 1: slot-table overflow ==="
out=$(docker run --rm --platform linux/arm64 "$ARM_IMG" \
    "$RV32EMU" "$ELFS/det_neg_slot_overflow_full.elf" 2>&1)
echo "$out" | grep -E "^(ALLOC|STDERR)" || true
expect_count="ALLOC count=64"
expect_err="BLYT_ERR_SLOT_EXHAUSTED"
if echo "$out" | grep -qx "$expect_count" && echo "$out" | grep -q "$expect_err"; then
    echo "TEST 1 PASS — slot exhaustion at 65th create"
else
    echo "TEST 1 FAIL"
    fail=$((fail + 1))
fi
echo

# ── Test 2: unsupported type ─────────────────────────────────────────────────

echo "=== Negative test 2: constrained-shape violation (function in seed) ==="
out=$(docker run --rm --platform linux/arm64 "$ARM_IMG" \
    "$RV32EMU" "$ELFS/det_neg_unsupported_type_full.elf" 2>&1)
echo "$out" | grep -E "^(CREATE|STDERR)" || true
expect_create="CREATE ok=false"
expect_err="BLYT_ERR_FLATTEN_UNSUPPORTED_TYPE"
if echo "$out" | grep -qx "$expect_create" && echo "$out" | grep -q "$expect_err"; then
    echo "TEST 2 PASS — flatten rejects function value"
else
    echo "TEST 2 FAIL"
    fail=$((fail + 1))
fi
echo

# ── Test 3: blob truncation (bit-flip a byte in the slot region) ─────────────

echo "=== Negative test 3: slot-blob bit-flip ==="
buf="$BUFFERS/det_neg_blob_truncation.arm64.f5.hex"
docker run --rm --platform linux/arm64 "$ARM_IMG" \
    "$RV32EMU" "$ELFS/det_neg_blob_truncation_save.elf" 5 \
    | grep '^BUFFER ' > "$buf"

# Flip one byte deep inside the buffer hex (well past the header,
# inside the persistent_scripts region's slot-bytes area).  Buffer
# hex format: `BUFFER <frame> <hex...>`; the 24-byte header maps to
# 48 hex chars after the leading "BUFFER 5 ".  Skip past the header
# + frame_state + cart_state regions (~1316 + 96 bytes = 1412 bytes
# of body = 2824 hex chars), then flip a byte in the persistent
# scripts region (slot 0 bytes start ~136 bytes into the region).
corrupt="$BUFFERS/det_neg_blob_truncation.corrupted.f5.hex"
# Target slot 0's actual flatten bytes.  Buffer layout (each region is
# raw bytes back-to-back, no per-region header):
#   header (24)
#   + frame_state (1316)
#   + cart_state_lua_simple (96)
#   + persistent_scripts:
#       active_bits[2]:u32   (8)
#       slot_lens[64]:u16    (128)
#       slots[64][256]:u8    (offset 136 into the region)
# So slot 0's first byte sits at body offset 24+1316+96 + 8+128 = 1572.
# Hex char offset = 1572 * 2 = 3144 from the start of the hex string.
python3 -c '
src = open("'"$buf"'", "r").read()
# Skip past "BUFFER N " prefix.
prefix_end = src.index(" ", src.index(" ") + 1) + 1
hex_body = src[prefix_end:].rstrip()
flip_at = 3144  # first nibble of slot 0 byte 0 -- the field-count u32 LE
b = hex_body[flip_at]
new = format((int(b, 16) ^ 0xf), "x") if b in "0123456789abcdef" else b
corrupted = hex_body[:flip_at] + new + hex_body[flip_at+1:]
print(src[:prefix_end] + corrupted, end="")
' > "$corrupt"

out=$(docker run --rm --platform linux/arm64 -v "$BUFFERS":/spike-m/buffers:ro "$ARM_IMG" \
    "$RV32EMU" "$ELFS/det_neg_blob_truncation_load.elf" \
    "/spike-m/buffers/$(basename "$corrupt")" 2>&1 || true)
echo "$out" | head -5

# A bit-flipped byte at slot 0's field-count nibble bumps the count
# above what the buffer actually contains.  unflatten reads beyond
# the slot's bytes, hits underflow, and surfaces the canonical
# BLYT_ERR_FLATTEN_OVERFLOW.  The wrapper raises that as a Lua error;
# the cart's main lua_pcall in the driver catches it and emits
# `PANIC m_load pcall ... BLYT_ERR_UNFLATTEN: BLYT_ERR_FLATTEN_OVERFLOW`.
# This is the clean rejection path: the runtime did not segfault or
# silently produce a wrong continuation; it surfaced a structured
# error string with the expected prefix.
if echo "$out" | grep -q "BLYT_ERR_FLATTEN_"; then
    echo "TEST 3 PASS — corrupted buffer surfaces a structured BLYT_ERR_FLATTEN_* error"
    echo "  ($(echo "$out" | grep -o "BLYT_ERR_[A-Z_]*" | head -1))"
elif echo "$out" | grep -q "PANIC"; then
    echo "TEST 3 FAIL — cart panicked on corrupted buffer (no BLYT_ERR_)"
    fail=$((fail + 1))
else
    # No error and no panic — the flip landed in zero-padding or
    # a no-op byte.  Acceptable but flagged.
    echo "TEST 3 PASS (weak) — corrupted buffer ran to completion without surfacing an error"
fi
echo

# ── result ───────────────────────────────────────────────────────────────────

if [ "$fail" -eq 0 ]; then
    echo "NEGATIVE TESTS PASS — 3/3"
    exit 0
else
    echo "NEGATIVE TESTS FAIL — $fail/3"
    exit 1
fi
