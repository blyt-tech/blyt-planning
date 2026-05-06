#!/bin/sh
# Spike N Stages 3-5 — Lua edit runner (l1-l10).
#
# Stages 3-4: PASS edits (l1-l5, l8) — full cross-host sweep.
# Stage 5: FAIL-WITH-DIAGNOSTIC edits (l6, l9, l10) — stderr equality.
# l7: PASS-on-FLATTEN-ERROR — pre-reload flatten failure gate.

set -eu

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
SWEEP="$REPO_ROOT/scripts/run-edit-sweep.sh"
S=15   # fixed save frame for non-sweep edits
S_MAX=29   # sweep range for l8

echo "=== Spike N Stages 3-5 — Lua edits (l1-l10) ==="

# Stage 3 — pure-data and body-change edits
"$SWEEP" det_lua_cart l1 det_lua_l1_pre_save.elf  det_lua_l1_post_load.elf  $S PASS
"$SWEEP" det_lua_cart l2 det_lua_l2_pre_save.elf  det_lua_l2_post_load.elf  $S PASS
"$SWEEP" det_lua_cart l3 det_lua_l3_pre_save.elf  det_lua_l3_post_load.elf  $S PASS
"$SWEEP" det_lua_cart l4 det_lua_l4_pre_save.elf  det_lua_l4_post_load.elf  $S PASS
"$SWEEP" det_lua_cart l5 det_lua_l5_pre_save.elf  det_lua_l5_post_load.elf  $S PASS

echo "Stage 3 PASS"

# Stage 4 — coroutine continuity
# l7: PASS-on-FLATTEN-ERROR (pre-reload save fails; reload never starts)
"$SWEEP" det_lua_cart_coroutine l7 det_lua_coroutine_pre_save.elf det_lua_l7_post_load.elf $S PASS-on-FLATTEN-ERROR

# l8: all-S sweep for coroutine body change
echo "=== l8 all-S sweep S=1..${S_MAX} ==="
fail=0
curr_s=1
while [ "$curr_s" -le "$S_MAX" ]; do
    "$SWEEP" det_lua_cart_coroutine l8 \
        det_lua_coroutine_pre_save.elf det_lua_l8_post_load.elf \
        "$curr_s" PASS || fail=$((fail+1))
    curr_s=$((curr_s + 1))
done
if [ "$fail" -gt 0 ]; then
    echo "l8 sweep FAIL — $fail divergences"
    exit 1
fi
echo "l8 sweep PASS — ${S_MAX} frames byte-identical"

echo "Stage 4 PASS"

# Stage 5 — clean-failure surface
"$SWEEP" det_lua_cart_coroutine l6  det_lua_coroutine_pre_save.elf det_lua_l6_post_load.elf  $S FAIL-WITH-DIAGNOSTIC
"$SWEEP" det_lua_cart_coroutine l9  det_lua_coroutine_pre_save.elf det_lua_l9_post_load.elf  $S FAIL-WITH-DIAGNOSTIC
"$SWEEP" det_lua_cart_coroutine l10 det_lua_coroutine_pre_save.elf det_lua_l10_post_load.elf $S FAIL-WITH-DIAGNOSTIC

echo "Stage 5 PASS"

echo "=== Spike N Stages 3-5 PASS ==="
