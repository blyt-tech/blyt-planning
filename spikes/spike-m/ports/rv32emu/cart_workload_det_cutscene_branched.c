/* Spike M — per-cart workload shim for det_cutscene_branched (Stage 2). */

#include "embed/blyt32_coroutine.h"
#include "embed/det_cutscene_branched.h"

const unsigned char *cart_wrapper_lua;
unsigned int         cart_wrapper_lua_len;
const unsigned char *cart_workload_lua;
unsigned int         cart_workload_lua_len;
const char          *cart_workload_name = "det_cutscene_branched";

void cart_workload_init(void)
{
    cart_wrapper_lua      = blyt32_coroutine_lua;
    cart_wrapper_lua_len  = blyt32_coroutine_lua_len;
    cart_workload_lua     = det_cutscene_branched_lua;
    cart_workload_lua_len = det_cutscene_branched_lua_len;
}
