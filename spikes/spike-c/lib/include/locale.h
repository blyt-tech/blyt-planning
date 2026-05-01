/* locale.h stub for the Lua rv32emu port.
 * Lua calls localeconv() to discover the radix character; we always say "."
 * (C locale) without supporting setlocale. */

#ifndef _LOCALE_H
#define _LOCALE_H

#define LC_ALL      0
#define LC_COLLATE  1
#define LC_CTYPE    2
#define LC_MONETARY 3
#define LC_NUMERIC  4
#define LC_TIME     5

struct lconv {
    char *decimal_point;
    char *thousands_sep;
    char *grouping;
    char *int_curr_symbol;
    char *currency_symbol;
    char *mon_decimal_point;
    char *mon_thousands_sep;
    char *mon_grouping;
    char *positive_sign;
    char *negative_sign;
};

struct lconv *localeconv(void);
char *setlocale(int category, const char *locale);

#endif /* _LOCALE_H */
