#!/usr/bin/env bash
# Spike T stage 3: string marshalling (small / >4 KiB retry / embedded NUL)
# + error model (catchable, coroutine resumable, 1000 consecutive errors).
set -euo pipefail

SPIKE_DIR="$(cd "$(dirname "$0")" && pwd)"
BLYT_REPO="${BLYT_REPO:-$(cd "$SPIKE_DIR/../../../blyt" && pwd)}"
SDK="$BLYT_REPO/build/sdk"
CART_PROJ="$SPIKE_DIR/carts/stage3-strings"
OUT="$SPIKE_DIR/digests/stage3"
mkdir -p "$OUT"

echo "== build cart =="
env BLYT_SDK_DIR="$SDK" \
    BLYT_OBJCOPY="$SDK/bin/blyt-objcopy" \
    BLYT_LUAC="$SDK/bin/blyt-luac" \
    BLYT_CLANG="$SDK/bin/blyt-clang" \
    "$SDK/bin/blyt" build "$CART_PROJ"
CART="$CART_PROJ/build/stage3-strings.blyt"

echo "== rv32 path =="
"$SDK/bin/blytplay" --headless "$CART" | grep -E '^(str_|err_)' > "$OUT/rv32.txt" || true

echo "== wasm path =="
node "$BLYT_REPO/tests/wasm/run_cart.js" "$BLYT_REPO/build/sdk/share/wasm" "$CART" \
    | grep -E '^(str_|err_)' > "$OUT/wasm.txt" || true

echo "== compare =="
cat "$OUT/rv32.txt"
for want in "str_small:HELLO, BLYT!|12" "str_big:5400|" "str_nul:5|97,0,98,0,99" \
            "err_caught:false|" "err_repeat:1000" "str_after_errors:STILL ALIVE"; do
    if ! grep -qF "$want" "$OUT/rv32.txt"; then
        echo "STAGE 3 FAIL: rv32 output missing '$want'"
        exit 1
    fi
done
if diff -u "$OUT/rv32.txt" "$OUT/wasm.txt"; then
    echo "STAGE 3 PASS: strings + errors identical across paths"
else
    echo "STAGE 3 FAIL: outputs differ"
    exit 1
fi
