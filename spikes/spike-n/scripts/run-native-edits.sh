#!/bin/sh
# Spike N Stage 2 — native edit runner (n1-n6).
#
# Runs the full native edit suite across both hosts.
# Each edit uses a fixed save frame (mid-point: frame 15).
# Expected: all 6 edits PASS; latency < 3000 ms each.

set -eu

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
SWEEP="$REPO_ROOT/scripts/run-edit-sweep.sh"
S=15

echo "=== Spike N Stage 2 — native edits (n1-n6) ==="

"$SWEEP" det_native_cart n1 det_native_n1_pre_save.elf  det_native_n1_post_load.elf  $S PASS
"$SWEEP" det_native_cart n2 det_native_n2_pre_save.elf  det_native_n2_post_load.elf  $S PASS
"$SWEEP" det_native_cart n3 det_native_n3_pre_save.elf  det_native_n3_post_load.elf  $S PASS
"$SWEEP" det_native_cart n4 det_native_n4_pre_save.elf  det_native_n4_post_load.elf  $S PASS
"$SWEEP" det_native_cart n5 det_native_n5_pre_save.elf  det_native_n5_post_load.elf  $S PASS
"$SWEEP" det_native_cart n6 det_native_n6_pre_save.elf  det_native_n6_post_load.elf  $S PASS

echo "=== Spike N Stage 2 PASS — all 6 native edits byte-identical cross-host ==="
