/*
 * PcPs4 PE32+ parser. See pcps4_pe.h for the role this plays.
 *
 * All multi-byte reads are little-endian (the PE format is always
 * little-endian) and go through bounds-checked helpers, so this file makes no
 * assumptions about host endianness or alignment.
 */

#include "pcps4_pe.h"

#include <string.h>

/* Bounded limits so a hostile file cannot exhaust time or memory. */
#define PCPS4_PE_MAX_SECTIONS       96
#define PCPS4_PE_MAX_IMPORT_DLLS    4096
#define PCPS4_PE_MAX_IMPORTS_PER_DLL 65536

/* --- little-endian, bounds-checked scalar reads ------------------------ */

static int pe_read_u16(const uint8_t *data, size_t size, size_t off,
                       uint16_t *out)
{
    if (off + 2 > size || off + 2 < off)
        return 0;
    *out = (uint16_t)(data[off] | ((uint16_t)data[off + 1] << 8));
    return 1;
}

static int pe_read_u32(const uint8_t *data, size_t size, size_t off,
                       uint32_t *out)
{
    if (off + 4 > size || off + 4 < off)
        return 0;
    *out = (uint32_t)data[off]
         | ((uint32_t)data[off + 1] << 8)
         | ((uint32_t)data[off + 2] << 16)
         | ((uint32_t)data[off + 3] << 24);
    return 1;
}

static int pe_read_u64(const uint8_t *data, size_t size, size_t off,
                       uint64_t *out)
{
    uint32_t lo, hi;
    if (!pe_read_u32(data, size, off, &lo))
        return 0;
    if (!pe_read_u32(data, size, off + 4, &hi))
        return 0;
    *out = (uint64_t)lo | ((uint64_t)hi << 32);
    return 1;
}

/* Read a NUL-terminated ASCII string starting at `off`, bounded by the image
 * and by `max` bytes. Returns the pointer, or NULL if unterminated in range. */
static const char *pe_read_cstr(const pcps4_pe_image *image, uint32_t off,
                                size_t max)
{
    size_t i;
    if (off >= image->size)
        return NULL;
    for (i = 0; i < max && (size_t)off + i < image->size; ++i) {
        if (image->data[off + i] == '\0')
            return (const char *)(image->data + off);
    }
    return NULL;
}

/* --- header parsing ---------------------------------------------------- */

pcps4_pe_status pcps4_pe_parse(const uint8_t *data, size_t size,
                               pcps4_pe_image *image)
{
    uint32_t e_lfanew = 0;
    uint32_t pe_sig = 0;
    uint16_t machine = 0;
    uint16_t num_sections = 0;
    uint16_t opt_size = 0;
    uint16_t opt_magic = 0;
    uint32_t num_dirs = 0;
    size_t coff_off, opt_off, dir_off, sec_off;
    uint32_t i;

    if (data == NULL || image == NULL)
        return PCPS4_PE_ERR_NULL;

    memset(image, 0, sizeof(*image));
    image->data = data;
    image->size = size;

    /* DOS header: "MZ" then e_lfanew at offset 0x3C. */
    if (size < 0x40)
        return PCPS4_PE_ERR_TOO_SMALL;
    if (data[0] != 'M' || data[1] != 'Z')
        return PCPS4_PE_ERR_NOT_MZ;
    if (!pe_read_u32(data, size, 0x3C, &e_lfanew))
        return PCPS4_PE_ERR_TOO_SMALL;

    /* NT signature "PE\0\0". */
    if (!pe_read_u32(data, size, e_lfanew, &pe_sig))
        return PCPS4_PE_ERR_BAD_OFFSET;
    if (pe_sig != 0x00004550u) /* 'P','E',0,0 little-endian */
        return PCPS4_PE_ERR_NOT_PE;

    /* COFF file header follows the 4-byte signature. */
    coff_off = (size_t)e_lfanew + 4;
    if (!pe_read_u16(data, size, coff_off + 0, &machine))
        return PCPS4_PE_ERR_TRUNCATED;
    if (!pe_read_u16(data, size, coff_off + 2, &num_sections))
        return PCPS4_PE_ERR_TRUNCATED;
    if (!pe_read_u16(data, size, coff_off + 16, &opt_size))
        return PCPS4_PE_ERR_TRUNCATED;

    if (machine != PCPS4_PE_MACHINE_AMD64)
        return PCPS4_PE_ERR_BAD_MACHINE;
    if (num_sections > PCPS4_PE_MAX_SECTIONS)
        return PCPS4_PE_ERR_TRUNCATED;

    /* Optional header (PE32+). COFF header is 20 bytes. */
    opt_off = coff_off + 20;
    if (!pe_read_u16(data, size, opt_off + 0, &opt_magic))
        return PCPS4_PE_ERR_TRUNCATED;
    if (opt_magic != PCPS4_PE_MAGIC_PE32PLUS)
        return PCPS4_PE_ERR_NOT_PE32PLUS;

    /* Fields within the PE32+ optional header. */
    if (!pe_read_u32(data, size, opt_off + 16, &image->entry_point_rva))
        return PCPS4_PE_ERR_TRUNCATED;
    if (!pe_read_u64(data, size, opt_off + 24, &image->image_base))
        return PCPS4_PE_ERR_TRUNCATED;
    if (!pe_read_u32(data, size, opt_off + 56, &image->size_of_image))
        return PCPS4_PE_ERR_TRUNCATED;
    if (!pe_read_u32(data, size, opt_off + 108, &num_dirs))
        return PCPS4_PE_ERR_TRUNCATED;

    if (num_dirs > PCPS4_PE_NUM_DATA_DIRECTORIES)
        num_dirs = PCPS4_PE_NUM_DATA_DIRECTORIES;

    /* Data directories start at optional-header offset 112 in PE32+. */
    dir_off = opt_off + 112;
    for (i = 0; i < num_dirs; ++i) {
        uint32_t rva = 0, sz = 0;
        if (!pe_read_u32(data, size, dir_off + (size_t)i * 8, &rva) ||
            !pe_read_u32(data, size, dir_off + (size_t)i * 8 + 4, &sz))
            return PCPS4_PE_ERR_TRUNCATED;
        image->directory_rva[i] = rva;
        image->directory_size[i] = sz;
    }

    /* Section table follows the whole optional header. */
    sec_off = opt_off + opt_size;
    if (sec_off < opt_off) /* overflow */
        return PCPS4_PE_ERR_OVERFLOW;
    /* Ensure every section header (40 bytes each) is in range. */
    if (sec_off + (size_t)num_sections * 40 > size ||
        sec_off + (size_t)num_sections * 40 < sec_off)
        return PCPS4_PE_ERR_TRUNCATED;

    image->machine = machine;
    image->section_count = num_sections;
    image->section_table_offset = (uint32_t)sec_off;
    return PCPS4_PE_OK;
}

pcps4_pe_status pcps4_pe_get_section(const pcps4_pe_image *image,
                                     uint16_t index, pcps4_pe_section *out)
{
    size_t off;
    uint32_t i;

    if (image == NULL || out == NULL)
        return PCPS4_PE_ERR_NULL;
    if (index >= image->section_count)
        return PCPS4_PE_ERR_BAD_OFFSET;

    off = (size_t)image->section_table_offset + (size_t)index * 40;

    memset(out, 0, sizeof(*out));
    for (i = 0; i < 8; ++i) {
        char c = (char)image->data[off + i];
        out->name[i] = c;
    }
    out->name[8] = '\0';

    if (!pe_read_u32(image->data, image->size, off + 8, &out->virtual_size) ||
        !pe_read_u32(image->data, image->size, off + 12, &out->virtual_address) ||
        !pe_read_u32(image->data, image->size, off + 16, &out->raw_size) ||
        !pe_read_u32(image->data, image->size, off + 20, &out->raw_offset) ||
        !pe_read_u32(image->data, image->size, off + 36, &out->characteristics))
        return PCPS4_PE_ERR_TRUNCATED;

    return PCPS4_PE_OK;
}

pcps4_pe_status pcps4_pe_rva_to_offset(const pcps4_pe_image *image,
                                       uint32_t rva, uint32_t *out_offset)
{
    uint16_t i;

    if (image == NULL || out_offset == NULL)
        return PCPS4_PE_ERR_NULL;

    for (i = 0; i < image->section_count; ++i) {
        pcps4_pe_section s;
        uint32_t span;
        if (pcps4_pe_get_section(image, i, &s) != PCPS4_PE_OK)
            continue;

        /* A section occupies [virtual_address, virtual_address + span). */
        span = s.virtual_size > s.raw_size ? s.virtual_size : s.raw_size;
        if (span == 0)
            continue;
        if (rva >= s.virtual_address &&
            rva < s.virtual_address + span) {
            uint32_t delta = rva - s.virtual_address;
            /* Only bytes backed by raw data are addressable in the file. */
            if (delta >= s.raw_size)
                return PCPS4_PE_ERR_BAD_OFFSET;
            if (s.raw_offset + delta < s.raw_offset) /* overflow */
                return PCPS4_PE_ERR_OVERFLOW;
            if ((size_t)s.raw_offset + delta >= image->size)
                return PCPS4_PE_ERR_BAD_OFFSET;
            *out_offset = s.raw_offset + delta;
            return PCPS4_PE_OK;
        }
    }
    return PCPS4_PE_ERR_BAD_OFFSET;
}

pcps4_pe_status pcps4_pe_iterate_imports(const pcps4_pe_image *image,
                                         pcps4_pe_import_fn cb, void *user)
{
    uint32_t import_rva, import_off;
    uint32_t desc;
    pcps4_pe_status rc;

    if (image == NULL || cb == NULL)
        return PCPS4_PE_ERR_NULL;

    import_rva = image->directory_rva[PCPS4_PE_DIRECTORY_IMPORT];
    if (import_rva == 0 || image->directory_size[PCPS4_PE_DIRECTORY_IMPORT] == 0)
        return PCPS4_PE_OK; /* no imports is valid */

    rc = pcps4_pe_rva_to_offset(image, import_rva, &import_off);
    if (rc != PCPS4_PE_OK)
        return rc;

    /* Each import descriptor is 20 bytes; a zeroed descriptor terminates. */
    for (desc = 0; desc < PCPS4_PE_MAX_IMPORT_DLLS; ++desc) {
        size_t d = (size_t)import_off + (size_t)desc * 20;
        uint32_t orig_first_thunk = 0, name_rva = 0, first_thunk = 0;
        uint32_t thunk_rva, thunk_off, name_off;
        const char *dll;
        uint32_t t;

        if (!pe_read_u32(image->data, image->size, d + 0, &orig_first_thunk) ||
            !pe_read_u32(image->data, image->size, d + 12, &name_rva) ||
            !pe_read_u32(image->data, image->size, d + 16, &first_thunk))
            return PCPS4_PE_ERR_TRUNCATED;

        if (orig_first_thunk == 0 && name_rva == 0 && first_thunk == 0)
            break; /* terminating null descriptor */

        if (pcps4_pe_rva_to_offset(image, name_rva, &name_off) != PCPS4_PE_OK)
            return PCPS4_PE_ERR_BAD_OFFSET;
        dll = pe_read_cstr(image, name_off, 256);
        if (dll == NULL)
            return PCPS4_PE_ERR_TRUNCATED;

        /* Prefer the import-name-table (original first thunk); fall back to
         * the IAT if the ILT is absent, as some linkers emit. */
        thunk_rva = orig_first_thunk ? orig_first_thunk : first_thunk;
        if (thunk_rva == 0)
            continue;
        if (pcps4_pe_rva_to_offset(image, thunk_rva, &thunk_off) != PCPS4_PE_OK)
            return PCPS4_PE_ERR_BAD_OFFSET;

        for (t = 0; t < PCPS4_PE_MAX_IMPORTS_PER_DLL; ++t) {
            uint64_t entry = 0;
            size_t te = (size_t)thunk_off + (size_t)t * 8;
            if (!pe_read_u64(image->data, image->size, te, &entry))
                return PCPS4_PE_ERR_TRUNCATED;
            if (entry == 0)
                break; /* end of this DLL's thunk array */

            if (entry & 0x8000000000000000ull) {
                /* Import by ordinal: low 16 bits. */
                uint16_t ord = (uint16_t)(entry & 0xFFFFu);
                if (cb(user, dll, 0, NULL, ord))
                    return PCPS4_PE_OK;
            } else {
                /* Import by name: entry is an RVA to a hint/name table entry;
                 * the name is 2 bytes in (after the 16-bit hint). */
                uint32_t hn_off;
                const char *fn;
                if (pcps4_pe_rva_to_offset(image, (uint32_t)entry, &hn_off)
                        != PCPS4_PE_OK)
                    return PCPS4_PE_ERR_BAD_OFFSET;
                fn = pe_read_cstr(image, hn_off + 2, 1024);
                if (fn == NULL)
                    return PCPS4_PE_ERR_TRUNCATED;
                if (cb(user, dll, 1, fn, 0))
                    return PCPS4_PE_OK;
            }
        }
    }

    return PCPS4_PE_OK;
}

const char *pcps4_pe_status_str(pcps4_pe_status status)
{
    switch (status) {
        case PCPS4_PE_OK:               return "ok";
        case PCPS4_PE_ERR_NULL:         return "null argument";
        case PCPS4_PE_ERR_TOO_SMALL:    return "buffer too small";
        case PCPS4_PE_ERR_NOT_MZ:       return "missing MZ signature";
        case PCPS4_PE_ERR_NOT_PE:       return "missing PE signature";
        case PCPS4_PE_ERR_NOT_PE32PLUS: return "not a 64-bit (PE32+) image";
        case PCPS4_PE_ERR_BAD_MACHINE:  return "machine is not x86-64";
        case PCPS4_PE_ERR_BAD_OFFSET:   return "offset outside image";
        case PCPS4_PE_ERR_OVERFLOW:     return "range arithmetic overflow";
        case PCPS4_PE_ERR_TRUNCATED:    return "structure truncated";
        default:                        return "unknown error";
    }
}
