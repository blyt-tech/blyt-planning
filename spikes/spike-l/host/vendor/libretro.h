/* Spike L — vendored libretro.h subset.
 *
 * The full upstream libretro.h is part of the RetroArch source tree
 * (https://github.com/libretro/libretro-common/blob/master/include/libretro.h)
 * and ships with the `libretro-dev` apt package as /usr/include/libretro.h.
 * To keep the spike source self-contained and compilable on hosts that
 * don't have the package installed, we vendor only the types and
 * constants the adapter uses. Field layouts and enum values match the
 * upstream API verbatim — the libretro ABI is stable
 * (RETRO_API_VERSION = 1).
 *
 * If the spike grows past the subset declared here, swap this for the
 * full upstream header (e.g. via Makefile -I/usr/include).
 */

#ifndef LIBRETRO_H_VENDORED
#define LIBRETRO_H_VENDORED

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RETRO_API_VERSION 1

/* Devices */
#define RETRO_DEVICE_JOYPAD 1

#define RETRO_DEVICE_ID_JOYPAD_B        0
#define RETRO_DEVICE_ID_JOYPAD_Y        1
#define RETRO_DEVICE_ID_JOYPAD_SELECT   2
#define RETRO_DEVICE_ID_JOYPAD_START    3
#define RETRO_DEVICE_ID_JOYPAD_UP       4
#define RETRO_DEVICE_ID_JOYPAD_DOWN     5
#define RETRO_DEVICE_ID_JOYPAD_LEFT     6
#define RETRO_DEVICE_ID_JOYPAD_RIGHT    7
#define RETRO_DEVICE_ID_JOYPAD_A        8
#define RETRO_DEVICE_ID_JOYPAD_X        9
#define RETRO_DEVICE_ID_JOYPAD_L        10
#define RETRO_DEVICE_ID_JOYPAD_R        11

/* Regions */
#define RETRO_REGION_NTSC 0
#define RETRO_REGION_PAL  1

/* Environment commands */
#define RETRO_ENVIRONMENT_SET_PIXEL_FORMAT       10
#define RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS  11
#define RETRO_ENVIRONMENT_GET_LOG_INTERFACE      27
#define RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME    18

/* Pixel formats */
enum retro_pixel_format {
    RETRO_PIXEL_FORMAT_0RGB1555 = 0,
    RETRO_PIXEL_FORMAT_XRGB8888 = 1,
    RETRO_PIXEL_FORMAT_RGB565   = 2,
    RETRO_PIXEL_FORMAT_UNKNOWN  = 0x7fffffff
};

/* Log levels */
enum retro_log_level {
    RETRO_LOG_DEBUG = 0,
    RETRO_LOG_INFO,
    RETRO_LOG_WARN,
    RETRO_LOG_ERROR,
    RETRO_LOG_DUMMY = 0x7fffffff
};

typedef void (*retro_log_printf_t)(enum retro_log_level level,
                                   const char *fmt, ...);

struct retro_log_callback {
    retro_log_printf_t log;
};

/* Geometry / timing */
struct retro_game_geometry {
    unsigned base_width;
    unsigned base_height;
    unsigned max_width;
    unsigned max_height;
    float    aspect_ratio;
};

struct retro_system_timing {
    double fps;
    double sample_rate;
};

struct retro_system_av_info {
    struct retro_game_geometry geometry;
    struct retro_system_timing timing;
};

struct retro_system_info {
    const char *library_name;
    const char *library_version;
    const char *valid_extensions;
    bool        need_fullpath;
    bool        block_extract;
};

struct retro_game_info {
    const char *path;
    const void *data;
    size_t      size;
    const char *meta;
};

struct retro_input_descriptor {
    unsigned    port;
    unsigned    device;
    unsigned    index;
    unsigned    id;
    const char *description;
};

/* Callback typedefs */
typedef bool     (*retro_environment_t)(unsigned cmd, void *data);
typedef void     (*retro_video_refresh_t)(const void *data,
                                          unsigned width, unsigned height,
                                          size_t pitch);
typedef void     (*retro_audio_sample_t)(int16_t left, int16_t right);
typedef size_t   (*retro_audio_sample_batch_t)(const int16_t *data,
                                               size_t frames);
typedef void     (*retro_input_poll_t)(void);
typedef int16_t  (*retro_input_state_t)(unsigned port, unsigned device,
                                        unsigned index, unsigned id);

#ifdef __cplusplus
}
#endif

#endif /* LIBRETRO_H_VENDORED */
