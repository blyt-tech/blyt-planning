#!/usr/bin/env bash
# Spike T stage 1: cross-path pairs() iteration-order parity.
#
# Builds carts/stage1-pairs, runs it on the rv32 path (blytplay --headless)
# and the WASM path (node tests/wasm/run_cart.js), and diffs the debug
# output.  PASS iff the two outputs are byte-identical and non-empty.
#
# Requires the blyt repo (spike-t-lua-bridge branch) built:
#   cmake --build build && cmake --build build --target sdk
set -euo pipefail

SPIKE_DIR="$(cd "$(dirname "$0")" && pwd)"
BLYT_REPO="${BLYT_REPO:-$(cd "$SPIKE_DIR/../../../blyt" && pwd)}"
SDK="$BLYT_REPO/build/sdk"
CART_PROJ="$SPIKE_DIR/carts/stage1-pairs"
OUT="$SPIKE_DIR/digests/stage1"
mkdir -p "$OUT"

echo "== build cart =="
env BLYT_SDK_DIR="$SDK" \
    BLYT_OBJCOPY="$SDK/bin/blyt-objcopy" \
    BLYT_LUAC="$SDK/bin/blyt-luac" \
    "$SDK/bin/blyt" build "$CART_PROJ"
CART="$CART_PROJ/build/stage1-pairs.blyt"

echo "== rv32 path =="
"$SDK/bin/blytplay" --headless "$CART" | grep '^pairs' > "$OUT/rv32.txt" || true

echo "== wasm path =="
node "$BLYT_REPO/tests/wasm/run_cart.js" "$BLYT_REPO/build/sdk/share/wasm" "$CART" \
    | grep '^pairs' > "$OUT/wasm.txt" || true

echo "== compare =="
cat "$OUT/rv32.txt"
if [ ! -s "$OUT/rv32.txt" ]; then
    echo "STAGE 1 FAIL: empty rv32 output"
    exit 1
fi
if diff -u "$OUT/rv32.txt" "$OUT/wasm.txt"; then
    echo "STAGE 1 PASS: pairs() order identical across paths"
else
    echo "STAGE 1 FAIL: pairs() order differs"
    exit 1
fi
