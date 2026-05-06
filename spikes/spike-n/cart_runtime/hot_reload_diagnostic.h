/* Spike N — hot-reload clean-failure diagnostic API.
 *
 * When an edit invalidates a live coroutine (cases l6, l9, l10 in the
 * Lua edit suite), the runtime surfaces a diagnostic naming the offender
 * rather than silently corrupting or crashing.
 *
 * Proposed format for ADR-0083 / ADR-0045 (§"Failure is a first-class
 * outcome"):
 *
 *   hot_reload: failed to migrate slot <slot> (persistent script)
 *     body: cart.lua:<line> (old) → cart.lua:??? (new)
 *     reason: function '<name>' not found in new code
 *     surfaced via: blyt32.on_hot_reload_failed(slot=<slot>, reason=...)
 *
 * The format is fixed so the harness can compare stderr byte-for-byte
 * across hosts.  A platform-specific format (e.g. different line endings,
 * locale-sensitive error strings) would break the cross-host gate.
 */

#ifndef CART_RUNTIME_HOT_RELOAD_DIAGNOSTIC_H
#define CART_RUNTIME_HOT_RELOAD_DIAGNOSTIC_H

#include <stdint.h>

/* Maximum length of the formatted diagnostic string (including NUL). */
#define HOT_RELOAD_DIAG_MAX 512

/* Failure reason codes — the diagnostic formatter selects the message. */
typedef enum {
    HRDG_FUNCTION_NOT_FOUND   = 0,  /* function name missing in new code */
    HRDG_FUNCTION_DELETED     = 1,  /* function explicitly deleted (vs renamed) */
    HRDG_RETYPE_REJECTED      = 2,  /* on_retype returned 0 for a required field */
    HRDG_SCHEMA_MISMATCH      = 3,  /* region layout changed but no descriptor */
    HRDG_SAVE_FLATTEN_FAILED  = 4,  /* pre-reload flatten of ctx failed */
} hot_reload_reason_t;

/* Format a diagnostic string into buf (max buf_cap bytes including NUL).
 * slot:     the persistent-script slot index that could not migrate.
 * func_name: the function name (for FUNCTION_NOT_FOUND / DELETED).
 * old_line:  source line in the pre-edit cart (0 if unknown).
 * reason:   selects the message body.
 * Returns the number of characters written (not counting NUL). */
int hot_reload_diagnostic_format(char *buf, int buf_cap,
                                  int slot, const char *func_name,
                                  int old_line, hot_reload_reason_t reason);

/* Print the formatted diagnostic to stderr.
 * Also fires the blyt32.on_hot_reload_failed hook if it is registered
 * (see blyt32_hot_reload.lua). */
void hot_reload_diagnostic_emit(int slot, const char *func_name,
                                  int old_line, hot_reload_reason_t reason);

/* Register the on_hot_reload_failed hook function pointer.  The Lua
 * cart calls blyt32.on_hot_reload_failed = function(slot, reason) ...
 * which routes through the C binding in lua_det_bindings_n.c; that
 * binding calls this to install the hook. */
typedef void (*hot_reload_hook_fn_t)(int slot, const char *reason);

void hot_reload_set_hook(hot_reload_hook_fn_t fn);

#endif /* CART_RUNTIME_HOT_RELOAD_DIAGNOSTIC_H */
