// Raw extern "C" declarations for spike-p's stub library.

extern "C" {
    // Image family
    pub(crate) fn blyt32_image_load(resource: u32) -> u32;
    pub(crate) fn blyt32_image_blit(image: u32, x: i32, y: i32, flags: u32);

    // Audio family
    pub(crate) fn blyt32_audio_sfx_play(resource: u32) -> u32;
    pub(crate) fn blyt32_audio_sfx_set_volume(voice: u32, vol: f32);

    // State buffer family
    pub(crate) fn blyt32_state_get_u32(slot: u32, field: u32) -> u32;
    pub(crate) fn blyt32_state_set_u32(slot: u32, field: u32, val: u32);

    // Dev / instrumentation (ADR-0085 range 900–999)
    pub(crate) fn blyt_test_save_now(frame: u32);
    pub(crate) fn blyt_emit_digest(frame: u32, hi: u32, lo: u32);
}
