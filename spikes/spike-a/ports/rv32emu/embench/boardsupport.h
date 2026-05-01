/* Embench board support header for rv32emu.
 * Included by support/support.h when HAVE_BOARDSUPPORT_H is defined. */

#ifndef BOARDSUPPORT_H
#define BOARDSUPPORT_H

/* warm_caches() call count: 1 iteration is enough to prime L1 in an emulator. */
#define WARMUP_HEAT 1

#endif /* BOARDSUPPORT_H */
