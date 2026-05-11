#!/usr/bin/env bash
# Spike R — in-guest Stage 3: build rv32emu and strace cart workloads.
#
# Invoked via SSH inside the Fedora 42 RISC-V QEMU guest.
# The spikes/ parent directory is mounted at /mnt/spikes via 9p virtfs.
#
# Builds rv32emu from spike-a source (CONFIG_EXT_A + multi-dynload patch),
# runs strace over spike-i carts, and saves the syscall union to results/.
#
# After this script completes, update seccomp_allowlist.h on the host
# based on the strace output in build/results/stage3-strace/.

set -uo pipefail

SPIKES=/mnt/spikes
SPIKE_A_RV32EMU=$SPIKES/spike-a/rv32emu
SPIKE_I_STAGE=$SPIKES/spike-i/build/guest-stage
SPIKE_Q_ELFS=$SPIKES/spike-q/wasm-build
SPIKE_R=$SPIKES/spike-r
RESULTS=$SPIKE_R/build/results
STRACE_OUT=$RESULTS/stage3-strace

mkdir -p "$STRACE_OUT"
exec > >(tee "$RESULTS/stage3-build.log") 2>&1

PASS=0
FAIL=0

ok()      { echo "  PASS: $*"; PASS=$(( PASS + 1 )); }
err()     { echo "  FAIL: $*" >&2; FAIL=$(( FAIL + 1 )); }
section() { echo ""; echo "=== $* ==="; }

echo "Spike R Stage 3 — rv32emu build + strace"
echo "Date:   $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
echo "Kernel: $(uname -r)"

# ── Install dependencies ──────────────────────────────────────────────────────
section "Dependencies"

NEED_PKGS=""
command -v strace   &>/dev/null || NEED_PKGS="$NEED_PKGS strace"
command -v gcc      &>/dev/null || NEED_PKGS="$NEED_PKGS gcc make"
command -v bc       &>/dev/null || NEED_PKGS="$NEED_PKGS bc"
command -v python3  &>/dev/null || NEED_PKGS="$NEED_PKGS python3"

if [ -n "$NEED_PKGS" ]; then
    echo "  Installing:$NEED_PKGS ..."
    # shellcheck disable=SC2086
    sudo dnf install -y -q $NEED_PKGS 2>&1 | grep -v '^Warning:' | tail -5
fi

for tool in strace gcc bc python3; do
    if command -v "$tool" &>/dev/null; then
        ok "$tool: $($tool --version 2>&1 | head -1 || echo 'ok')"
    else
        err "$tool not available"; exit 1
    fi
done

# ── Build rv32emu ─────────────────────────────────────────────────────────────
section "Build rv32emu (spike-a source, CONFIG_EXT_A, multi-dynload)"

RV32EMU_SRC=/tmp/rv32emu-stage3-src
RV32EMU_OUT=/tmp/rv32emu-stage3-out
RV32EMU_BIN=$RV32EMU_OUT/rv32emu

if [ -x "$RV32EMU_BIN" ]; then
    ok "rv32emu already present at $RV32EMU_BIN"
else
    echo "  Copying rv32emu source from virtfs..."
    rm -rf "$RV32EMU_SRC"
    cp -r "$SPIKE_A_RV32EMU" "$RV32EMU_SRC"

    echo "  Enabling CONFIG_EXT_A..."
    sed -i 's/# CONFIG_EXT_A is not set/CONFIG_EXT_A=y/' "$RV32EMU_SRC/.config"

    # Disable LTO — not all Fedora gcc installs have gcc-ar/binutils LTO support
    sed -i 's/^CONFIG_LTO=y/# CONFIG_LTO is not set/' "$RV32EMU_SRC/.config"

    echo "  Applying multi-dynload patch..."
    (cd "$RV32EMU_SRC" && python3 "$SPIKES/spike-i/patches/apply-multi-dynload.py")

    echo "  Building rv32emu (interpreter-only, no SDL)..."
    mkdir -p "$RV32EMU_OUT"
    if make -C "$RV32EMU_SRC" OUT="$RV32EMU_OUT" -j"$(nproc)" 2>&1 | tail -10; then
        :
    else
        err "rv32emu build failed"; exit 1
    fi

    if [ -x "$RV32EMU_BIN" ]; then
        ok "rv32emu built: $(file "$RV32EMU_BIN" | sed 's/.*: //')"
    else
        err "rv32emu binary not found after build"; exit 1
    fi
fi

RV32EMU_VER=$("$RV32EMU_BIN" --version 2>&1 | head -1 || echo "unknown version")
echo "  rv32emu: $RV32EMU_VER"
echo "  rv32emu commit: $(git -C "$SPIKE_A_RV32EMU" rev-parse HEAD 2>/dev/null || echo unknown)"

# ── Stage cart artifacts ──────────────────────────────────────────────────────
section "Stage cart artifacts"

LIBDIR=/tmp/rv32emu-stage3-libs
CARTDIR=/tmp/rv32emu-stage3-carts
mkdir -p "$LIBDIR" "$CARTDIR"

STAGED_ANY=0

for lib in libconsole.so libconsolelua.so; do
    src="$SPIKE_I_STAGE/lib/$lib"
    if [ -f "$src" ]; then
        cp "$src" "$LIBDIR/"; ok "lib: $lib"
    else
        err "missing $src"
    fi
done

for c in a b; do
    src="$SPIKE_I_STAGE/cases/case_$c/cart_$c"
    if [ -f "$src" ]; then
        cp "$src" "$CARTDIR/cart_$c"
        chmod +x "$CARTDIR/cart_$c"
        ok "cart_$c (spike-i case $c)"
        STAGED_ANY=1
    else
        err "spike-i cart_$c missing at $src"
    fi
done

# Spike Q rust cart
for candidate in "$SPIKE_Q_ELFS/rust_cart.elf" "$SPIKE_Q_ELFS/out/rust_cart.elf"; do
    if [ -f "$candidate" ]; then
        cp "$candidate" "$CARTDIR/rust_cart.elf"
        ok "rust_cart.elf (spike-q)"; STAGED_ANY=1; break
    fi
done

if [ "$STAGED_ANY" -eq 0 ]; then
    err "no cart binaries found"; exit 1
fi

# Adversary binary from spike-h (for a baseline no-lib run)
if [ -f "$SPIKES/spike-h/build/adversary" ]; then
    cp "$SPIKES/spike-h/build/adversary" "$CARTDIR/adversary"
    ok "adversary (spike-h, no libs needed)"
fi

# ── Run strace captures ───────────────────────────────────────────────────────
section "strace rv32emu"

STRACE_SECS=5

run_strace() {
    local label="$1" cart="$2" lib="${3:-}"
    local out="$STRACE_OUT/strace_${label}.txt"
    if [ ! -f "$cart" ]; then
        echo "  SKIP $label: $cart not found"; return
    fi
    echo "  strace $label (${STRACE_SECS}s)..."
    local rv32_args=("$RV32EMU_BIN")
    [ -n "$lib" ] && rv32_args+=(-L "$lib")
    rv32_args+=("$cart")
    timeout "$STRACE_SECS" strace -f -e trace=all -o "$out" \
        "${rv32_args[@]}" >/dev/null 2>&1 || true
    local lines
    lines=$(wc -l < "$out" 2>/dev/null || echo 0)
    echo "  → $out ($lines lines)"
    [ "$lines" -gt 0 ] && ok "strace $label" || err "strace $label: empty output"
}

# Baseline: adversary (static, no libs) gives minimum rv32emu syscall set
run_strace "adversary" "$CARTDIR/adversary"

# Cart workloads: need -L for shared libs
run_strace "native_c" "$CARTDIR/cart_a" "$LIBDIR"
run_strace "lua"      "$CARTDIR/cart_b" "$LIBDIR"
run_strace "rust_q"   "$CARTDIR/rust_cart.elf" "$LIBDIR"

# ── Collect syscall names and numbers ─────────────────────────────────────────
section "Syscall union"

UNION="$STRACE_OUT/syscall_names.txt"
NR_MAP="$STRACE_OUT/syscall_nr_map.txt"

# Collect unique syscall names from all strace files
grep -h '^[0-9]* \+[a-z_][a-z_0-9]*(' "$STRACE_OUT"/strace_*.txt 2>/dev/null \
    | sed 's/^[0-9 ]*//; s/(.*$//' \
    | sort -u > "$UNION" || true

echo "  Unique syscall names: $(wc -l < "$UNION")"
cat "$UNION"

# Extract syscall number → name pairs from strace output.
# strace -f with -e trace=all writes lines like: "PID  syscall(...) = RET"
# but does NOT show NR directly.  Use ausyscall if available, else grep from headers.
if command -v ausyscall &>/dev/null; then
    ausyscall --dump 2>/dev/null | awk '{print $1, $2}' | sort -n > "$NR_MAP"
elif [ -f /usr/include/asm-generic/unistd.h ]; then
    grep -E '^\s*#define\s+__NR_[a-z]' /usr/include/asm-generic/unistd.h \
        | awk '{print $3, $2}' \
        | sed 's/__NR_//' | sort -n > "$NR_MAP" || true
fi

# Build NR→name lookup for the names we observed
if [ -s "$NR_MAP" ]; then
    echo ""
    echo "  Observed syscalls with NR:"
    while IFS= read -r name; do
        nr=$(awk -v n="$name" '$2 == n {print $1}' "$NR_MAP" 2>/dev/null | head -1)
        printf "  %4s  %s\n" "${nr:--}" "$name"
    done < "$UNION"
fi

# ── Generate allowlist suggestion ─────────────────────────────────────────────
section "Allowlist suggestion"

SUGG="$STRACE_OUT/allowlist_suggestion.h"
{
    echo "/* Stage 3 allowlist suggestion — generated $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    echo " * rv32emu: $RV32EMU_VER"
    echo " * Kernel:  $(uname -r)"
    echo " * Source:  spike-a/rv32emu commit $(git -C "$SPIKE_A_RV32EMU" rev-parse HEAD 2>/dev/null || echo unknown)"
    echo " *"
    echo " * MANUAL REVIEW REQUIRED before updating seccomp_allowlist.h:"
    echo " *   1. Remove syscalls that are launcher-only (execve, unshare, pivot_root)."
    echo " *   2. Keep anything rv32emu itself needs, even if not in the old allowlist."
    echo " *   3. Note any NEW syscalls not in seccomp_unified_nrs[] (these are gaps)."
    echo " */"
    echo ""
    echo "/* Observed syscall names (alphabetical):"
    while IFS= read -r name; do
        nr=""
        [ -s "$NR_MAP" ] && nr=$(awk -v n="$name" '$2 == n {print $1}' "$NR_MAP" | head -1)
        printf " *   %s(%s)\n" "$name" "${nr:--}"
    done < "$UNION"
    echo " */"
} > "$SUGG"

echo "  Suggestion: $SUGG"

# ── Stage 4 — adversary re-verification + rv32emu end-to-end ─────────────────
section "Stage 4: adversary re-verification + rv32emu end-to-end"

LAUNCHER_R=/tmp/launcher_r
if [ ! -x "$LAUNCHER_R" ]; then
    # Try virtfs path (SCP'd before mount in qemu-test-fedora-r)
    LAUNCHER_R_VFS=$SPIKE_R/build/launcher_r
    if [ -f "$LAUNCHER_R_VFS" ]; then
        cp "$LAUNCHER_R_VFS" /tmp/launcher_r
        chmod +x /tmp/launcher_r
        LAUNCHER_R=/tmp/launcher_r
    fi
fi

if [ ! -x "$LAUNCHER_R" ]; then
    err "launcher_r not found — skipping Stage 4"
else
    ADVERSARY=$SPIKES/spike-h/build/adversary
    STAGE4_OK=1

    # socket → SIGSYS (socket(198) not in production allowlist)
    "$LAUNCHER_R" -- "$ADVERSARY" socket 2>/dev/null
    RC=$?
    if [ $RC -eq 159 ]; then
        ok "Stage 4: adversary socket → SIGSYS (rc=159)"
    else
        err "Stage 4: adversary socket → rc=$RC (expected 159=SIGSYS)"
        STAGE4_OK=0
    fi

    # uname → SIGSYS (uname(160) NOT in production rv32emu allowlist)
    "$LAUNCHER_R" -- "$ADVERSARY" uname 2>/dev/null
    RC=$?
    if [ $RC -eq 159 ]; then
        ok "Stage 4: adversary uname → SIGSYS (rc=159) — blocked by production filter"
    else
        err "Stage 4: adversary uname → rc=$RC (expected 159=SIGSYS)"
        STAGE4_OK=0
    fi

    # rv32emu end-to-end: run cart_a through launcher_r for 5 seconds
    # This is the primary Stage 4 gate: rv32emu must run without SIGSYS.
    echo "  Stage 4: launching rv32emu with cart_a under launcher_r (5s)..."
    timeout 5 "$LAUNCHER_R" -- "$RV32EMU_BIN" -L "$LIBDIR" "$CARTDIR/cart_a" \
        >/tmp/stage4-cart-output.txt 2>/tmp/stage4-cart-stderr.txt
    RV32_RC=$?
    CART_OUT=$(cat /tmp/stage4-cart-output.txt 2>/dev/null)
    echo "  rv32emu cart_a exit: rc=$RV32_RC"
    [ -s /tmp/stage4-cart-output.txt ] && echo "  cart output: $CART_OUT"
    [ -s /tmp/stage4-cart-stderr.txt ] && cat /tmp/stage4-cart-stderr.txt | tail -5

    # timeout(1) exits 124 on timeout; rv32emu exits 0 on clean cart exit
    # Either 0 (clean exit) or 124 (timed out — still running, no SIGSYS) is OK
    if [ $RV32_RC -eq 0 ] || [ $RV32_RC -eq 124 ]; then
        ok "Stage 4: rv32emu cart_a ran under launcher_r (rc=$RV32_RC — no SIGSYS)"
    elif [ $RV32_RC -eq 159 ]; then
        err "Stage 4: rv32emu cart_a got SIGSYS (rc=159) — seccomp filter too tight!"
        STAGE4_OK=0
    else
        err "Stage 4: rv32emu cart_a unexpected rc=$RV32_RC"
        STAGE4_OK=0
    fi

    if [ $STAGE4_OK -eq 1 ]; then
        ok "Stage 4: adversary probes correct + rv32emu cart runs under production filter"
    else
        err "Stage 4: one or more checks failed"
    fi
fi

# ── Summary ───────────────────────────────────────────────────────────────────
echo ""
echo "=========================================="
echo "Spike R Stage 3+4 complete"
printf "PASS: %-3d  FAIL: %d\n" "$PASS" "$FAIL"
echo "Stage 3 results: $STRACE_OUT"
echo "  syscall_names.txt    — union of observed names"
echo "  allowlist_suggestion.h — header stub for review"
echo "  strace_*.txt         — raw strace captures"
echo "=========================================="

[ "$FAIL" -eq 0 ] && exit 0 || exit 1
