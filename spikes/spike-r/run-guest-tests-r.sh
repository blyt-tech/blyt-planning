#!/usr/bin/env bash
# Spike R — in-guest test script (Stages 0–2).
#
# Invoked via SSH inside the Fedora 42 RISC-V QEMU guest.
# The spikes/ parent directory is mounted at /mnt/spikes via 9p virtfs.
#
# Stages:
#   0 — environment: seccomp syscall reachable; CONFIG_COMPAT confirmed
#   1 — seccomp_raw_test: Option A arch-dispatch filter + LIFO semantics
#   2 — launcher_r adversary probes: open/socket/execve/mprotect-exec → SIGSYS;
#                                    uname → exit 0

set -uo pipefail

SPIKES=/mnt/spikes
SPIKE_H=$SPIKES/spike-h
SPIKE_R=$SPIKES/spike-r
BUILD_H=$SPIKE_H/build
BUILD_R=$SPIKE_R/build
RESULTS=$BUILD_R/results

# Optional: accept binary paths as arguments to bypass 9p virtfs caching.
# When called from the Makefile, the fresh binaries are SCP'd to /tmp/.
SECCOMP_TEST="${1:-$BUILD_R/seccomp_raw_test}"
LAUNCHER_R="${2:-$BUILD_R/launcher_r}"
ADVERSARY=$BUILD_H/adversary

PASS=0
FAIL=0

ok()      { echo "  PASS: $*"; PASS=$(( PASS + 1 )); }
err()     { echo "  FAIL: $*" >&2; FAIL=$(( FAIL + 1 )); }
section() { echo ""; echo "=== $* ==="; }

mkdir -p "$RESULTS"
exec > >(tee "$RESULTS/guest-run-r.log") 2>&1

echo "Spike R — in-guest test run"
echo "Date:   $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
echo "Kernel: $(uname -r)"
echo "Arch:   $(uname -m)"

# ── Stage 0: environment ──────────────────────────────────────────────────────
section "Stage 0: environment"

# seccomp(2) syscall number on RISC-V is 277.
if grep -qr 'seccomp' /usr/include/asm-generic/unistd.h 2>/dev/null || \
   grep -qr '__NR_seccomp' /usr/include/ 2>/dev/null; then
    ok "seccomp syscall (__NR_seccomp=277) present in kernel headers"
else
    # Fall back to confirming via kallsyms
    if grep -q '__x64_sys_seccomp\|sys_seccomp' /proc/kallsyms 2>/dev/null; then
        ok "seccomp syscall present in /proc/kallsyms"
    else
        err "seccomp syscall not confirmed; filter load may fail"
    fi
fi

# CONFIG_COMPAT: Fedora 42 compiles this in unconditionally (Spike H finding).
# Confirm via compat_sys_* in kallsyms.
if grep -q 'compat_sys_' /proc/kallsyms 2>/dev/null; then
    ok "CONFIG_COMPAT: compat_sys_* present in /proc/kallsyms (compiled in)"
else
    KVER=$(uname -r)
    KCONFIG=/boot/config-$KVER
    if [ -f "$KCONFIG" ] && grep -q "^CONFIG_COMPAT=y" "$KCONFIG"; then
        ok "CONFIG_COMPAT=y in $KCONFIG"
    else
        err "CONFIG_COMPAT not confirmed — RV32 cart exec may fail"
    fi
fi

# Check seccomp_raw_test and launcher_r are present.
for f in "$SECCOMP_TEST" "$LAUNCHER_R"; do
    if [ -f "$f" ]; then
        chmod +x "$f" 2>/dev/null || true
        ok "binary present: $f ($(file "$f" | grep -o 'RISC-V.*' || echo 'RV64?'))"
    else
        err "binary missing: $f — run: make -C ../spike-r docker-build-r"
    fi
done

if [ ! -f "$ADVERSARY" ]; then
    err "adversary ELF missing: $ADVERSARY — run: make -C ../spike-h docker-build-carts"
fi

# ── Stage 1: seccomp_raw_test (arch-dispatch + LIFO semantics) ────────────────
section "Stage 1: raw BPF arch-dispatch filter (seccomp_raw_test)"

if [ ! -f "$SECCOMP_TEST" ]; then
    err "seccomp_raw_test not found — Stage 1 skipped"
elif [ ! -f "$ADVERSARY" ]; then
    err "adversary not found — Stage 1 skipped"
else
    # FINDING: This Fedora 42 kernel uses binfmt_misc + qemu-riscv32-static for
    # ILP32 binaries.  CONFIG_COMPAT provides compat_sys_* wrappers but does NOT
    # include the ELF32 loader — execv of an ILP32 binary returns ENOEXEC if
    # binfmt_misc is disabled.  The seccomp filter therefore applies to
    # qemu-riscv32-static (LP64, arch=RISCV64), not to a native ILP32 process.
    # seccomp_data.arch = AUDIT_ARCH_RISCV64 for all processes (confirmed by
    # Diagnostic E: RISCV64-only filter allows everything, ILP32 uname rc=0).
    echo "  [finding] ILP32 binaries run via binfmt_misc+qemu-riscv32-static on this kernel."
    echo "  [finding] arch=RISCV64 in seccomp_data for all processes (confirmed by test)."
    echo "  [finding] Seccomp filter applies to qemu-riscv32-static/rv32emu LP64 host calls."
    # Verify the ILP32 adversary runs without seccomp (via binfmt_misc+qemu-riscv32-static).
    "$ADVERSARY" uname >/dev/null 2>&1
    NOFILTER_RC=$?
    echo "  ILP32 adversary uname (no filter): rc=$NOFILTER_RC"
    if [ $NOFILTER_RC -eq 0 ]; then
        ok "ILP32 adversary runs without seccomp (via binfmt_misc+qemu-riscv32-static)"
    else
        err "ILP32 adversary failed without seccomp (rc=$NOFILTER_RC)"
    fi

    echo "  Running seccomp_raw_test..."
    "$SECCOMP_TEST" "$ADVERSARY"
    STAGE1_RC=$?
    if [ $STAGE1_RC -eq 0 ]; then
        ok "Stage 1: seccomp_raw_test PASS (all 3 sub-tests: A, B, C)"
    else
        err "Stage 1: seccomp_raw_test FAIL (rc=$STAGE1_RC)"
    fi
fi

# ── Stage 2: launcher_r adversary probes ──────────────────────────────────────
section "Stage 2: launcher_r adversary probes"

if [ ! -f "$LAUNCHER_R" ]; then
    err "launcher_r not found — Stage 2 skipped"
elif [ ! -f "$ADVERSARY" ]; then
    err "adversary not found — Stage 2 skipped"
else
    STAGE2_OK=1

    # STAGE 2 DESIGN NOTE (from Spike R findings):
    # This kernel uses binfmt_misc+qemu-riscv32-static for ILP32 binaries.
    # The seccomp filter applies to qemu-riscv32-static (LP64, arch=RISCV64).
    # Seccomp-testable isolation:
    #   socket (198): qemu-riscv32-static translates RV32 socket → host socket.
    #     host socket not in allowlist → SIGSYS. ✓ (tests seccomp blocking)
    #   uname (160): qemu-riscv32-static translates RV32 uname → host uname.
    #     host uname in allowlist → allowed. ✓ (tests seccomp allow)
    # Mount-namespace-isolated (NOT seccomp-testable without rootfs):
    #   open: host openat(56) is in allowlist (rv32emu needs it to load the cart ELF).
    #     Filesystem isolation provided by mount namespace (empty rootfs) in production.
    #   execve: host execve(221) is in allowlist (needed for launcher→cart exec).
    #     Blocking cart-side execve requires Option B two-phase filter (launcher installs
    #     phase-1, cart installs phase-2 that blocks execve via LIFO KILL).
    #     LIFO semantics confirmed by Test C in Stage 1.
    #   mprotect-exec: mprotect(226) is in allowlist; arg filtering needed for PROT_EXEC
    #     blocking, which requires seccomp with argument inspection or W^X kernel policy.

    # Test: socket → SIGSYS (seccomp alone blocks this LP64 syscall)
    "$LAUNCHER_R" -- "$ADVERSARY" socket 2>/dev/null
    RC=$?
    if [ $RC -eq 159 ]; then
        ok "launcher_r adversary socket → SIGSYS (rc=159) — seccomp correctly blocks socket"
    else
        err "launcher_r adversary socket → rc=$RC (expected 159=SIGSYS)"
        STAGE2_OK=0
    fi

    # Test: uname → exit 0 (seccomp allows this LP64 syscall)
    # Key difference from Spike H: the unified filter includes uname(160).
    # Spike H's libseccomp filter did not include uname on RISCV64 → killed.
    "$LAUNCHER_R" -- "$ADVERSARY" uname 2>/dev/null
    RC=$?
    if [ $RC -eq 0 ]; then
        ok "launcher_r adversary uname → exit 0 — seccomp correctly allows uname"
    else
        err "launcher_r adversary uname → rc=$RC (expected 0; is uname(160) in allowlist?)"
        STAGE2_OK=0
    fi

    if [ $STAGE2_OK -eq 1 ]; then
        ok "Stage 2: socket SIGSYS + uname exit-0 both correct"
        echo "  NOTE: open/execve/mprotect-exec isolation requires mount namespace or Option B"
    else
        err "Stage 2: one or more probes failed — see above"
    fi
fi

# ── Summary ───────────────────────────────────────────────────────────────────
echo ""
echo "=========================================="
echo "Spike R guest run complete (Stages 0-2)"
printf "PASS: %-3d  FAIL: %d\n" "$PASS" "$FAIL"
echo "Log: $RESULTS/guest-run-r.log"
echo "=========================================="

# Stage 3 reminder.
echo ""
echo "--- Stage 3 (strace allowlist derivation) ---"
echo "Run inside the guest (with rv32emu + cart workloads available):"
echo "  sudo bash /mnt/spikes/spike-r/derive_allowlist.sh <cart_dir>"
echo ""

[ "$FAIL" -eq 0 ] && exit 0 || exit 1
