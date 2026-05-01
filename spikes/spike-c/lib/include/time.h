/* time.h stand-in for the Lua rv32emu port. */

#ifndef _TIME_H
#define _TIME_H

#include <stddef.h>

typedef long time_t;
typedef long clock_t;
typedef long suseconds_t;

#define CLOCKS_PER_SEC 1000000L
#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

struct timespec {
    time_t tv_sec;
    long   tv_nsec;
};

struct tm {
    int tm_sec, tm_min, tm_hour;
    int tm_mday, tm_mon, tm_year;
    int tm_wday, tm_yday, tm_isdst;
};

clock_t clock(void);
time_t  time(time_t *);
double  difftime(time_t, time_t);
int     clock_gettime(int clk_id, struct timespec *ts);
size_t  strftime(char *s, size_t max, const char *fmt, const struct tm *tm);
struct tm *gmtime(const time_t *t);
struct tm *localtime(const time_t *t);
time_t  mktime(struct tm *t);

#endif /* _TIME_H */
