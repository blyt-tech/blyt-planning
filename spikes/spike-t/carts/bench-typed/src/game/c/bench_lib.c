/* Spike T (f): typed fast-path benchmark — one host→guest call, no bridge ops. */

#include <blyt.h>

BLYT_LUA_MODULE_EXPORT_I32(spike, bench, int32_t x) {
    return x * 2 + 1;
}
