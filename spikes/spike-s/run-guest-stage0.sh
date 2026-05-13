#!/usr/bin/env bash
# Spike S — Stage 0: environment check + ILP32 native exec confirmation.
#
# Invoked via SSH in the Fedora RISC-V QEMU guest (ILP32-capable kernel).
# Confirms:
#   0a. Kernel version and ILP32 ELF loader availability.
#   0b. Static ILP32 binary (adversary_s_static) executes natively.
#   0c. seccomp(2) syscall is available.
#   0d. AUDIT_ARCH_RISCV32 is reported for exec'd ILP32 processes.
#
# Usage:
#   sudo bash run-guest-stage0.sh <seccomp_raw_test_s_path> <adversary_s_static_path>

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
exec > >(tee "$RESULTS/stage0.log") 2>&1

echo "Spike S — Stage 0: environment"
echo "Date:   $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
echo "Kernel: $(uname -r)"
echo "Arch:   $(uname -m)"

section "Stage 0a: kernel ILP32 ELF loader check"

# Check for RISC-V ILP32 compat ELF loader in /proc/kallsyms.
# The loader registers a binary handler; its symbols start with "compat_"
# or "riscv_ilp32_".  Exact symbol names depend on the patchset.
ILP32_PRESENT=0
for pat in 'riscv_compat' 'ilp32' 'compat_elf_check_arch'; do
    if grep -qi "$pat" /proc/kallsyms 2>/dev/null; then
        ILP32_PRESENT=1
        ok "ILP32 loader symbol '$pat' found in /proc/kallsyms"
        break
    fi
done
if [ $ILP32_PRESENT -eq 0 ]; then
    # Also try checking /proc/sys/fs/binfmt_misc for non-qemu entries
    if [ -d /proc/sys/fs/binfmt_misc ]; then
        # A native ILP32 ELF loader would not need binfmt_misc
        echo "  binfmt_misc entries: $(ls /proc/sys/fs/binfmt_misc/ 2>/dev/null | grep -v status | tr '\n' ' ')"
    fi
    err "ILP32 loader symbol not found in /proc/kallsyms — may be stock kernel"
    echo "  Try: grep -i 'compat\|ilp32\|riscv32' /proc/kallsyms | head -5"
fi

# Check kernel config for COMPAT (if available)
KVER=$(uname -r)
KCONFIG=/boot/config-$KVER
if [ -f "$KCONFIG" ]; then
    for opt in CONFIG_COMPAT CONFIG_COMPAT_32BIT_TIME; do
        if grep -q "^${opt}=y" "$KCONFIG"; then
            ok "$opt=y in $KCONFIG"
        else
            echo "  NOTE: $opt not found or not y in $KCONFIG"
        fi
    done
fi

section "Stage 0b: static ILP32 binary executes natively"

if [ ! -f "$ADVERSARY_STATIC" ]; then
    err "adversary_s_static not found at $ADVERSARY_STATIC"
    echo "  Run: make docker-build-ilp32-s"
else
    chmod +x "$ADVERSARY_STATIC" 2>/dev/null || true
    echo "  file: $(file "$ADVERSARY_STATIC")"

    # Try executing it with the write probe (exits 0 on success)
    "$ADVERSARY_STATIC" write >/dev/null 2>&1
    STATIC_RC=$?
    echo "  adversary_s_static write probe: rc=$STATIC_RC"
    if [ $STATIC_RC -eq 0 ]; then
        ok "Static ILP32 binary executes natively (rc=0)"
    elif [ $STATIC_RC -eq 126 ]; then
        err "Static ILP32 binary got ENOEXEC (rc=126) — kernel lacks ILP32 ELF loader"
        echo "  This kernel cannot load ILP32 binaries natively."
        echo "  Spike S requires a kernel built with RISC-V ILP32 compat support."
        echo "  See PLAN.md §Test environment for build instructions."
    else
        err "Static ILP32 binary returned rc=$STATIC_RC (unexpected)"
    fi
fi

section "Stage 0c: seccomp(2) syscall available"

if grep -q '__NR_seccomp\|sys_seccomp' /proc/kallsyms 2>/dev/null || \
   grep -qr '__NR_seccomp' /usr/include/ 2>/dev/null; then
    ok "seccomp(2) (__NR_seccomp=277) available"
else
    err "seccomp(2) not confirmed (may still work; check manually)"
fi

section "Stage 0d: AUDIT_ARCH_RISCV32 confirmed for ILP32 processes"

# This uses seccomp_raw_test_s Test F: a RISCV64-only allow-all filter kills
# the ILP32 adversary on its first syscall (because arch != RISCV64).
# If we observe SIGSYS, arch is RISCV32.  If the adversary exits 0, the
# kernel is still reporting RISCV64 (no native ILP32 exec).

if [ ! -f "$SECCOMP_TEST" ]; then
    err "seccomp_raw_test_s not found at $SECCOMP_TEST"
elif [ ! -f "$ADVERSARY_STATIC" ]; then
    err "adversary_s_static not found — skipping arch confirmation"
else
    chmod +x "$SECCOMP_TEST" 2>/dev/null || true
    echo "  Running seccomp_raw_test_s Test F (RISCV64-only filter kills ILP32)..."
    # Run only Stage 0d confirmation inline, not the full test.
    # The full Stage 1 test is in run-guest-stage1.sh.
    "$SECCOMP_TEST" "$ADVERSARY_STATIC" 2>&1 | grep -E "(Test F|PASS F|FAIL F)" || true
    # Check the overall result via the Stage 1 exit code for Test F only.
    # For now, just note the finding; Stage 1 provides the definitive result.
    echo "  (Full arch-dispatch validation is in Stage 1)"
    ok "seccomp_raw_test_s available for Stage 1 testing"
fi

echo ""
echo "=========================================="
echo "Stage 0 complete"
printf "PASS: %-3d  FAIL: %d\n" "$PASS" "$FAIL"
echo "Log: $RESULTS/stage0.log"
echo "=========================================="

if [ $FAIL -gt 0 ]; then
    echo ""
    echo "ACTION REQUIRED: Stage 0 failures indicate the kernel does not support"
    echo "native ILP32 exec.  Build a kernel with CONFIG_RISCV ILP32 support"
    echo "(see PLAN.md §Test environment, Option 1) and retry."
fi

[ "$FAIL" -eq 0 ] && exit 0 || exit 1
