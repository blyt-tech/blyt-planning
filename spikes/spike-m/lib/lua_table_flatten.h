/* Spike M — constrained-shape Lua-table flattener.
 *
 * Takes a Lua table conforming to the constrained shape (string keys,
 * value subtypes from a fixed list, no nesting beyond a single flat
 * array) and emits a deterministic byte sequence.  Inverse rebuilds
 * an equivalent table.  Cross-host bit-identical output is the
 * headline property.
 *
 * Wire format (little-endian on every field that is not a single byte):
 *
 *   header:
 *     u32 field_count
 *   per field (in lexicographic key order — qsort by key bytes):
 *     u16 key_len
 *     u8  key_bytes[key_len]
 *     u8  tag
 *     union by tag:
 *       TAG_BOOLEAN (1):       u8 (0 or 1)
 *       TAG_INTEGER (2):       i64
 *       TAG_FLOAT   (3):       u64 (f64 bits, NaN-canonicalized)
 *       TAG_STRING  (4):       u32 len; u8 bytes[len]
 *       TAG_ARRAY   (5):       u8 elem_tag (BOOLEAN/INTEGER/FLOAT/STRING);
 *                              u32 elem_count;
 *                              elem_count × payload (no key, no per-elem
 *                                tag — element tag is fixed for the array)
 *
 * Errors:
 *   FLATTEN_OK                          0
 *   FLATTEN_ERR_OVERFLOW               -1   - dst buffer too small
 *   FLATTEN_ERR_UNSUPPORTED_TYPE       -2   - function/userdata/nested table
 *   FLATTEN_ERR_NON_STRING_KEY         -3   - top-level key is not a string
 *   FLATTEN_ERR_KEY_TOO_LONG           -4   - key exceeds LTF_MAX_KEY_LEN
 *   FLATTEN_ERR_ARRAY_MIXED_TYPES      -5   - flat-array values disagree in tag
 *   FLATTEN_ERR_ARRAY_NON_SEQUENCE     -6   - array has non-1..N integer keys
 *
 * Notes on Lua 5.3+ integer/float subtyping: `lua_isinteger()` distinguishes
 * the two; the wire format pins the choice at flatten time, so an `integer`
 * value always round-trips back to integer (not float), even though both
 * subtypes are `LUA_TNUMBER`.
 */

#ifndef SPIKE_M_LIB_LUA_TABLE_FLATTEN_H
#define SPIKE_M_LIB_LUA_TABLE_FLATTEN_H

#include <stdint.h>
#include "lua.h"

#define LTF_TAG_BOOLEAN 1
#define LTF_TAG_INTEGER 2
#define LTF_TAG_FLOAT   3
#define LTF_TAG_STRING  4
#define LTF_TAG_ARRAY   5

#define LTF_OK                          0
#define LTF_ERR_OVERFLOW               -1
#define LTF_ERR_UNSUPPORTED_TYPE       -2
#define LTF_ERR_NON_STRING_KEY         -3
#define LTF_ERR_KEY_TOO_LONG           -4
#define LTF_ERR_ARRAY_MIXED_TYPES      -5
#define LTF_ERR_ARRAY_NON_SEQUENCE     -6

#define LTF_MAX_KEY_LEN     64
#define LTF_MAX_FIELDS     32

/* Flatten the table at stack index `idx` into `dst[..cap]`.
 * On success returns LTF_OK and writes the byte count to *out_len.
 * On error returns a negative LTF_ERR_* code. */
int lua_table_flatten(lua_State *L, int idx, uint8_t *dst, uint32_t cap, uint32_t *out_len);

/* Reconstruct a Lua table from `src[..n]` and push it on the stack.
 * On success returns LTF_OK; on failure pushes nil and returns a
 * negative LTF_ERR_* code. */
int lua_table_unflatten(lua_State *L, const uint8_t *src, uint32_t n);

/* Map an LTF_ERR_* code to a stable string for error messages /
 * stderr-equality checks across hosts. */
const char *lua_table_flatten_strerror(int err);

#endif /* SPIKE_M_LIB_LUA_TABLE_FLATTEN_H */
