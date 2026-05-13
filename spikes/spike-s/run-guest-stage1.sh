#!/usr/bin/env bash
# Spike S — Stage 1: arch-dispatch filter across the exec boundary.
#
# Invoked via SSH in the Fedora RISC-V QEMU guest (ILP32-capable kernel).
# Runs seccomp_raw_test_s which validates:
#   A. LP64 write allowed under RV64 rules                  (expect rc=0)
#   B. LP64 socket blocked under RV64 rules                 (expect rc=159)
#   C. ILP32 write probe allowed under RV32 rules           (expect rc=0)
#   D. ILP32 socket probe blocked under RV32 rules          (expect rc=159)
#   E. ILP32 execve probe blocked under RV32 rules          (expect rc=159)
#   F. RISCV64-only filter kills ILP32 (confirms arch=RISCV32) (expect rc=159)
#
# Usage:
#   sudo bash run-guest-stage1.sh <seccomp_raw_test_s_path> <adversary_s_static_path>

set -uo pipefail

SPIKES=/mnt/spikes
SPIKE_S=$SPIKES/spike-s
BUILD_S=$SPIKE_S/build
RESULTS=/tmp/spike-s-results

SECCOMP_TEST="${1:-$BUILD_S/seccomp_raw_test_s}"
ADVERSARY_STATIC="${2:-$BUILD_S/adversary_s_static}"

PASS=0
FAIL=0
ok()      { echo "  PASS: $*"; PASS=$(( PASS + 1 )); }
err()     { echo "  FAIL: $*" >&2; FAIL=$(( FAIL + 1 )); }
section() { echo ""; echo "=== $* ==="; }

mkdir -p "$RESULTS"
exec > >(tee "$RESULTS/stage1.log") 2>&1

echo "Spike S — Stage 1: arch-dispatch filter across exec boundary"
echo "Date:   $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
echo "Kernel: $(uname -r)"

section "Stage 1: seccomp_raw_test_s"

if [ ! -f "$SECCOMP_TEST" ]; then
    err "seccomp_raw_test_s not found at $SECCOMP_TEST"
    echo "  Run: make docker-build-s"
    exit 1
fi
if [ ! -f "$ADVERSARY_STATIC" ]; then
    err "adversary_s_static not found at $ADVERSARY_STATIC"
    echo "  Run: make docker-build-ilp32-s"
    exit 1
fi

chmod +x "$SECCOMP_TEST" "$ADVERSARY_STATIC" 2>/dev/null || true

echo "  seccomp_raw_test_s: $(file "$SECCOMP_TEST" | grep -o 'RISC-V.*' || echo RV64)"
echo "  adversary_s_static: $(file "$ADVERSARY_STATIC" | grep -o 'RISC-V.*' || echo ILP32)"
echo ""

# Verify the static ILP32 binary runs without seccomp first (baseline).
echo "  Baseline: ILP32 adversary write probe without filter..."
"$ADVERSARY_STATIC" write >/dev/null 2>&1
BASELINE_RC=$?
echo "  Baseline write: rc=$BASELINE_RC"
if [ $BASELINE_RC -eq 0 ]; then
    ok "ILP32 adversary runs without seccomp"
elif [ $BASELINE_RC -eq 126 ]; then
    err "ILP32 adversary got ENOEXEC — kernel missing ILP32 ELF loader; Stage 1 cannot proceed"
    exit 1
else
    err "ILP32 adversary baseline rc=$BASELINE_RC (unexpected)"
fi

echo ""
echo "  Running seccomp_raw_test_s (Tests A-F)..."
"$SECCOMP_TEST" "$ADVERSARY_STATIC"
STAGE1_RC=$?

echo ""
if [ $STAGE1_RC -eq 0 ]; then
    ok "Stage 1: seccomp_raw_test_s ALL PASS (Tests A-F)"
    echo "  Arch-dispatch filter confirmed across exec boundary."
    echo "  AUDIT_ARCH_RISCV32 confirmed for ILP32 native processes."
    PASS=$(( PASS + 1 ))
else
    err "Stage 1: seccomp_raw_test_s FAIL (rc=$STAGE1_RC) — see output above"
    FAIL=$(( FAIL + 1 ))
fi

echo ""
echo "=========================================="
echo "Stage 1 complete"
printf "PASS: %-3d  FAIL: %d\n" "$PASS" "$FAIL"
echo "Log: $RESULTS/stage1.log"
echo "=========================================="

[ "$FAIL" -eq 0 ] && exit 0 || exit 1
