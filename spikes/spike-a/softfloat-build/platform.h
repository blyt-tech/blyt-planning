/* SoftFloat platform.h for fc32 Spike A — RV32, little-endian, freestanding.
 * Derived from build/template-not-FAST_INT64/platform.h. */

#define LITTLEENDIAN 1
#define INLINE inline

/* No thread-local storage: we're freestanding (no pthread, single-threaded). */
#define THREAD_LOCAL

#include "opts-GCC.h"
