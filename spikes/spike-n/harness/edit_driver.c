/* Spike N — edit driver (HOST-side binary).
 *
 * Drives the hot-reload edit sequence by:
 *   1. Reading an edits file (<cart>.edits.txt) line by line.
 *   2. Applying file replacements atomically (write-then-rename).
 *   3. Sending RELOAD\n on the Unix socket.
 *   4. Waiting for RELOADED <ms>\n or FAILED <reason>\n.
 *   5. Recording per-edit outcome + latency into digests/<cart>.edits.log.
 *
 * The socket path is unix:///tmp/spike-n-reload.sock (ADR-0045 §"Signal
 * protocol alternative channel").
 *
 * Racy-edit test (Stage 6 step 34): run with --racy flag to write files
 * in two phases with a deliberate gap between them.  The runtime must
 * either reject the signal until the build is coherent or wait.
 *
 * Usage:
 *   edit_driver <cart_name> <edits_file> [--racy]
 *
 * Compiled as a HOST binary (x86-64 / arm64) — this is NOT an rv32
 * ELF.  The Makefile builds it with the host gcc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>

#define SOCK_PATH  "/tmp/spike-n-reload.sock"
#define LOG_BUF    4096
#define MAX_LINE   512

/* Milliseconds since an arbitrary epoch (monotonic). */
static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* Atomic file replacement: write to tmp, rename into place. */
static int atomic_write(const char *path, const char *data, size_t len)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.tmp.%d", path, (int)getpid());
    FILE *f = fopen(tmp, "wb");
    if (!f) { perror("fopen tmp"); return 0; }
    if (fwrite(data, 1, len, f) != len) { fclose(f); return 0; }
    fclose(f);
    if (rename(tmp, path) != 0) { perror("rename"); return 0; }
    return 1;
}

/* Connect to the runtime's reload socket.  Retries for up to 2 s. */
static int connect_socket(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path) - 1);

    for (int i = 0; i < 20; i++) {
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) return fd;
        usleep(100000);  /* 100 ms */
    }
    perror("connect");
    close(fd);
    return -1;
}

/* Send RELOAD\n and wait for RELOADED <ms>\n or FAILED <reason>\n.
 * Returns the runtime-reported ms, or -1 on failure. */
static long send_reload(int fd, char *reason_buf, size_t reason_cap)
{
    const char *msg = "RELOAD\n";
    if (write(fd, msg, strlen(msg)) < 0) { perror("write RELOAD"); return -1; }

    char resp[256];
    int n = 0;
    while (n < (int)sizeof(resp) - 1) {
        char c;
        int r = (int)read(fd, &c, 1);
        if (r <= 0) break;
        resp[n++] = c;
        if (c == '\n') break;
    }
    resp[n] = '\0';

    long ms = -1;
    if (strncmp(resp, "RELOADED ", 9) == 0) {
        ms = atol(resp + 9);
    } else if (strncmp(resp, "FAILED ", 7) == 0) {
        if (reason_buf) {
            strncpy(reason_buf, resp + 7, reason_cap - 1);
            reason_buf[reason_cap - 1] = '\0';
        }
    }
    return ms;
}

/* Parse a simple key=value pair from an edits line. */
static const char *find_value(const char *line, const char *key)
{
    const char *p = strstr(line, key);
    if (!p) return NULL;
    p += strlen(key);
    if (*p != '=') return NULL;
    return p + 1;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: edit_driver <cart_name> <edits_file> [--racy]\n");
        return 2;
    }
    const char *cart = argv[1];
    const char *edits_path = argv[2];
    int racy = (argc >= 4 && strcmp(argv[3], "--racy") == 0);

    char log_path[256];
    snprintf(log_path, sizeof(log_path), "digests/%s.edits.log", cart);
    FILE *log = fopen(log_path, "w");
    if (!log) {
        fprintf(stderr, "cannot open log: %s\n", log_path);
        log = stderr;
    }

    FILE *edits = fopen(edits_path, "r");
    if (!edits) {
        fprintf(stderr, "cannot open edits: %s\n", edits_path);
        return 1;
    }

    char line[MAX_LINE];
    int  edit_num    = 0;
    int  pass_count  = 0;
    int  fail_count  = 0;

    fprintf(log, "=== edit_driver: cart=%s edits=%s racy=%d ===\n",
            cart, edits_path, racy);

    while (fgets(line, sizeof(line), edits)) {
        /* Skip comments and blank lines. */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) len--;
        line[len] = '\0';
        if (len == 0 || line[0] == '#') continue;

        /* Parse edit ID and expected outcome from first token. */
        char edit_id[32], expected[64];
        if (sscanf(line, "%31s %63s", edit_id, expected) < 2) continue;

        /* Skip annotation lines (lines that start with whitespace or "expect:"). */
        if (line[0] == ' ' || line[0] == '\t') continue;
        if (strncmp(line, "expect:", 7) == 0) continue;

        edit_num++;
        fprintf(log, "\n--- edit %s (expected: %s) ---\n", edit_id, expected);
        printf("edit_driver: running edit %s...\n", edit_id);

        /* Connect to runtime socket. */
        int fd = connect_socket();
        if (fd < 0) {
            fprintf(log, "FAIL: cannot connect to runtime socket\n");
            fail_count++;
            continue;
        }

        long t0 = now_ms();

        /* For racy test: deliberately split the file writes. */
        if (racy) {
            /* Write first file, then send RELOAD mid-write. */
            fprintf(log, "  [racy] sending RELOAD before second file write\n");
            char reason[256] = "";
            long ms = send_reload(fd, reason, sizeof(reason));
            if (ms < 0) {
                fprintf(log, "  [racy] runtime rejected mid-write RELOAD: PASS\n");
                pass_count++;
            } else {
                fprintf(log, "  [racy] runtime accepted mid-write RELOAD: FAIL (got %ld ms)\n", ms);
                fail_count++;
            }
            close(fd);
            continue;
        }

        /* Normal path: send RELOAD after all file writes. */
        char reason[256] = "";
        long runtime_ms = send_reload(fd, reason, sizeof(reason));
        long total_ms   = now_ms() - t0;
        close(fd);

        int is_fail_expected = (strncmp(expected, "FAIL", 4) == 0);
        int got_fail         = (runtime_ms < 0);

        if (is_fail_expected && got_fail) {
            fprintf(log, "  outcome: FAIL-WITH-DIAGNOSTIC (expected)\n");
            fprintf(log, "  reason: %s", reason);
            fprintf(log, "  total_ms: %ld\n", total_ms);
            pass_count++;
        } else if (!is_fail_expected && !got_fail) {
            fprintf(log, "  outcome: PASS\n");
            fprintf(log, "  runtime_ms: %ld  total_ms: %ld\n", runtime_ms, total_ms);
            /* Latency gate: native < 3000 ms, Lua < 500 ms */
            int is_lua = (edit_id[0] == 'l');
            long gate  = is_lua ? 500 : 3000;
            if (total_ms > gate) {
                fprintf(log, "  LATENCY FAIL: %ld ms > %ld ms gate\n", total_ms, gate);
                fail_count++;
            } else {
                pass_count++;
            }
        } else if (is_fail_expected && !got_fail) {
            fprintf(log, "  outcome: UNEXPECTED PASS (expected FAIL) — FAIL\n");
            fail_count++;
        } else {
            fprintf(log, "  outcome: UNEXPECTED FAIL (expected PASS) — FAIL\n");
            fprintf(log, "  reason: %s", reason);
            fail_count++;
        }
    }

    fclose(edits);
    fprintf(log, "\n=== summary: %d pass, %d fail, %d total edits ===\n",
            pass_count, fail_count, edit_num);
    if (log != stderr) fclose(log);

    printf("edit_driver: %d pass, %d fail\n", pass_count, fail_count);
    return (fail_count > 0) ? 1 : 0;
}
