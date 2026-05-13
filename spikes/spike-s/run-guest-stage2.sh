#!/usr/bin/env bash
# Spike S — Stage 2: ld.so startup syscall characterisation.
#
# Invoked via SSH in the Fedora RISC-V QEMU guest (ILP32-capable kernel).
# Determines the set of syscalls ld.so makes when loading a dynamic ILP32
# binary (adversary_s_dynamic with DT_NEEDED libblyt32.so).
#
# Steps:
#   2a. Place the ILP32 dynamic linker (ld-musl-riscv32.so.1) at the path
#       the musl-compiled cart binary's PT_INTERP specifies.
#   2b. Place libblyt32_stub.so in /tmp/ilp32-lib.
#   2c. Run strace from execve through main(), capturing all syscalls.
#   2d. Separate ld.so-phase syscalls from post-main syscalls.
#   2e. Produce phase1_ld_syscalls.txt for review.
#   2f. Gate: dynamic ILP32 binary starts and reaches main() without SIGSYS.
#
# Binaries expected at /tmp/ (SCP'd by the Makefile):
#   /tmp/adversary_s_dynamic   — ILP32 dynamic ELF
#   /tmp/libblyt32_stub.so     — stub library (no constructor at this stage)
#   /tmp/ld-musl-riscv32.so.1  — ILP32 musl dynamic linker from toolchain sysroot

set -uo pipefail

SPIKES=/mnt/spikes
SPIKE_S=$SPIKES/spike-s
BUILD_S=$SPIKE_S/build
RESULTS=/tmp/spike-s-results
STRACE_DIR=/tmp/spike-s-results/stage2-strace

PASS=0
FAIL=0
ok()      { echo "  PASS: $*"; PASS=$(( PASS + 1 )); }
err()     { echo "  FAIL: $*" >&2; FAIL=$(( FAIL + 1 )); }
section() { echo ""; echo "=== $* ==="; }

mkdir -p "$STRACE_DIR"
exec > >(tee "$RESULTS/stage2.log") 2>&1

echo "Spike S — Stage 2: ld.so startup syscall characterisation"
echo "Date:   $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
echo "Kernel: $(uname -r)"

# ── Locate binaries ────────────────────────────────────────────────────────────
ADVERSARY_DYN=/tmp/adversary_s_dynamic
LIBBLYT=/tmp/libblyt32_stub.so
LD_MUSL=/tmp/ld-musl-riscv32.so.1
ILP32_LIBDIR=/tmp/ilp32-lib

section "Stage 2a: install ILP32 dynamic linker"

if [ ! -f "$LD_MUSL" ]; then
    err "ld-musl-riscv32.so.1 not found at $LD_MUSL"
    echo "  Run: make docker-extract-ld-musl && scp to guest"
    exit 1
fi

# Determine PT_INTERP from the dynamic binary
PT_INTERP=""
if command -v readelf &>/dev/null; then
    PT_INTERP=$(readelf -l "$ADVERSARY_DYN" 2>/dev/null | \
        awk '/\[Requesting program interpreter:/{gsub(/\]$/,""); print $NF}')
fi
if [ -z "$PT_INTERP" ]; then
    # Fallback: musl ILP32 uses this canonical path
    PT_INTERP=/lib/ld-musl-riscv32.so.1
fi
echo "  PT_INTERP: $PT_INTERP"

PT_INTERP_DIR=$(dirname "$PT_INTERP")
sudo mkdir -p "$PT_INTERP_DIR"
sudo cp "$LD_MUSL" "$PT_INTERP"
sudo chmod +x "$PT_INTERP"
echo "  Installed ld.so at: $PT_INTERP"
echo "  file: $(file "$PT_INTERP")"
ok "ILP32 dynamic linker installed at $PT_INTERP"

# musl's libc IS the dynamic linker; DT_NEEDED libc.so is satisfied by the same file.
mkdir -p "$ILP32_LIBDIR"
cp "$LD_MUSL" "$ILP32_LIBDIR/libc.so"
ok "libc.so (musl) installed in $ILP32_LIBDIR (satisfies DT_NEEDED libc.so)"

section "Stage 2b: install libblyt32_stub.so"

cp "$LIBBLYT" "$ILP32_LIBDIR/libblyt32.so"
chmod +x "$ILP32_LIBDIR/libblyt32.so"
echo "  libblyt32.so installed at $ILP32_LIBDIR/libblyt32.so"
echo "  file: $(file "$ILP32_LIBDIR/libblyt32.so")"
ok "libblyt32_stub.so installed"

section "Stage 2c: strace dynamic ILP32 binary from execve to main()"

# Install strace if not present
if ! command -v strace &>/dev/null; then
    echo "  Installing strace..."
    sudo dnf install -y -q strace 2>&1 | tail -3
fi
command -v strace &>/dev/null && ok "strace available: $(strace --version 2>&1 | head -1)" \
    || { err "strace not available"; exit 1; }

chmod +x "$ADVERSARY_DYN" 2>/dev/null || true

# Probe: "write" exits 0 after calling write(2,...), so we can detect main() entry.
# Run with strace over the full execution.
STRACE_OUT=$STRACE_DIR/strace_dynamic_write.txt
echo "  strace ILP32 adversary_s_dynamic write probe..."
LD_LIBRARY_PATH=$ILP32_LIBDIR timeout 10 \
    strace -f -e trace=all -tt -o "$STRACE_OUT" \
    "$ADVERSARY_DYN" write >/dev/null 2>&1 || true
STRACE_LINES=$(wc -l < "$STRACE_OUT" 2>/dev/null || echo 0)
echo "  strace output: $STRACE_OUT ($STRACE_LINES lines)"
[ "$STRACE_LINES" -gt 0 ] && ok "strace captured output" || err "strace produced empty output"

section "Stage 2d: baseline run (no strace) — confirm binary reaches main()"

echo "  Running adversary_s_dynamic write probe without strace..."
LD_LIBRARY_PATH=$ILP32_LIBDIR "$ADVERSARY_DYN" write >/dev/null 2>&1
DYN_RC=$?
echo "  adversary_s_dynamic write: rc=$DYN_RC"
if [ $DYN_RC -eq 0 ]; then
    ok "Dynamic ILP32 binary reaches main() and exits 0"
elif [ $DYN_RC -eq 126 ]; then
    err "Dynamic ILP32 binary got ENOEXEC — ld.so or ILP32 loader issue"
elif [ $DYN_RC -eq 127 ]; then
    err "Dynamic ILP32 binary: ld.so could not find library (rc=127)"
    echo "  Check PT_INTERP path and LD_LIBRARY_PATH"
else
    err "Dynamic ILP32 binary: unexpected rc=$DYN_RC"
fi

section "Stage 2e: separate ld.so-phase syscalls from post-main"

LD_SYSCALLS=$RESULTS/phase1_ld_syscalls.txt
CART_SYSCALLS=$RESULTS/cart_phase2_syscalls.txt

if [ ! -s "$STRACE_OUT" ]; then
    err "No strace output to analyse"
else
    # Strategy: find the write syscall that indicates main() entry, then split.
    # strace -tt output format: "PID HH:MM:SS.us  syscall(...) = RET"
    # The write probe calls write(2, ".\n", 2) as its first syscall in main().
    # ld.so syscalls appear BEFORE that write.

    python3 - "$STRACE_OUT" "$LD_SYSCALLS" "$CART_SYSCALLS" <<'EOF'
import sys, re

strace_file = sys.argv[1]
ld_out = sys.argv[2]
cart_out = sys.argv[3]

# Extract syscall names from strace lines
# Format: "PID  HH:MM:SS.us  syscall_name(...) = RET"
syscall_re = re.compile(r'^\s*\d+\s+[\d:.]+\s+([a-z_][a-z0-9_]+)\(')

ld_calls = set()
cart_calls = set()
phase = 'ld'

with open(strace_file) as f:
    for line in f:
        m = syscall_re.match(line)
        if not m:
            continue
        name = m.group(1)
        # Detect transition to main(): the write probe calls write() as first syscall
        if phase == 'ld' and name == 'write':
            phase = 'main'
        if phase == 'ld':
            ld_calls.add(name)
        else:
            cart_calls.add(name)

with open(ld_out, 'w') as f:
    f.write("# Stage 2: ld.so-phase syscalls (from exec to first write in main)\n")
    f.write("# These must be in seccomp_phase1_riscv32_nrs[].\n")
    for n in sorted(ld_calls):
        f.write(n + "\n")

with open(cart_out, 'w') as f:
    f.write("# Stage 2: cart-code syscalls (from main() onwards)\n")
    f.write("# These must be in seccomp_phase2_riscv32_nrs[] (and phase1).\n")
    for n in sorted(cart_calls):
        f.write(n + "\n")

print(f"  ld.so-phase syscalls: {len(ld_calls)}")
print(f"  cart-phase syscalls:  {len(cart_calls)}")
EOF
    LD_COUNT=$(grep -vc '^#' "$LD_SYSCALLS" 2>/dev/null || echo 0)
    CART_COUNT=$(grep -vc '^#' "$CART_SYSCALLS" 2>/dev/null || echo 0)
    echo ""
    echo "  ld.so-phase syscalls ($LD_COUNT):"
    grep -v '^#' "$LD_SYSCALLS" | sed 's/^/    /'
    echo ""
    echo "  cart-phase syscalls ($CART_COUNT):"
    grep -v '^#' "$CART_SYSCALLS" | sed 's/^/    /'

    if [ "$LD_COUNT" -gt 0 ]; then
        ok "ld.so syscall characterisation complete ($LD_COUNT syscalls)"
        echo "  → Update seccomp_phase1_riscv32_nrs[] in seccomp_allowlist_s.h"
        echo "    and add seccomp(277) for the phase-2 constructor."
    else
        err "No ld.so-phase syscalls detected — check strace output"
    fi
fi

echo ""
echo "=========================================="
echo "Stage 2 complete"
printf "PASS: %-3d  FAIL: %d\n" "$PASS" "$FAIL"
echo "Outputs:"
echo "  $LD_SYSCALLS"
echo "  $CART_SYSCALLS"
echo "  $STRACE_OUT"
echo "=========================================="

[ "$FAIL" -eq 0 ] && exit 0 || exit 1
