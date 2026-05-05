/* Spike J — DAP server (host-side).
 *
 * Runs on a host pthread spawned at fc_consolelua_dap_listen() time. Reads
 * Debug Adapter Protocol messages on a TCP socket, communicates with the
 * master hook via a mutex-protected command/response queue.
 *
 * Production wiring (rv32emu): this code lives in the rv32emu host process;
 * libconsolelua.so's master_hook calls fc_dap_should_break() / fc_dap_pause_loop()
 * which consult the queue via custom ECALLs. For Stage 2's protocol-seam
 * validation, this code is linked directly with a Lua-direct host harness
 * — same address space, no rv32emu, identical protocol.
 */

#ifndef SPIKE_J_DAP_SERVER_H
#define SPIKE_J_DAP_SERVER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Spawn the DAP server pthread and start listening on 127.0.0.1:port.
 * Returns 0 on success, -1 on listen / bind failure. Non-blocking — the
 * caller continues straight into the cart loop. */
int  fc_consolelua_dap_listen(int port);

/* Stop the server, close the client connection, join the pthread.
 * Idempotent. */
void fc_consolelua_dap_shutdown(void);

/* Hook entry points consulted by master_hook.c via weak references. The
 * dispatcher calls fc_dap_should_break() on every LUA_HOOKLINE event when
 * a client is attached; fc_dap_pause_loop() when a pause-condition fires. */
struct lua_State;
struct lua_Debug;
bool fc_dap_should_break(struct lua_State *L, struct lua_Debug *ar);
void fc_dap_pause_loop(struct lua_State *L, struct lua_Debug *ar);

/* Synthetic-reload entry — invoked from the cart-side libconsolelua_reload.c
 * after the new lua_State is fully prepared. Emits loadedSource(reason:
 * "changed") to the client. Sequencing matters — see the plan's risk notes
 * on event ordering vs lua_close. */
void fc_dap_emit_loaded_source(const char *source_path);

/* True once the DAP client has sent configurationDone. The host harness
 * polls this with --wait to delay starting the cart until breakpoints are
 * installed. */
int  fc_dap_configuration_done(void);

#ifdef __cplusplus
}
#endif

#endif /* SPIKE_J_DAP_SERVER_H */
