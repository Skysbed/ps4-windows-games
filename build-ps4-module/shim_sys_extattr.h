#ifndef _PS4SHIM_SYS_EXTATTR_H
#define _PS4SHIM_SYS_EXTATTR_H
#include <sys/types.h>
/* PS4 has no extended attributes; prospero's compat layer defines these as stubs. */
ssize_t extattr_get_fd(int, int, const char *, void *, size_t);
ssize_t extattr_get_file(const char *, int, const char *, void *, size_t);
ssize_t extattr_set_fd(int, int, const char *, const void *, size_t);
int extattr_delete_fd(int, int, const char *);
#endif
