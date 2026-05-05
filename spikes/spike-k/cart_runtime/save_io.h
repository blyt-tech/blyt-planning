/* Spike K — minimal host file-IO surface for the load-side cart.
 *
 * The save cart writes the buffer hex to stdout via printf — that path
 * already exists in spike-b's lua_runtime.c.  The load cart needs to
 * read the buffer hex back from a host-side file (the harness redirects
 * the save cart's stdout to that file, then runs the load cart with
 * the file path as argv[1]).  None of spike-d's runtime exposes
 * open/read/close — they're added here, in spike-K, as direct ecalls.
 *
 * Calls return -1 on error (file missing, read past end, etc.); the
 * cart treats any error as a fatal harness bug — there is no recovery
 * path for "the buffer file we were told about does not exist".
 */

#ifndef CART_RUNTIME_SAVE_IO_H
#define CART_RUNTIME_SAVE_IO_H

#include <stdint.h>
#include <stddef.h>

/* Read up to `cap` bytes from `path` into `buf`.  Returns the number of
 * bytes read, or -1 on error.  On success the file's contents are an
 * exact prefix of `buf[0..result]`. */
long save_io_read_file(const char *path, void *buf, size_t cap);

/* Parse a hex blob in `src[..len]` (with optional trailing whitespace)
 * into `dst[..cap]`.  The blob may have a `BUFFER <frame> ` prefix —
 * if so, that prefix is consumed.  Returns the number of bytes decoded,
 * or -1 on parse error (odd hex digit count, non-hex char, dst overflow). */
long save_io_parse_hex(const char *src, size_t len, uint8_t *dst, size_t cap);

#endif /* CART_RUNTIME_SAVE_IO_H */
