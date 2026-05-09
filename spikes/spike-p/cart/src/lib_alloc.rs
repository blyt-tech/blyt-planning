// Stage 3/4 — Vec + Arc alloc cart with on_load two-category demonstration.
//
// State buffer layout (slot 0):
//   S_CTR       field 0x0001  — frame counter, incremented each update
//   S_ARC_INNER field 0x0002  — Arc's inner AtomicU32 value, tracked in state
//                               so on_load can restore the Arc to the right value
//
// Heap data categories (ADR-0087):
//   Resource-derived: RESOURCE_LOOKUP — set in init (or on_load for fresh process).
//                     Content is a constant; survives same-process rewind unchanged.
//   State-derived:    STATE_VEC — rebuilt in on_load from S_CTR.
//                     ARC_COUNTER — rebuilt in on_load from S_ARC_INNER.
//                     GROW_VEC — rebuilt in on_load by replaying the push history.
//
// Digest design (Stage 3 and Stage 4 save-run vs load-run comparison gate):
//   The digest covers: s_counter, resource_lookup_len, resource_lookup[0],
//                      arc_inner, gv_len, gv_sum.
//   STATE_VEC is intentionally EXCLUDED from the main digest because it
//   legitimately differs between the save-run and load-run: in the save-run,
//   STATE_VEC = [0;1] (set by init at S_CTR=0); in the load-run, STATE_VEC =
//   [5;6] (rebuilt by on_load at S_CTR=5).  This difference is the two-category
//   demonstration.  A separate "STATE_VEC" output line is emitted for human
//   verification that on_load rebuilt the correct value.
//
// Save trigger: blyt_test_save_now(s_ctr) when s_ctr == SAVE_AT_S_CTR.
// Save is taken BEFORE the S_CTR increment so the load run re-runs the same
// frame identically, producing matching digests from SAVE_AT_S_CTR onwards.

extern crate alloc;
use alloc::sync::Arc;
use alloc::vec::Vec;
use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicU32, Ordering};

use blyt32::{Slot, blyt_emit_digest, blyt_test_save_now};
use blyt32::buffers::{MainBuffer, FieldHandle};

const SLOT: Slot = Slot(0);
const S_CTR:       FieldHandle<MainBuffer> = FieldHandle::new(0x0001);
const S_ARC_INNER: FieldHandle<MainBuffer> = FieldHandle::new(0x0002);

struct SyncCell<T>(UnsafeCell<T>);
// SAFETY: single-threaded cart execution; no concurrent access.
unsafe impl<T> Sync for SyncCell<T> {}

static RESOURCE_LOOKUP: SyncCell<Option<Vec<u32>>> = SyncCell(UnsafeCell::new(None));
static STATE_VEC:       SyncCell<Option<Vec<u32>>> = SyncCell(UnsafeCell::new(None));
static ARC_COUNTER:     SyncCell<Option<Arc<AtomicU32>>> = SyncCell(UnsafeCell::new(None));
static GROW_VEC:        SyncCell<Option<Vec<u32>>> = SyncCell(UnsafeCell::new(None));

const SAVE_AT_S_CTR: u32 = 5;
const NFRAMES: u32 = 30;

fn fnv1a64(data: &[u32]) -> (u32, u32) {
    let mut h: u64 = 0xcbf29ce484222325;
    let prime: u64 = 0x00000100000001b3;
    for &v in data {
        for i in 0..4u32 {
            h ^= ((v >> (i * 8)) & 0xff) as u64;
            h = h.wrapping_mul(prime);
        }
    }
    ((h >> 32) as u32, h as u32)
}

static STATE_VEC_LABEL: &[u8] = b"STATE_VEC ";
static NEWLINE: &[u8] = b"\n";

fn emit_uint(v: u32) {
    let mut buf = [0u8; 10];
    let mut i = 9usize;
    let mut n = v;
    if n == 0 { buf[i] = b'0'; } else { while n > 0 { buf[i] = b'0' + (n % 10) as u8; n /= 10; i -= 1; } i += 1; }
    unsafe {
        core::arch::asm!(
            "ecall",
            in("a0") 1usize,
            in("a1") buf[i..].as_ptr(),
            in("a2") buf.len() - i,
            in("a7") 64usize,
            lateout("a0") _,
            options(nostack),
        );
    }
}

fn emit_bytes(s: &[u8]) {
    unsafe {
        core::arch::asm!(
            "ecall",
            in("a0") 1usize,
            in("a1") s.as_ptr(),
            in("a2") s.len(),
            in("a7") 64usize,
            lateout("a0") _,
            options(nostack),
        );
    }
}

#[no_mangle]
pub extern "C" fn fc_cart_init() {
    // Resource-derived data — constant, independent of state buffers.
    unsafe {
        *RESOURCE_LOOKUP.0.get() = Some(alloc::vec![0x1234_5678u32; 16]);
    }
    // State-derived data — delegate to on_load (S_CTR=0 in fresh state buffer).
    fc_cart_on_load();
}

#[no_mangle]
pub extern "C" fn fc_cart_on_load() {
    let s_ctr       = MainBuffer.get_u32(SLOT, S_CTR);
    let s_arc_inner = MainBuffer.get_u32(SLOT, S_ARC_INNER);

    // Initialize resource-derived data if not present (fresh-process restore).
    // In same-process rewind, RESOURCE_LOOKUP survives from init; this only
    // fires in a fresh rv32emu invocation that never called fc_cart_init.
    unsafe {
        if (*RESOURCE_LOOKUP.0.get()).is_none() {
            *RESOURCE_LOOKUP.0.get() = Some(alloc::vec![0x1234_5678u32; 16]);
        }
    }

    // Rebuild STATE_VEC from S_CTR.  This is the state-derived rebuild that
    // on_load is responsible for.  Its value INTENTIONALLY differs from what
    // init set (init uses S_CTR=0; on_load uses the restored S_CTR value).
    unsafe {
        let _ = (*STATE_VEC.0.get()).take();
        let len = (s_ctr as usize) % 16 + 1;
        *STATE_VEC.0.get() = Some(alloc::vec![s_ctr; len]);

        // Rebuild ARC_COUNTER from S_ARC_INNER.
        let _ = (*ARC_COUNTER.0.get()).take();
        *ARC_COUNTER.0.get() = Some(Arc::new(AtomicU32::new(s_arc_inner)));

        // Rebuild GROW_VEC by replaying the push history.  GROW_VEC grows when
        // new_s_ctr % 4 == 0 (same logic as fc_cart_update).  After s_ctr frames,
        // new_s_ctr went through 1..=s_ctr, so we can reconstruct GROW_VEC.
        let _ = (*GROW_VEC.0.get()).take();
        let mut gv: Vec<u32> = Vec::new();
        for new_s_ctr in 1u32..=s_ctr {
            if new_s_ctr % 4 == 0 && gv.len() < 32 {
                gv.push(new_s_ctr);
            }
        }
        *GROW_VEC.0.get() = Some(gv);
    }
}

#[no_mangle]
pub extern "C" fn fc_cart_update() {
    let s_ctr = MainBuffer.get_u32(SLOT, S_CTR);

    // Save checkpoint taken BEFORE incrementing S_CTR.
    if s_ctr == SAVE_AT_S_CTR {
        blyt_test_save_now(s_ctr);
    }

    let new_s_ctr = s_ctr + 1;
    MainBuffer.set_u32(SLOT, S_CTR, new_s_ctr);

    // Arc clone + fetch_add: exercises AMO refcount and inner AtomicU32.
    let new_arc_inner = unsafe {
        let arc_ref = (*ARC_COUNTER.0.get()).as_ref().unwrap();
        let arc2 = Arc::clone(arc_ref);
        let val = arc2.fetch_add(1, Ordering::SeqCst);
        assert_eq!(Arc::strong_count(&arc2), 2);
        drop(arc2);
        assert_eq!(Arc::strong_count(arc_ref), 1);
        val + 1
    };
    MainBuffer.set_u32(SLOT, S_ARC_INNER, new_arc_inner);

    // Grow persistent Vec every 4th frame (realloc path).
    unsafe {
        if let Some(ref mut gv) = *GROW_VEC.0.get() {
            if new_s_ctr % 4 == 0 && gv.len() < 32 {
                gv.push(new_s_ctr);
            }
        }
    }

    // Collect digest fields.
    let (rl_len, rl0) = unsafe {
        let rl = (*RESOURCE_LOOKUP.0.get()).as_ref().unwrap();
        (rl.len() as u32, rl[0])
    };
    let (sv_len, sv0) = unsafe {
        match (*STATE_VEC.0.get()).as_ref() {
            Some(sv) => (sv.len() as u32, sv[0]),
            None => (0, 0),
        }
    };
    let (gv_len, gv_sum) = unsafe {
        match (*GROW_VEC.0.get()).as_ref() {
            Some(gv) => (gv.len() as u32, gv.iter().fold(0u32, |a, &x| a.wrapping_add(x))),
            None => (0, 0),
        }
    };
    let arc_inner = MainBuffer.get_u32(SLOT, S_ARC_INNER);

    // Emit STATE_VEC as a human-readable demonstration line (NOT part of the
    // binary-diff digest gate).  This shows that on_load rebuilt STATE_VEC from
    // the restored S_CTR (load-run: sv0=5) vs init's S_CTR (save-run: sv0=0).
    emit_bytes(STATE_VEC_LABEL);
    emit_uint(sv_len);
    emit_bytes(b" ");
    emit_uint(sv0);
    emit_bytes(NEWLINE);

    // Main digest: fields that SHOULD MATCH between save-run and load-run.
    // STATE_VEC is excluded because it legitimately differs (ADR-0087 design).
    let (hi, lo) = fnv1a64(&[
        new_s_ctr, rl_len, rl0, arc_inner, gv_len, gv_sum,
    ]);
    blyt_emit_digest(s_ctr, hi, lo);

    if new_s_ctr >= NFRAMES {
        unsafe { core::arch::asm!("li a0, 0", "li a7, 93", "ecall", options(noreturn)) };
    }
}

#[no_mangle]
pub extern "C" fn fc_cart_draw() {}
