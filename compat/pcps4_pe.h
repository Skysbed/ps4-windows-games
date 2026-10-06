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
    PCPS4_PE_ERR_TRUNCATED = -9     /* a structure ran past the buffer end */
} pcps4_pe_status;

#define PCPS4_PE_MACHINE_AMD64 0x8664u
#define PCPS4_PE_MAGIC_PE32PLUS 0x20bu
#define PCPS4_PE_NUM_DATA_DIRECTORIES 16
#define PCPS4_PE_DIRECTORY_IMPORT 1

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

/* Human-readable text for a status code (never NULL). */
const char *pcps4_pe_status_str(pcps4_pe_status status);

#ifdef __cplusplus
}
#endif

#endif /* PCPS4_PE_H */
