#include <stdint.h>

__attribute__((section(".cart.info")))
static const char cart_info_stub[] = "FC32";

__attribute__((section(".cart.config")))
static const char cart_config_stub[] = "CF32";

__attribute__((section(".cart.resources"), visibility("default")))
const uint8_t _cart_lua_bytecode[] = {
#include "cart_lua_bytes.inc"
};

__attribute__((visibility("default")))
const uint32_t _cart_lua_bytecode_size = sizeof(_cart_lua_bytecode);
