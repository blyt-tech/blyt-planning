/* Spike M — per-cart workload shim for det_scripts_with_entities (Stage 3). */

#include "embed/blyt32_coroutine.h"
#include "embed/det_scripts_with_entities.h"

const unsigned char *cart_wrapper_lua;
unsigned int         cart_wrapper_lua_len;
const unsigned char *cart_workload_lua;
unsigned int         cart_workload_lua_len;
const char          *cart_workload_name = "det_scripts_with_entities";

void cart_workload_init(void)
{
    cart_wrapper_lua      = blyt32_coroutine_lua;
    cart_wrapper_lua_len  = blyt32_coroutine_lua_len;
    cart_workload_lua     = det_scripts_with_entities_lua;
    cart_workload_lua_len = det_scripts_with_entities_lua_len;
}
