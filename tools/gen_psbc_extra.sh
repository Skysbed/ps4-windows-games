#!/usr/bin/env bash
# Generate the Mesa headers that opengnm-psbc's Makefile does not produce.
# Upstream Mesa makes these with meson custom targets; the commands below are
# the same ones taken from the corresponding meson.build files.
#
# usage: gen_psbc_extra.sh <path to opengnm-psbc> [python]
set -euo pipefail

PSBC_DIR="${1:?path to opengnm-psbc}"
PY="${2:-python3}"
cd "$PSBC_DIR"

# glsl builtin types (src/compiler/meson.build)
"$PY" src/compiler/builtin_types_h.py src/compiler/builtin_types.h
"$PY" src/compiler/builtin_types_c.py src/compiler/builtin_types.c

# shader statistics table (src/util/meson.build)
"$PY" src/util/process_shader_stats.py \
    src/util/shader_stats.rnc src/util/shader_stats.xml \
    > src/util/shader_stats.h

# format tables (src/util/format/meson.build)
"$PY" src/util/format/u_format_table.py src/util/format/u_format.yaml --enums \
    > src/util/format/u_format_gen.h
"$PY" src/util/format/u_format_table.py src/util/format/u_format.yaml --header \
    > src/util/format/u_format_pack.h

"$PY" src/util/format/u_format_table.py src/util/format/u_format.yaml \
    > src/util/format/u_format_table.c

# AMD GFX10/11 vertex format tables (src/amd/common/meson.build)
"$PY" src/amd/common/gfx10_format_table.py \
    src/util/format/u_format.yaml src/amd/registers/gfx10-rsrc.json \
    src/amd/registers/gfx11-rsrc.json > src/amd/common/gfx10_format_table.c

# AMD CP packet definitions (src/amd/common/meson.build)
for gen in gfx11 gfx12; do
    "$PY" src/amd/packets/parse_cp_pm4_table_data_json.py \
        src/amd/packets/cp_pm4_table_data_gfx11.json src/amd/packets/pm4_it_opcodes_gfx11.h \
        src/amd/packets/cp_pm4_table_data_gfx12.json src/amd/packets/pm4_it_opcodes_gfx12.h \
        "$gen" packets_h > "src/amd/common/amd_cp_packets_${gen}.h"
done

# Vulkan struct-type casts (src/vulkan/util/meson.build)
"$PY" src/vulkan/util/vk_struct_type_cast_gen.py \
    --xml src/vulkan/registry/vk.xml \
    --out src/vulkan/util/vk_struct_type_cast.h --beta false

echo "psbc extra headers generated"
