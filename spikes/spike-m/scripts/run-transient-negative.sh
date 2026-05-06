#!/bin/sh
# Spike M Stage 5 — transient-coroutine boundary-cross negative test.
#
# Save at S=5 with a transient suspended; on a load resume the cart's
# pcall'd coroutine.resume(trans) hits the wrapper's boundary-cross
# check and throws the canonical ADR-0012 single-line error string,
# which the cart prints with a `STDERR ` prefix.
#
# 4-way comparison: save buffer + STDERR content + continuation
# DIGEST stream from frame S+1 must all be byte-identical across
# hosts in both directions.

set -eu

CART=det_transient_coroutine
S=5
F=$((S + 1))

ARM_IMG=fc32-spike-m-arm64
AMD_IMG=fc32-spike-m-amd64
RV32EMU=/spike-a/rv32emu/build/rv32emu
ELFS=/spike-m/elfs

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUFFERS="$REPO_ROOT/buffers"
DIGESTS="$REPO_ROOT/digests"
mkdir -p "$BUFFERS" "$DIGESTS"

BUF_ARM="$BUFFERS/${CART}.arm64.f${S}.hex"
BUF_AMD="$BUFFERS/${CART}.amd64.f${S}.hex"

echo "=== Save on both hosts at S=${S} ==="
docker run --rm --platform linux/arm64 "$ARM_IMG" \
    "$RV32EMU" "$ELFS/${CART}_save.elf" "$S" \
    | grep '^BUFFER ' > "$BUF_ARM"
docker run --rm --platform linux/amd64 "$AMD_IMG" \
    "$RV32EMU" "$ELFS/${CART}_save.elf" "$S" \
    | grep '^BUFFER ' > "$BUF_AMD"

if ! diff -q "$BUF_ARM" "$BUF_AMD" >/dev/null; then
    echo "BUFFER FAIL"
    exit 1
fi
echo "BUFFER PASS"

echo "=== 4 load directions: capture STDERR + DIGEST ==="
for combo in same.arm64:linux/arm64:$ARM_IMG:arm64 \
             same.amd64:linux/amd64:$AMD_IMG:amd64 \
             cross.arm64:linux/arm64:$ARM_IMG:amd64 \
             cross.amd64:linux/amd64:$AMD_IMG:arm64; do
    tag=${combo%%:*}
    rest=${combo#*:}
    plat=${rest%%:*}
    rest=${rest#*:}
    img=${rest%%:*}
    buf_src=${rest#*:}

    raw="/tmp/m_trans_${tag}.txt"
    docker run --rm --platform "$plat" -v "$BUFFERS":/spike-m/buffers:ro "$img" \
        "$RV32EMU" "$ELFS/${CART}_load.elf" "/spike-m/buffers/${CART}.${buf_src}.f${S}.hex" 2>&1 \
        > "$raw"
    grep '^STDERR ' "$raw" > "$DIGESTS/${CART}.stderr.${tag}.f${F}.txt"
    grep '^DIGEST ' "$raw" > "$DIGESTS/${CART}.${tag}.f${F}.txt"
done

h_se=$(sha256sum "$DIGESTS/${CART}.stderr.same.arm64.f${F}.txt" | awk '{print $1}')
h_dg=$(sha256sum "$DIGESTS/${CART}.same.arm64.f${F}.txt"        | awk '{print $1}')

for tag in same.amd64 cross.arm64 cross.amd64; do
    if [ "$h_se" != "$(sha256sum "$DIGESTS/${CART}.stderr.${tag}.f${F}.txt" | awk '{print $1}')" ]; then
        echo "STDERR mismatch on $tag"
        exit 1
    fi
    if [ "$h_dg" != "$(sha256sum "$DIGESTS/${CART}.${tag}.f${F}.txt" | awk '{print $1}')" ]; then
        echo "DIGEST mismatch on $tag"
        exit 1
    fi
done

echo "STDERR + DIGEST PASS — 4-way byte-identical at S=${S}"
echo "  stderr content: $(cat "$DIGESTS/${CART}.stderr.same.arm64.f${F}.txt")"
