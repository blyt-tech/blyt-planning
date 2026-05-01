#!/usr/bin/env python3
"""
Patch rv32emu to support a minimal dynamic loader for Spike C.

Run from the rv32emu/ directory:
    python3 /spike-c/patches/apply-dynamic-loader.py

Effects:
  src/main.c  -- adds fc32_libpath global and -L <dir> option
  src/elf.c   -- adds fc32_dynload() function at end of file
  src/elf.h   -- adds fc32_dynload() declaration
  src/riscv.c -- calls fc32_dynload() after elf_load()

With fc32_libpath == NULL (no -L flag) the binary behaves identically to upstream.
"""

import sys

# ---------------------------------------------------------------------------
# src/main.c: add fc32_libpath global and -L option
# ---------------------------------------------------------------------------

MAIN_C = "src/main.c"

with open(MAIN_C) as f:
    main_src = f.read()

# 1. Extend optstr to include L:
MAIN_OPTSTR_OLD = 'static const char *optstr = "tgqmhpd:a:k:i:b:x:";'
MAIN_OPTSTR_NEW = ('/* fc32 Spike C: libpath for dynamic loader */\n'
                   'char *fc32_libpath = NULL;\n\n'
                   'static const char *optstr = "tgqmhpd:a:k:i:b:x:L:";')

if MAIN_OPTSTR_OLD not in main_src:
    sys.exit("ERROR: optstr line not found in src/main.c -- already patched?")

main_src = main_src.replace(MAIN_OPTSTR_OLD, MAIN_OPTSTR_NEW, 1)

# 2. Add case 'L': before case 'm':
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
    sys.exit("ERROR: case 'm': not found in src/main.c -- already patched?")

main_src = main_src.replace(MAIN_CASE_OLD, MAIN_CASE_NEW, 1)

with open(MAIN_C, "w") as f:
    f.write(main_src)

print("src/main.c patched.")

# ---------------------------------------------------------------------------
# src/elf.h: add fc32_dynload declaration
# ---------------------------------------------------------------------------

ELF_H = "src/elf.h"

with open(ELF_H) as f:
    elf_h = f.read()

ELF_H_OLD = "/* get the first byte of ELF raw data */\nuint8_t *get_elf_first_byte(elf_t *e);"
ELF_H_NEW = ("/* get the first byte of ELF raw data */\n"
             "uint8_t *get_elf_first_byte(elf_t *e);\n\n"
             "/* fc32 Spike C: minimal dynamic loader.\n"
             " * Loads DT_NEEDED shared libraries from libpath into guest memory\n"
             " * and resolves relocations so the cart's PLT/GOT entries work.\n"
             " * Returns true on success, false on fatal error.\n"
             " */\n"
             "bool fc32_dynload(memory_t *mem, const uint8_t *cart_data,\n"
             "                  const char *libpath);")

if ELF_H_OLD not in elf_h:
    sys.exit("ERROR: expected tail of elf.h not found -- already patched?")

elf_h = elf_h.replace(ELF_H_OLD, ELF_H_NEW, 1)

with open(ELF_H, "w") as f:
    f.write(elf_h)

print("src/elf.h patched.")

# ---------------------------------------------------------------------------
# src/elf.c: add fc32_dynload() at the end
# ---------------------------------------------------------------------------

ELF_C = "src/elf.c"

with open(ELF_C) as f:
    elf_c = f.read()

ELF_C_OLD = "uint8_t *get_elf_first_byte(elf_t *e)\n{\n    return (uint8_t *) e->raw_data;\n}"
if ELF_C_OLD not in elf_c:
    sys.exit("ERROR: get_elf_first_byte not found in src/elf.c -- already patched?")

DYNLOAD_CODE = r"""
/* -------------------------------------------------------------------------
 * fc32 Spike C: minimal dynamic loader
 * -------------------------------------------------------------------------
 * Called from rv_create() after the main ELF's PT_LOAD segments are mapped.
 * Finds DT_NEEDED libraries in libpath, maps them at FC32_LIB_LOAD_ADDR,
 * applies RELA relocations, and fixes the cart's GOT/PLT.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Load .so at 128 MiB — within the default 256 MiB guest memory, well above
 * the cart's BSS (~2 MiB), and well below the stack (~255 MiB). */
#define FC32_LIB_LOAD_ADDR 0x08000000u

static void fc32_write32(memory_t *mem, uint32_t addr, uint32_t val)
{
    memory_write(mem, addr, (const uint8_t *)&val, 4);
}

/* RISC-V 32-bit relocation types */
#define R_RISCV_NONE        0
#define R_RISCV_32          1
#define R_RISCV_RELATIVE    3
#define R_RISCV_COPY        4
#define R_RISCV_JUMP_SLOT   5
#define R_RISCV_GLOB_DAT    19

#define ELF32_R_SYM(i)  ((i) >> 8)
#define ELF32_R_TYPE(i) ((unsigned char)(i))

/* Dynamic section entry */
struct fc32_Dyn {
    int32_t  d_tag;
    uint32_t d_val;
};

/* RELA relocation entry */
struct fc32_Rela {
    uint32_t r_offset;
    uint32_t r_info;
    int32_t  r_addend;
};

/* DT_* tags */
#define DT_NULL     0
#define DT_NEEDED   1
#define DT_PLTRELSZ 2
#define DT_STRTAB   5
#define DT_SYMTAB   6
#define DT_RELA     7
#define DT_RELASZ   8
#define DT_SYMENT   11
#define DT_JMPREL   23

#define STB_GLOBAL  1
#define STB_WEAK    2
#define ELF32_ST_BIND(i) ((i) >> 4)

/* Simple flat symbol table */
#define FC32_SYM_MAX 2048
typedef struct { char name[64]; uint32_t addr; } fc32_sym_t;
static int fc32_sym_count;
static fc32_sym_t fc32_syms[FC32_SYM_MAX];

static void fc32_sym_add(const char *name, uint32_t addr)
{
    if (fc32_sym_count >= FC32_SYM_MAX) return;
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

/* Given an ELF file image, find the file offset for a given virtual address */
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

bool fc32_dynload(memory_t *mem, const uint8_t *cart_data, const char *libpath)
{
    fc32_sym_count = 0;

    const struct Elf32_Ehdr *ehdr = (const struct Elf32_Ehdr *)cart_data;

    /* --- locate PT_DYNAMIC in cart --- */
    uint32_t dyn_fileoff = 0, dyn_filesz = 0;
    for (int p = 0; p < ehdr->e_phnum; p++) {
        const struct Elf32_Phdr *ph = (const struct Elf32_Phdr *)
            (cart_data + ehdr->e_phoff + p * ehdr->e_phentsize);
        if (ph->p_type == PT_DYNAMIC) {
            dyn_fileoff = ph->p_offset;
            dyn_filesz  = ph->p_filesz;
            break;
        }
    }
    if (!dyn_fileoff) return true; /* static ELF, nothing to do */

    /* --- parse cart PT_DYNAMIC --- */
    const struct fc32_Dyn *dyn = (const struct fc32_Dyn *)(cart_data + dyn_fileoff);
    uint32_t cart_strtab_vaddr = 0;
    uint32_t cart_symtab_vaddr = 0;
    uint32_t cart_jmprel_vaddr = 0, cart_jmprel_sz = 0;
    uint32_t cart_rela_vaddr   = 0, cart_rela_sz   = 0;
    uint32_t cart_syment       = sizeof(struct Elf32_Sym);

    /* count DT_NEEDED entries first pass */
    int needed_count = 0;
    for (const struct fc32_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
        case DT_NEEDED:   needed_count++; break;
        case DT_STRTAB:   cart_strtab_vaddr = d->d_val; break;
        case DT_SYMTAB:   cart_symtab_vaddr = d->d_val; break;
        case DT_JMPREL:   cart_jmprel_vaddr = d->d_val; break;
        case DT_PLTRELSZ: cart_jmprel_sz    = d->d_val; break;
        case DT_RELA:     cart_rela_vaddr   = d->d_val; break;
        case DT_RELASZ:   cart_rela_sz      = d->d_val; break;
        case DT_SYMENT:   cart_syment       = d->d_val; break;
        }
    }

    /* resolve cart strtab file offset */
    uint32_t cart_strtab_off = fc32_vaddr_to_off(cart_data, ehdr, cart_strtab_vaddr);
    const char *cart_strtab = (const char *)(cart_data + cart_strtab_off);

    /* collect DT_NEEDED names */
    char needed[64][64];
    int ni = 0;
    for (const struct fc32_Dyn *d = dyn; d->d_tag != DT_NULL && ni < 64; d++) {
        if (d->d_tag == DT_NEEDED)
            strncpy(needed[ni++], cart_strtab + d->d_val, 63);
    }

    /* --- load each needed library --- */
    uint32_t lib_base = FC32_LIB_LOAD_ADDR;

    for (int i = 0; i < ni; i++) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s", libpath, needed[i]);

        FILE *lf = fopen(path, "rb");
        if (!lf) {
            fprintf(stderr, "fc32_dynload: cannot open %s\n", path);
            return false;
        }
        fseek(lf, 0, SEEK_END);
        long lsz = ftell(lf);
        fseek(lf, 0, SEEK_SET);
        uint8_t *lib_data = (uint8_t *)malloc((size_t)lsz);
        if (!lib_data) { fclose(lf); return false; }
        fread(lib_data, 1, (size_t)lsz, lf);
        fclose(lf);

        const struct Elf32_Ehdr *lhdr = (const struct Elf32_Ehdr *)lib_data;

        /* map PT_LOAD segments into guest memory at lib_base */
        for (int p = 0; p < lhdr->e_phnum; p++) {
            const struct Elf32_Phdr *ph = (const struct Elf32_Phdr *)
                (lib_data + lhdr->e_phoff + p * lhdr->e_phentsize);
            if (ph->p_type != PT_LOAD) continue;
            uint32_t ga = lib_base + ph->p_vaddr;
            if (ph->p_filesz)
                memory_write(mem, ga, lib_data + ph->p_offset, ph->p_filesz);
            if (ph->p_memsz > ph->p_filesz)
                memory_fill(mem, ga + ph->p_filesz, ph->p_memsz - ph->p_filesz, 0);
        }

        /* locate .dynsym and .dynstr via section headers */
        uint32_t dynsym_off = 0, dynsym_sz = 0, dynsym_ent = sizeof(struct Elf32_Sym);
        uint32_t dynstr_off = 0;

        for (int s = 0; s < lhdr->e_shnum; s++) {
            const struct Elf32_Shdr *sh = (const struct Elf32_Shdr *)
                (lib_data + lhdr->e_shoff + s * lhdr->e_shentsize);
            if (sh->sh_type == SHT_DYNSYM) {
                dynsym_off = sh->sh_offset;
                dynsym_sz  = sh->sh_size;
                if (sh->sh_entsize) dynsym_ent = sh->sh_entsize;
                /* dynstr is the linked section */
                if (sh->sh_link < (uint32_t)lhdr->e_shnum) {
                    const struct Elf32_Shdr *ss = (const struct Elf32_Shdr *)
                        (lib_data + lhdr->e_shoff + sh->sh_link * lhdr->e_shentsize);
                    dynstr_off = ss->sh_offset;
                }
            }
        }

        /* register exported symbols */
        if (dynsym_off && dynstr_off) {
            const char *ds = (const char *)(lib_data + dynstr_off);
            uint32_t nsym = dynsym_sz / dynsym_ent;
            for (uint32_t si = 0; si < nsym; si++) {
                const struct Elf32_Sym *sym = (const struct Elf32_Sym *)
                    (lib_data + dynsym_off + si * dynsym_ent);
                if (!sym->st_value) continue;
                int bind = ELF32_ST_BIND(sym->st_info);
                if (bind != STB_GLOBAL && bind != STB_WEAK) continue;
                const char *sn = ds + sym->st_name;
                if (!*sn) continue;
                fc32_sym_add(sn, lib_base + sym->st_value);
            }
        }

        /* apply .so RELA sections (e.g. .rela.dyn) */
        /* locate section name string table */
        const struct Elf32_Shdr *shstr_sh = NULL;
        if (lhdr->e_shstrndx < lhdr->e_shnum)
            shstr_sh = (const struct Elf32_Shdr *)
                (lib_data + lhdr->e_shoff + lhdr->e_shstrndx * lhdr->e_shentsize);

        for (int s = 0; s < lhdr->e_shnum; s++) {
            const struct Elf32_Shdr *sh = (const struct Elf32_Shdr *)
                (lib_data + lhdr->e_shoff + s * lhdr->e_shentsize);
            if (sh->sh_type != SHT_RELA) continue;

            const char *shname = "";
            if (shstr_sh)
                shname = (const char *)(lib_data + shstr_sh->sh_offset + sh->sh_name);

            /* skip .rela.plt — those are handled as jump slots below */
            int is_plt = (strstr(shname, "plt") != NULL);

            uint32_t rela_ent = sh->sh_entsize ? sh->sh_entsize : sizeof(struct fc32_Rela);
            uint32_t nrela    = sh->sh_size / rela_ent;
            const struct fc32_Rela *relas = (const struct fc32_Rela *)(lib_data + sh->sh_offset);

            const struct Elf32_Sym *lib_dsym = dynsym_off ?
                (const struct Elf32_Sym *)(lib_data + dynsym_off) : NULL;
            const char *lib_ds = dynstr_off ? (const char *)(lib_data + dynstr_off) : NULL;

            for (uint32_t ri = 0; ri < nrela; ri++) {
                uint32_t r_off  = relas[ri].r_offset;
                uint32_t r_info = relas[ri].r_info;
                int32_t  r_add  = relas[ri].r_addend;
                uint32_t rtype  = ELF32_R_TYPE(r_info);
                uint32_t rsym   = ELF32_R_SYM(r_info);
                uint32_t guest_off = lib_base + r_off;

                switch (rtype) {
                case R_RISCV_RELATIVE:
                    fc32_write32(mem, guest_off, lib_base + (uint32_t)r_add);
                    break;
                case R_RISCV_32:
                    if (rsym && lib_dsym && lib_ds) {
                        const struct Elf32_Sym *sym = (const struct Elf32_Sym *)
                            ((const uint8_t *)lib_dsym + rsym * dynsym_ent);
                        uint32_t sv = fc32_sym_find(lib_ds + sym->st_name);
                        if (!sv) sv = lib_base + sym->st_value;
                        fc32_write32(mem, guest_off, sv + (uint32_t)r_add);
                    } else {
                        fc32_write32(mem, guest_off, lib_base + (uint32_t)r_add);
                    }
                    break;
                case R_RISCV_JUMP_SLOT:
                    if (is_plt && rsym && lib_dsym && lib_ds) {
                        const struct Elf32_Sym *sym = (const struct Elf32_Sym *)
                            ((const uint8_t *)lib_dsym + rsym * dynsym_ent);
                        uint32_t sv = fc32_sym_find(lib_ds + sym->st_name);
                        if (!sv) sv = lib_base + sym->st_value;
                        fc32_write32(mem, guest_off, sv);
                    }
                    break;
                case R_RISCV_GLOB_DAT:
                    if (rsym && lib_dsym && lib_ds) {
                        const struct Elf32_Sym *sym = (const struct Elf32_Sym *)
                            ((const uint8_t *)lib_dsym + rsym * dynsym_ent);
                        uint32_t sv = fc32_sym_find(lib_ds + sym->st_name);
                        if (!sv) sv = lib_base + sym->st_value;
                        fc32_write32(mem, guest_off, sv + (uint32_t)r_add);
                    }
                    break;
                case R_RISCV_NONE:
                    break;
                default:
                    fprintf(stderr,
                        "fc32_dynload: unhandled .so rela type %u at vaddr 0x%x\n",
                        rtype, r_off);
                    break;
                }
            }
        }

        free(lib_data);
    }

    /* --- resolve cart's PLT relocations (DT_JMPREL) --- */
    uint32_t cart_symtab_off = fc32_vaddr_to_off(cart_data, ehdr, cart_symtab_vaddr);
    const struct Elf32_Sym *cart_dsym = cart_symtab_off ?
        (const struct Elf32_Sym *)(cart_data + cart_symtab_off) : NULL;

    if (cart_jmprel_vaddr && cart_jmprel_sz && cart_dsym) {
        uint32_t jmprel_off = fc32_vaddr_to_off(cart_data, ehdr, cart_jmprel_vaddr);
        if (jmprel_off) {
            uint32_t nrela = cart_jmprel_sz / sizeof(struct fc32_Rela);
            const struct fc32_Rela *relas =
                (const struct fc32_Rela *)(cart_data + jmprel_off);
            for (uint32_t ri = 0; ri < nrela; ri++) {
                uint32_t r_off  = relas[ri].r_offset;
                uint32_t r_info = relas[ri].r_info;
                uint32_t rtype  = ELF32_R_TYPE(r_info);
                uint32_t rsym   = ELF32_R_SYM(r_info);
                if ((rtype != R_RISCV_JUMP_SLOT && rtype != R_RISCV_GLOB_DAT) || !rsym)
                    continue;
                const struct Elf32_Sym *sym = (const struct Elf32_Sym *)
                    ((const uint8_t *)cart_dsym + rsym * cart_syment);
                const char *sname = cart_strtab + sym->st_name;
                uint32_t sa = fc32_sym_find(sname);
                if (sa)
                    fc32_write32(mem, r_off, sa);
                else
                    fprintf(stderr, "fc32_dynload: unresolved PLT symbol '%s'\n", sname);
            }
        }
    }

    /* --- resolve cart's DT_RELA (non-PLT) relocations --- */
    if (cart_rela_vaddr && cart_rela_sz && cart_dsym) {
        uint32_t rela_off = fc32_vaddr_to_off(cart_data, ehdr, cart_rela_vaddr);
        if (rela_off) {
            uint32_t nrela = cart_rela_sz / sizeof(struct fc32_Rela);
            const struct fc32_Rela *relas =
                (const struct fc32_Rela *)(cart_data + rela_off);
            for (uint32_t ri = 0; ri < nrela; ri++) {
                uint32_t r_off  = relas[ri].r_offset;
                uint32_t r_info = relas[ri].r_info;
                int32_t  r_add  = relas[ri].r_addend;
                uint32_t rtype  = ELF32_R_TYPE(r_info);
                uint32_t rsym   = ELF32_R_SYM(r_info);
                switch (rtype) {
                case R_RISCV_GLOB_DAT:
                case R_RISCV_JUMP_SLOT:
                    if (rsym && cart_dsym) {
                        const struct Elf32_Sym *sym = (const struct Elf32_Sym *)
                            ((const uint8_t *)cart_dsym + rsym * cart_syment);
                        const char *sname = cart_strtab + sym->st_name;
                        uint32_t sa = fc32_sym_find(sname);
                        if (sa) fc32_write32(mem, r_off, sa + (uint32_t)r_add);
                        else fprintf(stderr,
                            "fc32_dynload: unresolved RELA symbol '%s'\n", sname);
                    }
                    break;
                case R_RISCV_RELATIVE:
                    /* cart is EXEC, load base = 0 */
                    fc32_write32(mem, r_off, (uint32_t)r_add);
                    break;
                case R_RISCV_NONE:
                    break;
                default:
                    fprintf(stderr,
                        "fc32_dynload: unhandled cart rela type %u\n", rtype);
                    break;
                }
            }
        }
    }

    return true;
}
"""

elf_c = elf_c.replace(ELF_C_OLD, ELF_C_OLD + "\n" + DYNLOAD_CODE, 1)

with open(ELF_C, "w") as f:
    f.write(elf_c)

print("src/elf.c patched.")

# ---------------------------------------------------------------------------
# src/riscv.c: call fc32_dynload() after elf_load()
# ---------------------------------------------------------------------------

RISCV_C = "src/riscv.c"

with open(RISCV_C) as f:
    riscv_src = f.read()

RISCV_OLD = "    assert(elf_load(elf, attr->mem));"
RISCV_NEW = """\
    assert(elf_load(elf, attr->mem));

    /* fc32 Spike C: load dynamic libraries if -L was given */
    { extern char *fc32_libpath;
      if (fc32_libpath)
          fc32_dynload(attr->mem, get_elf_first_byte(elf), fc32_libpath); }"""

if RISCV_OLD not in riscv_src:
    sys.exit("ERROR: elf_load assert not found in src/riscv.c -- already patched?")

riscv_src = riscv_src.replace(RISCV_OLD, RISCV_NEW, 1)

with open(RISCV_C, "w") as f:
    f.write(riscv_src)

print("src/riscv.c patched.")
print("Done. Build with: make OUT=build -j$(nproc)")
