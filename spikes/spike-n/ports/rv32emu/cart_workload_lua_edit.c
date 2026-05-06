/* Spike N Stages 3-5 — Lua edit cart workload shim.
 *
 * Compiled with -DLUA_EDIT_NUM=N -DLUA_EDIT_SIDE=PRE/POST to embed the
 * appropriate Lua source for each edit's save (PRE) or load (POST) ELF.
 *
 * Edit mapping:
 *   l1 PRE  → det_lua_cart_pre.lua
 *   l1 POST → det_lua_cart_l1_post.lua
 *   l2 PRE  → det_lua_cart_l1_post.lua   (l2 is applied on top of l1's state)
 *   l2 POST → det_lua_cart_l2_post.lua
 *   ... and so on for l3..l5
 *   l6..l10 use the coroutine variants
 *
 * The coroutine variants are handled by cart_workload_lua_coroutine_edit.c
 * (compiled separately for l6-l10 / l7-l10 scenarios).
 */

#include "embed/blyt32_coroutine.h"

#ifndef LUA_EDIT_NUM
#  error "LUA_EDIT_NUM must be defined (1..5)"
#endif

/* Select PRE vs POST source. */
#ifndef LUA_EDIT_SIDE_POST
#  define LUA_EDIT_SIDE_POST 0
#endif

#if LUA_EDIT_NUM == 1
#  if LUA_EDIT_SIDE_POST
#    include "embed/det_lua_cart_l1_post.h"
#    define WORKLOAD_NAME "det_lua_cart_l1_post"
#    define WORKLOAD_SRC  det_lua_cart_l1_post_lua
#    define WORKLOAD_LEN  det_lua_cart_l1_post_lua_len
#  else
#    include "embed/det_lua_cart_pre.h"
#    define WORKLOAD_NAME "det_lua_cart_pre"
#    define WORKLOAD_SRC  det_lua_cart_pre_lua
#    define WORKLOAD_LEN  det_lua_cart_pre_lua_len
#  endif

#elif LUA_EDIT_NUM == 2
#  if LUA_EDIT_SIDE_POST
#    include "embed/det_lua_cart_l2_post.h"
#    define WORKLOAD_NAME "det_lua_cart_l2_post"
#    define WORKLOAD_SRC  det_lua_cart_l2_post_lua
#    define WORKLOAD_LEN  det_lua_cart_l2_post_lua_len
#  else
#    include "embed/det_lua_cart_l1_post.h"
#    define WORKLOAD_NAME "det_lua_cart_l1_post"
#    define WORKLOAD_SRC  det_lua_cart_l1_post_lua
#    define WORKLOAD_LEN  det_lua_cart_l1_post_lua_len
#  endif

#elif LUA_EDIT_NUM == 3
#  if LUA_EDIT_SIDE_POST
#    include "embed/det_lua_cart_l3_post.h"
#    define WORKLOAD_NAME "det_lua_cart_l3_post"
#    define WORKLOAD_SRC  det_lua_cart_l3_post_lua
#    define WORKLOAD_LEN  det_lua_cart_l3_post_lua_len
#  else
#    include "embed/det_lua_cart_l2_post.h"
#    define WORKLOAD_NAME "det_lua_cart_l2_post"
#    define WORKLOAD_SRC  det_lua_cart_l2_post_lua
#    define WORKLOAD_LEN  det_lua_cart_l2_post_lua_len
#  endif

#elif LUA_EDIT_NUM == 4
#  if LUA_EDIT_SIDE_POST
#    include "embed/det_lua_cart_l4_post.h"
#    define WORKLOAD_NAME "det_lua_cart_l4_post"
#    define WORKLOAD_SRC  det_lua_cart_l4_post_lua
#    define WORKLOAD_LEN  det_lua_cart_l4_post_lua_len
#  else
#    include "embed/det_lua_cart_l3_post.h"
#    define WORKLOAD_NAME "det_lua_cart_l3_post"
#    define WORKLOAD_SRC  det_lua_cart_l3_post_lua
#    define WORKLOAD_LEN  det_lua_cart_l3_post_lua_len
#  endif

#elif LUA_EDIT_NUM == 5
#  if LUA_EDIT_SIDE_POST
#    include "embed/det_lua_cart_l5_post.h"
#    define WORKLOAD_NAME "det_lua_cart_l5_post"
#    define WORKLOAD_SRC  det_lua_cart_l5_post_lua
#    define WORKLOAD_LEN  det_lua_cart_l5_post_lua_len
#  else
#    include "embed/det_lua_cart_l4_post.h"
#    define WORKLOAD_NAME "det_lua_cart_l4_post"
#    define WORKLOAD_SRC  det_lua_cart_l4_post_lua
#    define WORKLOAD_LEN  det_lua_cart_l4_post_lua_len
#  endif

#else
#  error "LUA_EDIT_NUM must be 1..5 for this shim"
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
