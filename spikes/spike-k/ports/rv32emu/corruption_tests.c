/* Spike K — Stage 6 corruption-detection negative tests.
 *
 * Verifies the safety gates in save_state_load() fire on the failure
 * modes that would otherwise corrupt the runtime silently.  Each test
 * is constructed in-process (no host-side shell trickery): we save a
 * buffer, mutate it, and assert save_state_load() rejects it.
 *
 * The output is greppable PASS/FAIL lines that the harness checks.
 *
 * Tests:
 *   1. magic-byte mutation         → layout-hash and magic gates reject
 *   2. version mismatch            → version gate rejects
 *   3. layout_hash mutation        → layout-hash gate rejects
 *   4. truncation                  → bounded-read gate rejects
 *   5. total_size > size           → size-bound gate rejects
 *   6. unmodified buffer           → load succeeds (positive control)
 */

#include <stdint.h>
#include <stddef.h>

#include "../../cart_runtime/frame_state.h"
#include "../../cart_runtime/region_frame_state.h"
#include "../../cart_runtime/cart_state_whetstone.h"
#include "../../cart_runtime/save_state.h"

extern int printf(const char *, ...);

static uint8_t g_buf[8192];
static uint32_t g_size;

/* Construct a known-good buffer once.  Each test mutates a copy. */
static void make_buffer(void)
{
    /* Zero the regions to give a deterministic source — the buffer
     * captures whatever the regions hold; for negative-test purposes the
     * actual values don't matter, only the structure. */
    frame_state_t          *fs = region_frame_state_get();
    cart_state_whetstone_t *cs = cart_state_whetstone_get();
    {
        unsigned char *p = (unsigned char *)fs;
        for (size_t i = 0; i < sizeof(*fs); i++) p[i] = 0;
    }
    fs->frame     = 7;
    fs->rng_state = 0x1234567890abcdefULL;
    fs->rng_inc   = 0xfeedfacecafef00dULL;
    cs->a = 1.0f; cs->b = -1.0f; cs->c = -1.0f; cs->d = -1.0f;
    cs->e1[0] = 1.0f; cs->e1[1] = -1.0f; cs->e1[2] = -1.0f; cs->e1[3] = -1.0f;
    cs->t = 0.5f; cs->u = 0.75f;

    save_state_init();
    g_size = save_state_save(g_buf, sizeof(g_buf), 7);
}

static void copy_buf(uint8_t *dst)
{
    for (uint32_t i = 0; i < g_size; i++) dst[i] = g_buf[i];
}

#define TEST_FAIL_REJECTED(name) do {                                         \
    if (save_state_load(scratch, g_size)) {                                   \
        printf("CORRUPTION FAIL — %s accepted\n", name); fails++;             \
    } else {                                                                  \
        printf("CORRUPTION PASS — %s rejected\n", name);                      \
    }                                                                         \
} while (0)

int main(void)
{
    make_buffer();
    int fails = 0;

    static uint8_t scratch[8192];

    /* 1. magic-byte mutation */
    copy_buf(scratch);
    scratch[0] ^= 0xff;
    TEST_FAIL_REJECTED("magic-byte mutation");

    /* 2. version mismatch (bump version field at offset 4) */
    copy_buf(scratch);
    scratch[4] = 0x99;
    TEST_FAIL_REJECTED("version mismatch");

    /* 3. layout_hash mutation (8 bytes at offset 8) */
    copy_buf(scratch);
    scratch[8] ^= 0x01;
    TEST_FAIL_REJECTED("layout_hash mutation");

    /* 4. truncation: pass total_size - 8 to the load (drop the last 8 bytes) */
    copy_buf(scratch);
    if (save_state_load(scratch, g_size - 8)) {
        printf("CORRUPTION FAIL — truncation accepted\n"); fails++;
    } else {
        printf("CORRUPTION PASS — truncation rejected\n");
    }

    /* 5. total_size in header larger than the buffer we hand to load */
    copy_buf(scratch);
    /* total_size lives at byte offset 20: magic(4)+version(4)+layout_hash(8)+frame(4) */
    {
        uint32_t big = g_size + 1024;
        scratch[20] = (uint8_t)(big      );
        scratch[21] = (uint8_t)(big >>  8);
        scratch[22] = (uint8_t)(big >> 16);
        scratch[23] = (uint8_t)(big >> 24);
    }
    TEST_FAIL_REJECTED("total_size > buffer");

    /* 6. positive control: unmodified buffer must load */
    copy_buf(scratch);
    if (!save_state_load(scratch, g_size)) {
        printf("CORRUPTION FAIL — clean buffer rejected\n"); fails++;
    } else {
        printf("CORRUPTION PASS — clean buffer accepted\n");
    }

    if (fails > 0) {
        printf("CORRUPTION SUMMARY %d failures\n", fails);
        return 1;
    }
    printf("CORRUPTION SUMMARY all %s\n", "passed");
    return 0;
}
