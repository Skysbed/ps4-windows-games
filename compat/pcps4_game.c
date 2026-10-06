#include "pcps4_game.h"

#include "pcps4_loader.h"
#include "pcps4_winapi.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    PCPS4_GAME_READ_BAD_ARGUMENT = -1,
    PCPS4_GAME_READ_OPEN = -2,
    PCPS4_GAME_READ_SEEK_END = -3,
    PCPS4_GAME_READ_TELL = -4,
    PCPS4_GAME_READ_SEEK_START = -5,
    PCPS4_GAME_READ_ALLOC = -6,
    PCPS4_GAME_READ_READ = -7,
};

static int read_file(
    const char *path,
    uint8_t **out_data,
    size_t *out_size)
{
    FILE *file;
    long length;
    uint8_t *data;
    size_t got;
    int saved_errno = 0;

    if (path == NULL || out_data == NULL || out_size == NULL)
        return PCPS4_GAME_READ_BAD_ARGUMENT;

    *out_data = NULL;
    *out_size = 0;

    errno = 0;
    file = fopen(path, "rb");
    saved_errno = errno;
    if (file == NULL) {
        printf("[pcps4] game file: fopen failed path=%s errno=%d (%s)\n",
               path, saved_errno, strerror(saved_errno));
        return PCPS4_GAME_READ_OPEN;
    }

    errno = 0;
    if (fseek(file, 0, SEEK_END) != 0) {
        saved_errno = errno;
        printf("[pcps4] game file: fseek(SEEK_END) failed errno=%d (%s)\n",
               saved_errno, strerror(saved_errno));
        fclose(file);
        return PCPS4_GAME_READ_SEEK_END;
    }

    errno = 0;
    length = ftell(file);
    saved_errno = errno;
    if (length <= 0) {
        printf("[pcps4] game file: ftell returned %ld errno=%d (%s)\n",
               length, saved_errno, strerror(saved_errno));
        fclose(file);
        return PCPS4_GAME_READ_TELL;
    }

    errno = 0;
    if (fseek(file, 0, SEEK_SET) != 0) {
        saved_errno = errno;
        printf("[pcps4] game file: fseek(SEEK_SET) failed errno=%d (%s)\n",
               saved_errno, strerror(saved_errno));
        fclose(file);
        return PCPS4_GAME_READ_SEEK_START;
    }

    data = (uint8_t *)malloc((size_t)length);
    if (data == NULL) {
        printf("[pcps4] game file: malloc failed size=%u\n",
               (unsigned)length);
        fclose(file);
        return PCPS4_GAME_READ_ALLOC;
    }

    errno = 0;
    got = fread(data, 1, (size_t)length, file);
    saved_errno = errno;
    fclose(file);

    if (got != (size_t)length) {
        printf("[pcps4] game file: fread short read got=%u expected=%u "
               "errno=%d (%s)\n",
               (unsigned)got, (unsigned)length,
               saved_errno, strerror(saved_errno));
        free(data);
        return PCPS4_GAME_READ_READ;
    }

    *out_data = data;
    *out_size = got;
    printf("[pcps4] game file: read OK bytes=%u\n", (unsigned)got);
    return 0;
}

static void log_game_read_diagnostic(const char *path, int read_rc)
{
    printf("[pcps4] game load blocked before PE parser: path=%s read_rc=%d\n",
           path, read_rc);
    printf("[pcps4] game load hint: verify the installed package contains "
           "game/game.exe under /app0\n");
}

int pcps4_game_run_file(const char *path, int32_t *out_entry_ret)
{
    pcps4_pe_resolver resolver;
    uint8_t *exe = NULL;
    size_t exe_size = 0;
    pcps4_loader_status rc;
    int read_rc;

    if (path == NULL)
        return PCPS4_GAME_READ_BAD_ARGUMENT;

    read_rc = read_file(path, &exe, &exe_size);
    if (read_rc != 0) {
        log_game_read_diagnostic(path, read_rc);
        return read_rc;
    }

    pcps4_winapi_build_resolver(&resolver);

    printf("[pcps4] game load: %s (%u bytes)\n",
           path, (unsigned)exe_size);

    rc = pcps4_loader_run(
        exe,
        exe_size,
        &resolver,
        out_entry_ret);

    free(exe);

    if (rc != PCPS4_LOADER_OK) {
        printf("[pcps4] game load failed: %s\n",
               pcps4_loader_status_str(rc));
        return (int)rc;
    }

    return 0;
}

int pcps4_game_run_bundled(int32_t *out_entry_ret)
{
    return pcps4_game_run_file(
        "/app0/game/game.exe",
        out_entry_ret);
}
