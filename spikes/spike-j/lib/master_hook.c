/* Spike J — master hook dispatcher.
 *
 * Composition target: budget (Spike G) + throttle (Spike G.3) + DAP (this
 * spike) sharing the single lua_sethook slot via a runtime-owned dispatcher.
 *
 * The hot path — every call to fc_consolelua_master_hook — does not touch
 * lua_sethook. The mask is recomputed and re-installed only when the
 * configuration changes (DAP attach/detach; throttle disable from the dev UI).
 */

#include <stdint.h>
#include <stddef.h>

#include "lua.h"
#include "lauxlib.h"

#include "master_hook.h"

/* Single global config. The spike runs one lua_State per runtime; production
 * with multiple states can wire this through lua_getextraspace. */
hook_config_t fc_master_hook_cfg = {0};

/* Toolchain-provided clock. In libconsolelua.so this is a syscall (clock
 * gettime via ecall); the host harness substitutes via -DMASTER_HOOK_HOST_NOW. */
#ifdef MASTER_HOOK_HOST_NOW
extern uint64_t now_ns(void);
static uint64_t hook_now_ns(void) { return now_ns(); }
#else
/* Inline ecall to clock_gettime(CLOCK_MONOTONIC). Returns ns. The runtime
 * driver provides a struct timespec on the stack and reads it back.
 * Syscall 403 = clock_gettime64 — the Y2038-safe number RV32 musl and
 * rv32emu both use. The struct layout for clock_gettime64 has 64-bit
 * tv_sec and 32-bit tv_nsec, but rv32emu's syscall_clock_gettime writes
 * 4-byte tv_sec at offset 0 and 4-byte tv_nsec at offset 8 (matches our
 * sec/nsec layout when sec fits in 32 bits, which it always does for
 * CLOCK_MONOTONIC over any reasonable runtime). */
static uint64_t hook_now_ns(void) {
    struct { uint32_t sec_lo; uint32_t sec_hi; uint32_t nsec; uint32_t pad; }
        ts = {0, 0, 0, 0};
    register long a0 __asm__("a0") = 1; /* CLOCK_MONOTONIC */
    register void *a1 __asm__("a1") = &ts;
    register long a7 __asm__("a7") = 403;
    __asm__ volatile ("ecall" : "+r"(a0) : "r"(a1), "r"(a7) : "memory");
    return (uint64_t)ts.sec_lo * 1000000000ULL + (uint64_t)ts.nsec;
}
#endif

/* ── Spike G — budget body ───────────────────────────────────────────────── */

static void budget_check(lua_State *L, lua_Debug *ar) {
    (void)ar;
    uint64_t now = hook_now_ns();
    if (now - fc_master_hook_cfg.budget_tic_start_ns >
        fc_master_hook_cfg.budget_ns) {
        luaL_error(L, "budget exceeded");
    }
}

/* ── Spike G.3 — throttle body ───────────────────────────────────────────── */

static void throttle_step(lua_State *L, lua_Debug *ar) {
    (void)L; (void)ar;
    if (fc_master_hook_cfg.throttle_start_ns == 0) {
        fc_master_hook_cfg.throttle_start_ns = hook_now_ns();
    }
    fc_master_hook_cfg.throttle_target_ns += fc_master_hook_cfg.throttle_delay_ns;
    uint64_t deadline = fc_master_hook_cfg.throttle_start_ns +
                        fc_master_hook_cfg.throttle_target_ns;
    while (hook_now_ns() < deadline) { }
}

/* ── Spike J — DAP body ──────────────────────────────────────────────────── */

/* Compute current call-stack depth via lua_getstack walk. Used by step-over /
 * step-out to compare against the depth at which the step was initiated. */
static int dap_call_depth(lua_State *L) {
    int depth = 0;
    lua_Debug ar;
    while (lua_getstack(L, depth, &ar)) depth++;
    return depth;
}

static void dap_dispatch(lua_State *L, lua_Debug *ar) {
    if (!fc_master_hook_cfg.dap_enabled) return;

    /* Pause request — stop on next event of any kind. */
    if (fc_master_hook_cfg.dap_pending_pause) {
        if (fc_dap_pause_loop) fc_dap_pause_loop(L, ar);
        return;
    }

    /* Step modes — only on LINE events (Lua-level granularity). */
    if (ar->event == LUA_HOOKLINE && fc_master_hook_cfg.dap_step_mode != DAP_STEP_NONE) {
        int depth = dap_call_depth(L);
        bool should_pause = false;
        switch (fc_master_hook_cfg.dap_step_mode) {
        case DAP_STEP_IN:
            should_pause = true;
            break;
        case DAP_STEP_OVER:
            should_pause = depth <= fc_master_hook_cfg.dap_step_base_depth;
            break;
        case DAP_STEP_OUT:
            should_pause = depth < fc_master_hook_cfg.dap_step_base_depth;
            break;
        default:
            break;
        }
        if (should_pause) {
            fc_master_hook_cfg.dap_step_mode = DAP_STEP_NONE;
            if (fc_dap_pause_loop) fc_dap_pause_loop(L, ar);
            return;
        }
    }

    /* Breakpoint check — line events only. */
    if (ar->event == LUA_HOOKLINE) {
        if (fc_dap_should_break && fc_dap_should_break(L, ar)) {
            if (fc_dap_pause_loop) fc_dap_pause_loop(L, ar);
        }
    }
}

/* ── Dispatcher ─────────────────────────────────────────────────────────── */

void fc_consolelua_master_hook(lua_State *L, lua_Debug *ar) {
    if (fc_master_hook_cfg.budget_enabled && ar->event == LUA_HOOKCOUNT) {
        budget_check(L, ar);
    }
    if (fc_master_hook_cfg.throttle_enabled && ar->event == LUA_HOOKLINE) {
        throttle_step(L, ar);
    }
    if (fc_master_hook_cfg.dap_enabled) {
        dap_dispatch(L, ar);
    }
}

/* ── Install / rearm ────────────────────────────────────────────────────── */

void fc_consolelua_master_hook_rearm(void) {
    if (fc_master_hook_cfg.budget_enabled) {
        fc_master_hook_cfg.budget_tic_start_ns = hook_now_ns();
    }
    if (fc_master_hook_cfg.throttle_enabled) {
        fc_master_hook_cfg.throttle_start_ns  = 0;
        fc_master_hook_cfg.throttle_target_ns = 0;
    }
}

void fc_consolelua_master_hook_install(lua_State *L) {
    int mask = 0;
    int count = 0;
    if (fc_master_hook_cfg.budget_enabled) {
        mask |= LUA_MASKCOUNT;
        count = MASTER_HOOK_DEFAULT_COUNT;
    }
    if (fc_master_hook_cfg.throttle_enabled) {
        mask |= LUA_MASKLINE;
    }
    if (fc_master_hook_cfg.dap_enabled) {
        /* DAP needs LINE for step-over / step-in / breakpoints, CALL/RET for
         * accurate call-stack depth tracking under step-out. */
        mask |= LUA_MASKLINE | LUA_MASKCALL | LUA_MASKRET;
    }

    if (mask == 0) {
        lua_sethook(L, NULL, 0, 0);
    } else {
        lua_sethook(L, fc_consolelua_master_hook, mask, count);
    }
}
