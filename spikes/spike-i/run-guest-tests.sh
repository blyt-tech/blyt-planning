#!/usr/bin/env bash
# Spike I — in-guest test runner for the QEMU Fedora 42 RV64 image.
# Boots via cloud-init, virtfs share mounted at /mnt/spike-i.
#
# Validates that the four cart configurations load and execute end-to-end
# under the kernel's CONFIG_COMPAT path (RV32 ELFs exec'd on RV64 Linux)
# combined with the system dynamic linker resolving the multi-library
# layout the same way the rv32emu loader does in Stage 2.

set -uo pipefail

SPIKE_I=/mnt/spike-i
LIB_DIR=$SPIKE_I/lib
RESULTS=$SPIKE_I/build/results

mkdir -p "$RESULTS"
exec > >(tee "$RESULTS/guest-run.log") 2>&1

PASS=0
FAIL=0
ok()  { echo "  PASS: $*"; PASS=$(( PASS + 1 )); }
err() { echo "  FAIL: $*" >&2; FAIL=$(( FAIL + 1 )); }
section() { echo ""; echo "=== $* ==="; }

echo "Spike I — in-guest test run"
echo "Date: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
echo "Kernel: $(uname -r)"
echo "Arch:   $(uname -m)"

# ── kernel features ──────────────────────────────────────────────────────────
section "Kernel features"

KCONFIG=/boot/config-$(uname -r)
if [ -f "$KCONFIG" ] && grep -q "^CONFIG_COMPAT=y" "$KCONFIG"; then
    ok "CONFIG_COMPAT=y"
else
    echo "  note: CONFIG_COMPAT presence not confirmable from $KCONFIG"
fi

# ── install RV32 dynamic linker at PT_INTERP path ────────────────────────────
section "Install /lib/ld-musl-riscv32.so.1"

if [ ! -f "$SPIKE_I/qemu-guest/ld-musl-riscv32.so.1" ]; then
    err "ld-musl-riscv32.so.1 not found in virtfs share — host-side make target should have placed it"
else
    sudo cp "$SPIKE_I/qemu-guest/ld-musl-riscv32.so.1" /lib/ld-musl-riscv32.so.1
    sudo chmod +x /lib/ld-musl-riscv32.so.1
    ok "installed /lib/ld-musl-riscv32.so.1"
fi

# ── run each case ────────────────────────────────────────────────────────────
expect_a="frame 0
frame 1
frame 2
frame 3
frame 4
frame 5
frame 6
frame 7
frame 8
frame 9
OK"

expect_b="frame 0 mylib=42
frame 1 mylib=42
frame 2 mylib=42
frame 3 mylib=42
frame 4 mylib=42
frame 5 mylib=42
frame 6 mylib=42
frame 7 mylib=42
frame 8 mylib=42
frame 9 mylib=42
OK"

expect_c="$expect_a"

expect_d="cart-side init from C
frame 0 mylib=7
frame 1 mylib=7
frame 2 mylib=7
frame 3 mylib=7
frame 4 mylib=7
frame 5 mylib=7
frame 6 mylib=7
frame 7 mylib=7
frame 8 mylib=7
frame 9 mylib=7
OK"

run_case() {
    local name=$1 expected=$2
    local cart=$SPIKE_I/cases/case_${name}/cart_${name}
    if [ ! -x "$cart" ]; then err "cart_${name} missing at $cart"; return; fi
    local actual rc
    actual=$(LD_LIBRARY_PATH=$LIB_DIR "$cart" 2>&1); rc=$?
    if [ $rc -ne 0 ]; then
        err "case_${name}: rc=$rc (expected 0)"
        echo "  output: $actual"
        return
    fi
    if [ "$actual" = "$expected" ]; then
        ok "case_${name}: output matches expected ($(echo "$actual" | wc -l) lines, rc=0)"
    else
        err "case_${name}: output mismatch"
        echo "  expected:"; printf '%s\n' "$expected" | sed 's/^/    /'
        echo "  actual:";   printf '%s\n' "$actual"   | sed 's/^/    /'
    fi
}

section "Case a — C-only cart"
run_case a "$expect_a"

section "Case b — C cart + user library"
run_case b "$expect_b"

section "Case c — Lua-only cart"
run_case c "$expect_c"

section "Case d — Lua cart + C user library"
run_case d "$expect_d"

# ── LD_DEBUG=symbols evidence for cart_lua_modules weak resolution ──────────
section "LD_DEBUG=symbols — cart_lua_modules binding"

for c in a b c d; do
    cart=$SPIKE_I/cases/case_$c/cart_$c
    [ -x "$cart" ] || continue
    line=$(LD_LIBRARY_PATH=$LIB_DIR LD_DEBUG=symbols "$cart" 2>&1 \
            | grep "cart_lua_modules" | head -1 || true)
    if [ -n "$line" ]; then
        echo "  case_$c: $line"
    else
        echo "  case_$c: (no cart_lua_modules binding line — symbol weakly absent)"
    fi
done

echo ""
echo "=========================================="
echo "Spike I guest run complete"
printf "PASS: %-3d  FAIL: %d\n" "$PASS" "$FAIL"
echo "Log: $RESULTS/guest-run.log"
echo "=========================================="

[ "$FAIL" -eq 0 ] && exit 0 || exit 1
