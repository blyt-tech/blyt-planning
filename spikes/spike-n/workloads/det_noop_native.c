/* Spike N Stage 1 — no-op native cart workload.
 *
 * The simplest possible native cart: increments frame_count,
 * computes some_value = frame * 3, unused_count = frame * 7.
 * Used for the Stage 1 floor test.
 *
 * This file is included via the cart_native_noop.c port shim.
 * Actually the shim itself contains the logic; this file exists
 * as a placeholder to document the workload structure.
 *
 * Real logic lives in ports/rv32emu/cart_native_noop.c which implements:
 *   native_cart_init()
 *   native_cart_update(int frame)
 *   native_cart_emit_digest()
 */

/* Intentionally empty — implementation is in cart_native_noop.c */
