#include "pcps4_dll.h"
#include "pcps4_loader.h"
#include "pcps4_status.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mman.h>

extern int sceKernelUsleep(unsigned int microseconds);
extern int32_t sceKernelMmap(void *addr, size_t len, int32_t prot,
                             int32_t flags, int32_t fd, off_t offset,
                             void **res);
extern int32_t sceKernelMunmap(void *addr, size_t len);

#define PCPS4_DLL_MAX_MODULES 96u
#define PCPS4_DLL_NAME_MAX 128u
#define PCPS4_DLL_PATH_MAX 1024u
#define PCPS4_DLL_MAGIC 0x5043444cu

#define DLL_PROCESS_DETACH 0u
#define DLL_PROCESS_ATTACH 1u

#if defined(__x86_64__) && (defined(__clang__) || defined(__GNUC__))
#define PCPS4_MS_ABI __attribute__((ms_abi))
#else
#define PCPS4_MS_ABI
#endif

typedef int (PCPS4_MS_ABI *pcps4_dll_entry_fn)(
    void *module,
    uint32_t reason,
    void *reserved);

typedef struct pcps4_dll_module {
    uint32_t magic;
    uint32_t refs;
    uint32_t loading;
    void *base;
    size_t map_size;
    char name[PCPS4_DLL_NAME_MAX];
    pcps4_pe_image image;
} pcps4_dll_module;

static pcps4_dll_module g_modules[PCPS4_DLL_MAX_MODULES];
static volatile int g_lock;

static void lock(void)
{
    while (__atomic_exchange_n(&g_lock, 1, __ATOMIC_ACQUIRE) != 0)
        (void)sceKernelUsleep(100);
}

static void unlock(void)
{
    __atomic_store_n(&g_lock, 0, __ATOMIC_RELEASE);
}

static void normalize_name(const char *path, char *out, size_t out_size)
{
    const char *base;
    size_t n = 0;

    if (out == NULL || out_size == 0)
        return;

    out[0] = '\0';
    if (path == NULL)
        return;

    base = strrchr(path, '/');
    {
        const char *bs = strrchr(path, 92);
        if (bs != NULL && (base == NULL || bs > base))
            base = bs;
    }

    base = base ? base + 1 : path;

    while (base[n] != '\0' && n + 1 < out_size) {
        char c = base[n];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        out[n++] = c;
    }
    out[n] = '\0';

    if (strchr(out, '.') == NULL && n + 4 < out_size)
        memcpy(out + n, ".dll", 5);
}

static pcps4_dll_module *find_name_locked(const char *name)
{
    char normalized[PCPS4_DLL_NAME_MAX];

    normalize_name(name, normalized, sizeof(normalized));
    if (normalized[0] == '\0')
        return NULL;

    for (size_t i = 0; i < PCPS4_DLL_MAX_MODULES; ++i) {
        if (g_modules[i].magic == PCPS4_DLL_MAGIC &&
            strcmp(g_modules[i].name, normalized) == 0)
            return &g_modules[i];
    }
    return NULL;
}

static pcps4_dll_module *find_base_locked(void *base)
{
    if (base == NULL)
        return NULL;

    for (size_t i = 0; i < PCPS4_DLL_MAX_MODULES; ++i) {
        if (g_modules[i].magic == PCPS4_DLL_MAGIC &&
            g_modules[i].base == base)
            return &g_modules[i];
    }
    return NULL;
}

static pcps4_dll_module *find_free_locked(void)
{
    for (size_t i = 0; i < PCPS4_DLL_MAX_MODULES; ++i)
        if (g_modules[i].magic == 0)
            return &g_modules[i];
    return NULL;
}

static int read_file(const char *path, uint8_t **out_data, size_t *out_size)
{
    FILE *f;
    long len;
    uint8_t *data;

    if (path == NULL || out_data == NULL || out_size == NULL)
        return -1;

    *out_data = NULL;
    *out_size = 0;

    f = fopen(path, "rb");
    if (f == NULL)
        return -1;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }

    len = ftell(f);
    if (len <= 0 || (unsigned long)len > (unsigned long)SIZE_MAX) {
        fclose(f);
        return -1;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }

    data = (uint8_t *)malloc((size_t)len);
    if (data == NULL) {
        fclose(f);
        return -1;
    }

    if (fread(data, 1, (size_t)len, f) != (size_t)len) {
        fclose(f);
        free(data);
        return -1;
    }

    fclose(f);
    *out_data = data;
    *out_size = (size_t)len;
    return 0;
}

static size_t round_page(size_t n)
{
    const size_t page = 0x4000u;
    if (n == 0 || n > SIZE_MAX - (page - 1u))
        return 0;
    return (n + page - 1u) & ~(page - 1u);
}

static void module_log(const char *fmt, const char *name, unsigned a, unsigned b)
{
    pcps4_status_log(fmt, name, a, b);
}

void *pcps4_dll_find(const char *name)
{
    void *base = NULL;

    lock();
    {
        pcps4_dll_module *m = find_name_locked(name);
        if (m != NULL)
            base = m->base;
    }
    unlock();

    return base;
}

int pcps4_dll_is_module(void *module)
{
    int yes;

    lock();
    yes = find_base_locked(module) != NULL;
    unlock();
    return yes;
}

void *pcps4_dll_get_export(
    void *module,
    const char *name,
    uint16_t ordinal)
{
    void *result = NULL;

    lock();
    {
        pcps4_dll_module *m = find_base_locked(module);
        if (m != NULL)
            result = pcps4_pe_lookup_export(
                &m->image,
                (const uint8_t *)m->base,
                m->map_size,
                name,
                ordinal);
    }
    unlock();

    return result;
}

int pcps4_dll_free(void *module)
{
    int result = -1;

    lock();
    {
        pcps4_dll_module *m = find_base_locked(module);
        if (m != NULL) {
            if (m->refs != 0)
                --m->refs;
            result = 0;
        }
    }
    unlock();

    return result;
}

void *pcps4_dll_load(
    const char *path,
    const pcps4_pe_resolver *resolver)
{
    uint8_t *file_data = NULL;
    size_t file_size = 0;
    pcps4_pe_image probe;
    pcps4_pe_image image;
    uint32_t resolved = 0;
    uint32_t unresolved = 0;
    size_t map_size;
    void *mem = NULL;
    pcps4_dll_module *module = NULL;
    char name[PCPS4_DLL_NAME_MAX];
    pcps4_pe_status rc;

    if (path == NULL || resolver == NULL)
        return NULL;

    normalize_name(path, name, sizeof(name));
    if (name[0] == '\0')
        return NULL;

    lock();
    module = find_name_locked(name);
    if (module != NULL) {
        if (module->refs != UINT32_MAX)
            ++module->refs;
        mem = module->base;
        unlock();
        return mem;
    }
    module = find_free_locked();
    unlock();

    if (module == NULL) {
        pcps4_status_log("[pcps4] DLL load refused: module table full");
        return NULL;
    }

    if (read_file(path, &file_data, &file_size) != 0) {
        pcps4_status_log("[pcps4] DLL not found: %s", path);
        return NULL;
    }

    rc = pcps4_pe_parse(file_data, file_size, &probe);
    if (rc != PCPS4_PE_OK) {
        module_log("[pcps4] DLL %s parse failed: %s", name, 0, 0);
        free(file_data);
        return NULL;
    }

    map_size = round_page(probe.size_of_image);
    if (map_size == 0) {
        free(file_data);
        return NULL;
    }

    {
        int32_t krc = sceKernelMmap(
            NULL, map_size,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANON, -1, 0, &mem);
        if (krc != 0 || mem == NULL) {
            pcps4_status_log("[pcps4] DLL %s mmap failed: 0x%x",
                             name, (unsigned)krc);
            free(file_data);
            return NULL;
        }
    }

    memset(&image, 0, sizeof(image));
    rc = pcps4_pe_load(
        file_data, file_size,
        (uint8_t *)mem, map_size,
        (uint64_t)(uintptr_t)mem,
        resolver,
        &image,
        &resolved,
        &unresolved);

    free(file_data);

    if (rc != PCPS4_PE_OK || unresolved != 0) {
        pcps4_status_log(
            "[pcps4] DLL %s load failed: %s resolved=%u unresolved=%u",
            name,
            rc == PCPS4_PE_OK ? "unresolved imports" :
                pcps4_pe_status_str(rc),
            (unsigned)resolved,
            (unsigned)unresolved);
        (void)sceKernelMunmap(mem, map_size);
        return NULL;
    }

    if (image.entry_point_rva != 0 &&
        !pcps4_loader_entry_is_executable(&image)) {
        pcps4_status_log(
            "[pcps4] DLL %s refused: entry point not executable",
            name);
        (void)sceKernelMunmap(mem, map_size);
        return NULL;
    }

    if (pcps4_loader_apply_image_protections(
            &image, (uint8_t *)mem, map_size) != 0) {
        pcps4_status_log(
            "[pcps4] DLL %s protection setup failed",
            name);
        (void)sceKernelMunmap(mem, map_size);
        return NULL;
    }

    /*
     * Reserve the module slot before TLS/DllMain so circular dependency
     * resolution can still find the mapped image. The image is not advertised
     * as initialized until after DllMain returns.
     */
    lock();
    module = find_name_locked(name);
    if (module != NULL) {
        if (module->refs != UINT32_MAX)
            ++module->refs;
        unlock();
        (void)sceKernelMunmap(mem, map_size);
        return module->base;
    }
    module = find_free_locked();
    if (module == NULL) {
        unlock();
        (void)sceKernelMunmap(mem, map_size);
        pcps4_status_log("[pcps4] DLL load refused: module table full");
        return NULL;
    }

    memset(module, 0, sizeof(*module));
    module->magic = PCPS4_DLL_MAGIC;
    module->refs = 1;
    module->loading = 1;
    module->base = mem;
    module->map_size = map_size;
    memcpy(module->name, name, strlen(name) + 1);
    module->image = image;
    unlock();

    if (pcps4_loader_run_tls_callbacks(
            &module->image,
            (uint8_t *)mem,
            map_size) != 0) {
        pcps4_status_log(
            "[pcps4] DLL %s TLS initialization failed",
            name);
        lock();
        memset(module, 0, sizeof(*module));
        unlock();
        (void)sceKernelMunmap(mem, map_size);
        return NULL;
    }

    if (image.entry_point_rva != 0) {
        pcps4_dll_entry_fn entry =
            (pcps4_dll_entry_fn)(
                (uint8_t *)mem + image.entry_point_rva);

        if (entry(mem, DLL_PROCESS_ATTACH, NULL) == 0) {
            pcps4_status_log(
                "[pcps4] DLL %s DllMain(PROCESS_ATTACH) failed",
                name);
            lock();
            memset(module, 0, sizeof(*module));
            unlock();
            (void)sceKernelMunmap(mem, map_size);
            return NULL;
        }
    }

    lock();
    if (module->magic == PCPS4_DLL_MAGIC)
        module->loading = 0;
    unlock();

    pcps4_status_log(
        "[pcps4] DLL loaded: %s base=%p size=0x%zx",
        name, mem, map_size);

    return mem;
}
