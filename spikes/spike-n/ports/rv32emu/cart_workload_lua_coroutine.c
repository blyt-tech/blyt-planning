/* Spike N Stages 4-5 — Lua coroutine edit cart workload shim.
 *
 * Handles the coroutine-specific workloads (l6-l10 post, plus the shared
 * pre-save cart det_lua_cart_coroutine_pre).
 *
 * Compiled with:
 *   -DLUA_COROUTINE_PRE=1    → embed det_lua_cart_coroutine_pre.lua (save side)
 *   -DLUA_COROUTINE_EDIT=l6  → embed det_lua_cart_coroutine_l6_post.lua
 *   -DLUA_COROUTINE_EDIT=l7  → embed det_lua_cart_coroutine_l7_post.lua
 *   -DLUA_COROUTINE_EDIT=l8  → embed det_lua_cart_coroutine_l8_post.lua
 *   -DLUA_COROUTINE_EDIT=l9  → embed det_lua_cart_coroutine_l9_post.lua
 *   -DLUA_COROUTINE_EDIT=l10 → embed det_lua_cart_coroutine_l10_post.lua
 *
 * Note: LUA_COROUTINE_EDIT is a string token (e.g. l6) used in the
 * header filename; the preprocessor token-pastes it.
 */

#include "embed/blyt32_coroutine.h"

#if defined(LUA_COROUTINE_PRE)
#  include "embed/det_lua_cart_coroutine_pre.h"
#  define WORKLOAD_NAME "det_lua_cart_coroutine_pre"
#  define WORKLOAD_SRC  det_lua_cart_coroutine_pre_lua
#  define WORKLOAD_LEN  det_lua_cart_coroutine_pre_lua_len

#elif defined(LUA_COROUTINE_EDIT_l6)
#  include "embed/det_lua_cart_coroutine_l6_post.h"
#  define WORKLOAD_NAME "det_lua_cart_coroutine_l6_post"
#  define WORKLOAD_SRC  det_lua_cart_coroutine_l6_post_lua
#  define WORKLOAD_LEN  det_lua_cart_coroutine_l6_post_lua_len

#elif defined(LUA_COROUTINE_EDIT_l7)
#  include "embed/det_lua_cart_coroutine_l7_post.h"
#  define WORKLOAD_NAME "det_lua_cart_coroutine_l7_post"
#  define WORKLOAD_SRC  det_lua_cart_coroutine_l7_post_lua
#  define WORKLOAD_LEN  det_lua_cart_coroutine_l7_post_lua_len

#elif defined(LUA_COROUTINE_EDIT_l8)
#  include "embed/det_lua_cart_coroutine_l8_post.h"
#  define WORKLOAD_NAME "det_lua_cart_coroutine_l8_post"
#  define WORKLOAD_SRC  det_lua_cart_coroutine_l8_post_lua
#  define WORKLOAD_LEN  det_lua_cart_coroutine_l8_post_lua_len

#elif defined(LUA_COROUTINE_EDIT_l9)
#  include "embed/det_lua_cart_coroutine_l9_post.h"
#  define WORKLOAD_NAME "det_lua_cart_coroutine_l9_post"
#  define WORKLOAD_SRC  det_lua_cart_coroutine_l9_post_lua
#  define WORKLOAD_LEN  det_lua_cart_coroutine_l9_post_lua_len

#elif defined(LUA_COROUTINE_EDIT_l10)
#  include "embed/det_lua_cart_coroutine_l10_post.h"
#  define WORKLOAD_NAME "det_lua_cart_coroutine_l10_post"
#  define WORKLOAD_SRC  det_lua_cart_coroutine_l10_post_lua
#  define WORKLOAD_LEN  det_lua_cart_coroutine_l10_post_lua_len

#else
#  error "Must define LUA_COROUTINE_PRE or LUA_COROUTINE_EDIT_l<N>"
#endif

const unsigned char *cart_wrapper_lua;
unsigned int         cart_wrapper_lua_len;
const unsigned char *cart_workload_lua;
unsigned int         cart_workload_lua_len;
const char          *cart_workload_name = WORKLOAD_NAME;

void cart_workload_init(void)
{
    cart_wrapper_lua      = blyt32_coroutine_lua;
    cart_wrapper_lua_len  = blyt32_coroutine_lua_len;
    cart_workload_lua     = WORKLOAD_SRC;
    cart_workload_lua_len = WORKLOAD_LEN;
}
