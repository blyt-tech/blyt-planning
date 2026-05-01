/* errno.h for the Lua rv32emu port — single-threaded, freestanding. */

#ifndef _ERRNO_H
#define _ERRNO_H

extern int errno;

#define EDOM    33
#define EILSEQ  84
#define ERANGE  34
#define ENOMEM  12
#define EINVAL  22
#define ENOENT  2
#define EIO     5
#define EACCES  13
#define ENOSYS  38

#endif /* _ERRNO_H */
