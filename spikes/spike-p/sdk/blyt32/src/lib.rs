#![no_std]

pub mod allocator;
pub mod handles;
pub mod buffers;
pub mod errors;
pub(crate) mod ffi;

pub use handles::{ResourceHandle, ImageHandle, VoiceHandle, HandlerHandle, Slot};
pub use buffers::{Buffer, FieldHandle, MainBuffer, EnemiesBuffer};
pub use errors::BlytError;

// ── Image API ────────────────────────────────────────────────────────────────

#[inline(always)]
pub fn image_load(resource: ResourceHandle) -> Option<ImageHandle> {
    let h = unsafe { ffi::blyt32_image_load(resource.0) };
    if h == u32::MAX { None } else { Some(ImageHandle(h)) }
}

impl ImageHandle {
    #[inline(always)]
    pub fn blit(self, x: i32, y: i32, flags: u32) {
        unsafe { ffi::blyt32_image_blit(self.0, x, y, flags) }
    }
}

// ── Audio API ────────────────────────────────────────────────────────────────

#[inline(always)]
pub fn audio_sfx_play(resource: ResourceHandle) -> Option<VoiceHandle> {
    let h = unsafe { ffi::blyt32_audio_sfx_play(resource.0) };
    if h == u32::MAX { None } else { Some(VoiceHandle(h)) }
}

impl VoiceHandle {
    #[inline(always)]
    pub fn set_volume(self, vol: f32) {
        unsafe { ffi::blyt32_audio_sfx_set_volume(self.0, vol) }
    }
}

// ── Handler registration ─────────────────────────────────────────────────────

#[inline(always)]
pub fn register_handler(handle: HandlerHandle, handler: fn(u32) -> u32) {
    let _ = (handle, handler);
}

// ── Dev / instrumentation APIs ───────────────────────────────────────────────

/// Save current state to the configured save directory.  No-op if no save
/// directory has been configured (i.e. /tmp/spike-p/ does not exist or is
/// not writable).  `frame` is the caller's current frame index; it is
/// embedded in the save file so the load run can resume at the same frame.
#[inline(always)]
pub fn blyt_test_save_now(frame: u32) {
    unsafe { ffi::blyt_test_save_now(frame) }
}

/// Emit a per-frame digest line: "DIGEST <frame> <hi><lo>\n".
/// The cart computes FNV-1a-64 over its inputs and splits the 64-bit hash
/// into hi (upper 32 bits) and lo (lower 32 bits).
#[inline(always)]
pub fn blyt_emit_digest(frame: u32, hi: u32, lo: u32) {
    unsafe { ffi::blyt_emit_digest(frame, hi, lo) }
}
