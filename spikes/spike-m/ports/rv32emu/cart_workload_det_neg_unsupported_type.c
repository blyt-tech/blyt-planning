/* Spike M Stage 6 negative test — unsupported-type shim. */

#include "embed/blyt32_coroutine.h"
#include "embed/det_neg_unsupported_type.h"

const unsigned char *cart_wrapper_lua;
unsigned int         cart_wrapper_lua_len;
const unsigned char *cart_workload_lua;
unsigned int         cart_workload_lua_len;
const char          *cart_workload_name = "det_neg_unsupported_type";

void cart_workload_init(void)
{
    cart_wrapper_lua      = blyt32_coroutine_lua;
    cart_wrapper_lua_len  = blyt32_coroutine_lua_len;
    cart_workload_lua     = det_neg_unsupported_type_lua;
    cart_workload_lua_len = det_neg_unsupported_type_lua_len;
}
