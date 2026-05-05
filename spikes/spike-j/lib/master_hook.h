/* Spike J — runtime-owned master hook dispatcher.
 *
 * Exactly one lua_sethook consumer per lua_State. The dispatcher fans out to
 * whichever combination of budget / throttle / DAP handlers is enabled for
 * the current build. Production code MUST NOT call lua_sethook directly —
 * always go through fc_consolelua_master_hook_install().
 */

#ifndef SPIKE_J_MASTER_HOOK_H
#define SPIKE_J_MASTER_HOOK_H

#include <stdint.h>
#include <stdbool.h>

#include "lua.h"

/* Hook-fire counts. The dispatcher uses MASKCOUNT count for the budget hook;
 * MASKLINE / MASKCALL / MASKRET fire on every line / call / return regardless
 * of the count. Both can be active simultaneously — the dispatcher reads
 * ar->event to route. */
#ifndef MASTER_HOOK_DEFAULT_COUNT
#define MASTER_HOOK_DEFAULT_COUNT 1000
#endif

/* DAP step modes — values returned from the DAP server through the command
 * queue. The dispatcher uses these to decide whether to pause on the next
 * line / call / return event. */
typedef enum {
    DAP_STEP_NONE = 0,
    DAP_STEP_OVER,    /* next: pause on next line at <= current frame depth */
    DAP_STEP_IN,      /* stepIn: pause on next line at any depth */
    DAP_STEP_OUT,     /* stepOut: pause on next line at < current frame depth */
    DAP_STEP_PAUSE,   /* pause requested — stop on next event of any kind */
} dap_step_mode_t;

typedef struct {
    /* Composition flags — set at install time, immutable until the next
     * fc_consolelua_master_hook_install() call. */
    bool      budget_enabled;     /* Spike G — LUA_MASKCOUNT path */
    bool      throttle_enabled;   /* Spike G.3 — LUA_MASKLINE path */
    bool      dap_enabled;        /* Spike J — line/call/return as DAP needs */

    /* Spike G state */
    uint64_t  budget_ns;
    uint64_t  budget_tic_start_ns;

    /* Spike G.3 state */
    uint64_t  throttle_start_ns;
    uint64_t  throttle_target_ns;
    uint64_t  throttle_delay_ns;   /* per-line debt, calibrated per bench */

    /* Spike J state — the DAP server populates this through the command
     * queue; the master hook reads it on every fire. */
    dap_step_mode_t dap_step_mode;
    int             dap_step_base_depth;  /* call-stack depth when step started */
    int             dap_pending_pause;    /* 1 = client requested pause */
    /* Breakpoint table — opaque pointer, owned by libconsolelua_dap.c. The
     * dispatcher calls dap_should_break(L, ar) to consult it. */
    void           *dap_state;
} hook_config_t;

extern hook_config_t fc_master_hook_cfg;

/* Per-tic rearm — called from fc_console_main / cart_init / cart_update /
 * cart_draw to reset the budget timer. No-op if budget_enabled is false. */
void fc_consolelua_master_hook_rearm(void);

/* Recompute the dispatch mask from cfg.* and (re)install the master hook.
 * The only lua_sethook call in the runtime. */
void fc_consolelua_master_hook_install(lua_State *L);

/* The dispatcher itself — exposed for testing. Production code only ever sees
 * it via fc_consolelua_master_hook_install. */
void fc_consolelua_master_hook(lua_State *L, lua_Debug *ar);

/* DAP-side hooks the dispatcher calls. These are weak — if libconsolelua_dap.c
 * is not linked, the dispatcher's dap_dispatch path returns immediately. */
__attribute__((weak)) bool fc_dap_should_break(lua_State *L, lua_Debug *ar);
__attribute__((weak)) void fc_dap_pause_loop(lua_State *L, lua_Debug *ar);

#endif /* SPIKE_J_MASTER_HOOK_H */
