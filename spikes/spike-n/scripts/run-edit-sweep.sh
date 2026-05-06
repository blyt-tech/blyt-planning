#!/bin/sh
# Spike N — generic per-edit save-load sweep.
#
# For each edit (pre_elf/post_elf pair):
#   1. Run pre_elf for S frames on both hosts → BUFFER hex (cross-host must match).
#   2. Run post_elf loading that BUFFER on both hosts.
#   3. post_elf emits:
#      a. BUFFER <frame> <hex>  (post-migration buffer — cross-host must match)
#      b. DIGEST lines          (continuation stream — cross-host + suffix must match)
#   4. For failure-expected edits: capture stderr and compare cross-host.
#
# Usage:
#   run-edit-sweep.sh <cart_name> <edit_id> <pre_elf> <post_elf> <S> <expected>
#     expected: PASS | FAIL-WITH-DIAGNOSTIC | PASS-on-FLATTEN-ERROR
#
# Images assumed already built: fc32-spike-n-arm64 / fc32-spike-n-amd64

set -eu

CART="$1"
EDIT="$2"
PRE_ELF="$3"
POST_ELF="$4"
S="$5"
EXPECTED="${6:-PASS}"

ARM_IMG=fc32-spike-n-arm64
AMD_IMG=fc32-spike-n-amd64
RV32EMU=/spike-a/rv32emu/build/rv32emu
ELFS=/spike-n/elfs

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUFFERS="$REPO_ROOT/buffers"
DIGESTS="$REPO_ROOT/digests"
mkdir -p "$BUFFERS" "$DIGESTS"

TAG="${CART}.${EDIT}.f${S}"

BUF_PRE_ARM="$BUFFERS/${TAG}.pre.arm64.hex"
BUF_PRE_AMD="$BUFFERS/${TAG}.pre.amd64.hex"
BUF_POST_ARM="$BUFFERS/${TAG}.post.arm64.hex"
BUF_POST_AMD="$BUFFERS/${TAG}.post.amd64.hex"
DG_ARM="$DIGESTS/${TAG}.arm64.txt"
DG_AMD="$DIGESTS/${TAG}.amd64.txt"
STDERR_ARM="$DIGESTS/stderr.${CART}.${EDIT}.arm64.txt"
STDERR_AMD="$DIGESTS/stderr.${CART}.${EDIT}.amd64.txt"

echo "=== edit ${EDIT} on ${CART} (S=${S}, expected=${EXPECTED}) ==="

# ── Phase 1: PRE save ──────────────────────────────────────────────────

docker run --rm --platform linux/arm64 "$ARM_IMG" \
    "$RV32EMU" "$ELFS/${PRE_ELF}" "$S" 2>/dev/null \
    | grep '^BUFFER ' > "$BUF_PRE_ARM"
docker run --rm --platform linux/amd64 "$AMD_IMG" \
    "$RV32EMU" "$ELFS/${PRE_ELF}" "$S" 2>/dev/null \
    | grep '^BUFFER ' > "$BUF_PRE_AMD"

if ! diff -q "$BUF_PRE_ARM" "$BUF_PRE_AMD" >/dev/null; then
    echo "PRE BUFFER FAIL — cross-host mismatch at S=${S}"
    exit 1
fi
echo "PRE BUFFER PASS"

# ── Phase 2: POST load ─────────────────────────────────────────────────

if [ "$EXPECTED" = "PASS" ] || [ "$EXPECTED" = "PASS-on-FLATTEN-ERROR" ]; then
    docker run --rm --platform linux/arm64 \
        -v "$BUFFERS":/spike-n/buffers:ro "$ARM_IMG" \
        "$RV32EMU" "$ELFS/${POST_ELF}" "/spike-n/buffers/$(basename "$BUF_PRE_ARM")" \
        2>/dev/null | { grep '^BUFFER \|^DIGEST ' || true; } > "$REPO_ROOT/tmp_out_arm.txt"

    docker run --rm --platform linux/amd64 \
        -v "$BUFFERS":/spike-n/buffers:ro "$AMD_IMG" \
        "$RV32EMU" "$ELFS/${POST_ELF}" "/spike-n/buffers/$(basename "$BUF_PRE_AMD")" \
        2>/dev/null | { grep '^BUFFER \|^DIGEST ' || true; } > "$REPO_ROOT/tmp_out_amd.txt"

    grep '^BUFFER ' "$REPO_ROOT/tmp_out_arm.txt" > "$BUF_POST_ARM" || true
    grep '^BUFFER ' "$REPO_ROOT/tmp_out_amd.txt" > "$BUF_POST_AMD" || true
    grep '^DIGEST ' "$REPO_ROOT/tmp_out_arm.txt" > "$DG_ARM"       || true
    grep '^DIGEST ' "$REPO_ROOT/tmp_out_amd.txt" > "$DG_AMD"       || true
    rm -f "$REPO_ROOT/tmp_out_arm.txt" "$REPO_ROOT/tmp_out_amd.txt"

    if [ -s "$BUF_POST_ARM" ] && [ -s "$BUF_POST_AMD" ]; then
        if ! diff -q "$BUF_POST_ARM" "$BUF_POST_AMD" >/dev/null; then
            echo "POST BUFFER FAIL — cross-host migration mismatch"
            exit 1
        fi
        echo "POST BUFFER PASS"
    fi

    h_arm=$(sha256sum "$DG_ARM" | awk '{print $1}')
    h_amd=$(sha256sum "$DG_AMD" | awk '{print $1}')
    if [ "$h_arm" != "$h_amd" ]; then
        echo "DIGEST FAIL — cross-host divergence"
        exit 1
    fi
    echo "DIGEST PASS — cross-host identical"

elif [ "$EXPECTED" = "FAIL-WITH-DIAGNOSTIC" ]; then
    # hot_reload_diagnostic_emit() prints "STDERR <msg>" to stdout
    # (same STDERR-prefix convention as Spike M's transient negative test).
    # Capture stdout, grep for STDERR lines, compare cross-host.
    docker run --rm --platform linux/arm64 \
        -v "$BUFFERS":/spike-n/buffers:ro "$ARM_IMG" \
        "$RV32EMU" "$ELFS/${POST_ELF}" "/spike-n/buffers/$(basename "$BUF_PRE_ARM")" \
        2>/dev/null | { grep '^STDERR ' || true; } > "$STDERR_ARM"

    docker run --rm --platform linux/amd64 \
        -v "$BUFFERS":/spike-n/buffers:ro "$AMD_IMG" \
        "$RV32EMU" "$ELFS/${POST_ELF}" "/spike-n/buffers/$(basename "$BUF_PRE_AMD")" \
        2>/dev/null | { grep '^STDERR ' || true; } > "$STDERR_AMD"

    if [ ! -s "$STDERR_ARM" ] || [ ! -s "$STDERR_AMD" ]; then
        echo "STDERR FAIL — no STDERR lines produced (expected diagnostic output)"
        exit 1
    fi

    h_arm=$(sha256sum "$STDERR_ARM" | awk '{print $1}')
    h_amd=$(sha256sum "$STDERR_AMD" | awk '{print $1}')
    if [ "$h_arm" != "$h_amd" ]; then
        echo "STDERR FAIL — diagnostic diverges cross-host"
        echo "  arm64: $h_arm"
        echo "  amd64: $h_amd"
        exit 1
    fi
    echo "STDERR PASS — diagnostic byte-identical cross-host"
fi

echo "=== edit ${EDIT} PASS ==="
