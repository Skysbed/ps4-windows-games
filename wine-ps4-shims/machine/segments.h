#ifndef _PCPS4_MACHINE_SEGMENTS_H
#define _PCPS4_MACHINE_SEGMENTS_H
#define GSEL(s,r) (((s)<<3)|(r))
#define SEL_UPL 3
#define GUDATA_SEL 5
#define GUCODE32_SEL 4
#define GUCODE_SEL 6
#define GUFS_SEL 7
#define GUGS_SEL 8
#endif
