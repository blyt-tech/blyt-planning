#!/usr/bin/env bash
# Spike H — automated in-guest test script.
# Invoked by cloud-init on first boot inside qemu-system-riscv64.
# The virtfs share (spike-h root) is already mounted at /mnt/spike-h.
#
# Stages exercised here:
#   0 — kernel feature presence (CONFIG_COMPAT, cgroups v2 + cpu controller)
#   1 — RV32 ELF runs natively under CONFIG_COMPAT
#   3 — seccomp-bpf kills each forbidden probe; pivot_root sequence works
#   4 — cgroups v2 cpu.max throttles busy_loop ~10×; Lua workloads survive cgroup

set -uo pipefail

SPIKE_H=/mnt/spike-h
BUILD=$SPIKE_H/build
RESULTS=$BUILD/results
LAUNCHER=$BUILD/launcher   # pre-built RV64 binary by docker-build-launcher

PASS=0
FAIL=0

ok()  { echo "  PASS: $*"; PASS=$(( PASS + 1 )); }
err() { echo "  FAIL: $*" >&2; FAIL=$(( FAIL + 1 )); }
section() { echo ""; echo "=== $* ==="; }

# ── logging ───────────────────────────────────────────────────────────────────
mkdir -p "$RESULTS"
exec > >(tee "$RESULTS/guest-run.log") 2>&1

echo "Spike H — in-guest test run"
echo "Date: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
echo "Kernel: $(uname -r)"
echo "Arch:   $(uname -m)"

# ── Stage 0: kernel features ──────────────────────────────────────────────────
section "Stage 0: kernel features"

KVER=$(uname -r)
KCONFIG=/boot/config-$KVER
if [ -f "$KCONFIG" ] && grep -q "^CONFIG_COMPAT=y" "$KCONFIG"; then
    ok "CONFIG_COMPAT=y (kernel $KVER)"
else
    err "CONFIG_COMPAT not found in $KCONFIG"
    echo "  note: the spike may still partially work if COMPAT is built-in without config record"
fi

if mount | grep -q " type cgroup2"; then
    ok "cgroups v2 mounted"
    CONTROLLERS=$(cat /sys/fs/cgroup/cgroup.controllers 2>/dev/null || echo "(unavailable)")
    echo "  cgroup.controllers: $CONTROLLERS"
    if echo "$CONTROLLERS" | grep -qw "cpu"; then
        ok "cpu controller available"
    else
        err "cpu controller missing from cgroup.controllers"
    fi
else
    err "cgroups v2 not mounted (need systemd or explicit cgroup2 mount)"
fi

# ── Stage 1: RV32 execution under CONFIG_COMPAT ───────────────────────────────
section "Stage 1: RV32 ELF under CONFIG_COMPAT"

HELLO=$BUILD/hello
if [ ! -f "$HELLO" ]; then
    err "hello ELF not found at $HELLO"
else
    OUT=$("$HELLO" 2>/dev/null); RC=$?
    if [ $RC -eq 0 ] && [ "$OUT" = "OK" ]; then
        ok "hello.elf: output='$OUT' rc=$RC"
    else
        err "hello.elf: output='$OUT' rc=$RC (expected 'OK' rc=0)"
    fi

    # ELF header sanity — readelf is in binutils
    if command -v readelf &>/dev/null; then
        echo "  ELF headers:"
        readelf -h "$HELLO" 2>/dev/null \
            | grep -E "Class:|Machine:|Flags:" | sed 's/^/    /'
    fi
fi

# ── Stage 3: seccomp + namespace isolation ────────────────────────────────────
section "Stage 3a: seccomp-bpf (forbidden syscall probes)"

# The launcher is cross-compiled for RV64 by docker-build-launcher and shared
# via virtfs.  No in-guest build step needed.
if [ ! -f "$LAUNCHER" ]; then
    err "launcher not found at $LAUNCHER — run: make docker-build-launcher"
    echo ""; echo "=== SUMMARY ==="
    echo "PASS: $PASS  FAIL: $FAIL"
    exit 1
fi
chmod +x "$LAUNCHER" 2>/dev/null || true
ok "launcher available: $(file "$LAUNCHER" | grep -o 'RISC-V.*' || echo 'RV64 ELF')"

ADVERSARY=$BUILD/adversary
if [ ! -f "$ADVERSARY" ]; then
    err "adversary ELF not found at $ADVERSARY — Stage 3 tests skipped"
else
    STAGE3_SEC_OK=1
    for probe in open socket execve mprotect-exec; do
        "$LAUNCHER" -- "$ADVERSARY" "$probe" 2>/dev/null
        RC=$?
        if [ $RC -eq 159 ]; then
            ok "adversary $probe → SIGSYS (rc=159)"
        else
            err "adversary $probe → rc=$RC (expected 159=SIGSYS — filter leak?)"
            STAGE3_SEC_OK=0
        fi
    done

    # Sanity: allowed uname probe must exit 0
    "$LAUNCHER" -- "$ADVERSARY" uname 2>/dev/null
    RC=$?
    if [ $RC -eq 0 ]; then
        ok "adversary uname → exit 0 (allowed, sanity OK)"
    else
        err "adversary uname → rc=$RC (expected 0 — filter too tight?)"
        STAGE3_SEC_OK=0
    fi

    [ $STAGE3_SEC_OK -eq 1 ] \
        && ok "Stage 3a: all seccomp probes correct" \
        || err "Stage 3a: one or more seccomp probes failed"
fi

section "Stage 3b: pivot_root / mount namespace isolation"

ROOTFS=/tmp/spike-h-rootfs
mkdir -p "$ROOTFS"
cp "$HELLO"    "$ROOTFS/hello"    2>/dev/null || true
cp "$ADVERSARY" "$ROOTFS/adversary" 2>/dev/null || true

# hello.elf inside isolated rootfs — path is /hello relative to new root
"$LAUNCHER" --rootfs "$ROOTFS" --no-seccomp --no-netns -- /hello 2>/dev/null
RC=$?
if [ $RC -eq 0 ]; then
    ok "pivot_root: hello.elf ran inside isolated rootfs (rc=0, output OK)"
else
    err "pivot_root: hello.elf failed inside isolated rootfs (rc=$RC)"
fi

# Filesystem isolation check: with seccomp off, openat(/etc/passwd) should
# return ENOENT (not found in the isolated rootfs), not a valid fd.
if [ -f "$ROOTFS/adversary" ]; then
    # Rebuild rootfs (put_old was created+removed by previous pivot_root run)
    mkdir -p "$ROOTFS"
    cp "$ADVERSARY" "$ROOTFS/adversary" 2>/dev/null || true
    OUT=$("$LAUNCHER" --rootfs "$ROOTFS" --no-seccomp --no-netns -- /adversary open 2>&1)
    RC=$?
    echo "  pivot_root open probe: rc=$RC"
    echo "  $OUT" | grep -E "(returned|probe)" | sed 's/^/  /' || true
    if [ $RC -eq 1 ]; then
        ok "pivot_root: /etc/passwd not accessible inside isolated rootfs (ENOENT)"
    elif [ $RC -eq 0 ]; then
        err "pivot_root: /etc/passwd opened successfully — filesystem NOT isolated"
    else
        # rc=159 means seccomp killed it (--no-seccomp was supposed to skip this)
        err "pivot_root: open probe rc=$RC (unexpected)"
    fi
fi

# ── Stage 4: cgroups v2 cpu.max ───────────────────────────────────────────────
section "Stage 4a: cgroup cpu.max throttle (busy_loop)"

CGROUP=/sys/fs/cgroup/spike-h-test
mkdir -p "$CGROUP"
# Enable cpu controller at the root level
echo "+cpu" | tee /sys/fs/cgroup/cgroup.subtree_control >/dev/null 2>&1 || true

# 10% CPU: 50 ms quota per 500 ms period.
# Linux minimum cfs_quota_us is 1000 µs; the 5000 µs period used in plan §16
# works on bare metal but the 500 µs quota (5000 × 10%) is below the minimum.
# Use larger values to stay above the floor on all kernels.
echo "50000 500000" | tee "$CGROUP/cpu.max" >/dev/null
ACTUAL=$(cat "$CGROUP/cpu.max")
if echo "$ACTUAL" | grep -qE "^50000 500000"; then
    ok "cpu.max set to: $ACTUAL (10% CPU)"
else
    err "cpu.max write failed (got: '$ACTUAL')"
fi

BUSY=$BUILD/busy_loop
if [ ! -f "$BUSY" ]; then
    err "busy_loop ELF not found at $BUSY — Stage 4 test skipped"
else
    echo "  baseline (no cgroup)..."
    BASE_OUT=$("$BUSY" 2>/dev/null)
    echo "  $BASE_OUT"
    BASE_US=$(echo "$BASE_OUT" | grep -o 'elapsed_us=[0-9]*' | cut -d= -f2)

    echo "  throttled (10% CPU via cgroup)..."
    TROT_OUT=$("$LAUNCHER" --cgroup "$CGROUP" --no-seccomp --no-netns --no-mountns -- "$BUSY" 2>/dev/null)
    echo "  $TROT_OUT"
    TROT_US=$(echo "$TROT_OUT" | grep -o 'elapsed_us=[0-9]*' | cut -d= -f2)

    echo "  baseline_us=$BASE_US  throttled_us=$TROT_US"
    if [ -n "$BASE_US" ] && [ -n "$TROT_US" ] && [ "$BASE_US" -gt 0 ]; then
        # Fixed-point: ratio × 10 (avoids floating-point in bash)
        RATIO10=$(( TROT_US * 10 / BASE_US ))
        echo "  throttle ratio: ~$(( RATIO10 / 10 ))× (×10 fp: $RATIO10)"
        if [ "$RATIO10" -ge 50 ]; then
            ok "throttle ≥5× baseline (got ~$(( RATIO10 / 10 ))×, expected ~10×)"
        else
            err "throttle ratio $(( RATIO10 / 10 ))× too low — cgroup not effective?"
        fi
    else
        err "could not parse busy_loop elapsed_us from output"
    fi
fi

section "Stage 4b: Lua workloads under cgroup"

# Relax to 20% CPU for Lua (timing is not the point here — just mechanism)
echo "100000 500000" | tee "$CGROUP/cpu.max" >/dev/null

LUA_DOOM=$BUILD/lua/lua_cart_doom_tick.elf
LUA_ENTITY=$BUILD/lua/lua_cart_entity_update.elf

if [ -f "$LUA_DOOM" ]; then
    echo "  running lua_cart_doom_tick inside cgroup..."
    DOOM_RAW=$("$LAUNCHER" --cgroup "$CGROUP" --no-seccomp --no-netns --no-mountns \
            -- "$LUA_DOOM" 2>/dev/null)
    DOOM_SUM=$(echo "$DOOM_RAW" | grep "^SUMMARY" || true)
    echo "  ${DOOM_SUM:-(no SUMMARY line)}"
    [ -n "$DOOM_SUM" ] \
        && ok "lua_cart_doom_tick completed inside cgroup" \
        || err "lua_cart_doom_tick: no SUMMARY line (crashed or missing)"
else
    err "lua_cart_doom_tick.elf not found at $LUA_DOOM"
fi

if [ -f "$LUA_ENTITY" ]; then
    echo "  running lua_cart_entity_update inside cgroup..."
    ENT_RAW=$("$LAUNCHER" --cgroup "$CGROUP" --no-seccomp --no-netns --no-mountns \
            -- "$LUA_ENTITY" 2>/dev/null)
    ENT_SUM=$(echo "$ENT_RAW" | grep "^SUMMARY" || true)
    echo "  ${ENT_SUM:-(no SUMMARY line)}"
    [ -n "$ENT_SUM" ] \
        && ok "lua_cart_entity_update completed inside cgroup" \
        || err "lua_cart_entity_update: no SUMMARY line (crashed or missing)"
else
    err "lua_cart_entity_update.elf not found at $LUA_ENTITY"
fi

# ── Summary ───────────────────────────────────────────────────────────────────
echo ""
echo "=========================================="
echo "Spike H guest run complete"
printf "PASS: %-3d  FAIL: %d\n" "$PASS" "$FAIL"
echo "Log: $RESULTS/guest-run.log"
echo "=========================================="

[ "$FAIL" -eq 0 ] && exit 0 || exit 1
