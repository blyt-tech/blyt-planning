/* Spike J — Lua-direct host harness for the DAP composition validation.
 *
 * Single host process embedding Lua 5.4 directly. The same master_hook.c
 * source compiled into libconsolelua.so is linked here. The DAP server runs
 * on a host pthread sharing this process's address space — same protocol
 * shape as the rv32emu integration; the only difference is IPC mechanism
 * (direct memory access here vs. ECALLs into rv32emu in the production path).
 *
 * Usage:
 *   dap_lua_host <cart.luac> [--port 5678] [--wait]
 *     --wait blocks the cart from running until the DAP client connects and
 *     sends configurationDone — used by the test harness to set breakpoints
 *     before the cart loop starts.
 *
 * Frame loop matches spike-i's libconsole_rv32.c: 10 iterations of
 * fc_cart_init / fc_cart_update / fc_cart_draw, then "OK\n".
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include "../lib/master_hook.h"
#include "dap_server.h"

uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static lua_State *s_L = NULL;
static const uint8_t *s_bytecode = NULL;
static uint32_t s_bytecode_size = 0;
static char s_source_path[1024] = "@cart";

static int l_console_print(lua_State *L) {
    const char *s = luaL_checkstring(L, 1);
    fputs(s, stdout);
    fflush(stdout);
    return 0;
}

static void configure_sandbox(lua_State *L) {
    luaL_requiref(L, LUA_GNAME,      luaopen_base,   1); lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table,  1); lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME,luaopen_math,   1); lua_pop(L, 1);
    lua_pushcfunction(L, l_console_print);
    lua_setglobal(L, "console_print");
}

static int run_chunk(lua_State *L, const uint8_t *data, uint32_t size) {
    int rc = luaL_loadbuffer(L, (const char *)data, (size_t)size, s_source_path);
    if (rc != LUA_OK) {
        fprintf(stderr, "loadbuffer: %s\n", lua_tostring(L, -1));
        return 1;
    }
    rc = lua_pcall(L, 0, 0, 0);
    if (rc != LUA_OK) {
        fprintf(stderr, "pcall chunk: %s\n", lua_tostring(L, -1));
        return 1;
    }
    return 0;
}

static void apply_default_config(void) {
    fc_master_hook_cfg.budget_enabled    = 1;
    fc_master_hook_cfg.throttle_enabled  = 0;
    fc_master_hook_cfg.dap_enabled       = 1;
    fc_master_hook_cfg.budget_ns         = 16667000ULL;
    fc_master_hook_cfg.throttle_delay_ns = 0;
}

static int build_state(void) {
    s_L = luaL_newstate();
    if (!s_L) return 1;
    configure_sandbox(s_L);
    apply_default_config();
    fc_consolelua_master_hook_install(s_L);
    if (run_chunk(s_L, s_bytecode, s_bytecode_size) != 0) return 1;
    return 0;
}

void fc_consolelua_synthetic_reload(const uint8_t *new_bytecode, uint32_t new_size) {
    if (s_L) { lua_close(s_L); s_L = NULL; }
    /* Caller-owned bytes; copy so we own it across the lifetime of the state. */
    static uint8_t *owned = NULL;
    static uint32_t owned_size = 0;
    if (owned) { free(owned); owned = NULL; }
    owned = malloc(new_size);
    if (!owned) return;
    memcpy(owned, new_bytecode, new_size);
    owned_size = new_size;
    s_bytecode = owned;
    s_bytecode_size = owned_size;
    build_state();
}

static void rearm_then_call(const char *fn) {
    fc_consolelua_master_hook_rearm();
    lua_getglobal(s_L, fn);
    if (lua_isfunction(s_L, -1)) {
        if (lua_pcall(s_L, 0, 0, 0) != LUA_OK) {
            fprintf(stderr, "%s: %s\n", fn, lua_tostring(s_L, -1));
            lua_pop(s_L, 1);
        }
    } else {
        lua_pop(s_L, 1);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: dap_lua_host <cart.luac> [--port N] [--wait] [--source PATH]\n");
        return 2;
    }
    const char *cart_path = argv[1];
    int port = 5678;
    int wait_for_client = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--wait") == 0) wait_for_client = 1;
        else if (strcmp(argv[i], "--source") == 0 && i + 1 < argc) {
            snprintf(s_source_path, sizeof s_source_path, "@%s", argv[++i]);
        }
    }

    FILE *f = fopen(cart_path, "rb");
    if (!f) { perror(cart_path); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *bytes = malloc((size_t)sz);
    if (!bytes || (long)fread(bytes, 1, (size_t)sz, f) != sz) {
        fprintf(stderr, "read %s: short\n", cart_path);
        return 1;
    }
    fclose(f);
    s_bytecode = bytes;
    s_bytecode_size = (uint32_t)sz;

    if (fc_consolelua_dap_listen(port) != 0) {
        fprintf(stderr, "DAP listen on %d failed\n", port);
        return 1;
    }
    fprintf(stderr, "DAP listening on 127.0.0.1:%d\n", port);

    if (wait_for_client) {
        /* Block until the client has finished its initialize / setBreakpoints
         * exchange and sent configurationDone — gives the harness time to
         * install breakpoints before the cart's first frame. */
        while (!fc_dap_configuration_done()) usleep(10000);
    }

    if (build_state() != 0) return 1;

    /* Same shape as spike-i's fc_console_main. */
    rearm_then_call("init");
    for (int i = 0; i < 10; i++) {
        rearm_then_call("update");
        rearm_then_call("draw");
    }
    fputs("OK\n", stdout);

    fc_consolelua_dap_shutdown();
    return 0;
}
