#![no_std]

pub mod handles;
pub mod buffers;
pub mod errors;
pub(crate) mod ffi;

pub use handles::{ResourceHandle, ImageHandle, VoiceHandle, HandlerHandle, Slot};
pub use buffers::{Buffer, FieldHandle, MainBuffer, EnemiesBuffer};
pub use errors::BlytError;

// ── Image API ────────────────────────────────────────────────────────────────

/// Load an image resource.  Returns an ImageHandle on success.
/// Tier 1: returns None if the resource is not found (no allocator needed).
#[inline(always)]
pub fn image_load(resource: ResourceHandle) -> Option<ImageHandle> {
    let h = unsafe { ffi::blyt32_image_load(resource.0) };
    if h == u32::MAX { None } else { Some(ImageHandle(h)) }
}

impl ImageHandle {
    /// Blit image to screen at (x, y) with flags.  Tier 2: no Result.
    #[inline(always)]
    pub fn blit(self, x: i32, y: i32, flags: u32) {
        unsafe { ffi::blyt32_image_blit(self.0, x, y, flags) }
    }
}

// ── Audio API ────────────────────────────────────────────────────────────────

/// Play a one-shot SFX resource.  Returns a VoiceHandle.
/// Tier 1: returns None if no voice slot is available.
#[inline(always)]
pub fn audio_sfx_play(resource: ResourceHandle) -> Option<VoiceHandle> {
    let h = unsafe { ffi::blyt32_audio_sfx_play(resource.0) };
    if h == u32::MAX { None } else { Some(VoiceHandle(h)) }
}

impl VoiceHandle {
    /// Set voice volume [0.0, 1.0].  Tier 2: no Result.
    /// The f32 argument crosses the extern "C" boundary in an FPR under
    /// ilp32f ABI — this is the float ABI witness for Stage 2.
    #[inline(always)]
    pub fn set_volume(self, vol: f32) {
        unsafe { ffi::blyt32_audio_sfx_set_volume(self.0, vol) }
    }
}

// ── Handler registration ─────────────────────────────────────────────────────

/// Register an AI handler.  The handler parameter is `fn(u32) -> u32` (a
/// plain function pointer, NOT a closure) so the compiler rejects capturing
/// closures at the call site — the guard_closure_handler test validates this.
#[inline(always)]
pub fn register_handler(handle: HandlerHandle, handler: fn(u32) -> u32) {
    // Stub: in production this would register with the runtime scheduler.
    let _ = (handle, handler);
}
