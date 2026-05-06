/* Spike M — per-cart workload shim for det_two_scripts_destroy (Stage 3). */

#include "embed/blyt32_coroutine.h"
#include "embed/det_two_scripts_destroy.h"

const unsigned char *cart_wrapper_lua;
unsigned int         cart_wrapper_lua_len;
const unsigned char *cart_workload_lua;
unsigned int         cart_workload_lua_len;
const char          *cart_workload_name = "det_two_scripts_destroy";

void cart_workload_init(void)
{
    cart_wrapper_lua      = blyt32_coroutine_lua;
    cart_wrapper_lua_len  = blyt32_coroutine_lua_len;
    cart_workload_lua     = det_two_scripts_destroy_lua;
    cart_workload_lua_len = det_two_scripts_destroy_lua_len;
}
