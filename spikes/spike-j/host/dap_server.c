/* Spike J — DAP server implementation.
 *
 * Minimal Debug Adapter Protocol surface for VS Code's built-in DAP client:
 *   initialize, launch, configurationDone
 *   setBreakpoints (verified-line response)
 *   threads, stackTrace, scopes, variables
 *   continue, next, stepIn, stepOut, pause
 *   disconnect, terminate
 *   custom hot_reload
 *
 * Wire framing: HTTP-like "Content-Length: N\r\n\r\n<json>" per spec.
 * State: single client, single Lua thread (no multi-threading in cart yet).
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

#include "lua.h"
#include "lauxlib.h"

#include "../lib/master_hook.h"
#include "dap_server.h"

#define MAX_BREAKPOINTS 256
#define MAX_FRAMES      64
#define MAX_VARS        64
#define MAX_SOURCE_PATH 1024
#define MAX_MSG         (1 << 20)

typedef struct {
    char     source[MAX_SOURCE_PATH];
    int      line;
    int      verified;
    int      id;       /* DAP-side unique id */
} dap_breakpoint_t;

typedef struct {
    pthread_mutex_t mu;
    pthread_t       thr;
    int             listen_fd;
    int             client_fd;
    int             running;
    int             configuration_done;
    int             seq;             /* outgoing message sequence */

    /* Breakpoint table — indexed by source. Each setBreakpoints call replaces
     * the entire set for that source. */
    dap_breakpoint_t bps[MAX_BREAKPOINTS];
    int              n_bps;
    int              next_bp_id;

    /* Stop state — set by master_hook when fc_dap_pause_loop fires. The DAP
     * thread reads it to populate stackTrace / variables responses. */
    lua_State       *paused_L;
    int              paused;
    int              continue_pending;     /* DAP thread sets, hook clears */
    int              loaded_source_pending;
    char             loaded_source_path[MAX_SOURCE_PATH];
} dap_state_t;

static dap_state_t g_dap = { .mu = PTHREAD_MUTEX_INITIALIZER };

/* ── tiny JSON ─────────────────────────────────────────────────────────── */
/* DAP traffic is small and shape-stable. We hand-roll just enough to write
 * responses and parse the few fields we read. This keeps the spike
 * dependency-free; production should use cJSON or similar. */

static int json_get_int(const char *buf, const char *key, int def) {
    char k[64]; snprintf(k, sizeof k, "\"%s\"", key);
    const char *p = strstr(buf, k);
    if (!p) return def;
    p = strchr(p, ':'); if (!p) return def;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    return atoi(p);
}

static int json_get_string(const char *buf, const char *key,
                           char *out, size_t n) {
    char k[64]; snprintf(k, sizeof k, "\"%s\"", key);
    const char *p = strstr(buf, k);
    if (!p) return 0;
    p = strchr(p, ':'); if (!p) return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return 0;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < n) {
        if (*p == '\\' && p[1]) { out[i++] = p[1]; p += 2; }
        else                     out[i++] = *p++;
    }
    out[i] = 0;
    return 1;
}

/* Parse "lines": [ N1, N2, N3 ] into an int array. Returns count. */
static int json_get_int_array(const char *buf, const char *key,
                              int *out, int max) {
    char k[64]; snprintf(k, sizeof k, "\"%s\"", key);
    const char *p = strstr(buf, k);
    if (!p) return 0;
    p = strchr(p, '['); if (!p) return 0;
    p++;
    int n = 0;
    while (*p && *p != ']' && n < max) {
        while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n') p++;
        if (*p == ']') break;
        out[n++] = atoi(p);
        while (*p && *p != ',' && *p != ']') p++;
    }
    return n;
}

/* ── wire I/O ──────────────────────────────────────────────────────────── */

static int write_all(int fd, const char *p, size_t n) {
    while (n) {
        ssize_t w = send(fd, p, n, 0);
        if (w <= 0) return -1;
        p += w; n -= (size_t)w;
    }
    return 0;
}

static void send_msg(const char *json) {
    if (g_dap.client_fd < 0) return;
    char hdr[64];
    int hn = snprintf(hdr, sizeof hdr, "Content-Length: %zu\r\n\r\n", strlen(json));
    pthread_mutex_lock(&g_dap.mu);
    if (write_all(g_dap.client_fd, hdr, (size_t)hn) == 0)
        write_all(g_dap.client_fd, json, strlen(json));
    pthread_mutex_unlock(&g_dap.mu);
}

static void send_response(int request_seq, const char *command,
                          int success, const char *body_or_msg) {
    pthread_mutex_lock(&g_dap.mu);
    int seq = ++g_dap.seq;
    pthread_mutex_unlock(&g_dap.mu);
    char buf[MAX_MSG];
    if (success) {
        snprintf(buf, sizeof buf,
                 "{\"seq\":%d,\"type\":\"response\",\"request_seq\":%d,"
                 "\"command\":\"%s\",\"success\":true,\"body\":%s}",
                 seq, request_seq, command, body_or_msg ? body_or_msg : "{}");
    } else {
        snprintf(buf, sizeof buf,
                 "{\"seq\":%d,\"type\":\"response\",\"request_seq\":%d,"
                 "\"command\":\"%s\",\"success\":false,"
                 "\"message\":\"%s\"}",
                 seq, request_seq, command, body_or_msg ? body_or_msg : "");
    }
    send_msg(buf);
}

static void send_event(const char *event, const char *body) {
    pthread_mutex_lock(&g_dap.mu);
    int seq = ++g_dap.seq;
    pthread_mutex_unlock(&g_dap.mu);
    char buf[MAX_MSG];
    snprintf(buf, sizeof buf,
             "{\"seq\":%d,\"type\":\"event\",\"event\":\"%s\",\"body\":%s}",
             seq, event, body ? body : "{}");
    send_msg(buf);
}

/* ── request handlers ──────────────────────────────────────────────────── */

static void handle_initialize(int seq, const char *args) {
    (void)args;
    const char *body =
        "{\"supportsConfigurationDoneRequest\":true,"
        "\"supportsLoadedSourcesRequest\":true,"
        "\"supportsRestartRequest\":false,"
        "\"supportsStepBack\":false,"
        "\"supportsTerminateRequest\":true}";
    send_response(seq, "initialize", 1, body);
    send_event("initialized", "{}");
}

static void handle_launch(int seq, const char *args) {
    (void)args;
    send_response(seq, "launch", 1, "{}");
}

static void handle_configuration_done(int seq, const char *args) {
    (void)args;
    pthread_mutex_lock(&g_dap.mu);
    g_dap.configuration_done = 1;
    pthread_mutex_unlock(&g_dap.mu);
    send_response(seq, "configurationDone", 1, "{}");
}

int fc_dap_configuration_done(void) {
    pthread_mutex_lock(&g_dap.mu);
    int v = g_dap.configuration_done;
    pthread_mutex_unlock(&g_dap.mu);
    return v;
}

static int verify_breakpoint(const char *source, int line) {
    /* In production we'd consult the loaded chunk's line table. For the spike,
     * any positive line is "verified" as long as the source is set. The
     * deleted-line case (Stage 4 step 16) is signalled by the harness via a
     * sentinel line value <= 0 meaning "won't bind". */
    (void)source;
    return line > 0;
}

static void handle_set_breakpoints(int seq, const char *args) {
    char source[MAX_SOURCE_PATH] = {0};
    json_get_string(args, "path", source, sizeof source);

    int lines[MAX_BREAKPOINTS];
    int n = json_get_int_array(args, "lines", lines, MAX_BREAKPOINTS);

    pthread_mutex_lock(&g_dap.mu);
    /* Drop existing breakpoints for this source. */
    int kept = 0;
    for (int i = 0; i < g_dap.n_bps; i++) {
        if (strcmp(g_dap.bps[i].source, source) != 0) {
            if (kept != i) g_dap.bps[kept] = g_dap.bps[i];
            kept++;
        }
    }
    g_dap.n_bps = kept;
    /* Add the new set. */
    for (int i = 0; i < n && g_dap.n_bps < MAX_BREAKPOINTS; i++) {
        dap_breakpoint_t *bp = &g_dap.bps[g_dap.n_bps++];
        snprintf(bp->source, sizeof bp->source, "%s", source);
        bp->line = lines[i];
        bp->verified = verify_breakpoint(source, lines[i]);
        bp->id = ++g_dap.next_bp_id;
    }
    pthread_mutex_unlock(&g_dap.mu);

    /* Response: array of Breakpoint objects in the order the request gave. */
    char body[MAX_MSG];
    int off = snprintf(body, sizeof body, "{\"breakpoints\":[");
    pthread_mutex_lock(&g_dap.mu);
    for (int i = 0; i < n; i++) {
        int found_id = 0, found_verified = 0, found_line = lines[i];
        for (int j = 0; j < g_dap.n_bps; j++) {
            if (strcmp(g_dap.bps[j].source, source) == 0 &&
                g_dap.bps[j].line == lines[i]) {
                found_id = g_dap.bps[j].id;
                found_verified = g_dap.bps[j].verified;
                break;
            }
        }
        off += snprintf(body + off, sizeof(body) - off,
                        "%s{\"id\":%d,\"verified\":%s,\"line\":%d}",
                        i ? "," : "",
                        found_id,
                        found_verified ? "true" : "false",
                        found_line);
    }
    pthread_mutex_unlock(&g_dap.mu);
    snprintf(body + off, sizeof(body) - off, "]}");
    send_response(seq, "setBreakpoints", 1, body);
}

static void handle_threads(int seq, const char *args) {
    (void)args;
    send_response(seq, "threads", 1,
                  "{\"threads\":[{\"id\":1,\"name\":\"cart\"}]}");
}

/* Walk the paused Lua state's call stack, emit DAP StackFrame[]. */
static void handle_stack_trace(int seq, const char *args) {
    (void)args;
    pthread_mutex_lock(&g_dap.mu);
    lua_State *L = g_dap.paused_L;
    pthread_mutex_unlock(&g_dap.mu);
    if (!L) {
        send_response(seq, "stackTrace", 0, "not paused");
        return;
    }
    char body[MAX_MSG];
    int off = snprintf(body, sizeof body, "{\"stackFrames\":[");
    int frame = 0;
    lua_Debug ar;
    while (frame < MAX_FRAMES && lua_getstack(L, frame, &ar)) {
        lua_getinfo(L, "Snl", &ar);
        const char *name = ar.name ? ar.name : (ar.what ? ar.what : "?");
        const char *src  = ar.source ? ar.source : "?";
        if (*src == '@') src++;
        off += snprintf(body + off, sizeof(body) - off,
                        "%s{\"id\":%d,\"name\":\"%s\","
                        "\"source\":{\"path\":\"%s\"},"
                        "\"line\":%d,\"column\":1}",
                        frame ? "," : "",
                        frame, name, src, ar.currentline);
        frame++;
    }
    snprintf(body + off, sizeof(body) - off, "],\"totalFrames\":%d}", frame);
    send_response(seq, "stackTrace", 1, body);
}

static void handle_scopes(int seq, const char *args) {
    int frame_id = json_get_int(args, "frameId", 0);
    char body[256];
    snprintf(body, sizeof body,
             "{\"scopes\":[{\"name\":\"Locals\",\"variablesReference\":%d,"
             "\"expensive\":false}]}",
             frame_id + 1);
    send_response(seq, "scopes", 1, body);
}

static void handle_variables(int seq, const char *args) {
    int vref = json_get_int(args, "variablesReference", 0);
    int frame_id = vref - 1;
    pthread_mutex_lock(&g_dap.mu);
    lua_State *L = g_dap.paused_L;
    pthread_mutex_unlock(&g_dap.mu);
    if (!L || frame_id < 0) {
        send_response(seq, "variables", 0, "not paused");
        return;
    }
    char body[MAX_MSG];
    int off = snprintf(body, sizeof body, "{\"variables\":[");
    lua_Debug ar;
    if (lua_getstack(L, frame_id, &ar)) {
        const char *vn;
        int idx = 1;
        int first = 1;
        while ((vn = lua_getlocal(L, &ar, idx)) != NULL && idx <= MAX_VARS) {
            if (vn[0] != '(') {
                const char *vt = luaL_typename(L, -1);
                char val[256] = {0};
                if (lua_isstring(L, -1) || lua_isnumber(L, -1)) {
                    const char *s = lua_tostring(L, -1);
                    snprintf(val, sizeof val, "%s", s ? s : "?");
                } else {
                    snprintf(val, sizeof val, "%s", vt);
                }
                /* Crude JSON escape — strip quotes and backslashes. */
                for (char *q = val; *q; q++) if (*q == '"' || *q == '\\') *q = '_';
                off += snprintf(body + off, sizeof(body) - off,
                                "%s{\"name\":\"%s\",\"value\":\"%s\","
                                "\"type\":\"%s\",\"variablesReference\":0}",
                                first ? "" : ",", vn, val, vt);
                first = 0;
            }
            lua_pop(L, 1);
            idx++;
        }
    }
    snprintf(body + off, sizeof(body) - off, "]}");
    send_response(seq, "variables", 1, body);
}

static void handle_continue(int seq, const char *args) {
    (void)args;
    pthread_mutex_lock(&g_dap.mu);
    g_dap.continue_pending = 1;
    fc_master_hook_cfg.dap_step_mode = DAP_STEP_NONE;
    fc_master_hook_cfg.dap_pending_pause = 0;
    pthread_mutex_unlock(&g_dap.mu);
    send_response(seq, "continue", 1,
                  "{\"allThreadsContinued\":true}");
}

static void handle_step(int seq, const char *args, dap_step_mode_t mode) {
    (void)args;
    pthread_mutex_lock(&g_dap.mu);
    /* Compute base depth from paused state. */
    int depth = 0;
    if (g_dap.paused_L) {
        lua_Debug ar;
        while (lua_getstack(g_dap.paused_L, depth, &ar)) depth++;
    }
    fc_master_hook_cfg.dap_step_mode = mode;
    fc_master_hook_cfg.dap_step_base_depth = depth;
    fc_master_hook_cfg.dap_pending_pause = 0;
    g_dap.continue_pending = 1;
    pthread_mutex_unlock(&g_dap.mu);
    const char *cmd = mode == DAP_STEP_OVER ? "next"
                    : mode == DAP_STEP_IN   ? "stepIn"
                    : "stepOut";
    send_response(seq, cmd, 1, "{\"allThreadsContinued\":true}");
}

static void handle_pause(int seq, const char *args) {
    (void)args;
    pthread_mutex_lock(&g_dap.mu);
    fc_master_hook_cfg.dap_pending_pause = 1;
    pthread_mutex_unlock(&g_dap.mu);
    send_response(seq, "pause", 1, "{}");
}

static void handle_disconnect(int seq, const char *args) {
    (void)args;
    send_response(seq, "disconnect", 1, "{}");
    pthread_mutex_lock(&g_dap.mu);
    g_dap.continue_pending = 1;
    fc_master_hook_cfg.dap_step_mode = DAP_STEP_NONE;
    fc_master_hook_cfg.dap_pending_pause = 0;
    pthread_mutex_unlock(&g_dap.mu);
}

/* Custom hot_reload request — implemented by libconsolelua_reload.c which
 * tears down the lua_State and re-creates it from new bytecode. The reload
 * code calls fc_dap_emit_loaded_source() *after* the new state is fully
 * prepared (sequencing per the plan's risk notes). */
extern void fc_consolelua_synthetic_reload(const uint8_t *new_bytecode,
                                           uint32_t new_size);

/* Decode a hex-encoded bytecode payload from the hot_reload arguments.
 * Format: arguments.bytecodeHex = "DEADBEEF...". Caller frees the buffer. */
static uint8_t *decode_hex(const char *hex, uint32_t *out_size) {
    size_t n = strlen(hex);
    if (n & 1) return NULL;
    uint8_t *buf = malloc(n / 2);
    if (!buf) return NULL;
    for (size_t i = 0; i < n / 2; i++) {
        char c1 = hex[2*i], c2 = hex[2*i + 1];
        int v1 = c1 >= 'a' ? c1 - 'a' + 10 : c1 >= 'A' ? c1 - 'A' + 10 : c1 - '0';
        int v2 = c2 >= 'a' ? c2 - 'a' + 10 : c2 >= 'A' ? c2 - 'A' + 10 : c2 - '0';
        buf[i] = (uint8_t)((v1 << 4) | v2);
    }
    *out_size = (uint32_t)(n / 2);
    return buf;
}

static void handle_hot_reload(int seq, const char *args) {
    char src[MAX_SOURCE_PATH];
    if (!json_get_string(args, "source", src, sizeof src)) {
        send_response(seq, "hot_reload", 0, "missing source");
        return;
    }
    /* Bytecode shipped as hex string for transport simplicity. Production
     * would use a binary side-channel per ADR-0044; the spike's protocol
     * seam is the loadedSource emit, not the transport. */
    char *hex_buf = malloc(MAX_MSG);
    if (!hex_buf) { send_response(seq, "hot_reload", 0, "oom"); return; }
    int got = json_get_string(args, "bytecodeHex", hex_buf, MAX_MSG);
    if (!got) {
        free(hex_buf);
        send_response(seq, "hot_reload", 0, "missing bytecodeHex");
        return;
    }
    uint32_t size = 0;
    uint8_t *bytecode = decode_hex(hex_buf, &size);
    free(hex_buf);
    if (!bytecode) { send_response(seq, "hot_reload", 0, "bad hex"); return; }

    /* Queue the reload for the cart thread to execute. Sequencing is critical
     * — the cart thread closes the old state, builds the new one, re-installs
     * the master hook, then fc_consolelua_synthetic_reload calls
     * fc_dap_emit_loaded_source() before returning. */
    fc_consolelua_synthetic_reload(bytecode, size);
    /* Defer the emit so the new state is fully prepared first. The reload
     * function itself triggers it via fc_dap_emit_loaded_source called from
     * libconsolelua_reload.c after ensure_state() returns. For the spike's
     * single-threaded host harness, we emit it inline here after the reload
     * returns synchronously. */
    pthread_mutex_lock(&g_dap.mu);
    snprintf(g_dap.loaded_source_path, sizeof g_dap.loaded_source_path,
             "%s", src);
    g_dap.loaded_source_pending = 1;
    pthread_mutex_unlock(&g_dap.mu);

    free(bytecode);
    send_response(seq, "hot_reload", 1, "{}");
    fc_dap_emit_loaded_source(src);
}

/* ── dispatcher ────────────────────────────────────────────────────────── */

static void dispatch(const char *msg) {
    int seq = json_get_int(msg, "seq", 0);
    char cmd[64];
    if (!json_get_string(msg, "command", cmd, sizeof cmd)) return;

    if      (strcmp(cmd, "initialize")        == 0) handle_initialize(seq, msg);
    else if (strcmp(cmd, "launch")            == 0) handle_launch(seq, msg);
    else if (strcmp(cmd, "configurationDone") == 0) handle_configuration_done(seq, msg);
    else if (strcmp(cmd, "setBreakpoints")    == 0) handle_set_breakpoints(seq, msg);
    else if (strcmp(cmd, "threads")           == 0) handle_threads(seq, msg);
    else if (strcmp(cmd, "stackTrace")        == 0) handle_stack_trace(seq, msg);
    else if (strcmp(cmd, "scopes")            == 0) handle_scopes(seq, msg);
    else if (strcmp(cmd, "variables")         == 0) handle_variables(seq, msg);
    else if (strcmp(cmd, "continue")          == 0) handle_continue(seq, msg);
    else if (strcmp(cmd, "next")              == 0) handle_step(seq, msg, DAP_STEP_OVER);
    else if (strcmp(cmd, "stepIn")            == 0) handle_step(seq, msg, DAP_STEP_IN);
    else if (strcmp(cmd, "stepOut")           == 0) handle_step(seq, msg, DAP_STEP_OUT);
    else if (strcmp(cmd, "pause")             == 0) handle_pause(seq, msg);
    else if (strcmp(cmd, "disconnect")        == 0) handle_disconnect(seq, msg);
    else if (strcmp(cmd, "terminate")         == 0) handle_disconnect(seq, msg);
    else if (strcmp(cmd, "hot_reload")        == 0) handle_hot_reload(seq, msg);
    else send_response(seq, cmd, 0, "unknown command");
}

/* ── server thread ─────────────────────────────────────────────────────── */

static int read_msg(int fd, char *buf, size_t buf_size) {
    /* Read header line(s) until \r\n\r\n; parse Content-Length. */
    size_t hi = 0;
    while (hi + 4 <= buf_size) {
        ssize_t r = recv(fd, buf + hi, 1, 0);
        if (r <= 0) return -1;
        hi++;
        if (hi >= 4 && memcmp(buf + hi - 4, "\r\n\r\n", 4) == 0) break;
    }
    buf[hi] = 0;
    const char *cl = strstr(buf, "Content-Length:");
    if (!cl) return -1;
    int len = atoi(cl + 15);
    if (len < 0 || (size_t)len + 1 > buf_size) return -1;
    /* Drain the body. */
    size_t got = 0;
    while ((int)got < len) {
        ssize_t r = recv(fd, buf + got, (size_t)len - got, 0);
        if (r <= 0) return -1;
        got += (size_t)r;
    }
    buf[len] = 0;
    return len;
}

static void *dap_thread_main(void *arg) {
    (void)arg;
    static char buf[MAX_MSG];
    while (g_dap.running) {
        struct sockaddr_in cli;
        socklen_t cl = sizeof cli;
        int fd = accept(g_dap.listen_fd, (struct sockaddr *)&cli, &cl);
        if (fd < 0) {
            if (!g_dap.running) break;
            continue;
        }
        pthread_mutex_lock(&g_dap.mu);
        g_dap.client_fd = fd;
        pthread_mutex_unlock(&g_dap.mu);
        while (g_dap.running) {
            int n = read_msg(fd, buf, sizeof buf);
            if (n <= 0) break;
            dispatch(buf);
        }
        pthread_mutex_lock(&g_dap.mu);
        close(fd);
        g_dap.client_fd = -1;
        pthread_mutex_unlock(&g_dap.mu);
    }
    return NULL;
}

int fc_consolelua_dap_listen(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) { close(fd); return -1; }
    if (listen(fd, 1) < 0) { close(fd); return -1; }
    g_dap.listen_fd = fd;
    g_dap.client_fd = -1;
    g_dap.running = 1;
    fc_master_hook_cfg.dap_state = &g_dap;
    if (pthread_create(&g_dap.thr, NULL, dap_thread_main, NULL) != 0) {
        close(fd);
        g_dap.running = 0;
        return -1;
    }
    return 0;
}

void fc_consolelua_dap_shutdown(void) {
    if (!g_dap.running) return;
    g_dap.running = 0;
    if (g_dap.client_fd >= 0) shutdown(g_dap.client_fd, SHUT_RDWR);
    if (g_dap.listen_fd >= 0) shutdown(g_dap.listen_fd, SHUT_RDWR);
    pthread_join(g_dap.thr, NULL);
    if (g_dap.client_fd >= 0) close(g_dap.client_fd);
    if (g_dap.listen_fd >= 0) close(g_dap.listen_fd);
}

/* ── master_hook callbacks ─────────────────────────────────────────────── */

static const char *basename_of(const char *p) {
    const char *b = p;
    for (const char *q = p; *q; q++) if (*q == '/') b = q + 1;
    return b;
}

bool fc_dap_should_break(lua_State *L, lua_Debug *ar) {
    if (g_dap.client_fd < 0) return false;
    lua_getinfo(L, "Sl", ar);
    const char *src = ar->source ? ar->source : "";
    if (*src == '@') src++;
    const char *src_base = basename_of(src);
    pthread_mutex_lock(&g_dap.mu);
    bool hit = false;
    for (int i = 0; i < g_dap.n_bps; i++) {
        if (!g_dap.bps[i].verified) continue;
        if (g_dap.bps[i].line != ar->currentline) continue;
        const char *bp_base = basename_of(g_dap.bps[i].source);
        if (strcmp(bp_base, src_base) == 0) { hit = true; break; }
    }
    pthread_mutex_unlock(&g_dap.mu);
    return hit;
}

void fc_dap_pause_loop(lua_State *L, lua_Debug *ar) {
    (void)ar;
    pthread_mutex_lock(&g_dap.mu);
    g_dap.paused_L = L;
    g_dap.paused = 1;
    g_dap.continue_pending = 0;
    pthread_mutex_unlock(&g_dap.mu);
    char body[256];
    snprintf(body, sizeof body,
             "{\"reason\":\"breakpoint\",\"threadId\":1,"
             "\"allThreadsStopped\":true}");
    send_event("stopped", body);
    /* Block the cart thread until the DAP server posts continue/step. */
    while (g_dap.running) {
        pthread_mutex_lock(&g_dap.mu);
        int cont = g_dap.continue_pending;
        pthread_mutex_unlock(&g_dap.mu);
        if (cont) break;
        usleep(2000);
    }
    pthread_mutex_lock(&g_dap.mu);
    g_dap.paused = 0;
    g_dap.paused_L = NULL;
    g_dap.continue_pending = 0;
    pthread_mutex_unlock(&g_dap.mu);
    send_event("continued", "{\"threadId\":1,\"allThreadsContinued\":true}");
}

void fc_dap_emit_loaded_source(const char *source_path) {
    char body[MAX_SOURCE_PATH + 128];
    snprintf(body, sizeof body,
             "{\"reason\":\"changed\","
             "\"source\":{\"path\":\"%s\",\"name\":\"%s\"}}",
             source_path, source_path);
    send_event("loadedSource", body);
}
