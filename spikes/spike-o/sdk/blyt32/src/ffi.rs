// Raw extern "C" declarations.  Only pub(crate) — cart code uses the safe
// wrappers in lib.rs, not these declarations directly.
//
// All handle types use u32 here; the safe layer converts newtypes via their
// repr(transparent) guarantee.  f32 arguments exercise the ilp32f hard-float
// ABI (Spike O Stage 2 gate).

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
}
