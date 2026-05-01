/* Minimal math.h for the Embench rv32emu port.
 * huffbench includes math.h but calls no math functions.
 * wikisort calls sqrt() — implementation in libsys.c (integer Newton-Raphson).
 * fabs/fabsf use IEEE 754 bit manipulation (libsys.c), no softfloat needed. */

#ifndef _MATH_H
#define _MATH_H

double sqrt (double x);
double fabs (double x);
float  fabsf(float x);

#endif /* _MATH_H */
