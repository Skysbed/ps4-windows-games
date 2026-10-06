#include "pcps4_module.h"

#include "pcps4_loader.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mman.h>

extern int32_t sceKernelMmap(void *addr, size_t len, int32_t prot,
                             int32_t flags, int32_t fd, off_t offset,
                             void **res);
extern int32_t sceKernelMunmap(void *addr, size_t len);

#define PCPS4_MODULE_PAGE_SIZE 0x4000u
#define PCPS4_MODULE_MAX_FILE (256u * 1024u * 1024u)

static size_t round_page(size_t size)
{
    size_t r;

    if (size == 0 || size > SIZE_MAX - (PCPS4_MODULE_PAGE_SIZE - 1u))
        return 0;

    r = size + PCPS4_MODULE_PAGE_SIZE - 1u;
    r -= r % PCPS4_MODULE_PAGE_SIZE;
    return r;
}

static int normalize_module_name(
    const char *input, char *output, size_t output_size)
{
    const char *base;
    size_t length;
    size_t i;

    if (input == NULL || output == NULL || output_size < 2)
        return -1;

    base = strrchr(input, '/');
    if (base == NULL)
        base = strrchr(input, 92);
    base = base != NULL ? base + 1 : input;

    if (*base == '\0')
        return -1;

    length = strlen(base);
    if (length >= output_size)
        return -1;

    for (i = 0; i < length; ++i) {
        char ch = base[i];

        if (ch == '.' && i + 1 < length && base[i + 1] == '.')
            return -1;
        if (ch >= 'A' && ch <= 'Z')
            ch = (char)(ch - 'A' + 'a');
        output[i] = ch;
    }

    output[length] = '\0';

    if (strchr(output, '.') == NULL) {
        static const char suffix[] = ".dll";
        if (length + sizeof(suffix) > output_size)
            return -1;
        memcpy(output + length, suffix, sizeof(suffix));
    }

    return 0;
}

static int name_equal_ci(const char *a, const char *b)
{
    unsigned char ca;
    unsigned char cb;

    if (a == NULL || b == NULL)
        return 0;

    while (*a != '\0' && *b != '\0') {
        ca = (unsigned char)*a++;
        cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z')
            ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb)
            return 0;
    }

    return *a == '\0' && *b == '\0';
}

static int find_bundle_file(
    const char *module_name, char *out_path, size_t out_path_size)
{
    char normalized[128];
    char exact[1024];
    DIR *dir;
    struct dirent *entry;

    if (normalize_module_name(
            module_name, normalized, sizeof(normalized)) != 0)
        return -1;

    if (snprintf(
            exact, sizeof(exact), "/app0/game/%s", normalized) <= 0)
        return -1;

    {
        FILE *f = fopen(exact, "rb");
        if (f != NULL) {
            fclose(f);
            if (strlen(exact) + 1 > out_path_size)
                return -1;
            strcpy(out_path, exact);
            return 0;
        }
    }

    dir = opendir("/app0/game");
    if (dir == NULL)
        return 1;

    while ((entry = readdir(dir)) != NULL) {
        char candidate[1024];

        if (!name_equal_ci(entry->d_name, normalized))
            continue;
        if (snprintf(
                candidate, sizeof(candidate),
                "/app0/game/%s", entry->d_name) <= 0)
            continue;
        if (strlen(candidate) + 1 <= out_path_size) {
            strcpy(out_path, candidate);
            closedir(dir);
            return 0;
        }
    }

    closedir(dir);
    return 1;
}

static int read_file(
    const char *path, uint8_t **out_data, size_t *out_size)
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
        return 1;

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }

    length = ftell(file);
    if (length <= 0 || (uint64_t)length > PCPS4_MODULE_MAX_FILE) {
        fclose(file);
        return -1;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }

    data = (uint8_t *)malloc((size_t)length);
    if (data == NULL) {
        fclose(file);
        return -1;
    }

    got = fread(data, 1, (size_t)length, file);
    fclose(file);

    if (got != (size_t)length) {
        free(data);
        return -1;
    }

    *out_data = data;
    *out_size = got;
    return 0;
}

const pcps4_loaded_module *pcps4_module_loader_find(
    const pcps4_module_loader *loader, const char *name)
{
    char normalized[128];
    uint32_t i;

    if (loader == NULL || name == NULL ||
        normalize_module_name(name, normalized, sizeof(normalized)) != 0)
        return NULL;

    for (i = 0; i < loader->count; ++i) {
        if (name_equal_ci(loader->modules[i].name, normalized))
            return &loader->modules[i];
    }

    return NULL;
}

struct export_context {
    pcps4_module_loader *loader;
    pcps4_loaded_module *module;
    int error;
    uint32_t registered;
    uint32_t forwarded;
};

static int export_is_in_image(
    const pcps4_pe_image *image, uint32_t function_rva)
{
    uint16_t i;

    if (function_rva >= image->size_of_image)
        return 0;

    for (i = 0; i < image->section_count; ++i) {
        pcps4_pe_section section;
        uint32_t span;

        if (pcps4_pe_get_section(image, i, &section) != PCPS4_PE_OK)
            return 0;

        span = section.virtual_size > section.raw_size
            ? section.virtual_size : section.raw_size;

        if (function_rva >= section.virtual_address &&
            function_rva - section.virtual_address < span)
            return 1;
    }

    return 0;
}

static int register_export(
    void *user,
    const char *name,
    uint32_t ordinal,
    uint32_t function_rva,
    const char *forwarder)
{
    struct export_context *ctx = (struct export_context *)user;
    void *address;

    if (ctx == NULL || ctx->module == NULL)
        return 1;

    if (forwarder != NULL) {
        ++ctx->forwarded;
        printf("[pcps4] DLL %s: forwarded export %s -> %s\n",
               ctx->module->name,
               name != NULL ? name : "(ordinal)",
               forwarder);
        return 0;
    }

    if (!export_is_in_image(&ctx->module->image, function_rva)) {
        ctx->error = PCPS4_MODULE_ERR_PARSE;
        return 1;
    }

    address = ctx->module->memory + function_rva;

    if (name != NULL) {
        if (pcps4_pe_resolver_register(
                ctx->loader->resolver,
                ctx->module->name,
                name,
                address) != PCPS4_PE_OK) {
            ctx->error = PCPS4_MODULE_ERR_IMPORTS;
            return 1;
        }
        ++ctx->registered;
    }

    if (ordinal <= 0xFFFFu) {
        if (pcps4_pe_resolver_register_ordinal(
                ctx->loader->resolver,
                ctx->module->name,
                (uint16_t)ordinal,
                address) != PCPS4_PE_OK) {
            ctx->error = PCPS4_MODULE_ERR_IMPORTS;
            return 1;
        }
    }

    return 0;
}

struct dependency_context {
    pcps4_module_loader *loader;
    int error;
};

static int preload_dependency(
    void *user,
    const char *dll,
    int by_name,
    const char *name,
    uint16_t ordinal)
{
    struct dependency_context *ctx = (struct dependency_context *)user;
    void *fn;
    pcps4_module_status rc;

    if (ctx == NULL || ctx->loader == NULL)
        return 1;

    fn = by_name
        ? pcps4_pe_resolver_lookup(ctx->loader->resolver, dll, name)
        : pcps4_pe_resolver_lookup_ordinal(
              ctx->loader->resolver, dll, ordinal);

    if (fn != NULL)
        return 0;

    rc = pcps4_module_loader_load(ctx->loader, dll);
    if (rc == PCPS4_MODULE_OK || rc == PCPS4_MODULE_NOT_FOUND)
        return 0;

    ctx->error = (int)rc;
    return 1;
}

void pcps4_module_loader_init(
    pcps4_module_loader *loader, pcps4_pe_resolver *resolver)
{
    if (loader == NULL)
        return;

    memset(loader, 0, sizeof(*loader));
    loader->resolver = resolver;
}

pcps4_module_status pcps4_module_loader_load(
    pcps4_module_loader *loader, const char *name)
{
    char normalized[128];
    char path[1024];
    uint8_t *file_data = NULL;
    size_t file_size = 0;
    pcps4_pe_image image;
    pcps4_pe_status prc;
    pcps4_module_status result;
    pcps4_loaded_module *module;
    struct export_context export_ctx;
    struct dependency_context dep_ctx;
    uint32_t resolved = 0;
    uint32_t unresolved = 0;
    size_t map_size;
    void *memory = NULL;
    int32_t krc;

    if (loader == NULL || loader->resolver == NULL || name == NULL)
        return PCPS4_MODULE_ERR_NULL;

    if (normalize_module_name(name, normalized, sizeof(normalized)) != 0)
        return PCPS4_MODULE_NOT_FOUND;

    module = (pcps4_loaded_module *)pcps4_module_loader_find(
        loader, normalized);
    if (module != NULL)
        return module->loaded || module->loading
            ? PCPS4_MODULE_OK
            : PCPS4_MODULE_ERR_PARSE;

    if (loader->count >= PCPS4_MODULE_MAX_COUNT)
        return PCPS4_MODULE_ERR_ALLOC;

    result = (pcps4_module_status)find_bundle_file(
        normalized, path, sizeof(path));
    if (result != 0)
        return result == PCPS4_MODULE_NOT_FOUND
            ? PCPS4_MODULE_NOT_FOUND
            : PCPS4_MODULE_ERR_IO;

    if (read_file(path, &file_data, &file_size) != 0)
        return PCPS4_MODULE_ERR_IO;

    prc = pcps4_pe_parse(file_data, file_size, &image);
    if (prc != PCPS4_PE_OK) {
        printf("[pcps4] DLL %s: PE parse failed: %s\n",
               normalized, pcps4_pe_status_str(prc));
        free(file_data);
        return PCPS4_MODULE_ERR_PARSE;
    }

    map_size = round_page(image.size_of_image);
    if (map_size == 0) {
        free(file_data);
        return PCPS4_MODULE_ERR_PARSE;
    }

    krc = sceKernelMmap(
        NULL,
        map_size,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANON,
        -1,
        0,
        &memory);
    if (krc != 0 || memory == NULL) {
        printf("[pcps4] DLL %s: mmap failed: 0x%x\n",
               normalized, (unsigned)krc);
        free(file_data);
        return PCPS4_MODULE_ERR_ALLOC;
    }

    prc = pcps4_pe_map_image(
        &image,
        (uint8_t *)memory,
        map_size,
        (uint64_t)(uintptr_t)memory);
    free(file_data);

    if (prc != PCPS4_PE_OK) {
        printf("[pcps4] DLL %s: image map failed: %s\n",
               normalized, pcps4_pe_status_str(prc));
        sceKernelMunmap(memory, map_size);
        return PCPS4_MODULE_ERR_PARSE;
    }

    module = &loader->modules[loader->count++];
    memset(module, 0, sizeof(*module));
    strncpy(module->name, normalized, sizeof(module->name) - 1);
    module->memory = (uint8_t *)memory;
    module->map_size = map_size;
    module->image = image;
    module->loading = 1;

    printf("[pcps4] loading local DLL %s (%u bytes)\n",
           module->name, (unsigned)image.size_of_image);

    memset(&export_ctx, 0, sizeof(export_ctx));
    export_ctx.loader = loader;
    export_ctx.module = module;

    prc = pcps4_pe_iterate_exports(
        &module->image, register_export, &export_ctx);
    if (prc != PCPS4_PE_OK || export_ctx.error != 0) {
        printf("[pcps4] DLL %s: export registration failed\n",
               module->name);
        return PCPS4_MODULE_ERR_IMPORTS;
    }

    printf("[pcps4] DLL %s: exports registered=%u forwarded=%u\n",
           module->name,
           (unsigned)export_ctx.registered,
           (unsigned)export_ctx.forwarded);

    memset(&dep_ctx, 0, sizeof(dep_ctx));
    dep_ctx.loader = loader;

    prc = pcps4_pe_iterate_imports(
        &module->image, preload_dependency, &dep_ctx);
    if (prc != PCPS4_PE_OK || dep_ctx.error != 0) {
        printf("[pcps4] DLL %s: dependency scan failed\n",
               module->name);
        return PCPS4_MODULE_ERR_IMPORTS;
    }

    prc = pcps4_pe_bind_imports(
        &module->image,
        module->memory,
        module->map_size,
        loader->resolver,
        &resolved,
        &unresolved);
    if (prc != PCPS4_PE_OK) {
        printf("[pcps4] DLL %s: import bind failed: %s\n",
               module->name, pcps4_pe_status_str(prc));
        return PCPS4_MODULE_ERR_IMPORTS;
    }

    printf("[pcps4] DLL %s: imports resolved=%u unresolved=%u\n",
           module->name, (unsigned)resolved, (unsigned)unresolved);

    if (unresolved != 0) {
        printf("[pcps4] DLL %s: unresolved imports prevent initialization\n",
               module->name);
        (void)pcps4_pe_report_unresolved_imports(
            &module->image,
            loader->resolver,
            /* Use the generic loader logger by printing here. */
            [](void *unused, const char *dll, int by_name,
               const char *import_name, uint16_t ordinal) -> int {
                (void)unused;
                if (by_name)
                    printf("[pcps4] unresolved import: %s!%s\n",
                           dll, import_name);
                else
                    printf("[pcps4] unresolved import: %s!#%u\n",
                           dll, (unsigned)ordinal);
                return 0;
            },
            NULL);
        return PCPS4_MODULE_ERR_IMPORTS;
    }

    if (pcps4_loader_apply_image_protections(
            &module->image, module->memory, module->map_size) != 0) {
        printf("[pcps4] DLL %s: section protection failed\n",
               module->name);
        return PCPS4_MODULE_ERR_PROTECT;
    }

    if (pcps4_loader_run_tls_callbacks(
            &module->image, module->memory, module->map_size) != 0) {
        printf("[pcps4] DLL %s: TLS initialization failed\n",
               module->name);
        return PCPS4_MODULE_ERR_INIT;
    }

    if (pcps4_loader_run_dll_attach(
            &module->image, module->memory, module->map_size) != 0) {
        printf("[pcps4] DLL %s: DllMain(DLL_PROCESS_ATTACH) failed\n",
               module->name);
        return PCPS4_MODULE_ERR_INIT;
    }

    module->loading = 0;
    module->loaded = 1;
    printf("[pcps4] DLL %s loaded\n", module->name);
    return PCPS4_MODULE_OK;
}

pcps4_module_status pcps4_module_loader_preload_imports(
    pcps4_module_loader *loader, const pcps4_pe_image *image)
{
    struct dependency_context ctx;
    pcps4_pe_status rc;

    if (loader == NULL || loader->resolver == NULL || image == NULL)
        return PCPS4_MODULE_ERR_NULL;

    memset(&ctx, 0, sizeof(ctx));
    ctx.loader = loader;

    rc = pcps4_pe_iterate_imports(image, preload_dependency, &ctx);
    if (rc != PCPS4_PE_OK)
        return PCPS4_MODULE_ERR_IMPORTS;

    if (ctx.error != 0)
        return (pcps4_module_status)ctx.error;

    return PCPS4_MODULE_OK;
}

const char *pcps4_module_status_str(pcps4_module_status status)
{
    switch (status) {
        case PCPS4_MODULE_OK: return "ok";
        case PCPS4_MODULE_NOT_FOUND: return "not found in game directory";
        case PCPS4_MODULE_ERR_NULL: return "null argument";
        case PCPS4_MODULE_ERR_IO: return "module file I/O failed";
        case PCPS4_MODULE_ERR_PARSE: return "module PE parse/map failed";
        case PCPS4_MODULE_ERR_ALLOC: return "module memory allocation failed";
        case PCPS4_MODULE_ERR_IMPORTS: return "module exports/imports failed";
        case PCPS4_MODULE_ERR_PROTECT: return "module memory protection failed";
        case PCPS4_MODULE_ERR_INIT: return "module initialization failed";
        default: return "unknown";
    }
}
