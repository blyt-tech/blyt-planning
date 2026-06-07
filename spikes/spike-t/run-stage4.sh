#!/usr/bin/env bash
# Spike T stage 4: table get/set/geti/seti/rawlen + fixed-seed lua_next
# order parity, plus error unwind with tables on the exchange stack.
set -euo pipefail

SPIKE_DIR="$(cd "$(dirname "$0")" && pwd)"
BLYT_REPO="${BLYT_REPO:-$(cd "$SPIKE_DIR/../../../blyt" && pwd)}"
SDK="$BLYT_REPO/build/sdk"
CART_PROJ="$SPIKE_DIR/carts/stage4-tables"
OUT="$SPIKE_DIR/digests/stage4"
mkdir -p "$OUT"

echo "== build cart =="
env BLYT_SDK_DIR="$SDK" \
    BLYT_OBJCOPY="$SDK/bin/blyt-objcopy" \
    BLYT_LUAC="$SDK/bin/blyt-luac" \
    BLYT_CLANG="$SDK/bin/blyt-clang" \
    "$SDK/bin/blyt" build "$CART_PROJ"
CART="$CART_PROJ/build/stage4-tables.blyt"

echo "== rv32 path =="
"$SDK/bin/blytplay" --headless "$CART" | grep '^tbl_' > "$OUT/rv32.txt" || true

echo "== wasm path =="
node "$BLYT_REPO/tests/wasm/run_cart.js" "$BLYT_REPO/build/sdk/share/wasm" "$CART" \
    | grep '^tbl_' > "$OUT/wasm.txt" || true

echo "== compare =="
cat "$OUT/rv32.txt"
for want in "tbl_name:blyt" "tbl_total:30" "tbl_n:5" "tbl_fill:42,first" \
            "tbl_err:false|" ; do
    if ! grep -qF "$want" "$OUT/rv32.txt"; then
        echo "STAGE 4 FAIL: rv32 output missing '$want'"
        exit 1
    fi
done
if diff -u "$OUT/rv32.txt" "$OUT/wasm.txt"; then
    echo "STAGE 4 PASS: table ops + next order identical across paths"
else
    echo "STAGE 4 FAIL: outputs differ"
    exit 1
fi
