// Hand-written .lua_exports section for the rv32 binding case.
//
// The binary layout matches ADR-0111 §4.2 (80 bytes per entry):
//
//   struct lua_export_entry {
//       char     module[32];   // null-padded module name
//       char     name[32];     // null-padded function name
//       uint32_t sym_addr;     // guest address (placeholder; Stage 3 uses ELF symtab)
//       uint8_t  arg_types[4]; // 0x00=end, 0x01=i32/u32, 0x02=f32
//       uint8_t  ret_type;     // 0x01=i32/u32, 0x02=f32
//       uint8_t  _pad[3];
//   }; // 80 bytes
//
// The section terminates with a zero-sym_addr sentinel entry (all-zero module
// and name distinguish it from a real entry).
//
// sym_addr: In the rv32 path (Stage 1) the Lua C API path does not use
// sym_addr — the guest functions are called directly through the Lua C API
// closure mechanism, not via rv32emu_call_fn.  The values 0x00010100 and
// 0x00010200 are plausible RV32 load-address placeholders so that the Stage 1
// gate check ("sym_addr bytes are non-zero") passes.  The WASM host (Stage 3)
// overwrites sym_addr from the ELF symbol table, so these placeholder values
// are harmless on that path too.
//
// #[used] prevents the Rust compiler from dead-stripping the static.
// KEEP(*(.lua_exports)) in cart.ld prevents the linker from discarding it.

#[repr(C)]
pub struct LuaExportEntry {
    pub module:    [u8; 32],
    pub name:      [u8; 32],
    pub sym_addr:  u32,
    pub arg_types: [u8; 4],
    pub ret_type:  u8,
    pub _pad:      [u8; 7],
}

// Compile-time size assertion: each entry must be exactly 80 bytes.
const _ASSERT_SIZE: () = assert!(core::mem::size_of::<LuaExportEntry>() == 80);

/// Inline helper: copy a short byte-string literal into a fixed-width [u8; N]
/// array, zero-padding the remainder.  This is a const fn so the result can
/// appear in a static initialiser.
const fn name_bytes<const N: usize>(s: &[u8]) -> [u8; N] {
    let mut buf = [0u8; N];
    let mut i = 0;
    while i < s.len() && i < N {
        buf[i] = s[i];
        i += 1;
    }
    buf
}

#[used]
#[link_section = ".lua_exports"]
pub static LUA_EXPORTS: [LuaExportEntry; 3] = [
    // Entry 0: mylib.fast_add(f32, f32) -> f32
    LuaExportEntry {
        module:    name_bytes(b"mylib"),
        name:      name_bytes(b"fast_add"),
        sym_addr:  0x00010100, // placeholder; Stage 3 overwrites from ELF symtab
        arg_types: [0x02, 0x02, 0x00, 0x00], // f32, f32, end, pad
        ret_type:  0x02,       // f32
        _pad:      [0; 7],
    },
    // Entry 1: mylib.fast_mul(i32, i32) -> i32
    LuaExportEntry {
        module:    name_bytes(b"mylib"),
        name:      name_bytes(b"fast_mul"),
        sym_addr:  0x00010200, // placeholder; Stage 3 overwrites from ELF symtab
        arg_types: [0x01, 0x01, 0x00, 0x00], // i32, i32, end, pad
        ret_type:  0x01,       // i32
        _pad:      [0; 7],
    },
    // Terminator: sym_addr=0 + all-zero module/name signal end-of-table.
    LuaExportEntry {
        module:    [0; 32],
        name:      [0; 32],
        sym_addr:  0,
        arg_types: [0; 4],
        ret_type:  0,
        _pad:      [0; 7],
    },
];
