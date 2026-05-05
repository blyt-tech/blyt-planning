/* Spike J — GDB remote serial protocol stub.
 *
 * Lives inside rv32emu (host process; same process, separate TCP socket
 * from DAP). Speaks at the guest CPU level — register read/write, memory
 * read, single-step, breakpoints. Reports the pre-mapped library load
 * addresses to GDB via qXfer:libraries-svr4:read (NOT qOffsets, which can
 * only describe one ELF).
 *
 * Integration: rv32emu's main loop calls fc_gdb_stub_check_break() before
 * each instruction dispatch. If a breakpoint or vCont stepping condition
 * fires, the loop pauses and waits for a GDB command via fc_gdb_stub_step()
 * which blocks until the client sends c/s/etc.
 */

#ifndef SPIKE_J_GDB_STUB_H
#define SPIKE_J_GDB_STUB_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Library-list-svr4 entry. The synthetic library list is built from
 * fc32_dynload's in-memory map. */
typedef struct {
    const char *path;     /* host filesystem path of the .so */
    uint32_t    l_addr;   /* load base in guest memory */
    uint32_t    l_ld;     /* runtime address of dynamic section */
} fc_gdb_library_t;

typedef struct {
    /* Cart binary path (used for qXfer:exec-file:read). */
    const char       *exec_path;
    /* Pre-mapped library list. */
    const fc_gdb_library_t *libraries;
    int               n_libraries;
} fc_gdb_layout_t;

/* CPU state callbacks — provided by rv32emu so the stub can fulfil g/G/m/M
 * packets without depending on the emulator's internal headers. */
typedef struct {
    /* Read/write the 32 RV32 GPRs + PC into a 33×4-byte buffer. */
    void (*read_regs)(uint8_t out[33 * 4]);
    void (*write_regs)(const uint8_t in[33 * 4]);
    /* Read/write guest memory. Returns bytes actually transferred. */
    uint32_t (*read_mem)(uint32_t addr, uint8_t *dst, uint32_t n);
    uint32_t (*write_mem)(uint32_t addr, const uint8_t *src, uint32_t n);
    /* Patch / unpatch a software breakpoint via ebreak (0x00100073). */
    int (*set_breakpoint)(uint32_t addr);
    int (*clear_breakpoint)(uint32_t addr);
    /* Custom reload — invoked on qFc32:reload. */
    int (*reload_cart)(const char *path);
} fc_gdb_cpu_ops_t;

/* Configure the layout once at rv32emu startup; the stub returns it
 * verbatim in qXfer:libraries-svr4:read. */
void fc_gdb_stub_set_layout(const fc_gdb_layout_t *layout);

/* Spawn the listener pthread on 127.0.0.1:port. Returns 0 on success. */
int  fc_gdb_stub_listen(int port, const fc_gdb_cpu_ops_t *ops);

/* Stop the server. */
void fc_gdb_stub_shutdown(void);

/* Called by rv32emu's instruction loop before each dispatch. Returns 1 if
 * the loop should pause and call fc_gdb_stub_step() to wait for a client
 * command (breakpoint hit or single-step boundary). */
int  fc_gdb_stub_check_break(uint32_t pc);

/* Block until the client sends a continue / step / etc. Returns the next
 * action the CPU loop should take: 0 = continue freely, 1 = single-step
 * one instruction then re-check, 2 = exit. */
int  fc_gdb_stub_step(void);

/* Notify the stub that a reload happened (fired from rv32emu's
 * fc32_dynload after a successful qFc32:reload). The stub emits a
 * T05library:; stop-reply. */
void fc_gdb_stub_notify_reload(void);

#ifdef __cplusplus
}
#endif

#endif /* SPIKE_J_GDB_STUB_H */
