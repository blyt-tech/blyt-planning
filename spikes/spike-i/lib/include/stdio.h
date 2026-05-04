/* stdio.h stand-in for the Lua rv32emu port.
 * Lua references FILE*, fopen, fread, fputs, fprintf, snprintf, etc.  We do
 * not link liolib or loadlib (no filesystem in the guest), so the FILE-using
 * surface is unreachable at runtime — but the symbols must satisfy the
 * compiler.  Implementations either go to stdout (printf/fputs to stderr=2 or
 * stdout=1) or are stubs returning EOF/NULL.
 */

#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdarg.h>

typedef struct __FILE FILE;
extern FILE *const stdout;
extern FILE *const stderr;
extern FILE *const stdin;

#define EOF (-1)
#define BUFSIZ 8192
#define L_tmpnam 64
#define FILENAME_MAX 260
#define FOPEN_MAX 16
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

int    printf (const char *fmt, ...) __attribute__((format(printf,1,2)));
int    fprintf(FILE *f, const char *fmt, ...) __attribute__((format(printf,2,3)));
int    sprintf(char *s, const char *fmt, ...) __attribute__((format(printf,2,3)));
int    snprintf(char *s, size_t n, const char *fmt, ...)
       __attribute__((format(printf,3,4)));
int    vsnprintf(char *s, size_t n, const char *fmt, va_list ap);
int    vfprintf(FILE *f, const char *fmt, va_list ap);
int    vprintf(const char *fmt, va_list ap);
int    puts(const char *s);
int    fputs(const char *s, FILE *f);
int    fputc(int c, FILE *f);
int    putc(int c, FILE *f);
int    putchar(int c);
int    fflush(FILE *f);

/* FILE surface — unreachable at runtime in our build, but declared. */
FILE  *fopen  (const char *path, const char *mode);
FILE  *freopen(const char *path, const char *mode, FILE *f);
int    fclose (FILE *f);
size_t fread  (void *p, size_t sz, size_t n, FILE *f);
size_t fwrite (const void *p, size_t sz, size_t n, FILE *f);
int    fseek  (FILE *f, long off, int whence);
long   ftell  (FILE *f);
int    feof   (FILE *f);
int    ferror (FILE *f);
void   clearerr(FILE *f);
int    fgetc  (FILE *f);
int    getc   (FILE *f);
int    getchar(void);
char  *fgets  (char *s, int n, FILE *f);
int    ungetc (int c, FILE *f);
int    setvbuf(FILE *f, char *b, int mode, size_t sz);
FILE  *tmpfile(void);
char  *tmpnam (char *s);
int    remove (const char *p);
int    rename (const char *o, const char *n);
int    sscanf(const char *s, const char *fmt, ...) __attribute__((format(scanf,2,3)));

#endif /* _STDIO_H */
