/* Spike J — GDB remote serial protocol stub.
 *
 * Standalone TU; integrate into rv32emu by linking against the .o and
 * registering the cpu_ops at startup. The protocol surface targets
 * gdb-multiarch 14.x's riscv:rv32 mode and VS Code's "Native Debug" client.
 *
 * Packet framing: $<payload>#<csum> with `+`/`-` ack. Standard surface:
 *   qSupported, qXfer:exec-file:read, qXfer:libraries-svr4:read,
 *   ?, g, G, m/M, vCont, Z0/z0, vCont?
 *   Custom: qFc32:reload:<path>
 */

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "gdb_stub.h"

#define MAX_PACKET     (1 << 16)
#define MAX_BREAKS     128

typedef struct {
    pthread_mutex_t mu;
    pthread_t       thr;
    int             listen_fd;
    int             client_fd;
    int             running;

    fc_gdb_layout_t  layout;
    fc_gdb_cpu_ops_t ops;
    int              ops_set;

    /* Software breakpoints — set/clear-tracked. The actual ebreak patches
     * are applied via ops.set_breakpoint / ops.clear_breakpoint. */
    uint32_t         breaks[MAX_BREAKS];
    int              n_breaks;

    /* Stop / step state. */
    int              halted;          /* 1 = waiting for vCont */
    int              pending_action;  /* 0 = continue, 1 = step, 2 = exit */
    int              step_requested;
    int              reload_pending;
} gdb_state_t;

static gdb_state_t g_gdb = { .mu = PTHREAD_MUTEX_INITIALIZER };

/* ── packet I/O ─────────────────────────────────────────────────────────── */

static uint8_t hexnyb(int n) {
    static const char *t = "0123456789abcdef";
    return (uint8_t)t[n & 0xf];
}

static int from_hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void hex_encode(const uint8_t *src, size_t n, char *dst) {
    for (size_t i = 0; i < n; i++) {
        dst[2*i]     = (char)hexnyb(src[i] >> 4);
        dst[2*i + 1] = (char)hexnyb(src[i] & 0xf);
    }
    dst[2*n] = 0;
}

static int hex_decode(const char *src, uint8_t *dst, size_t cap) {
    size_t i = 0;
    while (src[2*i] && src[2*i + 1] && i < cap) {
        int hi = from_hex(src[2*i]);
        int lo = from_hex(src[2*i + 1]);
        if (hi < 0 || lo < 0) break;
        dst[i] = (uint8_t)((hi << 4) | lo);
        i++;
    }
    return (int)i;
}

static int send_pkt(const char *payload) {
    if (g_gdb.client_fd < 0) return -1;
    size_t len = strlen(payload);
    char *buf = malloc(len + 16);
    if (!buf) return -1;
    int csum = 0;
    for (size_t i = 0; i < len; i++) csum += (uint8_t)payload[i];
    csum &= 0xff;
    int n = snprintf(buf, len + 16, "$%s#%02x", payload, csum);
    pthread_mutex_lock(&g_gdb.mu);
    int rc = (int)send(g_gdb.client_fd, buf, (size_t)n, 0);
    pthread_mutex_unlock(&g_gdb.mu);
    free(buf);
    /* Read ack — `+` or `-`; we ignore retransmits. */
    char ack;
    recv(g_gdb.client_fd, &ack, 1, 0);
    return rc;
}

static int read_pkt(int fd, char *buf, size_t cap) {
    /* Wait for `$`, then read until `#`, then 2-byte hex csum. */
    char c;
    while (1) {
        ssize_t r = recv(fd, &c, 1, 0);
        if (r <= 0) return -1;
        if (c == '$') break;
        if (c == 0x03) {
            /* Ctrl-C interrupt — not handled in spike, ignore. */
            continue;
        }
    }
    size_t i = 0;
    while (i + 1 < cap) {
        ssize_t r = recv(fd, &c, 1, 0);
        if (r <= 0) return -1;
        if (c == '#') break;
        buf[i++] = c;
    }
    buf[i] = 0;
    /* Drain csum. */
    char csum[2];
    if (recv(fd, csum, 2, 0) != 2) return -1;
    /* Send ack. */
    char ack = '+';
    send(fd, &ack, 1, 0);
    return (int)i;
}

/* ── handlers ──────────────────────────────────────────────────────────── */

static void handle_qSupported(const char *args, char *out, size_t cap) {
    (void)args;
    snprintf(out, cap,
             "PacketSize=4000;qXfer:libraries-svr4:read+;"
             "qXfer:exec-file:read+;swbreak+;vContSupported+");
}

static void handle_qXfer_exec_file(const char *args, char *out, size_t cap) {
    /* args = "annex:offset,length" — annex is empty/process-id. */
    const char *p = strchr(args, ':');
    if (!p) { snprintf(out, cap, "E01"); return; }
    p++;
    uint32_t off = (uint32_t)strtoul(p, (char **)&p, 16);
    if (*p == ',') p++;
    uint32_t len = (uint32_t)strtoul(p, NULL, 16);
    const char *path = g_gdb.layout.exec_path ? g_gdb.layout.exec_path : "";
    size_t plen = strlen(path);
    if (off >= plen) { snprintf(out, cap, "l"); return; }
    uint32_t avail = (uint32_t)(plen - off);
    if (len > avail) len = avail;
    char prefix = (off + len >= plen) ? 'l' : 'm';
    if (cap < (size_t)len + 2) { snprintf(out, cap, "E02"); return; }
    out[0] = prefix;
    memcpy(out + 1, path + off, len);
    out[1 + len] = 0;
}

static void handle_qXfer_libraries(const char *args, char *out, size_t cap) {
    const char *p = strchr(args, ':');
    if (!p) { snprintf(out, cap, "E01"); return; }
    p++;
    uint32_t off = (uint32_t)strtoul(p, (char **)&p, 16);
    if (*p == ',') p++;
    uint32_t len = (uint32_t)strtoul(p, NULL, 16);
    /* Build the synthetic library list as XML. lmid is the link-map ID —
     * 0 for the default namespace; gdb requires it on each <library>. */
    char xml[4096];
    int xn = snprintf(xml, sizeof xml,
                      "<library-list-svr4 version=\"1.0\" main-lm=\"0x0\">");
    for (int i = 0; i < g_gdb.layout.n_libraries; i++) {
        const fc_gdb_library_t *lib = &g_gdb.layout.libraries[i];
        xn += snprintf(xml + xn, sizeof(xml) - xn,
                       "<library name=\"%s\" lm=\"0x%x\" "
                       "l_addr=\"0x%x\" l_ld=\"0x%x\" lmid=\"0x0\"/>",
                       lib->path, lib->l_addr, lib->l_addr, lib->l_ld);
    }
    xn += snprintf(xml + xn, sizeof(xml) - xn, "</library-list-svr4>");
    if (off >= (uint32_t)xn) { snprintf(out, cap, "l"); return; }
    uint32_t avail = (uint32_t)xn - off;
    if (len > avail) len = avail;
    char prefix = (off + len >= (uint32_t)xn) ? 'l' : 'm';
    if (cap < len + 2u) { snprintf(out, cap, "E02"); return; }
    out[0] = prefix;
    memcpy(out + 1, xml + off, len);
    out[1 + len] = 0;
}

static void handle_g(char *out, size_t cap) {
    if (!g_gdb.ops.read_regs) { snprintf(out, cap, "E01"); return; }
    uint8_t regs[33 * 4];
    g_gdb.ops.read_regs(regs);
    if (cap < sizeof(regs) * 2 + 1) { snprintf(out, cap, "E02"); return; }
    hex_encode(regs, sizeof regs, out);
}

static void handle_G(const char *args, char *out, size_t cap) {
    if (!g_gdb.ops.write_regs) { snprintf(out, cap, "E01"); return; }
    uint8_t regs[33 * 4];
    if (hex_decode(args, regs, sizeof regs) != sizeof regs) {
        snprintf(out, cap, "E02"); return;
    }
    g_gdb.ops.write_regs(regs);
    snprintf(out, cap, "OK");
}

static void handle_m(const char *args, char *out, size_t cap) {
    /* m<addr>,<len> */
    char *end;
    uint32_t addr = (uint32_t)strtoul(args, &end, 16);
    if (*end != ',') { snprintf(out, cap, "E01"); return; }
    uint32_t len = (uint32_t)strtoul(end + 1, NULL, 16);
    if (!g_gdb.ops.read_mem) { snprintf(out, cap, "E02"); return; }
    if (len > 4096 || cap < len * 2 + 1) { snprintf(out, cap, "E03"); return; }
    uint8_t *buf = malloc(len);
    if (!buf) { snprintf(out, cap, "E04"); return; }
    uint32_t got = g_gdb.ops.read_mem(addr, buf, len);
    hex_encode(buf, got, out);
    free(buf);
}

static void handle_M(const char *args, char *out, size_t cap) {
    char *end;
    uint32_t addr = (uint32_t)strtoul(args, &end, 16);
    if (*end != ',') { snprintf(out, cap, "E01"); return; }
    uint32_t len = (uint32_t)strtoul(end + 1, &end, 16);
    if (*end != ':') { snprintf(out, cap, "E02"); return; }
    end++;
    if (!g_gdb.ops.write_mem) { snprintf(out, cap, "E03"); return; }
    uint8_t *buf = malloc(len);
    if (!buf) { snprintf(out, cap, "E04"); return; }
    if (hex_decode(end, buf, len) != (int)len) {
        free(buf); snprintf(out, cap, "E05"); return;
    }
    g_gdb.ops.write_mem(addr, buf, len);
    free(buf);
    snprintf(out, cap, "OK");
}

static void handle_Z0(const char *args, char *out, size_t cap) {
    char *end;
    uint32_t addr = (uint32_t)strtoul(args, &end, 16);
    if (g_gdb.n_breaks >= MAX_BREAKS) { snprintf(out, cap, "E01"); return; }
    if (g_gdb.ops.set_breakpoint) g_gdb.ops.set_breakpoint(addr);
    g_gdb.breaks[g_gdb.n_breaks++] = addr;
    snprintf(out, cap, "OK");
}

static void handle_z0(const char *args, char *out, size_t cap) {
    char *end;
    uint32_t addr = (uint32_t)strtoul(args, &end, 16);
    int kept = 0;
    for (int i = 0; i < g_gdb.n_breaks; i++) {
        if (g_gdb.breaks[i] != addr) {
            if (kept != i) g_gdb.breaks[kept] = g_gdb.breaks[i];
            kept++;
        }
    }
    g_gdb.n_breaks = kept;
    if (g_gdb.ops.clear_breakpoint) g_gdb.ops.clear_breakpoint(addr);
    snprintf(out, cap, "OK");
}

static void handle_vCont_q(char *out, size_t cap) {
    snprintf(out, cap, "vCont;c;C;s;S");
}

static void handle_vCont(const char *args, char *out, size_t cap) {
    /* args looks like ";c" or ";s" or ";c:1" etc. */
    if (strchr(args, 's')) {
        pthread_mutex_lock(&g_gdb.mu);
        g_gdb.pending_action = 1;
        g_gdb.halted = 0;
        pthread_mutex_unlock(&g_gdb.mu);
    } else {
        pthread_mutex_lock(&g_gdb.mu);
        g_gdb.pending_action = 0;
        g_gdb.halted = 0;
        pthread_mutex_unlock(&g_gdb.mu);
    }
    /* No immediate response — emitted when CPU stops again. */
    out[0] = 0;
}

static void handle_qFc32_reload(const char *args, char *out, size_t cap) {
    if (!g_gdb.ops.reload_cart) { snprintf(out, cap, "E01"); return; }
    int rc = g_gdb.ops.reload_cart(args);
    if (rc != 0) { snprintf(out, cap, "E02"); return; }
    snprintf(out, cap, "OK");
    g_gdb.reload_pending = 1;
}

/* ── dispatch ──────────────────────────────────────────────────────────── */

static void handle_packet(const char *pkt) {
    static char out[MAX_PACKET];
    out[0] = 0;

    if (pkt[0] == 'q' && strncmp(pkt, "qSupported", 10) == 0) {
        handle_qSupported(pkt + 10, out, sizeof out);
    } else if (strncmp(pkt, "qXfer:exec-file:read:", 21) == 0) {
        handle_qXfer_exec_file(pkt + 21, out, sizeof out);
    } else if (strncmp(pkt, "qXfer:libraries-svr4:read:", 26) == 0) {
        handle_qXfer_libraries(pkt + 26, out, sizeof out);
    } else if (pkt[0] == '?') {
        snprintf(out, sizeof out, "T05");
    } else if (pkt[0] == 'g' && pkt[1] == 0) {
        handle_g(out, sizeof out);
    } else if (pkt[0] == 'G') {
        handle_G(pkt + 1, out, sizeof out);
    } else if (pkt[0] == 'm') {
        handle_m(pkt + 1, out, sizeof out);
    } else if (pkt[0] == 'M') {
        handle_M(pkt + 1, out, sizeof out);
    } else if (strncmp(pkt, "Z0,", 3) == 0) {
        handle_Z0(pkt + 3, out, sizeof out);
    } else if (strncmp(pkt, "z0,", 3) == 0) {
        handle_z0(pkt + 3, out, sizeof out);
    } else if (strcmp(pkt, "vCont?") == 0) {
        handle_vCont_q(out, sizeof out);
    } else if (strncmp(pkt, "vCont", 5) == 0) {
        handle_vCont(pkt + 5, out, sizeof out);
    } else if (strncmp(pkt, "qFc32:reload:", 13) == 0) {
        handle_qFc32_reload(pkt + 13, out, sizeof out);
    } else if (strcmp(pkt, "qAttached") == 0) {
        snprintf(out, sizeof out, "1");
    } else if (strncmp(pkt, "qC", 2) == 0) {
        snprintf(out, sizeof out, "QC1");
    } else {
        /* Unsupported — empty response per spec. */
        out[0] = 0;
    }
    send_pkt(out);
}

/* ── server thread ─────────────────────────────────────────────────────── */

static void *gdb_thread_main(void *arg) {
    (void)arg;
    static char pkt[MAX_PACKET];
    while (g_gdb.running) {
        struct sockaddr_in cli;
        socklen_t cl = sizeof cli;
        int fd = accept(g_gdb.listen_fd, (struct sockaddr *)&cli, &cl);
        if (fd < 0) { if (!g_gdb.running) break; continue; }
        pthread_mutex_lock(&g_gdb.mu);
        g_gdb.client_fd = fd;
        g_gdb.halted = 1;
        pthread_mutex_unlock(&g_gdb.mu);
        while (g_gdb.running) {
            int n = read_pkt(fd, pkt, sizeof pkt);
            if (n < 0) break;
            handle_packet(pkt);
            if (g_gdb.reload_pending) {
                send_pkt("T05library:;");
                g_gdb.reload_pending = 0;
            }
        }
        pthread_mutex_lock(&g_gdb.mu);
        close(fd);
        g_gdb.client_fd = -1;
        g_gdb.halted = 0;
        pthread_mutex_unlock(&g_gdb.mu);
    }
    return NULL;
}

/* ── public API ────────────────────────────────────────────────────────── */

void fc_gdb_stub_set_layout(const fc_gdb_layout_t *layout) {
    g_gdb.layout = *layout;
}

int fc_gdb_stub_listen(int port, const fc_gdb_cpu_ops_t *ops) {
    if (ops) { g_gdb.ops = *ops; g_gdb.ops_set = 1; }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) { close(fd); return -1; }
    if (listen(fd, 1) < 0) { close(fd); return -1; }
    g_gdb.listen_fd = fd;
    g_gdb.client_fd = -1;
    g_gdb.running = 1;
    if (pthread_create(&g_gdb.thr, NULL, gdb_thread_main, NULL) != 0) {
        close(fd); g_gdb.running = 0; return -1;
    }
    return 0;
}

void fc_gdb_stub_shutdown(void) {
    if (!g_gdb.running) return;
    g_gdb.running = 0;
    if (g_gdb.client_fd >= 0) shutdown(g_gdb.client_fd, SHUT_RDWR);
    if (g_gdb.listen_fd >= 0) shutdown(g_gdb.listen_fd, SHUT_RDWR);
    pthread_join(g_gdb.thr, NULL);
    if (g_gdb.client_fd >= 0) close(g_gdb.client_fd);
    if (g_gdb.listen_fd >= 0) close(g_gdb.listen_fd);
}

int fc_gdb_stub_check_break(uint32_t pc) {
    for (int i = 0; i < g_gdb.n_breaks; i++) {
        if (g_gdb.breaks[i] == pc) return 1;
    }
    return 0;
}

int fc_gdb_stub_step(void) {
    /* Notify client we're halted. */
    pthread_mutex_lock(&g_gdb.mu);
    g_gdb.halted = 1;
    pthread_mutex_unlock(&g_gdb.mu);
    send_pkt("T05swbreak:;");
    while (g_gdb.running) {
        pthread_mutex_lock(&g_gdb.mu);
        int halted = g_gdb.halted;
        pthread_mutex_unlock(&g_gdb.mu);
        if (!halted) break;
        usleep(2000);
    }
    return g_gdb.pending_action;
}

void fc_gdb_stub_notify_reload(void) {
    g_gdb.reload_pending = 1;
}
