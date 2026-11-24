/* Host test for __wrap_fchdir: a descriptor number reused behind the path table must never change the directory.
 * Built with -D__PROSPERO__ and --wrap for every call pw_wine_cwd_libc_ps4.c wraps. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int __real_open(const char *path, int flags, ...);
int __real_close(int fd);

static int failures;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

static int cwd_is(const char *want)
{
    char now[PATH_MAX];
    return getcwd(now, sizeof(now)) && !strcmp(now, want);
}

int main(void)
{
    char base[] = "/tmp/fchdir-test-XXXXXX", a[128], b[128], c[128], d[128], file[128];
    int fa, fb, fc, fd, ff, r;

    if (!mkdtemp(base)) return 2;
    snprintf(a, sizeof(a), "%s/a", base);
    snprintf(b, sizeof(b), "%s/b", base);
    snprintf(c, sizeof(c), "%s/c", base);
    snprintf(d, sizeof(d), "%s/d", base);
    snprintf(file, sizeof(file), "%s/file", base);
    mkdir(a, 0700); mkdir(b, 0700); mkdir(c, 0700); mkdir(d, 0700);

    fa = open(a, O_RDONLY);
    check(fa >= 0 && fchdir(fa) == 0 && cwd_is(a), "fchdir to a directory opened normally");
    check(fchdir(fa) == 0 && cwd_is(a), "repeat fchdir on the same descriptor");

    /* Another module closes the number and reuses it without this module seeing either call. */
    __real_close(fa);
    fb = __real_open(b, O_RDONLY);
    check(fb == fa, "test setup reused the descriptor number");
    errno = 0;
    r = fchdir(fb);
    check(r == -1 && errno == EACCES && cwd_is(a), "reused number is refused and the directory stays put");
    __real_close(fb);

    fc = open(c, O_RDONLY);
    fd = open(d, O_RDONLY);
    check(fc >= 0 && fd >= 0 && fchdir(fc) == 0 && cwd_is(c), "second directory works");
    check(dup2(fd, fc) == fc, "test setup replaced the descriptor with dup2");
    errno = 0;
    r = fchdir(fc);
    check(r == -1 && errno == EACCES && cwd_is(c), "dup2 replacement is refused and the directory stays put");
    close(fc);

    fc = open(c, O_RDONLY);
    check(fc >= 0 && fchdir(fc) == 0 && cwd_is(c), "a normal close and reopen works again");
    check(fchdir(fd) == 0 && cwd_is(d), "the other descriptor still works");

    ff = open(file, O_CREAT | O_RDWR, 0600);
    errno = 0;
    r = fchdir(ff);
    check(ff >= 0 && r == -1 && errno == ENOTDIR, "a file is still refused");

    close(ff); close(fc); close(fd);
    unlink(file); rmdir(a); rmdir(b); rmdir(c); rmdir(d); rmdir(base);
    printf("%s, %d failed\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
