#!/usr/bin/env bash
# Spike T stage 2: bridged scalar ops (>4 args, multi-return) + typed-path
# regression — identical output on the rv32 and WASM paths.
set -euo pipefail

SPIKE_DIR="$(cd "$(dirname "$0")" && pwd)"
BLYT_REPO="${BLYT_REPO:-$(cd "$SPIKE_DIR/../../../blyt" && pwd)}"
SDK="$BLYT_REPO/build/sdk"
CART_PROJ="$SPIKE_DIR/carts/stage2-scalar"
OUT="$SPIKE_DIR/digests/stage2"
mkdir -p "$OUT"

echo "== build cart =="
env BLYT_SDK_DIR="$SDK" \
    BLYT_OBJCOPY="$SDK/bin/blyt-objcopy" \
    BLYT_LUAC="$SDK/bin/blyt-luac" \
    BLYT_CLANG="$SDK/bin/blyt-clang" \
    "$SDK/bin/blyt" build "$CART_PROJ"
CART="$CART_PROJ/build/stage2-scalar.blyt"

echo "== rv32 path =="
"$SDK/bin/blytplay" --headless "$CART" | grep '^spike_' > "$OUT/rv32.txt" || true

echo "== wasm path =="
node "$BLYT_REPO/tests/wasm/run_cart.js" "$BLYT_REPO/build/sdk/share/wasm" "$CART" \
    | grep '^spike_' > "$OUT/wasm.txt" || true

echo "== compare =="
cat "$OUT/rv32.txt"
EXPECT_SUM="spike_sum5:15,5"
if ! grep -q "$EXPECT_SUM" "$OUT/rv32.txt"; then
    echo "STAGE 2 FAIL: rv32 output missing '$EXPECT_SUM'"
    exit 1
fi
if diff -u "$OUT/rv32.txt" "$OUT/wasm.txt"; then
    echo "STAGE 2 PASS: bridged scalar ops identical across paths"
else
    echo "STAGE 2 FAIL: outputs differ"
    exit 1
fi
