/* Spike M — per-cart workload shim for det_table_shapes (Stage 4). */

#include "embed/blyt32_coroutine.h"
#include "embed/det_table_shapes.h"

const unsigned char *cart_wrapper_lua;
unsigned int         cart_wrapper_lua_len;
const unsigned char *cart_workload_lua;
unsigned int         cart_workload_lua_len;
const char          *cart_workload_name = "det_table_shapes";

void cart_workload_init(void)
{
    cart_wrapper_lua      = blyt32_coroutine_lua;
    cart_wrapper_lua_len  = blyt32_coroutine_lua_len;
    cart_workload_lua     = det_table_shapes_lua;
    cart_workload_lua_len = det_table_shapes_lua_len;
}
