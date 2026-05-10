// Lua 5.4 C API declarations for the rv32 Rust binding layer.
//
// All symbols are resolved at runtime from libconsolelua.so via PLT; there is
// no static linking against liblua.  The declarations use *mut c_void for
// lua_State (opaque pointer) and match the LUA_32BITS configuration:
//   lua_Number  = float  (f32)
//   lua_Integer = int    (i32)
//
// LUA_REGISTRYINDEX = -(LUAI_MAXSTACK) - 1000
// LUAI_MAXSTACK is controlled by LUAI_IS32INT (whether int >= 32 bits), not by LUA_32BITS.
// On any platform with 32-bit int (including RV32), LUAI_IS32INT=1 and LUAI_MAXSTACK=1000000.
// So LUA_REGISTRYINDEX = -(1000000) - 1000 = -1001000.
// (The 15000 alternative applies only on 16-bit-int platforms — not RV32.)

use core::ffi::{c_int, c_void};

pub const LUA_REGISTRYINDEX: i32 = -1001000;

/// Lua C function type: receives the Lua state, returns number of results.
pub type lua_CFunction = unsafe extern "C" fn(*mut c_void) -> i32;

extern "C" {
    /// lua_settop — underlies the lua_pop macro: lua_pop(L,n) = lua_settop(L,-(n)-1)
    pub fn lua_settop(L: *mut c_void, idx: c_int);

    /// lua_createtable — underlies lua_newtable: lua_newtable(L) = lua_createtable(L,0,0)
    pub fn lua_createtable(L: *mut c_void, narr: c_int, nrec: c_int);

    /// lua_getfield — pushes t[k] where t is at the given index.  Returns the type.
    pub fn lua_getfield(L: *mut c_void, idx: c_int, k: *const u8) -> c_int;

    /// lua_setfield — pops a value and sets t[k] where t is at the given index.
    pub fn lua_setfield(L: *mut c_void, idx: c_int, k: *const u8);

    /// lua_pushcclosure — pushes a new C closure with n upvalues.
    /// lua_pushcfunction is a macro: lua_pushcfunction(L,f) = lua_pushcclosure(L,f,0)
    pub fn lua_pushcclosure(L: *mut c_void, f: lua_CFunction, n: c_int);

    /// lua_tonumberx — returns the value at idx as lua_Number (f32 with LUA_32BITS).
    /// *isnum is set to 1 if the value is a number (or a string coercible to one).
    pub fn lua_tonumberx(L: *mut c_void, idx: c_int, isnum: *mut c_int) -> f32;

    /// lua_pushnumber — pushes a lua_Number (f32 with LUA_32BITS) onto the stack.
    pub fn lua_pushnumber(L: *mut c_void, n: f32);

    /// lua_tointegerx — returns the value at idx as lua_Integer (i32 with LUA_32BITS).
    /// *isnum is set to 1 if the value is an integer (or a string coercible to one).
    pub fn lua_tointegerx(L: *mut c_void, idx: c_int, isnum: *mut c_int) -> i32;

    /// lua_pushinteger — pushes a lua_Integer (i32 with LUA_32BITS) onto the stack.
    pub fn lua_pushinteger(L: *mut c_void, n: i32);

    /// luaL_getsubtable — pushes t[fname] (creates it as a table if absent).
    /// Returns 1 if t[fname] already existed, 0 if it was created.
    pub fn luaL_getsubtable(L: *mut c_void, idx: c_int, fname: *const u8) -> c_int;

    /// lua_type — returns the type of the value at idx (LUA_TFUNCTION = 6, etc.).
    pub fn lua_type(L: *mut c_void, idx: c_int) -> c_int;
}
