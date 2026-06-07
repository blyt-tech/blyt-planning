#!/usr/bin/env bash
# Spike T stage 5:
#  (e) 10-frame combined determinism gate (strings + tables + next + errors
#      + multi-return + evolving state) — byte-exact rv32 vs WASM.
#  (f) overhead: bridged 10-op call vs typed fast path vs pure Lua, 10k
#      iterations, wall-clock minus empty-cart baseline, best of 3.
set -euo pipefail

SPIKE_DIR="$(cd "$(dirname "$0")" && pwd)"
BLYT_REPO="${BLYT_REPO:-$(cd "$SPIKE_DIR/../../../blyt" && pwd)}"
SDK="$BLYT_REPO/build/sdk"
OUT="$SPIKE_DIR/digests/stage5"
RES="$SPIKE_DIR/results"
mkdir -p "$OUT" "$RES"

build_cart() {
    env BLYT_SDK_DIR="$SDK" \
        BLYT_OBJCOPY="$SDK/bin/blyt-objcopy" \
        BLYT_LUAC="$SDK/bin/blyt-luac" \
        BLYT_CLANG="$SDK/bin/blyt-clang" \
        "$SDK/bin/blyt" build "$SPIKE_DIR/carts/$1" > /dev/null 2>&1
    echo "$SPIKE_DIR/carts/$1/build/$1.blyt"
}

echo "== (e) combined gate =="
GATE=$(build_cart stage5-gate)
"$SDK/bin/blytplay" --headless "$GATE" | grep '^gate:' > "$OUT/rv32.txt" || true
node "$BLYT_REPO/tests/wasm/run_cart.js" "$SDK/share/wasm" "$GATE" \
    | grep '^gate:' > "$OUT/wasm.txt" || true
cat "$OUT/rv32.txt"
LINES=$(wc -l < "$OUT/rv32.txt" | tr -d ' ')
if [ "$LINES" != "11" ]; then
    echo "STAGE 5 FAIL: expected 11 gate lines, got $LINES"
    exit 1
fi
if ! diff -u "$OUT/rv32.txt" "$OUT/wasm.txt"; then
    echo "STAGE 5 FAIL: gate streams differ"
    exit 1
fi
echo "gate: byte-exact across paths"

echo "== (f) overhead (WASM path, 10k calls, best of 3) =="
now_ms() { node -e 'process.stdout.write(String(Date.now()))'; }
run_wasm_ms() { # $1 = cart path → echoes best-of-3 elapsed ms
    local best=999999999
    for _ in 1 2 3; do
        local t0 t1
        t0=$(now_ms)
        node "$BLYT_REPO/tests/wasm/run_cart.js" "$SDK/share/wasm" "$1" > /dev/null 2>&1
        t1=$(now_ms)
        local d=$((t1 - t0))
        [ "$d" -lt "$best" ] && best=$d
    done
    echo "$best"
}

declare -a NAMES=(bench-empty bench-lua bench-typed bench-bridged)
declare -a MS=()
for n in "${NAMES[@]}"; do
    C=$(build_cart "$n")
    m=$(run_wasm_ms "$C")
    MS+=("$m")
    echo "$n: ${m} ms (full run)"
done

BASE=${MS[0]}
{
    echo "# Spike T (f) overhead — $(date '+%Y-%m-%d %H:%M')"
    echo "# 10k calls, node WASM path, best of 3, baseline ${BASE} ms subtracted"
    for i in 1 2 3; do
        n=${NAMES[$i]}
        d=$(( MS[i] - BASE ))
        [ "$d" -lt 0 ] && d=0
        us_per_call=$(node -e "console.log((($d * 1000) / 10000).toFixed(2))")
        echo "$n: ${d} ms net → ${us_per_call} µs/call"
    done
} | tee "$RES/overhead.txt"

echo "STAGE 5 PASS"
