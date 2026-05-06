/* Spike L — libretro core for the FC32 runtime.
 *
 * The libretro adapter IS the frontend in the ADR-0036 sense — it owns
 * the accumulator and pulls update + draw + framebuffer + audio out of
 * the runtime, then pushes them through the libretro callbacks the
 * frontend (RetroArch) installed during retro_set_*. Per PLAN.md
 * §"The libretro adapter is the frontend in the ADR-0036 sense", this
 * is the only place where the callback-pull vs frontend-pulls
 * inversion is reconciled.
 *
 * Headline LOC question: how many lines of adapter does the inversion
 * take? Per PLAN.md the gate is ≤ 1000 lines including comments. Count
 * across host C files only; lib/blyt_facade.{c,h} is runtime-side and
 * counted separately.
 */

#include "vendor/libretro.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "blyt_facade.h"

/* ── Module-local state ──────────────────────────────────────────────── */

#define FB_WIDTH    320
#define FB_HEIGHT   240
#define FB_PIXELS   (FB_WIDTH * FB_HEIGHT)
#define AUDIO_RATE  48000u
#define DECLARED_FPS 60u
#define TICK_NS      (1000000000ULL / DECLARED_FPS)
#define MAX_AUDIO_FRAMES_PER_RUN 4096

static blyt_runtime_t *rt = NULL;

static retro_video_refresh_t       video_refresh_cb;
static retro_audio_sample_t        audio_sample_cb;        /* unused; we batch */
static retro_audio_sample_batch_t  audio_sample_batch_cb;
static retro_environment_t         environ_cb;
static retro_input_poll_t          input_poll_cb;
static retro_input_state_t         input_state_cb;
static retro_log_printf_t          log_cb;

static uint32_t fb_xrgb8888[FB_PIXELS];
static int16_t  audio_mix_buf[2 * MAX_AUDIO_FRAMES_PER_RUN];

static uint32_t serialize_upper_bound = 0;

static uint64_t last_run_ns        = 0;
static uint64_t accumulator_ns     = 0;
static uint64_t total_audio_pushed = 0;
static uint64_t total_run_calls    = 0;
static uint32_t catch_ups_last_run = 0;

/* Frame trace for the Stage 2 sanity check (PLAN.md step 8). One row per
 * retro_run; CSV format. Written to a file when BLYT_TRACE_FRAMES is
 * set in the environment. */
static FILE *trace_file = NULL;

/* Test-mode override: when BLYT_FORCE_TICK_PER_RUN=1, retro_run advances
 * the runtime by exactly one tick per call, regardless of wall-clock
 * elapsed time. The wall-clock accumulator path (real RetroArch
 * cadence) and the deterministic per-call path (headless gates) share
 * everything except this branch. */
static int   force_tick_per_run = 0;

/* ── Timing ──────────────────────────────────────────────────────────── */

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ── Logging shim ────────────────────────────────────────────────────── */

static void fallback_log(enum retro_log_level lvl, const char *fmt, ...) {
    (void)lvl;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

/* ── Input mapping (declared in input_map.c) ─────────────────────────── */

uint16_t adapter_poll_button_mask(retro_input_state_t cb, int port);
extern const struct retro_input_descriptor adapter_input_descriptors[];

/* ── Palette expansion (declared in palette.c) ───────────────────────── */

void adapter_expand_palette(const uint8_t *src,
                            const uint32_t *palette,
                            uint32_t       *dst,
                            size_t          pixels);

/* ── Audio push (declared in audio_push.c) ───────────────────────────── */

int adapter_compute_audio_frames(uint32_t sample_rate,
                                 uint32_t declared_fps,
                                 uint64_t total_pushed,
                                 uint64_t total_run_calls);

/* ── libretro entry points ───────────────────────────────────────────── */

void retro_set_environment(retro_environment_t cb) {
    environ_cb = cb;

    bool no_game = false;   /* spike L always loads a cart */
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);

    struct retro_log_callback log;
    if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log)) log_cb = log.log;
    else                                                log_cb = fallback_log;
}

void retro_set_video_refresh(retro_video_refresh_t cb)      { video_refresh_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb)        { audio_sample_cb = cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
    audio_sample_batch_cb = cb;
}
void retro_set_input_poll(retro_input_poll_t cb)            { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb)          { input_state_cb = cb; }

void retro_init(void) {
    last_run_ns        = now_ns();
    accumulator_ns     = 0;
    total_audio_pushed = 0;
    total_run_calls    = 0;

    const char *force_env = getenv("BLYT_FORCE_TICK_PER_RUN");
    force_tick_per_run = (force_env && *force_env && *force_env != '0') ? 1 : 0;

    const char *trace_path = getenv("BLYT_TRACE_FRAMES");
    if (trace_path && *trace_path) {
        trace_file = fopen(trace_path, "w");
        if (trace_file) {
            fprintf(trace_file,
                    "run_index,wall_ns,delta_ns,acc_ns_pre,catch_ups,acc_ns_post,audio_frames\n");
        }
    }
}

void retro_deinit(void) {
    if (rt) { blyt_runtime_destroy(rt); rt = NULL; }
    if (trace_file) { fclose(trace_file); trace_file = NULL; }
}

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

void retro_get_system_info(struct retro_system_info *info) {
    memset(info, 0, sizeof(*info));
    info->library_name     = "blyt32 (Spike L)";
    info->library_version  = "spike-l-0";
    info->valid_extensions = "elf|cart_d";
    info->need_fullpath    = true;
    info->block_extract    = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info) {
    memset(info, 0, sizeof(*info));
    info->geometry.base_width   = FB_WIDTH;
    info->geometry.base_height  = FB_HEIGHT;
    info->geometry.max_width    = FB_WIDTH;
    info->geometry.max_height   = FB_HEIGHT;
    info->geometry.aspect_ratio = (float)FB_WIDTH / (float)FB_HEIGHT;
    info->timing.fps            = (double)DECLARED_FPS;
    info->timing.sample_rate    = (double)AUDIO_RATE;
}

void retro_set_controller_port_device(unsigned port, unsigned device) {
    (void)port; (void)device;
}

void retro_reset(void) {
    if (rt) blyt_runtime_reset(rt);
    accumulator_ns = 0;
    last_run_ns    = now_ns();
}

bool retro_load_game(const struct retro_game_info *info) {
    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
    if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
        if (log_cb) log_cb(RETRO_LOG_ERROR,
                           "blyt: XRGB8888 pixel format not supported\n");
        return false;
    }

    environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS,
               (void *)adapter_input_descriptors);

    const char *path = (info && info->path) ? info->path : "(synthetic)";
    rt = blyt_runtime_create(path);
    if (!rt) {
        if (log_cb) log_cb(RETRO_LOG_ERROR,
                           "blyt: blyt_runtime_create(%s) failed\n", path);
        return false;
    }

    /* PLAN.md §retro_serialize_size: compute the bound once at
     * cart-load time, hold it for the cart's lifetime. The facade
     * asserts the bound is stable post-init. */
    serialize_upper_bound = blyt_runtime_save_upper_bound(rt);
    blyt_runtime_assert_slots_locked(rt);

    if (log_cb) log_cb(RETRO_LOG_INFO,
                       "blyt: loaded %s, save_upper_bound=%u bytes\n",
                       path, serialize_upper_bound);

    last_run_ns    = now_ns();
    accumulator_ns = 0;
    return true;
}

bool retro_load_game_special(unsigned game_type,
                             const struct retro_game_info *info,
                             size_t num_info) {
    (void)game_type; (void)info; (void)num_info;
    return false;
}

void retro_unload_game(void) {
    if (rt) { blyt_runtime_destroy(rt); rt = NULL; }
}

unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }

void *retro_get_memory_data(unsigned id) { (void)id; return NULL; }
size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }

void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) {
    (void)index; (void)enabled; (void)code;
}

/* ── retro_run — the structural piece this spike adds ─────────────────── */

void retro_run(void) {
    if (!rt) return;

    /* Poll input once per run; latch the bitmask for every catch-up tick. */
    if (input_poll_cb) input_poll_cb();
    uint16_t buttons = adapter_poll_button_mask(input_state_cb, 0);

    /* Wall-clock accumulator drives ADR-0036 catch-up cap (≤ 3). */
    uint64_t now            = now_ns();
    uint64_t delta          = now - last_run_ns;
    last_run_ns             = now;
    uint64_t acc_pre        = accumulator_ns + delta;
    accumulator_ns          = acc_pre;

    int catch_ups = 0;
    if (force_tick_per_run) {
        /* Headless deterministic mode — exactly one update per call.
         * The wall-clock accumulator stays at 0 so trace files can
         * still distinguish the two modes. */
        blyt_runtime_set_button_state(rt, 0, buttons);
        blyt_runtime_update(rt);
        catch_ups = 1;
        accumulator_ns = 0;
    } else {
        while (accumulator_ns >= TICK_NS && catch_ups < 3) {
            blyt_runtime_set_button_state(rt, 0, buttons);
            blyt_runtime_update(rt);
            accumulator_ns -= TICK_NS;
            catch_ups++;
        }
        /* ADR-0036 spiral-of-death cap: drop excess so the runtime never
         * skips more than 3 ticks per render frame. */
        if (accumulator_ns >= TICK_NS) accumulator_ns = TICK_NS - 1;
    }

    catch_ups_last_run = (uint32_t)catch_ups;

    /* Always render once per retro_run. If the frontend ran us at a
     * faster cadence than the cart's declared fps (e.g. host vsync at
     * 144 Hz), most retro_runs do zero updates and re-blit the cached
     * framebuffer — that is the ADR-0036-correct behaviour. */
    blyt_runtime_draw(rt);

    /* Palette expansion happens here, against the current palette so
     * mid-frame palette animation round-trips correctly. */
    adapter_expand_palette(blyt_runtime_get_framebuffer(rt),
                           blyt_runtime_get_palette(rt),
                           fb_xrgb8888, FB_PIXELS);
    if (video_refresh_cb) {
        video_refresh_cb(fb_xrgb8888, FB_WIDTH, FB_HEIGHT,
                         FB_WIDTH * sizeof(uint32_t));
    }

    /* Audio: pull (sample_rate / declared_fps) ± drift correction
     * stereo frames per retro_run; push as one batch. */
    int target = adapter_compute_audio_frames(
        AUDIO_RATE, DECLARED_FPS, total_audio_pushed, total_run_calls);
    if (target > MAX_AUDIO_FRAMES_PER_RUN) target = MAX_AUDIO_FRAMES_PER_RUN;
    int n = blyt_runtime_pull_audio(rt, audio_mix_buf, target);
    if (audio_sample_batch_cb && n > 0) {
        audio_sample_batch_cb(audio_mix_buf, (size_t)n);
    }
    total_audio_pushed += (uint64_t)n;
    total_run_calls    += 1;

    if (trace_file) {
        fprintf(trace_file, "%llu,%llu,%llu,%llu,%d,%llu,%d\n",
                (unsigned long long)total_run_calls,
                (unsigned long long)now,
                (unsigned long long)delta,
                (unsigned long long)acc_pre,
                catch_ups,
                (unsigned long long)accumulator_ns,
                n);
    }
}

/* ── Save state ───────────────────────────────────────────────────────── */

size_t retro_serialize_size(void) { return (size_t)serialize_upper_bound; }

bool retro_serialize(void *data, size_t size) {
    if (!rt || !data) return false;
    if (size < serialize_upper_bound) return false;
    uint32_t n = blyt_runtime_save(rt, (uint8_t *)data, (uint32_t)size);
    if (n == 0) return false;
    if (n > size) {
        if (log_cb) log_cb(RETRO_LOG_ERROR,
                           "blyt: save_state wrote %u > upper_bound %zu — "
                           "facade miscomputed the bound\n",
                           (unsigned)n, size);
        return false;
    }
    return true;
}

bool retro_unserialize(const void *data, size_t size) {
    if (!rt || !data) return false;
    /* Spike-K contract: init the runtime, then load tracked regions on
     * top. PLAN.md §"Save state and rewind go through the same Spike K
     * buffer". */
    blyt_runtime_reset(rt);
    return blyt_runtime_load(rt, (const uint8_t *)data, (uint32_t)size);
}
