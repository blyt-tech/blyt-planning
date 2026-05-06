/* Spike N — hot-reload diagnostic string formatter and emitter.
 *
 * See hot_reload_diagnostic.h for the format spec and API.
 * No stdlib — uses extern printf idiom (same as all spike freestanding C).
 *
 * The diagnostic is printed to stdout prefixed with "STDERR " so the
 * harness can extract it via `grep '^STDERR '`.  This mirrors Spike M's
 * transient-coroutine error surface; rv32emu's emulated environment does
 * not guarantee separate stderr capture, so the STDERR-prefix convention
 * is the portable cross-host gate. */

#include <stdint.h>
#include <stddef.h>

#include "hot_reload_diagnostic.h"

extern int printf(const char *, ...);

/* Registered hook — NULL until cart installs one. */
static hot_reload_hook_fn_t g_hook;

void hot_reload_set_hook(hot_reload_hook_fn_t fn)
{
    g_hook = fn;
}

/* Minimal snprintf-like integer formatter into a char buffer.
 * Writes dec representation of v into buf+*pos, advances *pos.
 * Returns 0 if buf_cap exceeded. */
static int emit_uint(char *buf, int buf_cap, int *pos, unsigned long v)
{
    char tmp[24];
    int  n = 0;
    if (v == 0) { tmp[n++] = '0'; }
    else {
        unsigned long x = v;
        while (x) { tmp[n++] = (char)('0' + (x % 10)); x /= 10; }
        /* reverse */
        for (int i = 0, j = n-1; i < j; i++, j--) {
            char c = tmp[i]; tmp[i] = tmp[j]; tmp[j] = c;
        }
    }
    for (int i = 0; i < n; i++) {
        if (*pos >= buf_cap - 1) return 0;
        buf[(*pos)++] = tmp[i];
    }
    return 1;
}

static int emit_str(char *buf, int buf_cap, int *pos, const char *s)
{
    while (s && *s) {
        if (*pos >= buf_cap - 1) return 0;
        buf[(*pos)++] = *s++;
    }
    return 1;
}

int hot_reload_diagnostic_format(char *buf, int buf_cap,
                                  int slot, const char *func_name,
                                  int old_line, hot_reload_reason_t reason)
{
    int pos = 0;

    emit_str(buf, buf_cap, &pos, "hot_reload: failed to migrate slot ");
    emit_uint(buf, buf_cap, &pos, (unsigned long)(unsigned)slot);
    emit_str(buf, buf_cap, &pos, " (persistent script)\n");

    if (old_line > 0) {
        emit_str(buf, buf_cap, &pos, "  body: cart.lua:");
        emit_uint(buf, buf_cap, &pos, (unsigned long)(unsigned)old_line);
        emit_str(buf, buf_cap, &pos, " (old) -> cart.lua:??? (new)\n");
    }

    emit_str(buf, buf_cap, &pos, "  reason: ");
    switch (reason) {
    case HRDG_FUNCTION_NOT_FOUND:
        emit_str(buf, buf_cap, &pos, "function '");
        emit_str(buf, buf_cap, &pos, func_name ? func_name : "(unknown)");
        emit_str(buf, buf_cap, &pos, "' not found in new code\n");
        break;
    case HRDG_FUNCTION_DELETED:
        emit_str(buf, buf_cap, &pos, "function '");
        emit_str(buf, buf_cap, &pos, func_name ? func_name : "(unknown)");
        emit_str(buf, buf_cap, &pos, "' was deleted from new code\n");
        break;
    case HRDG_RETYPE_REJECTED:
        emit_str(buf, buf_cap, &pos, "field retype rejected by on_retype callback\n");
        break;
    case HRDG_SCHEMA_MISMATCH:
        emit_str(buf, buf_cap, &pos, "region layout changed but no migration descriptor registered\n");
        break;
    case HRDG_SAVE_FLATTEN_FAILED:
        emit_str(buf, buf_cap, &pos, "pre-reload ctx flatten failed (BLYT_ERR_FLATTEN_UNSUPPORTED_TYPE)\n");
        break;
    }

    emit_str(buf, buf_cap, &pos, "  surfaced via: blyt32.on_hot_reload_failed(slot=");
    emit_uint(buf, buf_cap, &pos, (unsigned long)(unsigned)slot);
    emit_str(buf, buf_cap, &pos, ", reason=...)\n");

    if (pos < buf_cap) buf[pos] = '\0';
    return pos;
}

void hot_reload_diagnostic_emit(int slot, const char *func_name,
                                  int old_line, hot_reload_reason_t reason)
{
    static char diag[HOT_RELOAD_DIAG_MAX];
    hot_reload_diagnostic_format(diag, HOT_RELOAD_DIAG_MAX,
                                   slot, func_name, old_line, reason);
    /* Emit with STDERR prefix to stdout — the harness greps for ^STDERR .
     * This mirrors Spike M's STDERR-prefix convention for the rv32emu
     * environment where separate stderr capture is not guaranteed. */
    printf("STDERR %s", diag);

    /* Fire the Lua hook if registered. */
    if (g_hook) {
        g_hook(slot, diag);
    }
}
