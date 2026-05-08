#![no_std]
#![no_main]

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    // Emit a diagnostic byte via ECALL 64 (write syscall), then loop.
    // In production this would call fc_console_die; for the spike a raw
    // ECALL is sufficient and avoids a PLT dependency in Stage 1.
    unsafe {
        core::arch::asm!(
            "li a0, 1",     // fd = stdout
            "la a1, 2f",    // buf = "!"
            "li a2, 1",     // len = 1
            "li a7, 64",    // SYS_write
            "ecall",
            "2:",
            ".byte 33",     // '!'
        );
    }
    loop {}
}

// ── Stage 1: minimal stub (no SDK, loader smoke test) ────────────────────────
#[cfg(feature = "stage1")]
mod stage1 {
    // Emit "INIT\n" via SYS_write (ECALL 64) to confirm fc_cart_init ran.
    // Uses a static byte string so the address is in rodata — no stack tricks.
    static SENTINEL: &[u8] = b"INIT\n";

    #[no_mangle]
    pub extern "C" fn fc_cart_init() {
        let buf = SENTINEL;
        unsafe {
            core::arch::asm!(
                "ecall",
                in("a0") 1usize,        // fd = stdout
                in("a1") buf.as_ptr(),  // buf pointer
                in("a2") buf.len(),     // count
                in("a7") 64usize,       // SYS_write
                lateout("a0") _,
                options(nostack),
            );
        }
    }
    #[no_mangle]
    pub extern "C" fn fc_cart_update() {}
    #[no_mangle]
    pub extern "C" fn fc_cart_draw() {}
}

// ── Stage 2: raw extern "C" float ABI witness ────────────────────────────────
#[cfg(feature = "stage2")]
mod stage2 {
    extern "C" {
        fn blyt32_audio_sfx_set_volume(voice: u32, vol: f32);
    }

    #[no_mangle]
    pub extern "C" fn fc_cart_init() {
        // Pass 0.5f32 across the extern "C" boundary.  Under ilp32f ABI this
        // lands in fa0 (FPR); under soft-float ilp32 it would land in a1.
        // The stub emits the raw IEEE 754 bits; the Makefile checks for
        // 3f000000 (IEEE 754 representation of 0.5).
        unsafe { blyt32_audio_sfx_set_volume(0, 0.5_f32) }
    }
    #[no_mangle]
    pub extern "C" fn fc_cart_update() {}
    #[no_mangle]
    pub extern "C" fn fc_cart_draw() {}
}

// ── Stage 3/4/5: full toy cart with SDK ─────────────────────────────────────
#[cfg(not(any(feature = "stage1", feature = "stage2")))]
mod full {
    use blyt32::{ResourceHandle, Slot};
    use blyt32::buffers::{MainBuffer, FieldHandle};

    // Stage 3: hardcoded constants (same values packer will generate in stage 4).
    // Stage 4 replaces these with include!(concat!(env!("OUT_DIR"), "/...")).
    const R_HERO: ResourceHandle = ResourceHandle(1);
    const V_SFX: ResourceHandle  = ResourceHandle(2);
    const S_CTR: FieldHandle<MainBuffer> = FieldHandle::new(0x00010001);

    extern "C" {
        // The FNV-1a-64 digest emitter from Spike D's cart_runtime.
        // Called via PLT into libconsole_spike_o.so at runtime.
        fn frame_state_emit_digest_simple(frame: u32, val: u32);
    }

    static mut FRAME: u32 = 0;

    #[no_mangle]
    pub extern "C" fn fc_cart_init() {
        // Image: load and blit hero sprite.
        if let Some(img) = blyt32::image_load(R_HERO) {
            img.blit(10, 20, 0);
        }
        // Audio: play SFX and set volume (f32 ABI witness).
        if let Some(voice) = blyt32::audio_sfx_play(V_SFX) {
            voice.set_volume(0.5_f32);
        }
        // State buffer: read–increment–write.
        let slot = Slot(0);
        let v = MainBuffer.get_u32(slot, S_CTR);
        MainBuffer.set_u32(slot, S_CTR, v.wrapping_add(1));
    }

    #[no_mangle]
    pub extern "C" fn fc_cart_update() {
        let slot = Slot(0);
        let v = MainBuffer.get_u32(slot, S_CTR);
        MainBuffer.set_u32(slot, S_CTR, v.wrapping_add(1));

        // Emit per-frame digest using the simple emitter (no full frame_state_t
        // required — just a frame index and the current S_CTR value).
        let frame = unsafe { FRAME };
        let ctr = MainBuffer.get_u32(slot, S_CTR);
        unsafe { frame_state_emit_digest_simple(frame, ctr) };
        unsafe { FRAME = frame.wrapping_add(1) };
    }

    #[no_mangle]
    pub extern "C" fn fc_cart_draw() {}
}
