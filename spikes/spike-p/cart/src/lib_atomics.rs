// Stage 1 — A-extension smoke test: LR/SC and AMO instruction emission and
// execution in rv32emu.
//
// FINDING: Rust's AtomicU32 on riscv32imafc-unknown-none-elf uses LLVM's
// single-threaded atomic lowering — regular loads/stores, no AMO/LR-SC opcodes.
// This is expected for bare-metal targets with no threading runtime.
//
// To verify rv32emu's A-extension support, explicit inline assembly functions
// emit amoadd.w (AMO path) and lr.w/sc.w (LR/SC path).  These also serve as
// the correctness check: their results must match the equivalent non-atomic
// operations at equivalent values.
//
// The disassembly gate checks for lr.w, sc.w, or amo* from these functions.

use core::sync::atomic::{AtomicU32, Ordering};

// ── Explicit AMO helper ───────────────────────────────────────────────────────
// Returns the OLD value of *addr, atomically adds `val`.
fn explicit_amoadd(addr: &AtomicU32, val: u32) -> u32 {
    let old: u32;
    unsafe {
        core::arch::asm!(
            "amoadd.w.aqrl {old}, {val}, ({addr})",
            old  = out(reg) old,
            val  = in(reg) val,
            addr = in(reg) addr as *const AtomicU32,
        );
    }
    old
}

// ── Explicit LR/SC helper ─────────────────────────────────────────────────────
// Returns the OLD value, atomically writes (old + val) via LR/SC loop.
fn explicit_lrsc_add(addr: &AtomicU32, val: u32) -> u32 {
    loop {
        let old: u32;
        let new: u32;
        let sc_status: u32;
        unsafe {
            core::arch::asm!(
                "lr.w.aq  {old},      ({addr})",
                "add      {new}, {old}, {val}",
                "sc.w.rl  {sc}, {new}, ({addr})",
                old  = out(reg) old,
                new  = out(reg) new,
                sc   = out(reg) sc_status,
                val  = in(reg) val,
                addr = in(reg) addr as *const AtomicU32,
            );
        }
        if sc_status == 0 {
            return old;
        }
    }
}

static AMOADD_TARGET: AtomicU32 = AtomicU32::new(0);
static LRSC_TARGET:   AtomicU32 = AtomicU32::new(1000);
static COUNTER:       AtomicU32 = AtomicU32::new(0);
static ONCE_STATE:    AtomicU32 = AtomicU32::new(0);
static ONCE_VAL:      AtomicU32 = AtomicU32::new(0);

fn init_once() {
    if ONCE_STATE.compare_exchange(0, 1, Ordering::SeqCst, Ordering::Relaxed).is_ok() {
        ONCE_VAL.store(0xCAFE_F00D, Ordering::Release);
    }
}

const NFRAMES: u32 = 10;
static FRAME: AtomicU32 = AtomicU32::new(0);

extern "C" {
    fn blyt_emit_digest(frame: u32, hi: u32, lo: u32);
}

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

#[no_mangle]
pub extern "C" fn fc_cart_init() {
    // Warm up COUNTER via Rust AtomicU32 (single-threaded lowering, no AMO emitted).
    for _ in 0..10 {
        COUNTER.fetch_add(1, Ordering::SeqCst);
    }

    // Exercise AMO path: amoadd.w.aqrl
    let old_amo = explicit_amoadd(&AMOADD_TARGET, 42);
    assert_eq!(old_amo, 0);  // initial value was 0
    assert_eq!(AMOADD_TARGET.load(Ordering::Relaxed), 42);

    // Exercise LR/SC path: lr.w.aq + sc.w.rl
    let old_lrsc = explicit_lrsc_add(&LRSC_TARGET, 1);
    assert_eq!(old_lrsc, 1000);  // initial value was 1000
    assert_eq!(LRSC_TARGET.load(Ordering::Relaxed), 1001);

    // Once init: exercises Rust's compare_exchange (single-threaded lowering).
    init_once();
    init_once();  // second call: should be a no-op
}

#[no_mangle]
pub extern "C" fn fc_cart_update() {
    let frame = FRAME.fetch_add(1, Ordering::Relaxed);
    COUNTER.fetch_add(1, Ordering::SeqCst);

    // AMO add each update: builds up a known sum.
    explicit_amoadd(&AMOADD_TARGET, 1);

    let counter_val = COUNTER.load(Ordering::Acquire);
    let once_val    = ONCE_VAL.load(Ordering::Relaxed);
    let amo_val     = AMOADD_TARGET.load(Ordering::Relaxed);

    let (hi, lo) = fnv1a64(&[frame, counter_val, once_val, amo_val]);
    unsafe { blyt_emit_digest(frame, hi, lo) };

    if frame + 1 >= NFRAMES {
        unsafe { core::arch::asm!("li a0, 0", "li a7, 93", "ecall", options(noreturn)) };
    }
}

#[no_mangle]
pub extern "C" fn fc_cart_draw() {}

#[no_mangle]
pub extern "C" fn fc_cart_on_load() {}
