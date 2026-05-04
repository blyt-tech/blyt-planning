/* Spike I Stage 5 — Lua-direct WASM variant of libconsole_rv32.c.
 *
 * The function bodies are identical at the source level; only the host
 * primitives (process exit, write to stdout) differ. The RV32 build issues
 * ECALL 93 / ECALL 64; the WASM build calls libc directly (Emscripten's
 * runtime maps stdout-write to the JS environment's print hook).
 *
 * No PLT, no dynamic linking, no weak-undef resolution — everything is
 * statically linked into one WASM module by emcc. The same fc_console_main
 * runtime loop runs.
 */

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

void fc_console_print(const char *s) {
    write(1, s, strlen(s));
}

__attribute__((weak)) extern void fc_cart_init(void);
__attribute__((weak)) extern void fc_cart_update(void);
__attribute__((weak)) extern void fc_cart_draw(void);
__attribute__((weak)) extern const uint8_t  _cart_lua_bytecode[];
__attribute__((weak)) extern const uint32_t _cart_lua_bytecode_size;
__attribute__((weak)) extern void fc_consolelua_set_bytecode(
    const uint8_t *data, uint32_t size);

void fc_console_main(void) {
    if (&_cart_lua_bytecode && fc_consolelua_set_bytecode) {
        fc_consolelua_set_bytecode(_cart_lua_bytecode, _cart_lua_bytecode_size);
    }
    fc_cart_init();
    for (int i = 0; i < 10; i++) {
        fc_cart_update();
        fc_cart_draw();
    }
    fc_console_print("OK\n");
}
