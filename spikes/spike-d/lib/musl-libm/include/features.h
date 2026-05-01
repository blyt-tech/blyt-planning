/* Shim for musl's <features.h>, included by the *_data.h headers we
 * vendor.  The data headers only need the `hidden` visibility macro;
 * everything else from the system features.h (glibc-style time64 /
 * `bits/wordsize.h` machinery) we want to keep out of the freestanding
 * RV32 build. */

#ifndef _FEATURES_H
#define _FEATURES_H

#ifndef hidden
#define hidden __attribute__((__visibility__("hidden")))
#endif

#endif /* _FEATURES_H */
