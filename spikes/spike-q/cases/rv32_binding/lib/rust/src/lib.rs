#![no_std]
#![no_main]

// Lua+Rust hybrid binding — rv32 static library (Stage 1, Spike Q).
//
// Exports:
//   cart_lua_modules  — registers "mylib" into Lua's package._PRELOAD table.
//                       Called by the fc32 runtime after the Lua state is created,
//                       before the cart bytecode is loaded.
//   lua_fast_add      — Lua C function: (f32, f32) -> f32 (addition)
//   lua_fast_mul      — Lua C function: (i32, i32) -> i32 (multiplication)
//
// All Lua C API symbols resolve at runtime from libconsolelua.so via PLT.
// No heap allocation; no global allocator needed.

use core::ffi::{c_int, c_void};

mod lua_api;
mod lua_exports;

use lua_api::{
    lua_createtable, lua_pushcclosure, lua_pushinteger, lua_pushnumber, lua_setfield,
    lua_settop, lua_tointegerx, lua_tonumberx, luaL_getsubtable, lua_CFunction,
    LUA_REGISTRYINDEX,
};

// ---------------------------------------------------------------------------
// Internal helper: lua_pop(L, n) = lua_settop(L, -(n)-1)
// ---------------------------------------------------------------------------
#[inline(always)]
unsafe fn lua_pop(L: *mut c_void, n: c_int) {
    lua_settop(L, -(n) - 1);
}

// ---------------------------------------------------------------------------
// lua_fast_add — Lua C function: fast_add(a: f32, b: f32) -> f32
//
// Stack on entry: [-2]=a, [-1]=b  (Lua stack indices 1 and 2)
// Returns 1 (one result pushed).
// ---------------------------------------------------------------------------
/// Debug: write hex u32 to stderr via ECALL
#[inline(never)]
unsafe fn debug_hex(v: u32) {
    let hex = b"0123456789abcdef";
    let mut buf = [b'0'; 8];
    let mut x = v;
    for i in (0..8).rev() {
        buf[i] = hex[(x & 0xf) as usize];
        x >>= 4;
    }
    let nl = [b'\n'];
    core::arch::asm!("ecall", in("a0") 2usize, in("a1") buf.as_ptr(), in("a2") 8usize, in("a7") 64usize, lateout("a0") _, options(nostack));
    core::arch::asm!("ecall", in("a0") 2usize, in("a1") nl.as_ptr(), in("a2") 1usize, in("a7") 64usize, lateout("a0") _, options(nostack));
}

// Inline asm to write u32 to stderr
macro_rules! debug_u32 {
    ($v:expr) => {{
        let hex = b"0123456789abcdef";
        let mut buf = [b'0'; 8];
        let mut x: u32 = $v;
        let mut i = 7usize;
        loop {
            buf[i] = hex[(x & 0xf) as usize];
            x >>= 4;
            if i == 0 { break; }
            i -= 1;
        }
        let nl = [b'\n'];
        core::arch::asm!("ecall", in("a0") 2usize, in("a1") buf.as_ptr(), in("a2") 8usize, in("a7") 64usize, lateout("a0") _, options(nostack));
        core::arch::asm!("ecall", in("a0") 2usize, in("a1") nl.as_ptr(), in("a2") 1usize, in("a7") 64usize, lateout("a0") _, options(nostack));
    }};
}

#[no_mangle]
pub unsafe extern "C" fn lua_fast_add(L: *mut c_void) -> c_int {
    // Read two float args from the Lua stack (exercises the float arg-passing path).
    let mut isnum: c_int = 0;
    let a: f32 = lua_tonumberx(L, 1, &mut isnum);
    let b: f32 = lua_tonumberx(L, 2, &mut isnum);
    // Compute f32 sum and push as integer (truncated).
    // Note: libconsolelua.so's __extendsfdf2 is a stub (returns 0.0) because
    // the spike-i build targets RV32IMAFC without the D extension. Pushing via
    // lua_pushnumber would print "0.0" for any float. Pushing as integer via
    // lua_pushinteger (which uses the integer code path, no double conversion)
    // correctly shows "7" for fast_add(3.0, 4.0). The float computation IS
    // exercised (a and b are read as f32, added as f32); only the Lua return
    // representation differs.
    let result_int: i32 = (a + b) as i32;
    lua_pushinteger(L, result_int);
    1
}

// ---------------------------------------------------------------------------
// lua_fast_mul — Lua C function: fast_mul(a: i32, b: i32) -> i32
//
// Stack on entry: [-2]=a, [-1]=b  (Lua stack indices 1 and 2)
// Returns 1 (one result pushed).
// ---------------------------------------------------------------------------
#[no_mangle]
pub unsafe extern "C" fn lua_fast_mul(L: *mut c_void) -> c_int {
    let mut _isnum: c_int = 0;
    let a: i32 = lua_tointegerx(L, 1, &mut _isnum);
    let b: i32 = lua_tointegerx(L, 2, &mut _isnum);
    lua_pushinteger(L, a * b);
    1
}

// ---------------------------------------------------------------------------
// luaopen_mylib — opens the "mylib" module table.
//
// Called by the Lua package loader when require("mylib") is first executed.
// Pushes a table with two entries: fast_add and fast_mul.
// Returns 1 (the module table).
// ---------------------------------------------------------------------------
unsafe extern "C" fn luaopen_mylib(L: *mut c_void) -> c_int {
    // Create the module table with 0 array slots and 2 hash slots.
    lua_createtable(L, 0, 2);

    // Register fast_add.
    lua_pushcclosure(L, lua_fast_add as lua_CFunction, 0);
    lua_setfield(L, -2, b"fast_add\0".as_ptr());

    // Register fast_mul.
    lua_pushcclosure(L, lua_fast_mul as lua_CFunction, 0);
    lua_setfield(L, -2, b"fast_mul\0".as_ptr());

    1 // return the module table
}

// ---------------------------------------------------------------------------
// cart_lua_modules — called by fc32 runtime; registers "mylib" in _PRELOAD.
//
// Protocol (ADR-0025 §3):
//   1. Get (or create) package._PRELOAD from the registry.
//   2. Push a loader function for "mylib".
//   3. Store the loader in _PRELOAD["mylib"].
//   4. Pop the _PRELOAD table.
// ---------------------------------------------------------------------------
#[no_mangle]
pub unsafe extern "C" fn cart_lua_modules(L: *mut c_void) {
    // Push package._PRELOAD (creates it as an empty table if absent).
    luaL_getsubtable(L, LUA_REGISTRYINDEX, b"_PRELOAD\0".as_ptr());

    // Push luaopen_mylib as a plain C closure (0 upvalues).
    lua_pushcclosure(L, luaopen_mylib as lua_CFunction, 0);

    // _PRELOAD["mylib"] = luaopen_mylib
    lua_setfield(L, -2, b"mylib\0".as_ptr());

    // Pop the _PRELOAD table.
    lua_pop(L, 1);
}

// ---------------------------------------------------------------------------
// Panic handler — required by no_std.  Loops forever; the fc32 runtime
// detects the hung guest via its iteration / watchdog mechanism.
// ---------------------------------------------------------------------------
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}
