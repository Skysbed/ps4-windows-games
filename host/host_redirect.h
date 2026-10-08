/* Force-included when building the runtime for the Mac test harness.
 * Redirects the console's /app0 and /data paths into a sandbox directory. */
#ifndef PCPS4_HOST_REDIRECT_H
#define PCPS4_HOST_REDIRECT_H
#include <stdio.h>
FILE *host_fopen(const char *path, const char *mode);
#define fopen host_fopen
#endif
