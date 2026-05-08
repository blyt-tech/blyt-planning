/* Spike O — C reference cart for the Stage 3 digest gate.
 *
 * Calls the same five stub functions in the same order with the same
 * arguments as the Rust toy cart's fc_cart_init / fc_cart_update pair.
 * The per-frame digest stream must be byte-for-byte identical to the
 * Rust cart's stream on both amd64 and arm64.
 *
 * A mismatch means the Rust SDK wrapper changed the observable behaviour
 * of an API call — wrong argument, wrong call order, or a type coercion
 * that changed a value.
 *
 * Linked against libconsole_spike_o.so under -march=rv32imfc_zicsr -mabi=ilp32f.
 */

#include <stdint.h>

/* ── Stub API declarations (matches libconsole_stub_ext.c) ─────────────────── */

extern uint32_t blyt32_image_load(uint32_t resource);
extern void     blyt32_image_blit(uint32_t image, int32_t x, int32_t y, uint32_t flags);
extern uint32_t blyt32_audio_sfx_play(uint32_t resource);
extern void     blyt32_audio_sfx_set_volume(uint32_t voice, float vol);
extern uint32_t blyt32_state_get_u32(uint32_t slot, uint32_t field);
extern void     blyt32_state_set_u32(uint32_t slot, uint32_t field, uint32_t val);
extern void     frame_state_emit_digest_simple(uint32_t frame, uint32_t val);

/* ── Constant values — must match the Rust cart's hardcoded constants ──────── */
/* R_HERO = ResourceHandle(1), V_SFX = ResourceHandle(2),
 * S_CTR  = FieldHandle<MainBuffer>::new(0x00010001) */
#define R_HERO  1u
#define V_SFX   2u
#define S_CTR   0x00010001u
#define SLOT_0  0u

static uint32_t g_frame = 0;

/* fc_cart_init: load image, play SFX + set volume, read-increment-write state. */
void fc_cart_init(void) {
    uint32_t img   = blyt32_image_load(R_HERO);
    blyt32_image_blit(img, 10, 20, 0);

    uint32_t voice = blyt32_audio_sfx_play(V_SFX);
    blyt32_audio_sfx_set_volume(voice, 0.5f);

    uint32_t v = blyt32_state_get_u32(SLOT_0, S_CTR);
    blyt32_state_set_u32(SLOT_0, S_CTR, v + 1u);
}

/* fc_cart_update: increment state, emit digest — mirrors Rust full cart. */
void fc_cart_update(void) {
    uint32_t v = blyt32_state_get_u32(SLOT_0, S_CTR);
    blyt32_state_set_u32(SLOT_0, S_CTR, v + 1u);

    uint32_t ctr = blyt32_state_get_u32(SLOT_0, S_CTR);
    frame_state_emit_digest_simple(g_frame, ctr);
    g_frame++;
}

void fc_cart_draw(void) {}
