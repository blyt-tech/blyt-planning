/* Spike N Stage 1 — Lua no-op cart workload shim.
 *
 * Mirrors the spike-m pattern: embed blyt32_coroutine.lua and the
 * workload .lua into the ELF via xxd-generated headers, then expose
 * them via the fixed names that the generic m_driver_{save,load,full}.c
 * expect.
 */

#include "embed/blyt32_coroutine.h"
#include "embed/det_noop_lua.h"

const unsigned char *cart_wrapper_lua;
unsigned int         cart_wrapper_lua_len;
const unsigned char *cart_workload_lua;
unsigned int         cart_workload_lua_len;
const char          *cart_workload_name = "det_noop_lua";

void cart_workload_init(void)
{
    cart_wrapper_lua      = blyt32_coroutine_lua;
    cart_wrapper_lua_len  = blyt32_coroutine_lua_len;
    cart_workload_lua     = det_noop_lua_lua;
    cart_workload_lua_len = det_noop_lua_lua_len;
}
