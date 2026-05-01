/* Minimal config.h for the Embench rv32emu port.
 * Required by support/chip.c which does #include "config.h" unconditionally.
 * The Python build system would generate this; we provide a static version. */

#define HAVE_BOARDSUPPORT_H 1
/* HAVE_CHIPSUPPORT_H intentionally undefined — no chip-specific file needed */
