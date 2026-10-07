#include "pcps4_game.h"

#if PCPS4_DXVK
/* vulkan-ps4 driver breadcrumbs -> /data/vk_ps4_breadcrumb.log (weak: absent without the GPU stack) */
extern void vk_ps4_log_open(const char *path) __attribute__((weak));
#endif
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
    /* ~230 KB: keep it off the stack, and valid for GetProcAddress later. */
    static pcps4_pe_resolver resolver;
    uint8_t *exe = NULL;
    size_t exe_size = 0;
    pcps4_loader_status rc;

    if (path == NULL)
        return -1;

    /* Route PE loading, import resolution, and execution messages to the
     * package-versioned Windows-game log, separate from runtime status. */
    pcps4_status_game_log_begin();

    if (read_file(path, &exe, &exe_size) != 0) {
        pcps4_status_log("[pcps4] game load: could not read %s", path);
        pcps4_status_game_log_end();
        return -2;
    }

    pcps4_winapi_build_resolver(&resolver);

#if PCPS4_DXVK
    if (vk_ps4_log_open) vk_ps4_log_open(NULL);
#endif

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
        pcps4_status_game_log_end();
        return (int)rc;
    }

    pcps4_status_log("[pcps4] Windows game execution returned successfully");
    pcps4_status_game_log_end();
    return 0;
}

int pcps4_game_run_bundled(int32_t *out_entry_ret)
{
    return pcps4_game_run_file(
        "/app0/game/game.exe",
        out_entry_ret);
}
