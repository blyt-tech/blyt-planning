#!/usr/bin/env bash
# Spike S — Stage 3: ILP32 native cart syscall allowlist derivation.
#
# Invoked via SSH in the Fedora RISC-V QEMU guest (ILP32-capable kernel).
# Runs strace over native ILP32 cart workloads (spike-i case_a/b, spike-q
# rust_cart), subtracts the ld.so-phase syscalls (Stage 2 output), and
# produces seccomp_allowlist_s.h suggestion for the phase-2 allowlist.
#
# Also confirms:
#   - RISC-V ILP32 uses the same syscall NRs as LP64 (no ILP32-specific NRs).
#   - The cart workloads can run natively under the ILP32 ELF loader.
#
# Prerequisites:
#   - Stage 2 completed (ld.so installed, libblyt32_stub.so present).
#   - spike-i guest-stage artifacts present in virtfs.
#   - spike-q wasm-build elfs present in virtfs.
#   - adversary_s_dynamic + libblyt32.so in /tmp/ilp32-lib.

set -uo pipefail

SPIKES=/mnt/spikes
SPIKE_S=$SPIKES/spike-s
SPIKE_I_STAGE=$SPIKES/spike-i/build/guest-stage
SPIKE_Q_ELFS=$SPIKES/spike-q/wasm-build
BUILD_S=$SPIKE_S/build
RESULTS=/tmp/spike-s-results
STRACE_DIR=/tmp/spike-s-results/stage3-strace

LD_PHASE_SYSCALLS=$RESULTS/phase1_ld_syscalls.txt
ILP32_LIBDIR=/tmp/ilp32-lib

PASS=0
FAIL=0
ok()      { echo "  PASS: $*"; PASS=$(( PASS + 1 )); }
err()     { echo "  FAIL: $*" >&2; FAIL=$(( FAIL + 1 )); }
section() { echo ""; echo "=== $* ==="; }

mkdir -p "$STRACE_DIR"
exec > >(tee "$RESULTS/stage3.log") 2>&1

echo "Spike S — Stage 3: ILP32 cart syscall allowlist derivation"
echo "Date:   $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
echo "Kernel: $(uname -r)"

section "Stage 3: dependencies"

command -v strace &>/dev/null || sudo dnf install -y -q strace 2>&1 | tail -3
command -v python3 &>/dev/null || sudo dnf install -y -q python3 2>&1 | tail -3
for tool in strace python3; do
    command -v "$tool" &>/dev/null && ok "$tool: $($tool --version 2>&1 | head -1)" \
        || { err "$tool not available"; exit 1; }
done

# Confirm ld.so is installed (Stage 2 prerequisite)
if ! ls /lib/ld-musl-riscv32*.so* >/dev/null 2>&1; then
    err "ILP32 ld.so not found at /lib/ld-musl-riscv32*.so* — run Stage 2 first"
    exit 1
fi
ok "ILP32 ld.so present: $(ls /lib/ld-musl-riscv32*.so* | head -1)"

# ld.so-phase syscalls from Stage 2 (for subtraction)
if [ -f "$LD_PHASE_SYSCALLS" ]; then
    LD_PHASE_N=$(grep -vc '^#' "$LD_PHASE_SYSCALLS" 2>/dev/null || echo 0)
    ok "Stage 2 ld.so syscall list: $LD_PHASE_N syscalls"
else
    echo "  WARNING: Stage 2 ld.so syscall list not found; subtraction skipped"
    LD_PHASE_N=0
fi

section "Stage 3: stage cart artifacts"

CARTDIR=/tmp/ilp32-carts
mkdir -p "$CARTDIR" "$ILP32_LIBDIR"

STAGED_ANY=0

# spike-i cart workloads (ILP32 ELFs compiled against libconsole.so)
for lib in libconsole.so libconsolelua.so; do
    src="$SPIKE_I_STAGE/lib/$lib"
    if [ -f "$src" ]; then
        cp "$src" "$ILP32_LIBDIR/"; ok "lib: $lib"
    else
        echo "  WARNING: $src not found (spike-i stage not present)"
    fi
done

for c in a b; do
    src="$SPIKE_I_STAGE/cases/case_$c/cart_$c"
    if [ -f "$src" ]; then
        cp "$src" "$CARTDIR/cart_$c"
        chmod +x "$CARTDIR/cart_$c"
        echo "  cart_$c: $(file "$CARTDIR/cart_$c" | grep -o 'RISC-V.*' || echo '?')"
        ok "cart_$c (spike-i case $c)"
        STAGED_ANY=1
    else
        echo "  SKIP: spike-i cart_$c not found (not blocking)"
    fi
done

# spike-q rust cart
for candidate in "$SPIKE_Q_ELFS/rust_cart.elf" "$SPIKE_Q_ELFS/out/rust_cart.elf"; do
    if [ -f "$candidate" ]; then
        cp "$candidate" "$CARTDIR/rust_cart.elf"
        ok "rust_cart.elf (spike-q)"; STAGED_ANY=1; break
    fi
done
[ -z "${candidate:-}" ] || true

# adversary_s_dynamic (minimal cart with DT_NEEDED libblyt32.so)
if [ -f /tmp/adversary_s_dynamic ]; then
    cp /tmp/adversary_s_dynamic "$CARTDIR/adversary_dynamic"
    ok "adversary_s_dynamic (spike-s baseline cart)"
    STAGED_ANY=1
fi
if [ -f /tmp/libblyt32_stub.so ]; then
    cp /tmp/libblyt32_stub.so "$ILP32_LIBDIR/libblyt32.so"
    ok "libblyt32_stub.so installed in $ILP32_LIBDIR"
fi
# musl libc.so is the dynamic linker; needed by DT_NEEDED libc.so in musl binaries
if ls /lib/ld-musl-riscv32*.so* >/dev/null 2>&1; then
    cp "$(ls /lib/ld-musl-riscv32*.so* | head -1)" "$ILP32_LIBDIR/libc.so"
    ok "libc.so (musl) installed in $ILP32_LIBDIR"
fi

if [ "$STAGED_ANY" -eq 0 ]; then
    err "no cart binaries found"; exit 1
fi

section "Stage 3: strace cart workloads"

STRACE_SECS=5

# run_strace LABEL CART [ARG...]
run_strace() {
    local label="$1" cart="$2"
    shift 2
    local out="$STRACE_DIR/strace_${label}.txt"
    if [ ! -f "$cart" ]; then
        echo "  SKIP $label: $cart not found"; return
    fi
    echo "  strace $label (${STRACE_SECS}s) LD_LIBRARY_PATH=$ILP32_LIBDIR..."
    timeout "$STRACE_SECS" \
        strace -f -e trace=all -o "$out" \
        env LD_LIBRARY_PATH="$ILP32_LIBDIR" "$cart" "$@" >/dev/null 2>&1 || true
    local lines; lines=$(wc -l < "$out" 2>/dev/null || echo 0)
    echo "  → $out ($lines lines)"
    [ "$lines" -gt 0 ] && ok "strace $label" || err "strace $label: empty output"
}

# adversary_s_dynamic: write probe exits 0 cleanly → captures full ld.so + main path
run_strace "adversary"  "$CARTDIR/adversary_dynamic"  write
run_strace "cart_a"     "$CARTDIR/cart_a"
run_strace "cart_b"     "$CARTDIR/cart_b"
run_strace "rust_cart"  "$CARTDIR/rust_cart.elf"

section "Stage 3: syscall union + NR mapping"

UNION="$STRACE_DIR/syscall_names.txt"
NR_MAP="$STRACE_DIR/syscall_nr_map.txt"
CART_ONLY="$STRACE_DIR/cart_only_syscalls.txt"

# Collect unique syscall names from all strace files
grep -h '^[0-9 ]*[a-z_][a-z_0-9]*(' "$STRACE_DIR"/strace_*.txt 2>/dev/null \
    | sed 's/^[0-9 ]*//; s/(.*$//' | sort -u > "$UNION" || true

echo "  Union syscall names ($(wc -l < "$UNION" 2>/dev/null)):"
cat "$UNION" | sed 's/^/    /'

# Build NR → name map from kernel headers
if [ -f /usr/include/asm-generic/unistd.h ]; then
    grep -E '^\s*#define\s+__NR_[a-z]' /usr/include/asm-generic/unistd.h \
        | awk '{print $3, $2}' | sed 's/__NR_//' | sort -n > "$NR_MAP" 2>/dev/null || true
fi

# Subtract ld.so-phase syscalls to isolate cart-only syscalls
if [ -f "$LD_PHASE_SYSCALLS" ] && [ "$LD_PHASE_N" -gt 0 ]; then
    comm -23 \
        <(grep -v '^#' "$UNION" | sort) \
        <(grep -v '^#' "$LD_PHASE_SYSCALLS" | sort) \
        > "$CART_ONLY" 2>/dev/null || true
    echo ""
    echo "  Cart-only syscalls (after subtracting ld.so-phase):"
    cat "$CART_ONLY" | sed 's/^/    /'
    ok "cart-only syscall set produced: $(wc -l < "$CART_ONLY") syscalls"
else
    cp "$UNION" "$CART_ONLY" 2>/dev/null || true
    echo "  (ld.so subtraction skipped; cart_only = full union)"
fi

section "Stage 3: verify unified NR table (ILP32 == LP64)"

# Check for any ILP32-specific compat NRs.
# RISC-V has no compat NRs (unified table), so none should appear.
# ARM32 had mmap2(192), clock_gettime64(403), etc. — those should NOT appear here.
ILP32_SPECIFIC_NRS="192 403 422 325 327"  # known compat-only NRs from ARM/x86
FOUND_COMPAT=0
if [ -s "$NR_MAP" ]; then
    for nr in $ILP32_SPECIFIC_NRS; do
        name=$(awk -v n="$nr" '$1 == n {print $2}' "$NR_MAP" 2>/dev/null | head -1)
        if grep -qw "${name:-__NOMATCH__}" "$UNION" 2>/dev/null; then
            echo "  WARNING: ILP32-specific compat NR $nr ($name) found in trace!"
            FOUND_COMPAT=1
        fi
    done
fi
if [ $FOUND_COMPAT -eq 0 ]; then
    ok "No ARM/x86 compat NRs found — RISC-V unified syscall table confirmed"
else
    err "ILP32-specific compat NRs found — update seccomp_allowlist_s.h accordingly"
fi

section "Stage 3: generate seccomp_allowlist_s.h suggestion"

SUGG="$STRACE_DIR/allowlist_suggestion.h"
{
    echo "/* Stage 3 allowlist suggestion — $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    echo " * Kernel: $(uname -r)"
    echo " *"
    echo " * MANUAL REVIEW REQUIRED before updating seccomp_allowlist_s.h."
    echo " * Replace the placeholder arrays with empirically validated lists."
    echo " * Add seccomp(277) to phase-1 RISCV32 (for phase-2 constructor)."
    echo " */"
    echo ""
    echo "/* Phase-1 RISCV32 (ld.so startup syscalls): */"
    if [ -f "$LD_PHASE_SYSCALLS" ]; then
        while IFS= read -r name; do
            [[ "$name" =~ ^# ]] && continue
            nr=""
            [ -s "$NR_MAP" ] && nr=$(awk -v n="$name" '$2==n{print $1}' "$NR_MAP" | head -1)
            printf "/* %3s */ %s,\n" "${nr:--}" "$name"
        done < "$LD_PHASE_SYSCALLS"
    fi
    echo "/* 277 */ seccomp,  /* phase-2 constructor */"
    echo ""
    echo "/* Phase-2 RISCV32 (cart-code syscalls only): */"
    while IFS= read -r name; do
        nr=""
        [ -s "$NR_MAP" ] && nr=$(awk -v n="$name" '$2==n{print $1}' "$NR_MAP" | head -1)
        printf "/* %3s */ %s,\n" "${nr:--}" "$name"
    done < "$CART_ONLY"
} > "$SUGG"

echo "  Suggestion written to: $SUGG"
ok "seccomp_allowlist_s.h suggestion generated"

echo ""
echo "=========================================="
echo "Stage 3 complete"
printf "PASS: %-3d  FAIL: %d\n" "$PASS" "$FAIL"
echo "Outputs:"
echo "  $UNION          — full syscall union"
echo "  $CART_ONLY      — cart-only (ld.so subtracted)"
echo "  $NR_MAP         — NR → name map"
echo "  $SUGG           — allowlist suggestion header"
echo "  $STRACE_DIR/strace_*.txt  — raw strace captures"
echo ""
echo "Next: update seccomp_phase1_riscv32_nrs[] and seccomp_phase2_riscv32_nrs[]"
echo "in seccomp_allowlist_s.h, rebuild, then run Stage 4."
echo "=========================================="

[ "$FAIL" -eq 0 ] && exit 0 || exit 1
