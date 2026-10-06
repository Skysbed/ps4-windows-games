#include "pcps4_game.h"
#include "pcps4_status.h"

#include "pcps4_loader.h"
#include "pcps4_winapi.h"

#include <stdio.h>
#include <stdlib.h>

static int read_file(
    const char *path,
    uint8_t **out_data,
    size_t *out_size)
{
    FILE *file;
    long length;
    uint8_t *data;
    size_t got;

    if (path == NULL || out_data == NULL || out_size == NULL)
        return -1;

    *out_data = NULL;
    *out_size = 0;

    file = fopen(path, "rb");
    if (file == NULL)
        return -2;

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -3;
    }

    length = ftell(file);
    if (length <= 0) {
        fclose(file);
        return -4;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -5;
    }

    data = (uint8_t *)malloc((size_t)length);
    if (data == NULL) {
        fclose(file);
        return -6;
    }

    got = fread(data, 1, (size_t)length, file);
    fclose(file);

    if (got != (size_t)length) {
        free(data);
        return -7;
    }

    *out_data = data;
    *out_size = got;
    return 0;
}

int pcps4_game_run_file(const char *path, int32_t *out_entry_ret)
{
    pcps4_pe_resolver resolver;
    uint8_t *exe = NULL;
    size_t exe_size = 0;
    pcps4_loader_status rc;

    if (path == NULL)
        return -1;

    if (read_file(path, &exe, &exe_size) != 0) {
        pcps4_status_log("[pcps4] game load: could not read %s", path);
        return -2;
    }

    pcps4_winapi_build_resolver(&resolver);

    pcps4_status_log("[pcps4] game load: %s (%u bytes)",
           path, (unsigned)exe_size);

    rc = pcps4_loader_run(
        exe,
        exe_size,
        &resolver,
        out_entry_ret);

    free(exe);

    if (rc != PCPS4_LOADER_OK) {
        pcps4_status_log("[pcps4] game load failed: %s",
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
