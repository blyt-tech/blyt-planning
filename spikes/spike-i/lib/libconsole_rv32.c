#include <stdint.h>

/* Process exit — used by Lua's panic handler and abort() */
void _exit(int code) {
    register long a0 __asm__("a0") = code;
    register long a7 __asm__("a7") = 93;
    __asm__ volatile ("ecall" : "+r"(a0) : "r"(a7) : "memory");
    __builtin_unreachable();
}
void exit(int code)  { _exit(code); }
void abort(void)     { _exit(1); }

static unsigned strlen_local(const char *s) {
    unsigned n = 0;
    while (s[n]) n++;
    return n;
}

void fc_console_print(const char *s) {
    unsigned len = strlen_local(s);
    register long a0 __asm__("a0") = 1;
    register const char *a1 __asm__("a1") = s;
    register unsigned a2 __asm__("a2") = len;
    register long a7 __asm__("a7") = 64;
    __asm__ volatile ("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
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
