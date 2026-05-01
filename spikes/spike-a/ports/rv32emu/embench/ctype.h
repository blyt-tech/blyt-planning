/* Minimal ctype.h for the Embench rv32emu port.
 * Overrides the system ctype.h (found first via -I.) to avoid pulling in
 * glibc's __ctype_b_loc() locale machinery which is not available with
 * -nostdlib.  Functions are defined as inline macros; libsys.c also provides
 * the out-of-line definitions for code that takes their address. */

#ifndef _CTYPE_H
#define _CTYPE_H

static inline int isdigit(int c)  { return c >= '0' && c <= '9'; }
static inline int isspace(int c)  { return c == ' ' || c == '\t' || c == '\n'
                                        || c == '\r' || c == '\f' || c == '\v'; }
static inline int isalpha(int c)  { return (c >= 'a' && c <= 'z')
                                        || (c >= 'A' && c <= 'Z'); }
static inline int isalnum(int c)  { return isalpha(c) || isdigit(c); }
static inline int isupper(int c)  { return c >= 'A' && c <= 'Z'; }
static inline int islower(int c)  { return c >= 'a' && c <= 'z'; }
static inline int isprint(int c)  { return c >= 0x20 && c < 0x7f; }
static inline int isgraph(int c)  { return c > 0x20 && c < 0x7f; }
static inline int ispunct(int c)  { return isgraph(c) && !isalnum(c); }
static inline int iscntrl(int c)  { return (c >= 0 && c < 0x20) || c == 0x7f; }
static inline int isxdigit(int c) { return isdigit(c)
                                        || (c >= 'a' && c <= 'f')
                                        || (c >= 'A' && c <= 'F'); }
static inline int tolower(int c)  { return isupper(c) ? c + 32 : c; }
static inline int toupper(int c)  { return islower(c) ? c - 32 : c; }

#endif /* _CTYPE_H */
