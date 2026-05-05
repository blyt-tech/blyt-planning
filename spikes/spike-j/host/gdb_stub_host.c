/* Spike J — standalone GDB stub harness.
 *
 * Drives gdb_stub.c with synthetic CPU/memory state so the protocol surface
 * (qSupported / qXfer:libraries-svr4:read / Z0 / vCont) can be tested
 * end-to-end against gdb-multiarch without rv32emu in the loop.
 *
 * Stage 3 step 11's `(gdb) info sharedlibrary` assertion only requires the
 * libraries-svr4 reply to be syntactically correct and contain both
 * libraries at the expected addresses. The CPU register / memory protocol
 * is exercised by gdb_test.py against a paused-at-entry stub.
 *
 * Mock CPU: a 33×4 byte register file, 64 KiB RAM mapped at the cart's
 * load address, halted at PC = entry point. Single-step is a no-op
 * (PC + 4); breakpoints record the address and immediately fire.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "gdb_stub.h"

/* Parse an ELF32 to find the file VMA of the DYNAMIC segment. GDB uses
 * `l_addr = l_ld - file_dyn_vma` to compute the load base, so getting
 * file_dyn_vma right is load-bearing for the libraries-svr4 reply.
 * Returns the VMA on success, or 0 on failure (gdb will then misinterpret
 * the load base — see the plan's risk note on l_ld). */
static uint32_t elf32_dyn_vma(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint8_t hdr[52];
    if (fread(hdr, 1, sizeof hdr, f) != sizeof hdr ||
        memcmp(hdr, "\x7f""ELF\x01", 5) != 0) {
        fclose(f); return 0;
    }
    uint32_t e_phoff   = *(uint32_t *)(hdr + 28);
    uint16_t e_phentsz = *(uint16_t *)(hdr + 42);
    uint16_t e_phnum   = *(uint16_t *)(hdr + 44);
    if (fseek(f, (long)e_phoff, SEEK_SET) != 0) { fclose(f); return 0; }
    uint32_t result = 0;
    for (int i = 0; i < e_phnum; i++) {
        uint8_t ph[32];
        if (fread(ph, 1, e_phentsz, f) != e_phentsz) break;
        uint32_t p_type  = *(uint32_t *)(ph + 0);
        uint32_t p_vaddr = *(uint32_t *)(ph + 8);
        if (p_type == 2 /* PT_DYNAMIC */) { result = p_vaddr; break; }
    }
    fclose(f);
    return result;
}

#define MOCK_RAM_BASE  0x00010000
#define MOCK_RAM_SIZE  (64 * 1024)

static uint8_t mock_regs[33 * 4];
static uint8_t mock_ram[MOCK_RAM_SIZE];

static void mock_read_regs(uint8_t out[33 * 4]) {
    memcpy(out, mock_regs, sizeof mock_regs);
}
static void mock_write_regs(const uint8_t in[33 * 4]) {
    memcpy(mock_regs, in, sizeof mock_regs);
}
static uint32_t mock_read_mem(uint32_t addr, uint8_t *dst, uint32_t n) {
    if (addr < MOCK_RAM_BASE || addr - MOCK_RAM_BASE >= MOCK_RAM_SIZE) {
        memset(dst, 0, n);
        return n;
    }
    uint32_t off = addr - MOCK_RAM_BASE;
    uint32_t avail = MOCK_RAM_SIZE - off;
    uint32_t k = n < avail ? n : avail;
    memcpy(dst, mock_ram + off, k);
    return k;
}
static uint32_t mock_write_mem(uint32_t addr, const uint8_t *src, uint32_t n) {
    if (addr < MOCK_RAM_BASE || addr - MOCK_RAM_BASE >= MOCK_RAM_SIZE) return 0;
    uint32_t off = addr - MOCK_RAM_BASE;
    uint32_t avail = MOCK_RAM_SIZE - off;
    uint32_t k = n < avail ? n : avail;
    memcpy(mock_ram + off, src, k);
    return k;
}
static int mock_set_break(uint32_t addr) { (void)addr; return 0; }
static int mock_clear_break(uint32_t addr) { (void)addr; return 0; }
static int mock_reload(const char *path) { (void)path; return 0; }

int main(int argc, char **argv) {
    int port = 1234;
    const char *exec_path = "/spike-j/cases/case_b/cart_b";
    const char *libconsole_path    = "/spike-j/lib/libconsole.so";
    const char *libconsolelua_path = "/spike-j/lib/libconsolelua.so";
    uint32_t libconsole_base    = 0x08000000;
    uint32_t libconsolelua_base = 0x08010000;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--exec") && i + 1 < argc) exec_path = argv[++i];
        else if (!strcmp(argv[i], "--libconsole") && i + 1 < argc) libconsole_path = argv[++i];
        else if (!strcmp(argv[i], "--libconsolelua") && i + 1 < argc) libconsolelua_path = argv[++i];
    }

    fc_gdb_library_t libs[] = {
        { libconsole_path,    libconsole_base,
          libconsole_base    + elf32_dyn_vma(libconsole_path) },
        { libconsolelua_path, libconsolelua_base,
          libconsolelua_base + elf32_dyn_vma(libconsolelua_path) },
    };
    fprintf(stderr, "libconsole.so:    l_addr=0x%x l_ld=0x%x\n",
            libs[0].l_addr, libs[0].l_ld);
    fprintf(stderr, "libconsolelua.so: l_addr=0x%x l_ld=0x%x\n",
            libs[1].l_addr, libs[1].l_ld);
    fc_gdb_layout_t layout = {
        .exec_path = exec_path,
        .libraries = libs,
        .n_libraries = 2,
    };
    fc_gdb_cpu_ops_t ops = {
        .read_regs = mock_read_regs,
        .write_regs = mock_write_regs,
        .read_mem = mock_read_mem,
        .write_mem = mock_write_mem,
        .set_breakpoint = mock_set_break,
        .clear_breakpoint = mock_clear_break,
        .reload_cart = mock_reload,
    };

    /* PC (reg 32) starts at the cart's entry. */
    uint32_t entry = MOCK_RAM_BASE;
    memcpy(mock_regs + 32 * 4, &entry, 4);

    fc_gdb_stub_set_layout(&layout);
    if (fc_gdb_stub_listen(port, &ops) != 0) {
        fprintf(stderr, "gdb_stub listen %d failed\n", port);
        return 1;
    }
    fprintf(stderr, "gdb_stub listening on 127.0.0.1:%d\n", port);
    /* Block until SIGINT/SIGTERM. */
    signal(SIGPIPE, SIG_IGN);
    pause();
    fc_gdb_stub_shutdown();
    return 0;
}
