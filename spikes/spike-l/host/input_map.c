/* Spike L — input mapping (ADR-0017 ↔ libretro RetroPad).
 *
 * Libretro's standard RETRO_DEVICE_JOYPAD exposes the SNES-style
 * button set. ADR-0017 maps cleanly onto it with the conventional
 * A/B and X/Y swaps:
 *
 *   ADR-0017 A == primary action == south position == JOYPAD_B
 *   ADR-0017 B == secondary       == east position  == JOYPAD_A
 *
 * The retro_input_descriptor strings expose the ADR-0017 names in
 * RetroArch's input config UI so users see "A" / "B" / "L" / "R"
 * etc. — not RetroPad's letters.
 */

#include "vendor/libretro.h"
#include <stdint.h>

#include "blyt_facade.h"

const struct retro_input_descriptor adapter_input_descriptors[] = {
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,     "Up"     },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "Down"   },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "Left"   },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,  "Right"  },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,      "A"      },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,      "B"      },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,      "X"      },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,      "Y"      },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,      "L"      },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,      "R"      },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,  "Start"  },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },
    { 0 },
};

uint16_t adapter_poll_button_mask(retro_input_state_t cb, int port) {
    if (!cb) return 0;
    uint16_t m = 0;
#define BIT(retro_id, blyt_bit) \
    if (cb(port, RETRO_DEVICE_JOYPAD, 0, (retro_id))) m |= (blyt_bit)
    BIT(RETRO_DEVICE_ID_JOYPAD_UP,     BLYT_BUTTON_UP);
    BIT(RETRO_DEVICE_ID_JOYPAD_DOWN,   BLYT_BUTTON_DOWN);
    BIT(RETRO_DEVICE_ID_JOYPAD_LEFT,   BLYT_BUTTON_LEFT);
    BIT(RETRO_DEVICE_ID_JOYPAD_RIGHT,  BLYT_BUTTON_RIGHT);
    BIT(RETRO_DEVICE_ID_JOYPAD_B,      BLYT_BUTTON_A);   /* south = A */
    BIT(RETRO_DEVICE_ID_JOYPAD_A,      BLYT_BUTTON_B);   /* east  = B */
    BIT(RETRO_DEVICE_ID_JOYPAD_Y,      BLYT_BUTTON_X);
    BIT(RETRO_DEVICE_ID_JOYPAD_X,      BLYT_BUTTON_Y);
    BIT(RETRO_DEVICE_ID_JOYPAD_L,      BLYT_BUTTON_L);
    BIT(RETRO_DEVICE_ID_JOYPAD_R,      BLYT_BUTTON_R);
    BIT(RETRO_DEVICE_ID_JOYPAD_START,  BLYT_BUTTON_START);
    BIT(RETRO_DEVICE_ID_JOYPAD_SELECT, BLYT_BUTTON_SELECT);
#undef BIT
    return m;
}
