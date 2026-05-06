"""Spike L — minimal ctypes driver for the libretro core.

Loads runtime_libretro.so via ctypes and exposes Pythonic wrappers for
the entry points the headless gates need: load_game, run, serialize,
unserialize, get framebuffer / audio, set buttons. RetroArch's own CLI
is not used — the libretro ABI is C and stable, so a small ctypes shim
is the cleanest way to drive the core deterministically.

This corresponds to the "small Lua-side hook ... via libretro_common's
reference frontend headers" path discussed in PLAN.md Stage 4 step 15.
"""

import ctypes
import ctypes.util
import os
from dataclasses import dataclass


FB_WIDTH  = 320
FB_HEIGHT = 240
FB_PIXELS = FB_WIDTH * FB_HEIGHT


# ── libretro callback ABI (matches host/vendor/libretro.h) ────────────────

# typedef bool (*retro_environment_t)(unsigned cmd, void *data);
RETRO_ENV_FN = ctypes.CFUNCTYPE(
    ctypes.c_bool, ctypes.c_uint, ctypes.c_void_p)
# typedef void (*retro_video_refresh_t)(const void*, unsigned, unsigned, size_t);
RETRO_VIDEO_FN = ctypes.CFUNCTYPE(
    None, ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint, ctypes.c_size_t)
# typedef void (*retro_audio_sample_t)(int16_t, int16_t);
RETRO_AUDIO_FN = ctypes.CFUNCTYPE(
    None, ctypes.c_int16, ctypes.c_int16)
# typedef size_t (*retro_audio_sample_batch_t)(const int16_t*, size_t);
RETRO_AUDIO_BATCH_FN = ctypes.CFUNCTYPE(
    ctypes.c_size_t, ctypes.POINTER(ctypes.c_int16), ctypes.c_size_t)
# typedef void (*retro_input_poll_t)(void);
RETRO_POLL_FN = ctypes.CFUNCTYPE(None)
# typedef int16_t (*retro_input_state_t)(unsigned, unsigned, unsigned, unsigned);
RETRO_STATE_FN = ctypes.CFUNCTYPE(
    ctypes.c_int16, ctypes.c_uint, ctypes.c_uint,
    ctypes.c_uint, ctypes.c_uint)


class RetroGameInfo(ctypes.Structure):
    _fields_ = [
        ("path", ctypes.c_char_p),
        ("data", ctypes.c_void_p),
        ("size", ctypes.c_size_t),
        ("meta", ctypes.c_char_p),
    ]


class RetroGameGeometry(ctypes.Structure):
    _fields_ = [
        ("base_width",  ctypes.c_uint),
        ("base_height", ctypes.c_uint),
        ("max_width",   ctypes.c_uint),
        ("max_height",  ctypes.c_uint),
        ("aspect_ratio", ctypes.c_float),
    ]


class RetroSystemTiming(ctypes.Structure):
    _fields_ = [
        ("fps",         ctypes.c_double),
        ("sample_rate", ctypes.c_double),
    ]


class RetroSystemAvInfo(ctypes.Structure):
    _fields_ = [
        ("geometry", RetroGameGeometry),
        ("timing",   RetroSystemTiming),
    ]


# Environment commands the core might query.
ENV_SET_PIXEL_FORMAT      = 10
ENV_SET_INPUT_DESCRIPTORS = 11
ENV_GET_LOG_INTERFACE     = 27
ENV_SET_SUPPORT_NO_GAME   = 18

DEVICE_JOYPAD = 1
JOYPAD_B      = 0
JOYPAD_Y      = 1
JOYPAD_SELECT = 2
JOYPAD_START  = 3
JOYPAD_UP     = 4
JOYPAD_DOWN   = 5
JOYPAD_LEFT   = 6
JOYPAD_RIGHT  = 7
JOYPAD_A      = 8
JOYPAD_X      = 9
JOYPAD_L      = 10
JOYPAD_R      = 11


@dataclass
class AudioCapture:
    samples: list  # list of (left, right) tuples


class RetroCore:
    def __init__(self, core_path: str):
        if not os.path.exists(core_path):
            raise FileNotFoundError(core_path)
        # Headless tests need deterministic frame advancement; the core
        # honours BLYT_FORCE_TICK_PER_RUN by running exactly one update
        # per retro_run regardless of wall-clock elapsed time. The env
        # var must be set BEFORE the core's retro_init reads it.
        os.environ["BLYT_FORCE_TICK_PER_RUN"] = "1"
        self.lib = ctypes.CDLL(core_path)

        self._wire_signatures()
        self._buttons = 0
        self.audio = AudioCapture(samples=[])
        self.last_video_size = (0, 0)
        self.last_video_pitch = 0
        self.last_video_buf = None
        self.video_callback_count = 0

        # Bind callback Python -> C; keep references alive.
        self._env_cb   = RETRO_ENV_FN(self._env_handler)
        self._video_cb = RETRO_VIDEO_FN(self._video_handler)
        self._audio_cb = RETRO_AUDIO_FN(self._audio_handler)
        self._batch_cb = RETRO_AUDIO_BATCH_FN(self._audio_batch_handler)
        self._poll_cb  = RETRO_POLL_FN(self._poll_handler)
        self._state_cb = RETRO_STATE_FN(self._state_handler)

        self.lib.retro_set_environment(self._env_cb)
        self.lib.retro_set_video_refresh(self._video_cb)
        self.lib.retro_set_audio_sample(self._audio_cb)
        self.lib.retro_set_audio_sample_batch(self._batch_cb)
        self.lib.retro_set_input_poll(self._poll_cb)
        self.lib.retro_set_input_state(self._state_cb)

        self.lib.retro_init()

    def _wire_signatures(self):
        L = self.lib
        L.retro_init.restype = None
        L.retro_deinit.restype = None
        L.retro_api_version.restype = ctypes.c_uint
        L.retro_get_system_av_info.argtypes = [ctypes.POINTER(RetroSystemAvInfo)]
        L.retro_get_system_av_info.restype = None
        L.retro_load_game.argtypes = [ctypes.POINTER(RetroGameInfo)]
        L.retro_load_game.restype = ctypes.c_bool
        L.retro_unload_game.restype = None
        L.retro_run.restype = None
        L.retro_serialize_size.restype = ctypes.c_size_t
        L.retro_serialize.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        L.retro_serialize.restype = ctypes.c_bool
        L.retro_unserialize.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        L.retro_unserialize.restype = ctypes.c_bool
        L.retro_reset.restype = None
        L.retro_set_environment.argtypes = [RETRO_ENV_FN]
        L.retro_set_video_refresh.argtypes = [RETRO_VIDEO_FN]
        L.retro_set_audio_sample.argtypes = [RETRO_AUDIO_FN]
        L.retro_set_audio_sample_batch.argtypes = [RETRO_AUDIO_BATCH_FN]
        L.retro_set_input_poll.argtypes = [RETRO_POLL_FN]
        L.retro_set_input_state.argtypes = [RETRO_STATE_FN]

    # ── libretro callbacks ────────────────────────────────────────────
    def _env_handler(self, cmd, data):
        if cmd == ENV_SET_PIXEL_FORMAT:
            return True
        if cmd == ENV_SET_INPUT_DESCRIPTORS:
            return True
        if cmd == ENV_SET_SUPPORT_NO_GAME:
            return True
        if cmd == ENV_GET_LOG_INTERFACE:
            return False  # core falls back to its own log
        return False

    def _video_handler(self, data, w, h, pitch):
        self.last_video_size = (int(w), int(h))
        self.last_video_pitch = int(pitch)
        # Copy out so the framebuffer survives across retro_run boundaries.
        nbytes = int(pitch) * int(h)
        if data and nbytes > 0:
            buf = (ctypes.c_uint8 * nbytes).from_address(data)
            self.last_video_buf = bytes(buf)
        self.video_callback_count += 1

    def _audio_handler(self, l, r):
        self.audio.samples.append((int(l), int(r)))

    def _audio_batch_handler(self, data, frames):
        # data points at frames*2 int16 (interleaved stereo).
        n = int(frames)
        for i in range(n):
            self.audio.samples.append((int(data[2*i]), int(data[2*i + 1])))
        return frames

    def _poll_handler(self):
        pass

    def _state_handler(self, port, device, index, ident):
        if device != DEVICE_JOYPAD or port != 0:
            return 0
        return 1 if (self._buttons >> int(ident)) & 1 else 0

    # ── Higher-level helpers ──────────────────────────────────────────
    def load(self, cart_path: str) -> bool:
        info = RetroGameInfo()
        info.path = cart_path.encode("utf-8")
        info.data = None
        info.size = 0
        info.meta = None
        return bool(self.lib.retro_load_game(ctypes.byref(info)))

    def av_info(self) -> RetroSystemAvInfo:
        info = RetroSystemAvInfo()
        self.lib.retro_get_system_av_info(ctypes.byref(info))
        return info

    def run(self):
        self.lib.retro_run()

    def reset(self):
        self.lib.retro_reset()

    def set_buttons_by_retro_ids(self, ids: list):
        m = 0
        for i in ids:
            m |= 1 << i
        self._buttons = m

    def clear_audio(self):
        self.audio.samples = []

    def serialize_size(self) -> int:
        return int(self.lib.retro_serialize_size())

    def serialize(self) -> bytes:
        n = self.serialize_size()
        buf = (ctypes.c_uint8 * n)()
        if not bool(self.lib.retro_serialize(buf, n)):
            raise RuntimeError("retro_serialize failed")
        return bytes(buf)

    def unserialize(self, data: bytes) -> bool:
        n = len(data)
        buf = (ctypes.c_uint8 * n).from_buffer_copy(data)
        return bool(self.lib.retro_unserialize(buf, n))

    def close(self):
        self.lib.retro_unload_game()
        self.lib.retro_deinit()


def fnv1a_64(data: bytes) -> int:
    h = 0xcbf29ce484222325
    for b in data:
        h ^= b
        h = (h * 0x100000001b3) & 0xffffffffffffffff
    return h
