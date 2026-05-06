/* Spike M — per-cart workload shim for det_cutscene_linear.
 *
 * Each cart compiles ONE shim to expose its specific workload bytes
 * to the generic m_driver_{save,load,full}.c.  The wrapper module
 * (blyt32_coroutine.lua) is the same for every cart — only the
 * workload differs.
 */

#include "embed/blyt32_coroutine.h"
#include "embed/det_cutscene_linear.h"

/* xxd -i emits the byte arrays and length as non-const externs.  Expose
 * them via fixed names that the generic drivers reference; runtime
 * assignment in cart_workload_init() avoids "non-constant initializer"
 * compile errors that appear when the cart_*_len = blyt32_*_len pattern
 * is used at file scope. */
const unsigned char *cart_wrapper_lua;
unsigned int         cart_wrapper_lua_len;
const unsigned char *cart_workload_lua;
unsigned int         cart_workload_lua_len;
const char          *cart_workload_name = "det_cutscene_linear";

void cart_workload_init(void)
{
    cart_wrapper_lua      = blyt32_coroutine_lua;
    cart_wrapper_lua_len  = blyt32_coroutine_lua_len;
    cart_workload_lua     = det_cutscene_linear_lua;
    cart_workload_lua_len = det_cutscene_linear_lua_len;
}
