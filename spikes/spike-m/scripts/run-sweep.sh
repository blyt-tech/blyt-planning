#!/bin/sh
# Spike M — generic per-cart all-S save-frame sweep.
#
# For every save frame S in [S_MIN..S_MAX]:
#   1. Save on both arm64 and amd64; assert byte-identical buffer.
#   2. Run the four load combinations (same.{arm64,amd64} +
#      cross.{arm64←amd64,amd64←arm64}); assert all four digest
#      streams sha256-equal.
#   3. Assert the digest stream matches the same-host straight-through
#      full-run suffix from frame S+1 onwards.
#
# Usage:
#   run-sweep.sh <cart_name> <S_min> <S_max>
#
# The two host docker images are assumed already built:
#   fc32-spike-m-arm64
#   fc32-spike-m-amd64
#
# Buffers + digests land under spikes/spike-m/{buffers,digests}/.
# The straight-through full digest stream (the suffix reference) is
# captured from each host once, into
#   digests/<cart>.full.arm64.txt
#   digests/<cart>.full.amd64.txt

set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: $0 <cart_name> <S_min> <S_max>" >&2
    exit 2
fi

CART="$1"
S_MIN="$2"
S_MAX="$3"

ARM_IMG=fc32-spike-m-arm64
AMD_IMG=fc32-spike-m-amd64
RV32EMU=/spike-a/rv32emu/build/rv32emu
ELFS=/spike-m/elfs

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUFFERS="$REPO_ROOT/buffers"
DIGESTS="$REPO_ROOT/digests"
mkdir -p "$BUFFERS" "$DIGESTS"

FULL_ARM="$DIGESTS/${CART}.full.arm64.txt"
FULL_AMD="$DIGESTS/${CART}.full.amd64.txt"

echo "=== Capturing straight-through digest streams (${CART}) ==="
docker run --rm --platform linux/arm64 "$ARM_IMG" \
    "$RV32EMU" "$ELFS/${CART}_full.elf" 2>/dev/null \
    | grep '^DIGEST ' > "$FULL_ARM"
docker run --rm --platform linux/amd64 "$AMD_IMG" \
    "$RV32EMU" "$ELFS/${CART}_full.elf" 2>/dev/null \
    | grep '^DIGEST ' > "$FULL_AMD"

if ! diff -q "$FULL_ARM" "$FULL_AMD" >/dev/null; then
    echo "FULL FAIL — straight-through diverges across hosts"
    exit 1
fi
echo "FULL PASS — straight-through identical across hosts"

echo "=== Sweep S=${S_MIN}..${S_MAX} on ${CART} ==="
fail=0
S=$S_MIN
while [ "$S" -le "$S_MAX" ]; do
    BUF_ARM="$BUFFERS/${CART}.arm64.f${S}.hex"
    BUF_AMD="$BUFFERS/${CART}.amd64.f${S}.hex"

    docker run --rm --platform linux/arm64 "$ARM_IMG" \
        "$RV32EMU" "$ELFS/${CART}_save.elf" "$S" 2>/dev/null \
        | grep '^BUFFER ' > "$BUF_ARM"
    docker run --rm --platform linux/amd64 "$AMD_IMG" \
        "$RV32EMU" "$ELFS/${CART}_save.elf" "$S" 2>/dev/null \
        | grep '^BUFFER ' > "$BUF_AMD"

    if ! diff -q "$BUF_ARM" "$BUF_AMD" >/dev/null; then
        echo "S=$S BUFFER FAIL"
        fail=$((fail + 1))
        S=$((S + 1))
        continue
    fi

    F=$((S + 1))

    DG_SAME_ARM="$DIGESTS/${CART}.same.arm64.f${F}.txt"
    DG_SAME_AMD="$DIGESTS/${CART}.same.amd64.f${F}.txt"
    DG_CROSS_ARM="$DIGESTS/${CART}.cross.arm64.f${F}.txt"
    DG_CROSS_AMD="$DIGESTS/${CART}.cross.amd64.f${F}.txt"

    docker run --rm --platform linux/arm64 -v "$BUFFERS":/spike-m/buffers:ro "$ARM_IMG" \
        "$RV32EMU" "$ELFS/${CART}_load.elf" "/spike-m/buffers/$(basename "$BUF_ARM")" 2>/dev/null \
        | { grep '^DIGEST ' || true; } > "$DG_SAME_ARM"
    docker run --rm --platform linux/amd64 -v "$BUFFERS":/spike-m/buffers:ro "$AMD_IMG" \
        "$RV32EMU" "$ELFS/${CART}_load.elf" "/spike-m/buffers/$(basename "$BUF_AMD")" 2>/dev/null \
        | { grep '^DIGEST ' || true; } > "$DG_SAME_AMD"
    docker run --rm --platform linux/arm64 -v "$BUFFERS":/spike-m/buffers:ro "$ARM_IMG" \
        "$RV32EMU" "$ELFS/${CART}_load.elf" "/spike-m/buffers/$(basename "$BUF_AMD")" 2>/dev/null \
        | { grep '^DIGEST ' || true; } > "$DG_CROSS_ARM"
    docker run --rm --platform linux/amd64 -v "$BUFFERS":/spike-m/buffers:ro "$AMD_IMG" \
        "$RV32EMU" "$ELFS/${CART}_load.elf" "/spike-m/buffers/$(basename "$BUF_ARM")" 2>/dev/null \
        | { grep '^DIGEST ' || true; } > "$DG_CROSS_AMD"

    h1=$(sha256sum "$DG_SAME_ARM"  | awk '{print $1}')
    h2=$(sha256sum "$DG_SAME_AMD"  | awk '{print $1}')
    h3=$(sha256sum "$DG_CROSS_ARM" | awk '{print $1}')
    h4=$(sha256sum "$DG_CROSS_AMD" | awk '{print $1}')

    if [ "$h1" != "$h2" ] || [ "$h1" != "$h3" ] || [ "$h1" != "$h4" ]; then
        echo "S=$S DIGEST 4-WAY FAIL ($h1 / $h2 / $h3 / $h4)"
        fail=$((fail + 1))
        S=$((S + 1))
        continue
    fi

    suffix_line=$((S + 2))
    suffix_hash=$(tail -n +"$suffix_line" "$FULL_ARM" | sha256sum | awk '{print $1}')
    if [ "$h1" != "$suffix_hash" ]; then
        echo "S=$S SUFFIX MISMATCH (load=$h1 vs straight-through suffix=$suffix_hash)"
        fail=$((fail + 1))
        S=$((S + 1))
        continue
    fi

    printf "S=%s " "$S"
    S=$((S + 1))
done
echo

if [ "$fail" -eq 0 ]; then
    runs=$(( (S_MAX - S_MIN + 1) * 4 ))
    echo "SWEEP PASS — ${CART}: ${runs} cross-host runs byte-identical"
    exit 0
else
    echo "SWEEP FAIL — ${CART}: $fail divergences"
    exit 1
fi
