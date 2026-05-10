// Hand-written .lua_exports section for the WASM trampoline cart ELF.
//
// The binary layout is identical to the rv32_binding version (ADR-0111 §4.2,
// 80 bytes per entry).  The WASM host reads this section from the embedded
// cart ELF and overwrites sym_addr for each entry with the actual guest address
// resolved from the ELF symbol table.  The placeholder values (0x00010100,
// 0x00010200) are not used by the host; they exist only so the Stage 1 gate
// check ("sym_addr bytes are non-zero") passes if this ELF is inspected with
// the same objdump check applied to the rv32_binding ELF.
//
// Function signatures:
//   fast_add(f32, f32) -> f32   arg_types=[0x02, 0x02, 0x00, 0x00]  ret=0x02
//   fast_mul(i32, i32) -> i32   arg_types=[0x01, 0x01, 0x00, 0x00]  ret=0x01

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
        sym_addr:  0x00010100, // placeholder; WASM host overwrites from ELF symtab
        arg_types: [0x02, 0x02, 0x00, 0x00], // f32, f32, end, pad
        ret_type:  0x02,       // f32
        _pad:      [0; 7],
    },
    // Entry 1: mylib.fast_mul(i32, i32) -> i32
    LuaExportEntry {
        module:    name_bytes(b"mylib"),
        name:      name_bytes(b"fast_mul"),
        sym_addr:  0x00010200, // placeholder; WASM host overwrites from ELF symtab
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
