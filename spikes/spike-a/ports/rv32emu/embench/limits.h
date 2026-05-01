/* limits.h for the Embench rv32emu port (rv32, ILP32).
 * wikisort includes <limits.h> for LONG_MAX/LONG_MIN. */

#ifndef _LIMITS_H
#define _LIMITS_H

#define CHAR_BIT    8
#define SCHAR_MIN   (-128)
#define SCHAR_MAX   127
#define UCHAR_MAX   255U
#define CHAR_MIN    SCHAR_MIN
#define CHAR_MAX    SCHAR_MAX
#define SHRT_MIN    (-32768)
#define SHRT_MAX    32767
#define USHRT_MAX   65535U
#define INT_MIN     (-2147483647 - 1)
#define INT_MAX     2147483647
#define UINT_MAX    4294967295U
#define LONG_MIN    (-2147483647L - 1L)   /* rv32: long is 32-bit */
#define LONG_MAX    2147483647L
#define ULONG_MAX   4294967295UL

#endif /* _LIMITS_H */
