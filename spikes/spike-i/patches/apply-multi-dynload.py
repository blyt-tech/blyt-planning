#!/usr/bin/env python3
"""
Patch rv32emu with spike-i multi-library dynamic loader.

Run from the rv32emu/ directory (no prior spike-c patch required):
    python3 /spike-i/patches/apply-multi-dynload.py

Applies all changes in one pass (idempotent — safe to run more than once):
  src/main.c  -- adds fc32_libpath global and -L option
  src/elf.h   -- adds fc32_dynload() declaration
  src/elf.c   -- adds full multi-library fc32_dynload() implementation
  src/riscv.c -- calls fc32_dynload() after elf_load()

With fc32_libpath == NULL (no -L flag) the binary behaves identically to upstream.
"""

import sys

# ---------------------------------------------------------------------------
# src/main.c
# ---------------------------------------------------------------------------

MAIN_C = "src/main.c"
with open(MAIN_C) as f:
    main_src = f.read()

if "fc32_libpath" not in main_src:
    MAIN_OPTSTR_OLD = 'static const char *optstr = "tgqmhpd:a:k:i:b:x:";'
    MAIN_OPTSTR_NEW = ('/* fc32 Spike I: libpath for dynamic loader */\n'
                       'char *fc32_libpath = NULL;\n\n'
                       'static const char *optstr = "tgqmhpd:a:k:i:b:x:L:";')
    if MAIN_OPTSTR_OLD not in main_src:
        sys.exit("ERROR: optstr line not found in src/main.c")
    main_src = main_src.replace(MAIN_OPTSTR_OLD, MAIN_OPTSTR_NEW, 1)

    MAIN_CASE_OLD = """\
        case 'm':
            opt_misaligned = true;
            break;"""
    MAIN_CASE_NEW = """\
        case 'L':
            fc32_libpath = optarg;
            emu_argc++;
            break;
        case 'm':
            opt_misaligned = true;
            break;"""
    if MAIN_CASE_OLD not in main_src:
        sys.exit("ERROR: case 'm': not found in src/main.c")
    main_src = main_src.replace(MAIN_CASE_OLD, MAIN_CASE_NEW, 1)

    with open(MAIN_C, "w") as f:
        f.write(main_src)
    print("src/main.c patched.")
else:
    print("src/main.c already patched, skipping.")

# ---------------------------------------------------------------------------
# src/elf.h
# ---------------------------------------------------------------------------

ELF_H = "src/elf.h"
with open(ELF_H) as f:
    elf_h = f.read()

if "fc32_dynload" not in elf_h:
    ELF_H_OLD = "/* get the first byte of ELF raw data */\nuint8_t *get_elf_first_byte(elf_t *e);"
    ELF_H_NEW = ("/* get the first byte of ELF raw data */\n"
                 "uint8_t *get_elf_first_byte(elf_t *e);\n\n"
                 "/* fc32 Spike I: multi-library dynamic loader.\n"
                 " * Loads DT_NEEDED shared libraries recursively from libpath,\n"
                 " * builds a global symbol table including cart .dynsym exports,\n"
                 " * and resolves all relocations bidirectionally.\n"
                 " */\n"
                 "bool fc32_dynload(memory_t *mem, const uint8_t *cart_data,\n"
                 "                  const char *libpath);")
    if ELF_H_OLD not in elf_h:
        sys.exit("ERROR: expected tail of elf.h not found")
    elf_h = elf_h.replace(ELF_H_OLD, ELF_H_NEW, 1)
    with open(ELF_H, "w") as f:
        f.write(elf_h)
    print("src/elf.h patched.")
else:
    print("src/elf.h already patched, skipping.")

# ---------------------------------------------------------------------------
# src/elf.c -- add / replace fc32_dynload
# ---------------------------------------------------------------------------

ELF_C = "src/elf.c"
with open(ELF_C) as f:
    elf_c = f.read()

ANCHOR = "uint8_t *get_elf_first_byte(elf_t *e)\n{\n    return (uint8_t *) e->raw_data;\n}"
if ANCHOR not in elf_c:
    sys.exit("ERROR: get_elf_first_byte not found in src/elf.c")

DYNLOAD_IMPL = r"""
/* -------------------------------------------------------------------------
 * fc32 Spike I: multi-library dynamic loader
 * -------------------------------------------------------------------------
 * Called from rv_create() after the main ELF's PT_LOAD segments are mapped.
 *
 * Key differences from Spike C's single-library loader:
 *   1. Cart's own .dynsym exports are added to the global symbol table FIRST,
 *      so libconsole's weak undef refs (fc_cart_init etc.) resolve back to
 *      the cart for C carts.
 *   2. DT_NEEDED libraries are loaded recursively, deduped by soname, with
 *      non-overlapping load addresses.
 *   3. Library RELA relocations are applied using the unified global table
 *      (lib-to-lib, lib-to-cart all handled).
 *   4. Unresolved weak refs get GOT entry zero — correct ELFOSABI behaviour.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Load .so files starting at 128 MiB (within default 256 MiB guest memory) */
#define FC32_LIB_LOAD_ADDR 0x08000000u
#define FC32_LIB_ALIGN     0x1000u

static void fc32_write32(memory_t *mem, uint32_t addr, uint32_t val)
{
    memory_write(mem, addr, (const uint8_t *)&val, 4);
}

/* RISC-V 32-bit relocation types */
#define R_RISCV_NONE      0
#define R_RISCV_32        1
#define R_RISCV_RELATIVE  3
#define R_RISCV_JUMP_SLOT 5
#define R_RISCV_GLOB_DAT  19

#define ELF32_R_SYM(i)  ((i) >> 8)
#define ELF32_R_TYPE(i) ((unsigned char)(i))

struct fc32_Dyn  { int32_t d_tag; uint32_t d_val; };
struct fc32_Rela { uint32_t r_offset; uint32_t r_info; int32_t r_addend; };

#define DT_NULL     0
#define DT_NEEDED   1
#define DT_PLTRELSZ 2
#define DT_STRTAB   5
#define DT_SYMTAB   6
#define DT_RELA     7
#define DT_RELASZ   8
#define DT_SYMENT   11
#define DT_JMPREL   23

#define STB_GLOBAL 1
#define STB_WEAK   2
#define ELF32_ST_BIND(i) ((i) >> 4)
#define SHN_UNDEF  0

/* Global symbol table — cart exports first, then library exports */
#define FC32_SYM_MAX 4096
typedef struct { char name[64]; uint32_t addr; } fc32_sym_t;
static int        fc32_sym_count;
static fc32_sym_t fc32_syms[FC32_SYM_MAX];

static void fc32_sym_add(const char *name, uint32_t addr)
{
    if (fc32_sym_count >= FC32_SYM_MAX) return;
    for (int i = 0; i < fc32_sym_count; i++)
        if (!strcmp(fc32_syms[i].name, name)) return; /* first definition wins */
    strncpy(fc32_syms[fc32_sym_count].name, name, 63);
    fc32_syms[fc32_sym_count].name[63] = '\0';
    fc32_syms[fc32_sym_count].addr = addr;
    fc32_sym_count++;
}

static uint32_t fc32_sym_find(const char *name)
{
    for (int i = 0; i < fc32_sym_count; i++)
        if (!strcmp(fc32_syms[i].name, name)) return fc32_syms[i].addr;
    return 0;
}

/* Virtual address to file offset within an ELF image */
static uint32_t fc32_vaddr_to_off(const uint8_t *data,
                                   const struct Elf32_Ehdr *ehdr,
                                   uint32_t vaddr)
{
    for (int p = 0; p < ehdr->e_phnum; p++) {
        const struct Elf32_Phdr *ph = (const struct Elf32_Phdr *)
            (data + ehdr->e_phoff + p * ehdr->e_phentsize);
        if (ph->p_type != PT_LOAD) continue;
        if (vaddr >= ph->p_vaddr && vaddr < ph->p_vaddr + ph->p_filesz)
            return ph->p_offset + (vaddr - ph->p_vaddr);
    }
    return 0;
}

/* Compute the highest guest address occupied by a library's PT_LOAD segments */
static uint32_t fc32_lib_extent(const uint8_t *data,
                                 const struct Elf32_Ehdr *ehdr,
                                 uint32_t base)
{
    uint32_t top = base;
    for (int p = 0; p < ehdr->e_phnum; p++) {
        const struct Elf32_Phdr *ph = (const struct Elf32_Phdr *)
            (data + ehdr->e_phoff + p * ehdr->e_phentsize);
        if (ph->p_type != PT_LOAD) continue;
        uint32_t end = base + ph->p_vaddr + ph->p_memsz;
        if (end > top) top = end;
    }
    return top;
}

/* Loaded-library dedup registry. lib_data is kept alive so the second-pass
 * relocator can re-read .rela.dyn / .rela.plt after all libraries have
 * registered their symbols. */
#define FC32_LIB_MAX 8
static char     fc32_loaded[FC32_LIB_MAX][64];
static uint32_t fc32_loaded_base[FC32_LIB_MAX];
static uint8_t *fc32_loaded_data[FC32_LIB_MAX];
static int      fc32_loaded_count;

static int fc32_lib_loaded(const char *soname)
{
    for (int i = 0; i < fc32_loaded_count; i++)
        if (!strcmp(fc32_loaded[i], soname)) return 1;
    return 0;
}

static void fc32_lib_register(const char *soname, uint32_t base, uint8_t *data)
{
    if (fc32_loaded_count >= FC32_LIB_MAX) return;
    strncpy(fc32_loaded[fc32_loaded_count], soname, 63);
    fc32_loaded_base[fc32_loaded_count] = base;
    fc32_loaded_data[fc32_loaded_count] = data;
    fc32_loaded_count++;
}

/* Apply RELA relocations for one library using the global symbol table */
static void fc32_apply_lib_rela(memory_t *mem,
                                 const uint8_t *lib_data,
                                 const struct Elf32_Ehdr *lhdr,
                                 uint32_t lib_base,
                                 const struct Elf32_Sym *dynsym,
                                 uint32_t dynsym_ent,
                                 const char *dynstr)
{
    for (int s = 0; s < lhdr->e_shnum; s++) {
        const struct Elf32_Shdr *sh = (const struct Elf32_Shdr *)
            (lib_data + lhdr->e_shoff + s * lhdr->e_shentsize);
        if (sh->sh_type != SHT_RELA) continue;

        uint32_t rela_ent = sh->sh_entsize ? sh->sh_entsize : sizeof(struct fc32_Rela);
        uint32_t nrela    = sh->sh_size / rela_ent;
        const struct fc32_Rela *relas =
            (const struct fc32_Rela *)(lib_data + sh->sh_offset);

        for (uint32_t ri = 0; ri < nrela; ri++) {
            uint32_t r_off  = relas[ri].r_offset;
            uint32_t r_info = relas[ri].r_info;
            int32_t  r_add  = relas[ri].r_addend;
            uint32_t rtype  = ELF32_R_TYPE(r_info);
            uint32_t rsym   = ELF32_R_SYM(r_info);
            uint32_t goff   = lib_base + r_off;

            switch (rtype) {
            case R_RISCV_RELATIVE:
                fc32_write32(mem, goff, lib_base + (uint32_t)r_add);
                break;
            case R_RISCV_32:
                if (rsym && dynsym && dynstr) {
                    const struct Elf32_Sym *sym = (const struct Elf32_Sym *)
                        ((const uint8_t *)dynsym + rsym * dynsym_ent);
                    uint32_t sv = fc32_sym_find(dynstr + sym->st_name);
                    if (!sv && sym->st_value) sv = lib_base + sym->st_value;
                    fc32_write32(mem, goff, sv + (uint32_t)r_add);
                } else {
                    fc32_write32(mem, goff, (uint32_t)r_add);
                }
                break;
            case R_RISCV_JUMP_SLOT:
                if (rsym && dynsym && dynstr) {
                    const struct Elf32_Sym *sym = (const struct Elf32_Sym *)
                        ((const uint8_t *)dynsym + rsym * dynsym_ent);
                    uint32_t sv = fc32_sym_find(dynstr + sym->st_name);
                    if (!sv && sym->st_value) sv = lib_base + sym->st_value;
                    /* zero for unresolved weak ref — correct ABI */
                    fc32_write32(mem, goff, sv);
                }
                break;
            case R_RISCV_GLOB_DAT:
                if (rsym && dynsym && dynstr) {
                    const struct Elf32_Sym *sym = (const struct Elf32_Sym *)
                        ((const uint8_t *)dynsym + rsym * dynsym_ent);
                    uint32_t sv = fc32_sym_find(dynstr + sym->st_name);
                    if (!sv && sym->st_value) sv = lib_base + sym->st_value;
                    fc32_write32(mem, goff, sv + (uint32_t)r_add);
                }
                break;
            case R_RISCV_NONE:
                break;
            default:
                fprintf(stderr, "fc32_dynload: unhandled lib rela type %u\n", rtype);
                break;
            }
        }
    }
}

/* Load one library into guest memory; recurse into its DT_NEEDED deps first */
static bool fc32_load_lib(memory_t *mem, const char *libpath,
                           const char *soname, uint32_t *next_base)
{
    if (fc32_lib_loaded(soname)) return true;

    char path[512];
    snprintf(path, sizeof path, "%s/%s", libpath, soname);
    FILE *lf = fopen(path, "rb");
    if (!lf) { fprintf(stderr, "fc32_dynload: cannot open %s\n", path); return false; }
    fseek(lf, 0, SEEK_END); long lsz = ftell(lf); fseek(lf, 0, SEEK_SET);
    uint8_t *lib_data = (uint8_t *)malloc((size_t)lsz);
    if (!lib_data) { fclose(lf); return false; }
    if (fread(lib_data, 1, (size_t)lsz, lf) != (size_t)lsz) {
        fprintf(stderr, "fc32_dynload: short read %s\n", path);
        free(lib_data); fclose(lf); return false;
    }
    fclose(lf);

    const struct Elf32_Ehdr *lhdr = (const struct Elf32_Ehdr *)lib_data;
    uint32_t lib_base = *next_base;
    fc32_lib_register(soname, lib_base, lib_data);

    /* Map PT_LOAD segments */
    for (int p = 0; p < lhdr->e_phnum; p++) {
        const struct Elf32_Phdr *ph = (const struct Elf32_Phdr *)
            (lib_data + lhdr->e_phoff + p * lhdr->e_phentsize);
        if (ph->p_type != PT_LOAD) continue;
        uint32_t ga = lib_base + ph->p_vaddr;
        if (ph->p_filesz) memory_write(mem, ga, lib_data + ph->p_offset, ph->p_filesz);
        if (ph->p_memsz > ph->p_filesz)
            memory_fill(mem, ga + ph->p_filesz, ph->p_memsz - ph->p_filesz, 0);
    }
    uint32_t top = fc32_lib_extent(lib_data, lhdr, lib_base);
    *next_base = (top + FC32_LIB_ALIGN - 1) & ~(FC32_LIB_ALIGN - 1);

    /* Find .dynsym / .dynstr via section headers */
    uint32_t dynsym_off = 0, dynsym_sz = 0, dynsym_ent = sizeof(struct Elf32_Sym);
    uint32_t dynstr_off = 0;
    for (int s = 0; s < lhdr->e_shnum; s++) {
        const struct Elf32_Shdr *sh = (const struct Elf32_Shdr *)
            (lib_data + lhdr->e_shoff + s * lhdr->e_shentsize);
        if (sh->sh_type == SHT_DYNSYM) {
            dynsym_off = sh->sh_offset; dynsym_sz = sh->sh_size;
            if (sh->sh_entsize) dynsym_ent = sh->sh_entsize;
            if (sh->sh_link < (uint32_t)lhdr->e_shnum) {
                const struct Elf32_Shdr *ss = (const struct Elf32_Shdr *)
                    (lib_data + lhdr->e_shoff + sh->sh_link * lhdr->e_shentsize);
                dynstr_off = ss->sh_offset;
            }
        }
    }

    /* Register this library's exported symbols in the global table */
    if (dynsym_off && dynstr_off) {
        const char *ds = (const char *)(lib_data + dynstr_off);
        uint32_t nsym = dynsym_sz / dynsym_ent;
        for (uint32_t si = 1; si < nsym; si++) {
            const struct Elf32_Sym *sym = (const struct Elf32_Sym *)
                (lib_data + dynsym_off + si * dynsym_ent);
            if (sym->st_shndx == SHN_UNDEF) continue; /* skip undef refs */
            if (!sym->st_value) continue;
            int bind = ELF32_ST_BIND(sym->st_info);
            if (bind != STB_GLOBAL && bind != STB_WEAK) continue;
            const char *sn = ds + sym->st_name;
            if (!*sn) continue;
            fc32_sym_add(sn, lib_base + sym->st_value);
        }
    }

    /* Recurse into this library's DT_NEEDED. Relocations are applied later,
     * in a second pass (fc32_relocate_libs), once every library's symbols
     * have been registered globally. Otherwise libconsole's weak undef refs
     * to libconsolelua would resolve to zero (libconsolelua hasn't loaded
     * yet at the time libconsole's RELA would otherwise be processed). */
    for (int p = 0; p < lhdr->e_phnum; p++) {
        const struct Elf32_Phdr *ph = (const struct Elf32_Phdr *)
            (lib_data + lhdr->e_phoff + p * lhdr->e_phentsize);
        if (ph->p_type != PT_DYNAMIC) continue;
        const struct fc32_Dyn *dyn = (const struct fc32_Dyn *)(lib_data + ph->p_offset);
        uint32_t sub_st_vaddr = 0;
        for (const struct fc32_Dyn *d = dyn; d->d_tag != DT_NULL; d++)
            if (d->d_tag == DT_STRTAB) { sub_st_vaddr = d->d_val; break; }
        uint32_t sub_st_off = fc32_vaddr_to_off(lib_data, lhdr, sub_st_vaddr);
        const char *sub_st = (const char *)(lib_data + sub_st_off);
        for (const struct fc32_Dyn *d = dyn; d->d_tag != DT_NULL; d++)
            if (d->d_tag == DT_NEEDED)
                if (!fc32_load_lib(mem, libpath, sub_st + d->d_val, next_base))
                    return false;
        break;
    }

    /* lib_data stays alive — fc32_loaded_data[] owns it until the second pass. */
    return true;
}

/* Second pass: apply relocations to every loaded library now that the global
 * symbol table is complete. Frees each library's lib_data when done. */
static void fc32_relocate_libs(memory_t *mem)
{
    for (int i = 0; i < fc32_loaded_count; i++) {
        uint8_t *lib_data = fc32_loaded_data[i];
        if (!lib_data) continue;
        const struct Elf32_Ehdr *lhdr = (const struct Elf32_Ehdr *)lib_data;
        uint32_t lib_base = fc32_loaded_base[i];
        uint32_t dynsym_off = 0, dynsym_ent = sizeof(struct Elf32_Sym);
        uint32_t dynstr_off = 0;
        for (int s = 0; s < lhdr->e_shnum; s++) {
            const struct Elf32_Shdr *sh = (const struct Elf32_Shdr *)
                (lib_data + lhdr->e_shoff + s * lhdr->e_shentsize);
            if (sh->sh_type == SHT_DYNSYM) {
                dynsym_off = sh->sh_offset;
                if (sh->sh_entsize) dynsym_ent = sh->sh_entsize;
                if (sh->sh_link < (uint32_t)lhdr->e_shnum) {
                    const struct Elf32_Shdr *ss = (const struct Elf32_Shdr *)
                        (lib_data + lhdr->e_shoff + sh->sh_link * lhdr->e_shentsize);
                    dynstr_off = ss->sh_offset;
                }
            }
        }
        const struct Elf32_Sym *dynsym = dynsym_off ?
            (const struct Elf32_Sym *)(lib_data + dynsym_off) : NULL;
        const char *dynstr = dynstr_off ? (const char *)(lib_data + dynstr_off) : NULL;
        fc32_apply_lib_rela(mem, lib_data, lhdr, lib_base, dynsym, dynsym_ent, dynstr);
        free(lib_data);
        fc32_loaded_data[i] = NULL;
    }
}

bool fc32_dynload(memory_t *mem, const uint8_t *cart_data, const char *libpath)
{
    fc32_sym_count    = 0;
    fc32_loaded_count = 0;

    const struct Elf32_Ehdr *ehdr = (const struct Elf32_Ehdr *)cart_data;

    /* Locate cart PT_DYNAMIC */
    uint32_t dyn_fileoff = 0;
    for (int p = 0; p < ehdr->e_phnum; p++) {
        const struct Elf32_Phdr *ph = (const struct Elf32_Phdr *)
            (cart_data + ehdr->e_phoff + p * ehdr->e_phentsize);
        if (ph->p_type == PT_DYNAMIC) { dyn_fileoff = ph->p_offset; break; }
    }
    if (!dyn_fileoff) return true; /* static ELF — nothing to do */

    const struct fc32_Dyn *dyn = (const struct fc32_Dyn *)(cart_data + dyn_fileoff);
    uint32_t cart_strtab_vaddr = 0, cart_symtab_vaddr = 0;
    uint32_t cart_jmprel_vaddr = 0, cart_jmprel_sz    = 0;
    uint32_t cart_rela_vaddr   = 0, cart_rela_sz      = 0;
    uint32_t cart_syment       = sizeof(struct Elf32_Sym);

    for (const struct fc32_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
        case DT_STRTAB:   cart_strtab_vaddr = d->d_val; break;
        case DT_SYMTAB:   cart_symtab_vaddr = d->d_val; break;
        case DT_JMPREL:   cart_jmprel_vaddr = d->d_val; break;
        case DT_PLTRELSZ: cart_jmprel_sz    = d->d_val; break;
        case DT_RELA:     cart_rela_vaddr   = d->d_val; break;
        case DT_RELASZ:   cart_rela_sz      = d->d_val; break;
        case DT_SYMENT:   cart_syment       = d->d_val; break;
        }
    }

    uint32_t cart_strtab_off = fc32_vaddr_to_off(cart_data, ehdr, cart_strtab_vaddr);
    const char *cart_strtab  = (const char *)(cart_data + cart_strtab_off);

    /* STEP 1: Add cart's .dynsym exports to the global table FIRST.
     * This is what allows libconsole's weak refs (fc_cart_init etc.) to
     * resolve back into the cart binary for C carts. */
    uint32_t cart_symtab_off = fc32_vaddr_to_off(cart_data, ehdr, cart_symtab_vaddr);
    const struct Elf32_Sym *cart_dsym = cart_symtab_off ?
        (const struct Elf32_Sym *)(cart_data + cart_symtab_off) : NULL;

    for (int s = 0; s < ehdr->e_shnum; s++) {
        const struct Elf32_Shdr *sh = (const struct Elf32_Shdr *)
            (cart_data + ehdr->e_shoff + s * ehdr->e_shentsize);
        if (sh->sh_type != SHT_DYNSYM) continue;
        uint32_t ent = sh->sh_entsize ? sh->sh_entsize : cart_syment;
        uint32_t nsym = sh->sh_size / ent;
        for (uint32_t si = 1; si < nsym; si++) {
            const struct Elf32_Sym *sym = (const struct Elf32_Sym *)
                (cart_data + sh->sh_offset + si * ent);
            /* Skip UND entries — for the cart these are PLT stubs whose
             * st_value points to the cart's own .plt (e.g. snprintf, fc_*).
             * Registering them would shadow the real library symbols and
             * cause cart PLT GOT entries to point at themselves (infinite
             * loop on the first PLT call). */
            if (sym->st_shndx == SHN_UNDEF) continue;
            if (!sym->st_value) continue;
            int bind = ELF32_ST_BIND(sym->st_info);
            if (bind != STB_GLOBAL && bind != STB_WEAK) continue;
            const char *sn = cart_strtab + sym->st_name;
            if (!*sn) continue;
            fc32_sym_add(sn, sym->st_value); /* EXEC cart: vaddr == runtime addr */
        }
        break;
    }

    /* STEP 2: Collect and load DT_NEEDED libraries recursively */
    char needed[FC32_LIB_MAX][64];
    int ni = 0;
    for (const struct fc32_Dyn *d = dyn; d->d_tag != DT_NULL && ni < FC32_LIB_MAX; d++)
        if (d->d_tag == DT_NEEDED)
            strncpy(needed[ni++], cart_strtab + d->d_val, 63);

    uint32_t next_base = FC32_LIB_LOAD_ADDR;
    for (int i = 0; i < ni; i++)
        if (!fc32_load_lib(mem, libpath, needed[i], &next_base))
            return false;

    /* STEP 2b: Now that every library's symbols are in the global table,
     * apply each library's relocations. This resolves libconsole's weak
     * undef refs (fc_cart_init, fc_consolelua_set_bytecode, ...) to
     * libconsolelua's exports for Lua carts. */
    fc32_relocate_libs(mem);

    /* STEP 3: Resolve cart's PLT relocations (DT_JMPREL) */
    if (cart_jmprel_vaddr && cart_jmprel_sz && cart_dsym) {
        uint32_t jmprel_off = fc32_vaddr_to_off(cart_data, ehdr, cart_jmprel_vaddr);
        if (jmprel_off) {
            uint32_t nrela = cart_jmprel_sz / sizeof(struct fc32_Rela);
            const struct fc32_Rela *relas =
                (const struct fc32_Rela *)(cart_data + jmprel_off);
            for (uint32_t ri = 0; ri < nrela; ri++) {
                uint32_t rtype = ELF32_R_TYPE(relas[ri].r_info);
                uint32_t rsym  = ELF32_R_SYM(relas[ri].r_info);
                if ((rtype != R_RISCV_JUMP_SLOT && rtype != R_RISCV_GLOB_DAT) || !rsym)
                    continue;
                const struct Elf32_Sym *sym = (const struct Elf32_Sym *)
                    ((const uint8_t *)cart_dsym + rsym * cart_syment);
                uint32_t sa = fc32_sym_find(cart_strtab + sym->st_name);
                if (sa)
                    fc32_write32(mem, relas[ri].r_offset, sa);
                else
                    fprintf(stderr, "fc32_dynload: unresolved PLT '%s'\n",
                            cart_strtab + sym->st_name);
            }
        }
    }

    /* STEP 4: Resolve cart's DT_RELA (non-PLT) relocations */
    if (cart_rela_vaddr && cart_rela_sz && cart_dsym) {
        uint32_t rela_off = fc32_vaddr_to_off(cart_data, ehdr, cart_rela_vaddr);
        if (rela_off) {
            uint32_t nrela = cart_rela_sz / sizeof(struct fc32_Rela);
            const struct fc32_Rela *relas =
                (const struct fc32_Rela *)(cart_data + rela_off);
            for (uint32_t ri = 0; ri < nrela; ri++) {
                uint32_t r_off = relas[ri].r_offset;
                int32_t  r_add = relas[ri].r_addend;
                uint32_t rtype = ELF32_R_TYPE(relas[ri].r_info);
                uint32_t rsym  = ELF32_R_SYM(relas[ri].r_info);
                switch (rtype) {
                case R_RISCV_GLOB_DAT:
                case R_RISCV_JUMP_SLOT:
                    if (rsym) {
                        const struct Elf32_Sym *sym = (const struct Elf32_Sym *)
                            ((const uint8_t *)cart_dsym + rsym * cart_syment);
                        uint32_t sa = fc32_sym_find(cart_strtab + sym->st_name);
                        if (sa) fc32_write32(mem, r_off, sa + (uint32_t)r_add);
                        else fprintf(stderr, "fc32_dynload: unresolved RELA '%s'\n",
                                     cart_strtab + sym->st_name);
                    }
                    break;
                case R_RISCV_32:
                    if (rsym) {
                        const struct Elf32_Sym *sym = (const struct Elf32_Sym *)
                            ((const uint8_t *)cart_dsym + rsym * cart_syment);
                        uint32_t sa = fc32_sym_find(cart_strtab + sym->st_name);
                        if (!sa) sa = sym->st_value;
                        fc32_write32(mem, r_off, sa + (uint32_t)r_add);
                    } else {
                        fc32_write32(mem, r_off, (uint32_t)r_add);
                    }
                    break;
                case R_RISCV_RELATIVE:
                    fc32_write32(mem, r_off, (uint32_t)r_add); /* EXEC: base=0 */
                    break;
                case R_RISCV_NONE:
                    break;
                default:
                    fprintf(stderr, "fc32_dynload: unhandled cart rela %u\n", rtype);
                    break;
                }
            }
        }
    }

    return true;
}
"""

# Check whether fc32_dynload is already present (either version)
if "fc32_dynload" in elf_c:
    # Find and replace any existing fc32_dynload block (look for our comment marker)
    SPIKE_I_MARKER = "/* -------------------------------------------------------------------------\n * fc32 Spike I: multi-library dynamic loader"
    SPIKE_C_MARKER = "/* -------------------------------------------------------------------------\n * fc32 Spike C: minimal dynamic loader"
    marker = None
    if SPIKE_I_MARKER in elf_c:
        marker = SPIKE_I_MARKER
    elif SPIKE_C_MARKER in elf_c:
        marker = SPIKE_C_MARKER

    if marker:
        # Find the function's closing brace
        func_start = "bool fc32_dynload(memory_t *mem, const uint8_t *cart_data, const char *libpath)\n{"
        func_idx = elf_c.index(func_start)
        depth = 0; i = func_idx; end_idx = -1
        while i < len(elf_c):
            if elf_c[i] == '{': depth += 1
            elif elf_c[i] == '}':
                depth -= 1
                if depth == 0: end_idx = i + 1; break
            i += 1
        comment_idx = elf_c.index(marker)
        elf_c = elf_c[:comment_idx] + DYNLOAD_IMPL.lstrip('\n') + elf_c[end_idx:]
        print("src/elf.c: replaced existing fc32_dynload with multi-library version.")
    else:
        print("src/elf.c: fc32_dynload present but marker not found — manual check needed.")
        sys.exit(1)
else:
    # Append after get_elf_first_byte
    elf_c = elf_c.replace(ANCHOR, ANCHOR + "\n" + DYNLOAD_IMPL, 1)
    print("src/elf.c: appended fc32_dynload (multi-library version).")

with open(ELF_C, "w") as f:
    f.write(elf_c)

# ---------------------------------------------------------------------------
# src/riscv.c
# ---------------------------------------------------------------------------

RISCV_C = "src/riscv.c"
with open(RISCV_C) as f:
    riscv_src = f.read()

if "fc32_dynload" not in riscv_src:
    RISCV_OLD = "    assert(elf_load(elf, attr->mem));"
    RISCV_NEW = """\
    assert(elf_load(elf, attr->mem));

    /* fc32 Spike I: load dynamic libraries if -L was given */
    { extern char *fc32_libpath;
      if (fc32_libpath)
          fc32_dynload(attr->mem, get_elf_first_byte(elf), fc32_libpath); }"""
    if RISCV_OLD not in riscv_src:
        sys.exit("ERROR: elf_load assert not found in src/riscv.c")
    riscv_src = riscv_src.replace(RISCV_OLD, RISCV_NEW, 1)
    with open(RISCV_C, "w") as f:
        f.write(riscv_src)
    print("src/riscv.c patched.")
else:
    print("src/riscv.c already patched, skipping.")

print("Done. Build with: make OUT=build -j$(nproc)")
