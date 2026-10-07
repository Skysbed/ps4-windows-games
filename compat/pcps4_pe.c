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
    if (!pe_read_u32(data, size, opt_off + 60, &image->size_of_headers))
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

/* Base relocation types we understand. */
#define PCPS4_PE_REL_ABSOLUTE 0
#define PCPS4_PE_REL_DIR64    10

static pcps4_pe_status pe_apply_relocations(const pcps4_pe_image *image,
                                            uint8_t *dest, size_t dest_size,
                                            uint64_t load_base)
{
    uint32_t reloc_rva = image->directory_rva[PCPS4_PE_DIRECTORY_BASERELOC];
    uint32_t reloc_size = image->directory_size[PCPS4_PE_DIRECTORY_BASERELOC];
    uint64_t delta;
    uint32_t reloc_off, cursor, end;

    if (reloc_rva == 0 || reloc_size == 0)
        return PCPS4_PE_OK; /* nothing to rebase */

    delta = load_base - image->image_base;
    if (delta == 0)
        return PCPS4_PE_OK; /* loaded at preferred base: no fixups needed */

    if (pcps4_pe_rva_to_offset(image, reloc_rva, &reloc_off) != PCPS4_PE_OK)
        return PCPS4_PE_ERR_BAD_OFFSET;

    cursor = reloc_off;
    if (reloc_off + reloc_size < reloc_off)
        return PCPS4_PE_ERR_OVERFLOW;
    end = reloc_off + reloc_size;

    /* Walk .reloc blocks: [PageRVA u32][BlockSize u32][entries u16...] */
    while (cursor + 8 <= end) {
        uint32_t page_rva = 0, block_size = 0;
        uint32_t entries, i, entry_base;

        if (!pe_read_u32(image->data, image->size, cursor + 0, &page_rva) ||
            !pe_read_u32(image->data, image->size, cursor + 4, &block_size))
            return PCPS4_PE_ERR_TRUNCATED;

        if (block_size < 8)
            break; /* malformed or terminator */
        if (cursor + block_size < cursor || cursor + block_size > end)
            return PCPS4_PE_ERR_TRUNCATED;

        entries = (block_size - 8) / 2;
        entry_base = cursor + 8;

        for (i = 0; i < entries; ++i) {
            uint16_t e = 0;
            uint32_t type, offset, target;
            if (!pe_read_u16(image->data, image->size,
                             entry_base + (uint32_t)i * 2, &e))
                return PCPS4_PE_ERR_TRUNCATED;

            type = (uint32_t)(e >> 12);
            offset = (uint32_t)(e & 0x0FFFu);

            if (type == PCPS4_PE_REL_ABSOLUTE)
                continue; /* padding entry */
            if (type != PCPS4_PE_REL_DIR64)
                return PCPS4_PE_ERR_UNSUPPORTED_RELOC;

            if (page_rva + offset < page_rva)
                return PCPS4_PE_ERR_OVERFLOW;
            target = page_rva + offset;
            if ((size_t)target + 8 > dest_size || (size_t)target + 8 < target)
                return PCPS4_PE_ERR_NO_SPACE;

            {
                /* Read, add delta, write back the 64-bit pointer in dest. */
                uint64_t v =
                    (uint64_t)dest[target]
                    | ((uint64_t)dest[target + 1] << 8)
                    | ((uint64_t)dest[target + 2] << 16)
                    | ((uint64_t)dest[target + 3] << 24)
                    | ((uint64_t)dest[target + 4] << 32)
                    | ((uint64_t)dest[target + 5] << 40)
                    | ((uint64_t)dest[target + 6] << 48)
                    | ((uint64_t)dest[target + 7] << 56);
                v += delta;
                dest[target + 0] = (uint8_t)(v & 0xFF);
                dest[target + 1] = (uint8_t)((v >> 8) & 0xFF);
                dest[target + 2] = (uint8_t)((v >> 16) & 0xFF);
                dest[target + 3] = (uint8_t)((v >> 24) & 0xFF);
                dest[target + 4] = (uint8_t)((v >> 32) & 0xFF);
                dest[target + 5] = (uint8_t)((v >> 40) & 0xFF);
                dest[target + 6] = (uint8_t)((v >> 48) & 0xFF);
                dest[target + 7] = (uint8_t)((v >> 56) & 0xFF);
            }
        }

        cursor += block_size;
    }

    return PCPS4_PE_OK;
}

pcps4_pe_status pcps4_pe_map_image(const pcps4_pe_image *image,
                                   uint8_t *dest, size_t dest_size,
                                   uint64_t load_base)
{
    uint16_t i;
    uint32_t headers;

    if (image == NULL || dest == NULL)
        return PCPS4_PE_ERR_NULL;
    if (image->size_of_image == 0 || dest_size < image->size_of_image)
        return PCPS4_PE_ERR_NO_SPACE;

    memset(dest, 0, dest_size);

    /* Copy the headers region. */
    headers = image->size_of_headers;
    if (headers > image->size)
        headers = (uint32_t)image->size;
    if (headers > dest_size)
        return PCPS4_PE_ERR_NO_SPACE;
    memcpy(dest, image->data, headers);

    /* Copy each section's raw bytes to its virtual address. */
    for (i = 0; i < image->section_count; ++i) {
        pcps4_pe_section s;
        uint32_t copy;
        if (pcps4_pe_get_section(image, i, &s) != PCPS4_PE_OK)
            return PCPS4_PE_ERR_TRUNCATED;
        if (s.raw_size == 0)
            continue;

        copy = s.raw_size;
        /* Clamp to what the file actually contains. */
        if ((size_t)s.raw_offset + copy > image->size) {
            if (s.raw_offset >= image->size)
                continue;
            copy = (uint32_t)(image->size - s.raw_offset);
        }
        /* Clamp to the destination image window. */
        if ((size_t)s.virtual_address + copy > dest_size)
            return PCPS4_PE_ERR_NO_SPACE;

        memcpy(dest + s.virtual_address, image->data + s.raw_offset, copy);
    }

    return pe_apply_relocations(image, dest, dest_size, load_base);
}

/* --- import resolution ------------------------------------------------- */

static int pe_ascii_lower(int c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 'a';
    return c;
}

static int pe_streq_ci(const char *a, const char *b)
{
    size_t i;
    for (i = 0;; ++i) {
        int ca = pe_ascii_lower((unsigned char)a[i]);
        int cb = pe_ascii_lower((unsigned char)b[i]);
        if (ca != cb)
            return 0;
        if (ca == '\0')
            return 1;
    }
}

static void pe_copy_bounded(char *dst, size_t dst_size, const char *src)
{
    size_t i;
    if (dst_size == 0)
        return;
    for (i = 0; i + 1 < dst_size && src[i] != '\0'; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
}

void pcps4_pe_resolver_init(pcps4_pe_resolver *resolver)
{
    if (resolver == NULL)
        return;
    memset(resolver, 0, sizeof(*resolver));
}

pcps4_pe_status pcps4_pe_resolver_register(pcps4_pe_resolver *resolver,
                                           const char *dll, const char *name,
                                           void *fn)
{
    uint32_t i;

    if (resolver == NULL || dll == NULL || name == NULL)
        return PCPS4_PE_ERR_NULL;

    /* Replace an existing entry for the same dll!name. */
    for (i = 0; i < resolver->count; ++i) {
        if (pe_streq_ci(resolver->symbols[i].dll, dll) &&
            strcmp(resolver->symbols[i].name, name) == 0) {
            resolver->symbols[i].fn = fn;
            return PCPS4_PE_OK;
        }
    }

    if (resolver->count >= PCPS4_PE_MAX_RESOLVER_SYMBOLS) {
        resolver->overflowed += 1; /* reported by the loader; never silent */
        return PCPS4_PE_ERR_NO_SPACE;
    }

    pe_copy_bounded(resolver->symbols[resolver->count].dll,
                    sizeof(resolver->symbols[0].dll), dll);
    pe_copy_bounded(resolver->symbols[resolver->count].name,
                    sizeof(resolver->symbols[0].name), name);
    resolver->symbols[resolver->count].ordinal = 0;
    resolver->symbols[resolver->count].by_ordinal = 0;
    resolver->symbols[resolver->count].fn = fn;
    ++resolver->count;
    return PCPS4_PE_OK;
}

pcps4_pe_status pcps4_pe_resolver_register_ordinal(
    pcps4_pe_resolver *resolver,
    const char *dll,
    uint16_t ordinal,
    void *fn)
{
    uint32_t i;

    if (resolver == NULL || dll == NULL || ordinal == 0)
        return PCPS4_PE_ERR_NULL;

    for (i = 0; i < resolver->count; ++i) {
        if (resolver->symbols[i].by_ordinal != 0 &&
            resolver->symbols[i].ordinal == ordinal &&
            pe_streq_ci(resolver->symbols[i].dll, dll)) {
            resolver->symbols[i].fn = fn;
            return PCPS4_PE_OK;
        }
    }

    if (resolver->count >= PCPS4_PE_MAX_RESOLVER_SYMBOLS)
        return PCPS4_PE_ERR_NO_SPACE;

    pe_copy_bounded(resolver->symbols[resolver->count].dll,
                    sizeof(resolver->symbols[0].dll), dll);
    resolver->symbols[resolver->count].name[0] = '\0';
    resolver->symbols[resolver->count].ordinal = ordinal;
    resolver->symbols[resolver->count].by_ordinal = 1;
    resolver->symbols[resolver->count].reserved = 0;
    resolver->symbols[resolver->count].fn = fn;
    ++resolver->count;
    return PCPS4_PE_OK;
}

void *pcps4_pe_resolver_lookup(const pcps4_pe_resolver *resolver,
                               const char *dll, const char *name)
{
    uint32_t i;
    if (resolver == NULL || dll == NULL || name == NULL)
        return NULL;
    for (i = 0; i < resolver->count; ++i) {
        if (resolver->symbols[i].by_ordinal == 0 &&
            pe_streq_ci(resolver->symbols[i].dll, dll) &&
            strcmp(resolver->symbols[i].name, name) == 0)
            return resolver->symbols[i].fn;
    }
    return NULL;
}

void *pcps4_pe_resolver_lookup_ordinal(
    const pcps4_pe_resolver *resolver,
    const char *dll,
    uint16_t ordinal)
{
    uint32_t i;

    if (resolver == NULL || dll == NULL || ordinal == 0)
        return NULL;

    for (i = 0; i < resolver->count; ++i) {
        if (resolver->symbols[i].by_ordinal != 0 &&
            resolver->symbols[i].ordinal == ordinal &&
            pe_streq_ci(resolver->symbols[i].dll, dll))
            return resolver->symbols[i].fn;
    }

    return NULL;
}

static const char *pe_mapped_cstr(
    const uint8_t *mapped,
    size_t mapped_size,
    uint32_t off,
    size_t max)
{
    size_t i;

    if (mapped == NULL || off >= mapped_size)
        return NULL;

    for (i = 0; i < max && (size_t)off + i < mapped_size; ++i) {
        if (mapped[off + i] == '\0')
            return (const char *)(mapped + off);
    }

    return NULL;
}

void *pcps4_pe_lookup_export(
    const pcps4_pe_image *image,
    const uint8_t *mapped,
    size_t mapped_size,
    const char *name,
    uint16_t ordinal)
{
    uint32_t export_rva;
    uint32_t export_size;
    uint32_t base;
    uint32_t functions;
    uint32_t names;
    uint32_t eat_rva;
    uint32_t name_table_rva;
    uint32_t ordinal_table_rva;
    uint32_t function_index = UINT32_MAX;

    if (image == NULL || mapped == NULL || mapped_size == 0)
        return NULL;

    export_rva = image->directory_rva[PCPS4_PE_DIRECTORY_EXPORT];
    export_size = image->directory_size[PCPS4_PE_DIRECTORY_EXPORT];

    if (export_rva == 0 || export_size < 40u ||
        (size_t)export_rva + export_size > mapped_size)
        return NULL;

    if (!pe_read_u32(mapped, mapped_size, (size_t)export_rva + 16u, &base) ||
        !pe_read_u32(mapped, mapped_size, (size_t)export_rva + 20u, &functions) ||
        !pe_read_u32(mapped, mapped_size, (size_t)export_rva + 24u, &names) ||
        !pe_read_u32(mapped, mapped_size, (size_t)export_rva + 28u, &eat_rva) ||
        !pe_read_u32(mapped, mapped_size, (size_t)export_rva + 32u, &name_table_rva) ||
        !pe_read_u32(mapped, mapped_size, (size_t)export_rva + 36u, &ordinal_table_rva))
        return NULL;

    if (functions == 0 || eat_rva == 0)
        return NULL;

    if (name != NULL) {
        if (name_table_rva == 0 || ordinal_table_rva == 0)
            return NULL;

        for (uint32_t i = 0; i < names; ++i) {
            uint32_t name_rva = 0;
            uint16_t name_index = 0;
            const char *export_name;

            if (!pe_read_u32(
                    mapped,
                    mapped_size,
                    (size_t)name_table_rva + (size_t)i * 4u,
                    &name_rva) ||
                !pe_read_u16(
                    mapped,
                    mapped_size,
                    (size_t)ordinal_table_rva + (size_t)i * 2u,
                    &name_index))
                return NULL;

            export_name = pe_mapped_cstr(
                mapped, mapped_size, name_rva, 1024);
            if (export_name == NULL)
                return NULL;

            if (strcmp(export_name, name) == 0) {
                function_index = name_index;
                break;
            }
        }
    } else {
        if ((uint32_t)ordinal < base ||
            (uint32_t)ordinal - base >= functions)
            return NULL;
        function_index = (uint32_t)ordinal - base;
    }

    if (function_index >= functions)
        return NULL;

    uint32_t function_rva = 0;
    if (!pe_read_u32(
            mapped,
            mapped_size,
            (size_t)eat_rva + (size_t)function_index * 4u,
            &function_rva) ||
        function_rva == 0)
        return NULL;

    /*
     * PE marks an export as forwarded when its EAT RVA falls inside the
     * export-directory range. Such a value is a string, not code.
     */
    if (function_rva >= export_rva &&
        (uint64_t)function_rva < (uint64_t)export_rva + export_size)
        return NULL;

    if ((size_t)function_rva >= mapped_size)
        return NULL;

    return (void *)(mapped + function_rva);
}

static void pe_write_u64(uint8_t *b, size_t off, uint64_t v)
{
    b[off + 0] = (uint8_t)(v & 0xFF);
    b[off + 1] = (uint8_t)((v >> 8) & 0xFF);
    b[off + 2] = (uint8_t)((v >> 16) & 0xFF);
    b[off + 3] = (uint8_t)((v >> 24) & 0xFF);
    b[off + 4] = (uint8_t)((v >> 32) & 0xFF);
    b[off + 5] = (uint8_t)((v >> 40) & 0xFF);
    b[off + 6] = (uint8_t)((v >> 48) & 0xFF);
    b[off + 7] = (uint8_t)((v >> 56) & 0xFF);
}

pcps4_pe_status pcps4_pe_bind_imports(const pcps4_pe_image *image,
                                      uint8_t *mapped, size_t mapped_size,
                                      const pcps4_pe_resolver *resolver,
                                      uint32_t *out_resolved,
                                      uint32_t *out_unresolved)
{
    uint32_t import_rva, import_off, desc;
    uint32_t resolved = 0, unresolved = 0;

    if (out_resolved) *out_resolved = 0;
    if (out_unresolved) *out_unresolved = 0;

    if (image == NULL || mapped == NULL || resolver == NULL)
        return PCPS4_PE_ERR_NULL;

    import_rva = image->directory_rva[PCPS4_PE_DIRECTORY_IMPORT];
    if (import_rva == 0 || image->directory_size[PCPS4_PE_DIRECTORY_IMPORT] == 0)
        return PCPS4_PE_OK; /* nothing to bind */

    if (pcps4_pe_rva_to_offset(image, import_rva, &import_off) != PCPS4_PE_OK)
        return PCPS4_PE_ERR_BAD_OFFSET;

    for (desc = 0; desc < PCPS4_PE_MAX_IMPORT_DLLS; ++desc) {
        size_t d = (size_t)import_off + (size_t)desc * 20;
        uint32_t orig_first_thunk = 0, name_rva = 0, first_thunk = 0;
        uint32_t ilt_rva, ilt_off, name_off, t;
        const char *dll;

        if (!pe_read_u32(image->data, image->size, d + 0, &orig_first_thunk) ||
            !pe_read_u32(image->data, image->size, d + 12, &name_rva) ||
            !pe_read_u32(image->data, image->size, d + 16, &first_thunk))
            return PCPS4_PE_ERR_TRUNCATED;

        if (orig_first_thunk == 0 && name_rva == 0 && first_thunk == 0)
            break;
        if (first_thunk == 0)
            continue; /* no IAT to patch */

        if (pcps4_pe_rva_to_offset(image, name_rva, &name_off) != PCPS4_PE_OK)
            return PCPS4_PE_ERR_BAD_OFFSET;
        dll = pe_read_cstr(image, name_off, 256);
        if (dll == NULL)
            return PCPS4_PE_ERR_TRUNCATED;

        /* Names come from the ILT (or the IAT if there is no ILT). */
        ilt_rva = orig_first_thunk ? orig_first_thunk : first_thunk;
        if (pcps4_pe_rva_to_offset(image, ilt_rva, &ilt_off) != PCPS4_PE_OK)
            return PCPS4_PE_ERR_BAD_OFFSET;

        for (t = 0; t < PCPS4_PE_MAX_IMPORTS_PER_DLL; ++t) {
            uint64_t entry = 0;
            uint32_t iat_slot_rva;
            void *fn = NULL;

            if (!pe_read_u64(image->data, image->size,
                             (size_t)ilt_off + (size_t)t * 8, &entry))
                return PCPS4_PE_ERR_TRUNCATED;
            if (entry == 0)
                break;

            iat_slot_rva = first_thunk + t * 8;
            if ((size_t)iat_slot_rva + 8 > mapped_size ||
                (size_t)iat_slot_rva + 8 < iat_slot_rva)
                return PCPS4_PE_ERR_NO_SPACE;

            if (entry & 0x8000000000000000ull) {
                uint16_t ordinal = (uint16_t)(entry & 0xFFFFu);
                fn = pcps4_pe_resolver_lookup_ordinal(
                    resolver, dll, ordinal);
                if (fn == NULL && resolver->fallback != NULL)
                    fn = resolver->fallback(
                        resolver->fallback_user,
                        dll, 0, NULL, ordinal);
            } else {
                uint32_t hn_off;
                const char *name;
                if (pcps4_pe_rva_to_offset(image, (uint32_t)entry, &hn_off)
                        != PCPS4_PE_OK)
                    return PCPS4_PE_ERR_BAD_OFFSET;
                name = pe_read_cstr(image, hn_off + 2, 1024);
                if (name == NULL)
                    return PCPS4_PE_ERR_TRUNCATED;
                fn = pcps4_pe_resolver_lookup(resolver, dll, name);
                if (fn == NULL && resolver->fallback != NULL)
                    fn = resolver->fallback(
                        resolver->fallback_user,
                        dll, 1, name, 0);
            }

            pe_write_u64(mapped, iat_slot_rva, (uint64_t)(uintptr_t)fn);
            if (fn != NULL)
                ++resolved;
            else
                ++unresolved;
        }
    }

    if (out_resolved) *out_resolved = resolved;
    if (out_unresolved) *out_unresolved = unresolved;
    return PCPS4_PE_OK;
}


pcps4_pe_status pcps4_pe_report_unresolved_imports(
    const pcps4_pe_image *image,
    const pcps4_pe_resolver *resolver,
    pcps4_pe_unresolved_fn cb,
    void *user)
{
    uint32_t import_rva, import_off, desc;

    if (image == NULL || resolver == NULL || cb == NULL)
        return PCPS4_PE_ERR_NULL;

    import_rva = image->directory_rva[PCPS4_PE_DIRECTORY_IMPORT];
    if (import_rva == 0 ||
        image->directory_size[PCPS4_PE_DIRECTORY_IMPORT] == 0)
        return PCPS4_PE_OK;

    if (pcps4_pe_rva_to_offset(image, import_rva, &import_off) != PCPS4_PE_OK)
        return PCPS4_PE_ERR_BAD_OFFSET;

    for (desc = 0; desc < PCPS4_PE_MAX_IMPORT_DLLS; ++desc) {
        size_t d = (size_t)import_off + (size_t)desc * 20u;
        uint32_t orig_first_thunk = 0;
        uint32_t name_rva = 0;
        uint32_t first_thunk = 0;
        uint32_t ilt_rva;
        uint32_t ilt_off;
        uint32_t name_off;
        const char *dll;

        if (!pe_read_u32(image->data, image->size, d + 0, &orig_first_thunk) ||
            !pe_read_u32(image->data, image->size, d + 12, &name_rva) ||
            !pe_read_u32(image->data, image->size, d + 16, &first_thunk))
            return PCPS4_PE_ERR_TRUNCATED;

        if (orig_first_thunk == 0 && name_rva == 0 && first_thunk == 0)
            break;

        if (first_thunk == 0)
            continue;

        if (pcps4_pe_rva_to_offset(image, name_rva, &name_off) != PCPS4_PE_OK)
            return PCPS4_PE_ERR_BAD_OFFSET;

        dll = pe_read_cstr(image, name_off, 256);
        if (dll == NULL)
            return PCPS4_PE_ERR_TRUNCATED;

        ilt_rva = orig_first_thunk ? orig_first_thunk : first_thunk;
        if (pcps4_pe_rva_to_offset(image, ilt_rva, &ilt_off) != PCPS4_PE_OK)
            return PCPS4_PE_ERR_BAD_OFFSET;

        for (uint32_t t = 0; t < PCPS4_PE_MAX_IMPORTS_PER_DLL; ++t) {
            uint64_t entry = 0;
            void *fn = NULL;

            if (!pe_read_u64(
                    image->data, image->size,
                    (size_t)ilt_off + (size_t)t * 8u, &entry))
                return PCPS4_PE_ERR_TRUNCATED;

            if (entry == 0)
                break;

            if (entry & 0x8000000000000000ull) {
                uint16_t ordinal = (uint16_t)(entry & 0xFFFFu);
                fn = pcps4_pe_resolver_lookup_ordinal(
                    resolver, dll, ordinal);
                if (fn == NULL && resolver->fallback != NULL)
                    fn = resolver->fallback(
                        resolver->fallback_user,
                        dll, 0, NULL, ordinal);
                if (fn == NULL &&
                    cb(user, dll, 0, NULL, ordinal) != 0)
                    return PCPS4_PE_OK;
            } else {
                uint32_t hint_name_off;
                const char *name;

                if (pcps4_pe_rva_to_offset(
                        image, (uint32_t)entry, &hint_name_off) != PCPS4_PE_OK)
                    return PCPS4_PE_ERR_BAD_OFFSET;

                name = pe_read_cstr(image, hint_name_off + 2, 1024);
                if (name == NULL)
                    return PCPS4_PE_ERR_TRUNCATED;

                fn = pcps4_pe_resolver_lookup(resolver, dll, name);
                if (fn == NULL && resolver->fallback != NULL)
                    fn = resolver->fallback(
                        resolver->fallback_user,
                        dll, 1, name, 0);
                if (fn == NULL &&
                    cb(user, dll, 1, name, 0) != 0)
                    return PCPS4_PE_OK;
            }
        }
    }

    return PCPS4_PE_OK;
}

pcps4_pe_status pcps4_pe_load(const uint8_t *exe, size_t exe_size,
                              uint8_t *dest, size_t dest_size,
                              uint64_t load_base,
                              const pcps4_pe_resolver *resolver,
                              pcps4_pe_image *out_image,
                              uint32_t *out_resolved,
                              uint32_t *out_unresolved)
{
    pcps4_pe_status rc;

    if (out_resolved) *out_resolved = 0;
    if (out_unresolved) *out_unresolved = 0;

    if (out_image == NULL)
        return PCPS4_PE_ERR_NULL;

    rc = pcps4_pe_parse(exe, exe_size, out_image);
    if (rc != PCPS4_PE_OK)
        return rc;

    rc = pcps4_pe_map_image(out_image, dest, dest_size, load_base);
    if (rc != PCPS4_PE_OK)
        return rc;

    if (resolver != NULL) {
        rc = pcps4_pe_bind_imports(out_image, dest, dest_size, resolver,
                                   out_resolved, out_unresolved);
        if (rc != PCPS4_PE_OK)
            return rc;
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
        case PCPS4_PE_ERR_UNSUPPORTED_RELOC: return "unsupported relocation type";
        case PCPS4_PE_ERR_NO_SPACE:     return "destination buffer too small";
        default:                        return "unknown error";
    }
}
