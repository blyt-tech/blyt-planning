/* Spike M — constrained-shape Lua-table flattener implementation.
 *
 * Determinism strategy:
 *   • Walk the table with `lua_next` to collect string keys; reject
 *     non-string-keyed pairs at the top level (arrays at top-level
 *     would be ambiguous with the constrained-shape rule).
 *   • Sort keys lexicographically by raw bytes (memcmp tie-break by
 *     length).  Insertion-ordered iteration via `lua_next` is
 *     implementation-defined; the sort is the cross-host gate.
 *   • Emit each field by (key, tag, value-bytes).  Numbers split by
 *     `lua_isinteger` so that an integer value always serializes as
 *     i64 and a float always as f64 — round-trip preserves subtype.
 *   • NaN-canonicalize f64 to a single quiet-NaN pattern at write time.
 *
 * Memory: the spike's flattener works in fixed stack arrays
 * (LTF_MAX_FIELDS = 32 keys at LTF_MAX_KEY_LEN = 64 bytes apiece);
 * larger tables fail with LTF_ERR_OVERFLOW.  No heap allocation.
 */

#include <stdint.h>
#include <stddef.h>

#include "lua.h"
#include "lauxlib.h"

#include "lua_table_flatten.h"

/* IEEE 754 double-precision quiet NaN, payload zero. */
#define CANONICAL_QNAN64_BITS 0x7ff8000000000000ULL

static inline uint64_t canonicalize_nan64(double x)
{
    union { double d; uint64_t u; } v = { .d = x };
    uint64_t exp_bits = (v.u >> 52) & 0x7ffULL;
    uint64_t frac     = v.u & 0xfffffffffffffULL;
    if (exp_bits == 0x7ff && frac != 0) {
        return CANONICAL_QNAN64_BITS;
    }
    return v.u;
}

/* ── byte-sink helpers ───────────────────────────────────────────────────── */

typedef struct {
    uint8_t *dst;
    uint32_t cap;
    uint32_t pos;
    int      overflowed;
} sink_t;

static void put_u8(sink_t *s, uint8_t v)
{
    if (s->pos + 1 > s->cap) { s->overflowed = 1; return; }
    s->dst[s->pos++] = v;
}

static void put_u16(sink_t *s, uint16_t v)
{
    put_u8(s, (uint8_t)(v & 0xff));
    put_u8(s, (uint8_t)((v >> 8) & 0xff));
}

static void put_u32(sink_t *s, uint32_t v)
{
    put_u8(s, (uint8_t)(v & 0xff));
    put_u8(s, (uint8_t)((v >> 8) & 0xff));
    put_u8(s, (uint8_t)((v >> 16) & 0xff));
    put_u8(s, (uint8_t)((v >> 24) & 0xff));
}

static void put_u64(sink_t *s, uint64_t v)
{
    put_u32(s, (uint32_t)(v & 0xffffffffull));
    put_u32(s, (uint32_t)((v >> 32) & 0xffffffffull));
}

static void put_bytes(sink_t *s, const uint8_t *src, uint32_t n)
{
    if (s->pos + n > s->cap) { s->overflowed = 1; return; }
    for (uint32_t i = 0; i < n; i++) s->dst[s->pos++] = src[i];
}

/* ── byte-source helpers ─────────────────────────────────────────────────── */

typedef struct {
    const uint8_t *src;
    uint32_t       len;
    uint32_t       pos;
    int            underflowed;
} source_t;

static uint8_t take_u8(source_t *s)
{
    if (s->pos + 1 > s->len) { s->underflowed = 1; return 0; }
    return s->src[s->pos++];
}

static uint16_t take_u16(source_t *s)
{
    uint16_t a = take_u8(s);
    uint16_t b = take_u8(s);
    return (uint16_t)(a | (b << 8));
}

static uint32_t take_u32(source_t *s)
{
    uint32_t a = take_u8(s);
    uint32_t b = take_u8(s);
    uint32_t c = take_u8(s);
    uint32_t d = take_u8(s);
    return a | (b << 8) | (c << 16) | (d << 24);
}

static uint64_t take_u64(source_t *s)
{
    uint64_t lo = take_u32(s);
    uint64_t hi = take_u32(s);
    return lo | (hi << 32);
}

/* ── key collection + sort ───────────────────────────────────────────────── */

typedef struct {
    uint8_t  bytes[LTF_MAX_KEY_LEN];
    uint16_t len;
    int      stack_idx;   /* index in saved-keys table — see below */
} key_entry_t;

/* Compare two key_entry_t — lexicographic on bytes, length tiebreak. */
static int key_cmp(const key_entry_t *a, const key_entry_t *b)
{
    uint16_t n = a->len < b->len ? a->len : b->len;
    for (uint16_t i = 0; i < n; i++) {
        if (a->bytes[i] != b->bytes[i])
            return (int)a->bytes[i] - (int)b->bytes[i];
    }
    if (a->len != b->len) return (int)a->len - (int)b->len;
    return 0;
}

/* Insertion sort — N <= 32, and stable / simple is plenty. */
static void key_sort(key_entry_t *arr, int n)
{
    for (int i = 1; i < n; i++) {
        key_entry_t cur = arr[i];
        int j = i - 1;
        while (j >= 0 && key_cmp(&arr[j], &cur) > 0) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = cur;
    }
}

/* ── value emission ──────────────────────────────────────────────────────── */

/* Emit a single value at stack index `vi`.  Returns 0 on success or a
 * negative LTF_ERR_* on failure.  Pushes nothing extra to the stack. */
static int emit_value(lua_State *L, int vi, sink_t *out);

/* Detect a flat-array (sequence) shape: integer keys 1..N, no holes,
 * all values share the same primitive subtype (boolean/integer/float/
 * string).  Returns the element tag and length in *out_tag / *out_n,
 * or a negative LTF_ERR_* on failure. */
static int detect_array(lua_State *L, int ti, uint8_t *out_tag, uint32_t *out_n)
{
    /* Walk integer-keyed entries 1..N as long as `lua_rawgeti` returns
     * a non-nil value.  Determine each value's tag; require all tags
     * equal.  If any non-integer key exists we treat the table as a
     * record, not an array — but `detect_array` is called only when
     * the caller has already concluded the value should be flattened
     * as an array (e.g. it's a value-position table). */
    ti = lua_absindex(L, ti);
    uint32_t n = 0;
    uint8_t  tag = 0;
    lua_Integer i = 1;
    for (;;) {
        int t = lua_rawgeti(L, ti, i);
        if (t == LUA_TNIL) { lua_pop(L, 1); break; }
        uint8_t this_tag;
        switch (t) {
            case LUA_TBOOLEAN: this_tag = LTF_TAG_BOOLEAN; break;
            case LUA_TNUMBER:  this_tag = lua_isinteger(L, -1) ? LTF_TAG_INTEGER : LTF_TAG_FLOAT; break;
            case LUA_TSTRING:  this_tag = LTF_TAG_STRING;  break;
            default:           lua_pop(L, 1); return LTF_ERR_UNSUPPORTED_TYPE;
        }
        if (n == 0) tag = this_tag;
        else if (this_tag != tag) { lua_pop(L, 1); return LTF_ERR_ARRAY_MIXED_TYPES; }
        lua_pop(L, 1);
        n++;
        i++;
        if (n > 0xfffffffeu) return LTF_ERR_OVERFLOW;
    }
    /* Sanity check: total field count via `lua_next` should equal n —
     * otherwise the table has non-integer keys mixed with the sequence,
     * which we reject as a non-pure-sequence array. */
    uint32_t total = 0;
    lua_pushnil(L);
    while (lua_next(L, ti) != 0) {
        total++;
        lua_pop(L, 1);
    }
    if (total != n) return LTF_ERR_ARRAY_NON_SEQUENCE;

    *out_tag = tag;
    *out_n   = n;
    return 0;
}

static int emit_array(lua_State *L, int ti, sink_t *out)
{
    ti = lua_absindex(L, ti);
    uint8_t  elem_tag = 0;
    uint32_t elem_n   = 0;
    int rc = detect_array(L, ti, &elem_tag, &elem_n);
    if (rc < 0) return rc;
    put_u8(out, LTF_TAG_ARRAY);
    put_u8(out, elem_tag);
    put_u32(out, elem_n);
    for (uint32_t i = 0; i < elem_n; i++) {
        lua_rawgeti(L, ti, (lua_Integer)(i + 1));
        switch (elem_tag) {
            case LTF_TAG_BOOLEAN:
                put_u8(out, lua_toboolean(L, -1) ? 1u : 0u);
                break;
            case LTF_TAG_INTEGER: {
                lua_Integer v = lua_tointeger(L, -1);
                put_u64(out, (uint64_t)v);
                break;
            }
            case LTF_TAG_FLOAT: {
                lua_Number v = lua_tonumber(L, -1);
                put_u64(out, canonicalize_nan64((double)v));
                break;
            }
            case LTF_TAG_STRING: {
                size_t sl = 0;
                const char *sb = lua_tolstring(L, -1, &sl);
                put_u32(out, (uint32_t)sl);
                put_bytes(out, (const uint8_t *)sb, (uint32_t)sl);
                break;
            }
        }
        lua_pop(L, 1);
    }
    return 0;
}

static int emit_value(lua_State *L, int vi, sink_t *out)
{
    int t = lua_type(L, vi);
    switch (t) {
        case LUA_TBOOLEAN:
            put_u8(out, LTF_TAG_BOOLEAN);
            put_u8(out, lua_toboolean(L, vi) ? 1u : 0u);
            return 0;
        case LUA_TNUMBER:
            if (lua_isinteger(L, vi)) {
                put_u8(out, LTF_TAG_INTEGER);
                lua_Integer v = lua_tointeger(L, vi);
                put_u64(out, (uint64_t)v);
            } else {
                put_u8(out, LTF_TAG_FLOAT);
                lua_Number v = lua_tonumber(L, vi);
                put_u64(out, canonicalize_nan64((double)v));
            }
            return 0;
        case LUA_TSTRING: {
            size_t sl = 0;
            const char *sb = lua_tolstring(L, vi, &sl);
            put_u8(out, LTF_TAG_STRING);
            put_u32(out, (uint32_t)sl);
            put_bytes(out, (const uint8_t *)sb, (uint32_t)sl);
            return 0;
        }
        case LUA_TTABLE: {
            /* Top-level fields can be flat arrays only — recursive
             * tables are out of scope per the constrained shape. */
            return emit_array(L, vi, out);
        }
        default:
            return LTF_ERR_UNSUPPORTED_TYPE;
    }
}

/* ── public flatten ──────────────────────────────────────────────────────── */

int lua_table_flatten(lua_State *L, int idx, uint8_t *dst, uint32_t cap, uint32_t *out_len)
{
    if (out_len) *out_len = 0;

    int abs_idx = lua_absindex(L, idx);
    if (lua_type(L, abs_idx) != LUA_TTABLE) return LTF_ERR_UNSUPPORTED_TYPE;

    /* Phase 1 — collect string keys.  Use lua_next; reject non-string
     * keys at the top level. */
    key_entry_t keys[LTF_MAX_FIELDS];
    int nkeys = 0;

    lua_pushnil(L);
    while (lua_next(L, abs_idx) != 0) {
        /* key at -2, value at -1.  Don't pop key — lua_next needs it. */
        if (lua_type(L, -2) != LUA_TSTRING) {
            lua_pop(L, 2);
            return LTF_ERR_NON_STRING_KEY;
        }
        if (nkeys >= LTF_MAX_FIELDS) { lua_pop(L, 2); return LTF_ERR_OVERFLOW; }
        size_t kl = 0;
        const char *kb = lua_tolstring(L, -2, &kl);
        if (kl > LTF_MAX_KEY_LEN) { lua_pop(L, 2); return LTF_ERR_KEY_TOO_LONG; }
        keys[nkeys].len = (uint16_t)kl;
        for (size_t i = 0; i < kl; i++) keys[nkeys].bytes[i] = (uint8_t)kb[i];
        nkeys++;
        lua_pop(L, 1);  /* pop value; keep key for next iteration */
    }

    /* Phase 2 — sort keys lexicographically. */
    key_sort(keys, nkeys);

    /* Phase 3 — emit. */
    sink_t out = { .dst = dst, .cap = cap, .pos = 0, .overflowed = 0 };
    put_u32(&out, (uint32_t)nkeys);
    for (int i = 0; i < nkeys; i++) {
        put_u16(&out, keys[i].len);
        put_bytes(&out, keys[i].bytes, keys[i].len);
        /* Re-fetch the value by key — the sorted order may not match
         * the original iteration order. */
        lua_pushlstring(L, (const char *)keys[i].bytes, keys[i].len);
        lua_rawget(L, abs_idx);
        int rc = emit_value(L, -1, &out);
        lua_pop(L, 1);
        if (rc < 0) return rc;
        if (out.overflowed) return LTF_ERR_OVERFLOW;
    }

    if (out.overflowed) return LTF_ERR_OVERFLOW;
    if (out_len) *out_len = out.pos;
    return LTF_OK;
}

/* ── public unflatten ────────────────────────────────────────────────────── */

static int unflatten_value(lua_State *L, source_t *src);

static int unflatten_array(lua_State *L, source_t *src)
{
    uint8_t  elem_tag = take_u8(src);
    uint32_t n        = take_u32(src);
    if (src->underflowed) return LTF_ERR_OVERFLOW;
    lua_createtable(L, (int)(n > 0xfffffu ? 0xfffffu : n), 0);
    for (uint32_t i = 0; i < n; i++) {
        switch (elem_tag) {
            case LTF_TAG_BOOLEAN:
                lua_pushboolean(L, take_u8(src) ? 1 : 0);
                break;
            case LTF_TAG_INTEGER: {
                int64_t v = (int64_t)take_u64(src);
                lua_pushinteger(L, (lua_Integer)v);
                break;
            }
            case LTF_TAG_FLOAT: {
                union { double d; uint64_t u; } v;
                v.u = take_u64(src);
                lua_pushnumber(L, (lua_Number)v.d);
                break;
            }
            case LTF_TAG_STRING: {
                uint32_t sl = take_u32(src);
                if (src->pos + sl > src->len) { src->underflowed = 1; lua_pop(L, 1); return LTF_ERR_OVERFLOW; }
                lua_pushlstring(L, (const char *)(src->src + src->pos), (size_t)sl);
                src->pos += sl;
                break;
            }
            default:
                lua_pop(L, 1);
                return LTF_ERR_UNSUPPORTED_TYPE;
        }
        if (src->underflowed) { lua_pop(L, 2); return LTF_ERR_OVERFLOW; }
        lua_rawseti(L, -2, (lua_Integer)(i + 1));
    }
    return 0;
}

static int unflatten_value(lua_State *L, source_t *src)
{
    uint8_t tag = take_u8(src);
    if (src->underflowed) return LTF_ERR_OVERFLOW;
    switch (tag) {
        case LTF_TAG_BOOLEAN:
            lua_pushboolean(L, take_u8(src) ? 1 : 0);
            return 0;
        case LTF_TAG_INTEGER: {
            int64_t v = (int64_t)take_u64(src);
            lua_pushinteger(L, (lua_Integer)v);
            return 0;
        }
        case LTF_TAG_FLOAT: {
            union { double d; uint64_t u; } v;
            v.u = take_u64(src);
            lua_pushnumber(L, (lua_Number)v.d);
            return 0;
        }
        case LTF_TAG_STRING: {
            uint32_t sl = take_u32(src);
            if (src->pos + sl > src->len) { src->underflowed = 1; return LTF_ERR_OVERFLOW; }
            lua_pushlstring(L, (const char *)(src->src + src->pos), (size_t)sl);
            src->pos += sl;
            return 0;
        }
        case LTF_TAG_ARRAY:
            return unflatten_array(L, src);
        default:
            return LTF_ERR_UNSUPPORTED_TYPE;
    }
}

int lua_table_unflatten(lua_State *L, const uint8_t *src, uint32_t n)
{
    source_t s = { .src = src, .len = n, .pos = 0, .underflowed = 0 };
    uint32_t nkeys = take_u32(&s);
    if (s.underflowed) { lua_pushnil(L); return LTF_ERR_OVERFLOW; }
    if (nkeys > LTF_MAX_FIELDS) { lua_pushnil(L); return LTF_ERR_OVERFLOW; }
    lua_createtable(L, 0, (int)nkeys);
    for (uint32_t i = 0; i < nkeys; i++) {
        uint16_t kl = take_u16(&s);
        if (s.underflowed || kl > LTF_MAX_KEY_LEN) { lua_pop(L, 1); lua_pushnil(L); return LTF_ERR_OVERFLOW; }
        if (s.pos + kl > s.len) { lua_pop(L, 1); lua_pushnil(L); return LTF_ERR_OVERFLOW; }
        lua_pushlstring(L, (const char *)(s.src + s.pos), (size_t)kl);
        s.pos += kl;
        int rc = unflatten_value(L, &s);
        if (rc < 0) { lua_pop(L, 2); /* table + key */ lua_pushnil(L); return rc; }
        lua_rawset(L, -3);
    }
    if (s.underflowed) { lua_pop(L, 1); lua_pushnil(L); return LTF_ERR_OVERFLOW; }
    return LTF_OK;
}

const char *lua_table_flatten_strerror(int err)
{
    switch (err) {
        case LTF_OK:                       return "ok";
        case LTF_ERR_OVERFLOW:             return "BLYT_ERR_FLATTEN_OVERFLOW";
        case LTF_ERR_UNSUPPORTED_TYPE:     return "BLYT_ERR_FLATTEN_UNSUPPORTED_TYPE";
        case LTF_ERR_NON_STRING_KEY:       return "BLYT_ERR_FLATTEN_NON_STRING_KEY";
        case LTF_ERR_KEY_TOO_LONG:         return "BLYT_ERR_FLATTEN_KEY_TOO_LONG";
        case LTF_ERR_ARRAY_MIXED_TYPES:    return "BLYT_ERR_FLATTEN_ARRAY_MIXED_TYPES";
        case LTF_ERR_ARRAY_NON_SEQUENCE:   return "BLYT_ERR_FLATTEN_ARRAY_NON_SEQUENCE";
        default:                           return "BLYT_ERR_FLATTEN_UNKNOWN";
    }
}
