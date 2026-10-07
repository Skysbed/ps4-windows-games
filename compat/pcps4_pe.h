#ifndef PCPS4_PE_H
#define PCPS4_PE_H

/*
 * PcPs4 PE (Portable Executable) loader front-end.
 *
 * This parses the Windows PE32+ (x86-64) executable format: DOS stub,
 * NT headers, section table and the import directory. PS4's CPU is x86-64,
 * so a Windows binary's machine code is the same instruction set; the job
 * of actually running one is (1) understanding the container, which is this
 * file, (2) mapping the sections into memory, and (3) resolving the imported
 * Windows APIs against a runtime. This module covers (1) and reports exactly
 * what a binary needs for (2) and (3).
 *
 * Everything here is bounds-checked against the in-memory image size. A
 * malformed file is rejected with an error code, never a crash: that matches
 * the rest of PcPs4's fault-containment approach.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum pcps4_pe_status {
    PCPS4_PE_OK = 0,
    PCPS4_PE_ERR_NULL = -1,         /* null argument */
    PCPS4_PE_ERR_TOO_SMALL = -2,    /* buffer smaller than required headers */
    PCPS4_PE_ERR_NOT_MZ = -3,       /* missing "MZ" DOS signature */
    PCPS4_PE_ERR_NOT_PE = -4,       /* missing "PE\0\0" NT signature */
    PCPS4_PE_ERR_NOT_PE32PLUS = -5, /* not a PE32+ (64-bit) optional header */
    PCPS4_PE_ERR_BAD_MACHINE = -6,  /* machine is not x86-64 */
    PCPS4_PE_ERR_BAD_OFFSET = -7,   /* an offset/RVA fell outside the image */
    PCPS4_PE_ERR_OVERFLOW = -8,     /* an arithmetic range check overflowed */
    PCPS4_PE_ERR_TRUNCATED = -9,    /* a structure ran past the buffer end */
    PCPS4_PE_ERR_UNSUPPORTED_RELOC = -10, /* a relocation type we don't apply */
    PCPS4_PE_ERR_NO_SPACE = -11     /* destination buffer too small to map */
} pcps4_pe_status;

#define PCPS4_PE_MACHINE_AMD64 0x8664u
#define PCPS4_PE_MAGIC_PE32PLUS 0x20bu
#define PCPS4_PE_NUM_DATA_DIRECTORIES 16
#define PCPS4_PE_DIRECTORY_EXPORT 0
#define PCPS4_PE_DIRECTORY_IMPORT 1
#define PCPS4_PE_DIRECTORY_BASERELOC 5
#define PCPS4_PE_DIRECTORY_TLS 9

/* Characteristics bits we care about. */
#define PCPS4_PE_SCN_MEM_EXECUTE 0x20000000u
#define PCPS4_PE_SCN_MEM_READ    0x40000000u
#define PCPS4_PE_SCN_MEM_WRITE   0x80000000u

typedef struct pcps4_pe_section {
    char     name[9];      /* NUL-terminated, up to 8 chars */
    uint32_t virtual_size;
    uint32_t virtual_address;   /* RVA where the section is mapped */
    uint32_t raw_size;
    uint32_t raw_offset;        /* file offset of the section bytes */
    uint32_t characteristics;
} pcps4_pe_section;

typedef struct pcps4_pe_image {
    const uint8_t *data;
    size_t         size;

    uint16_t machine;
    uint16_t section_count;
    uint64_t image_base;
    uint32_t entry_point_rva;
    uint32_t size_of_image;
    uint32_t size_of_headers;

    uint32_t directory_rva[PCPS4_PE_NUM_DATA_DIRECTORIES];
    uint32_t directory_size[PCPS4_PE_NUM_DATA_DIRECTORIES];

    uint32_t section_table_offset; /* file offset of first section header */
} pcps4_pe_image;

/*
 * Parse the headers of a PE image already loaded into memory. Does not take
 * ownership of `data`; the caller must keep it alive while `image` is used.
 */
pcps4_pe_status pcps4_pe_parse(const uint8_t *data, size_t size,
                               pcps4_pe_image *image);

/* Read section header `index` (0-based) into `out`. */
pcps4_pe_status pcps4_pe_get_section(const pcps4_pe_image *image,
                                     uint16_t index,
                                     pcps4_pe_section *out);

/*
 * Translate a relative virtual address to a file offset using the section
 * table. Returns PCPS4_PE_ERR_BAD_OFFSET if the RVA maps to no section.
 */
pcps4_pe_status pcps4_pe_rva_to_offset(const pcps4_pe_image *image,
                                       uint32_t rva, uint32_t *out_offset);

/*
 * Callback invoked once per imported symbol. `dll` is the importing module
 * name (e.g. "KERNEL32.dll"). If `by_name` is non-zero, `name` is the
 * imported function name; otherwise `ordinal` is the import ordinal and
 * `name` is NULL. Return non-zero to stop iteration early.
 */
typedef int (*pcps4_pe_import_fn)(void *user,
                                  const char *dll,
                                  int by_name,
                                  const char *name,
                                  uint16_t ordinal);

/*
 * Walk the import directory, calling `cb` for every imported symbol. Safe on
 * a binary with no imports (returns PCPS4_PE_OK, zero calls). Bounded so a
 * malformed/cyclic table cannot loop forever.
 */
pcps4_pe_status pcps4_pe_iterate_imports(const pcps4_pe_image *image,
                                         pcps4_pe_import_fn cb, void *user);

/*
 * Map a parsed image into a caller-provided memory block laid out by RVA:
 * copies the headers and every section to its virtual address, zero-fills the
 * gaps, then applies base relocations so the image is correct at `load_base`
 * (which may differ from its preferred image_base). `dest` must be at least
 * `image->size_of_image` bytes. On success `dest` holds the ready-to-run image
 * and the entry point is at dest + image->entry_point_rva.
 *
 * This does not change memory protections or make anything executable; that is
 * the OS-layer step that follows. It is the "place it in memory correctly"
 * stage of loading a binary.
 */
pcps4_pe_status pcps4_pe_map_image(const pcps4_pe_image *image,
                                   uint8_t *dest, size_t dest_size,
                                   uint64_t load_base);

/* --- import resolution ------------------------------------------------- */

#define PCPS4_PE_MAX_RESOLVER_SYMBOLS 512

typedef struct pcps4_pe_symbol {
    char     dll[32];
    char     name[64];
    uint16_t ordinal;
    uint8_t  by_ordinal;
    uint8_t  reserved;
    void    *fn;
} pcps4_pe_symbol;

typedef void *(*pcps4_pe_resolver_fallback_fn)(
    void *user,
    const char *dll,
    int by_name,
    const char *name,
    uint16_t ordinal);

/*
 * A flat registry of Win32 (or any DLL) symbols that PcPs4 provides. The host
 * registers the APIs it implements; binding then patches a loaded image's
 * import table to point at them. Symbols the registry does not have are what
 * the Win32 runtime still needs to implement.
 */
typedef struct pcps4_pe_resolver {
    pcps4_pe_symbol symbols[PCPS4_PE_MAX_RESOLVER_SYMBOLS];
    uint32_t        count;
    pcps4_pe_resolver_fallback_fn fallback;
    void            *fallback_user;
} pcps4_pe_resolver;

void pcps4_pe_resolver_init(pcps4_pe_resolver *resolver);

/* Register a handler for dll!name. Re-registering the same pair replaces it. */
pcps4_pe_status pcps4_pe_resolver_register(pcps4_pe_resolver *resolver,
                                           const char *dll, const char *name,
                                           void *fn);

/* Returns the handler for dll!name, or NULL if not registered. */
void *pcps4_pe_resolver_lookup(const pcps4_pe_resolver *resolver,
                               const char *dll, const char *name);

/* Register/lookup a handler for a DLL ordinal import. */
pcps4_pe_status pcps4_pe_resolver_register_ordinal(
    pcps4_pe_resolver *resolver,
    const char *dll,
    uint16_t ordinal,
    void *fn);

void *pcps4_pe_resolver_lookup_ordinal(
    const pcps4_pe_resolver *resolver,
    const char *dll,
    uint16_t ordinal);

/*
 * Look up an exported symbol in a mapped PE image. Names are matched exactly;
 * ordinals use the image's export ordinal base. Forwarded exports return NULL
 * because they require resolving another module.
 */
void *pcps4_pe_lookup_export(
    const pcps4_pe_image *image,
    const uint8_t *mapped,
    size_t mapped_size,
    const char *name,
    uint16_t ordinal);

/*
 * Bind a mapped image's imports. For each imported-by-name symbol, look it up
 * in `resolver` and write the handler pointer into that image's IAT slot; a
 * symbol with no handler is left NULL and counted as unresolved; ordinal
 * imports are resolved through the ordinal registry when present. `mapped` is the buffer produced by
 * pcps4_pe_map_image. Counts are optional (pass NULL to ignore).
 *
 * An unresolved count > 0 is not an error: it is the precise list of APIs the
 * binary needs that PcPs4 does not yet provide.
 */
pcps4_pe_status pcps4_pe_bind_imports(const pcps4_pe_image *image,
                                      uint8_t *mapped, size_t mapped_size,
                                      const pcps4_pe_resolver *resolver,
                                      uint32_t *out_resolved,
                                      uint32_t *out_unresolved);

/*
 * Convenience: parse + map + bind in one call. `exe`/`exe_size` is the raw
 * file; `dest`/`dest_size` is the memory the image is mapped into (at
 * `load_base`, which should equal the real address of `dest` when the image
 * will be executed there). `resolver` may be NULL to skip binding. On success
 * `out_image` describes the image and the counts report binding results.
 */
/*
 * Report each import that does not resolve through the resolver.
 * This lets the loader print an actionable module/API list before execution.
 */
typedef int (*pcps4_pe_unresolved_fn)(void *user,
                                     const char *dll,
                                     int by_name,
                                     const char *name,
                                     uint16_t ordinal);

pcps4_pe_status pcps4_pe_report_unresolved_imports(
    const pcps4_pe_image *image,
    const pcps4_pe_resolver *resolver,
    pcps4_pe_unresolved_fn cb,
    void *user);

pcps4_pe_status pcps4_pe_load(const uint8_t *exe, size_t exe_size,
                              uint8_t *dest, size_t dest_size,
                              uint64_t load_base,
                              const pcps4_pe_resolver *resolver,
                              pcps4_pe_image *out_image,
                              uint32_t *out_resolved,
                              uint32_t *out_unresolved);

/* Human-readable text for a status code (never NULL). */
const char *pcps4_pe_status_str(pcps4_pe_status status);

#ifdef __cplusplus
}
#endif

#endif /* PCPS4_PE_H */
