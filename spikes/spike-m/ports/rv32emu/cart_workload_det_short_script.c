/* Spike M — per-cart workload shim for det_short_script (Stage 3). */

#include "embed/blyt32_coroutine.h"
#include "embed/det_short_script.h"

const unsigned char *cart_wrapper_lua;
unsigned int         cart_wrapper_lua_len;
const unsigned char *cart_workload_lua;
unsigned int         cart_workload_lua_len;
const char          *cart_workload_name = "det_short_script";

void cart_workload_init(void)
{
    cart_wrapper_lua      = blyt32_coroutine_lua;
    cart_wrapper_lua_len  = blyt32_coroutine_lua_len;
    cart_workload_lua     = det_short_script_lua;
    cart_workload_lua_len = det_short_script_lua_len;
}
