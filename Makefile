# PcPs4: PC source -> compatibility layers -> OpenOrbis -> PKG

PROJECT       := PcPs4
TITLE         := PcPs4
VERSION       := 1.00
TITLE_ID      := PCPS00001
CONTENT_ID    := IV0000-PCPS00001_00-PCPS4TRANSLATE01

TOOLCHAIN     := $(OO_PS4_TOOLCHAIN)
PCPS4_ENABLE_OPENGNM ?= 0
PCPS4_RUN_GAME ?= 1
PCPS4_BUNDLED_GAME ?= 0
PCPS4_GAME_DIR ?=
PCPS4_GRAPHICS_ROOT ?= external/ps4-graphics
PCPS4_OPENGNM_DIR := $(PCPS4_GRAPHICS_ROOT)/opengnm
INTDIR        := build
GENERATED     := $(INTDIR)/generated
GNM_RENDERER_OBJ := $(INTDIR)/pcps4_gnm_renderer.o
PACKAGE_ROOT  := $(INTDIR)/package
REPORT        := $(INTDIR)/translation.json

PYTHON        := python3
TRANSLATOR    := tools/pcps4_translate.py

PC_C_SOURCES  := $(wildcard pc_source/*.c)
PC_CPP_SOURCES := $(wildcard pc_source/*.cpp)
TRANSLATED_C  := $(patsubst pc_source/%.c,$(GENERATED)/%.c,$(PC_C_SOURCES))
TRANSLATED_CPP := $(patsubst pc_source/%.cpp,$(GENERATED)/%.cpp,$(PC_CPP_SOURCES))
TRANSLATED    := $(TRANSLATED_C) $(TRANSLATED_CPP)

CC  := clang
CXX := clang++
LD  := ld.lld

COMMONFLAGS := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables \
               -isysroot $(TOOLCHAIN) \
               -isystem $(TOOLCHAIN)/include \
               -Icompat $(if $(filter 1,$(PCPS4_ENABLE_OPENGNM)),-I$(PCPS4_OPENGNM_DIR)/include,)
CFLAGS   := $(COMMONFLAGS) -std=c11 \
            -DPCPS4_ENABLE_OPENGNM=$(PCPS4_ENABLE_OPENGNM) \
            -DPCPS4_RUN_GAME=$(PCPS4_RUN_GAME) \
            -DPCPS4_BUNDLED_GAME=$(PCPS4_BUNDLED_GAME) \
            $(if $(PCPS4_ENABLE_GNM),-DPCPS4_ENABLE_GNM=1,) -c
CXXFLAGS := $(COMMONFLAGS) \
            -isystem $(TOOLCHAIN)/include/c++/v1 -c

OBJS_C    := $(patsubst $(GENERATED)/%.c,$(INTDIR)/%.o,$(TRANSLATED_C))
OBJS_CPP  := $(patsubst $(GENERATED)/%.cpp,$(INTDIR)/%.cpp.o,$(TRANSLATED_CPP))
OBJS      := $(OBJS_C) $(OBJS_CPP)

SYS_OBJ   := $(INTDIR)/pcps4_sys.o
FAULT_OBJ := $(INTDIR)/pcps4_fault.o
GFX_OBJ   := $(INTDIR)/pcps4_gfx.o
GNM_OBJ   := $(INTDIR)/pcps4_gnm.o
GPU_OBJ   := $(INTDIR)/pcps4_gpu.o
D3D11_OBJ := $(INTDIR)/pcps4_d3d11.o
SHADER_OBJ := $(INTDIR)/pcps4_shader.o
PE_OBJ    := $(INTDIR)/pcps4_pe.o
LOADER_OBJ := $(INTDIR)/pcps4_loader.o
STATUS_OBJ := $(INTDIR)/pcps4_status.o
XINPUT_OBJ := $(INTDIR)/pcps4_xinput.o
WINAPI_OBJ := $(INTDIR)/pcps4_winapi.o
GAME_OBJ  := $(INTDIR)/pcps4_game.o
D3D11_COM_OBJ := $(INTDIR)/pcps4_d3d11_com.o

ELF := $(INTDIR)/$(PROJECT).elf
OELF := $(INTDIR)/$(PROJECT).oelf

SFO := $(PACKAGE_ROOT)/sce_sys/param.sfo
PACKAGE_EBOOT := $(PACKAGE_ROOT)/eboot.bin
GP4 := $(PACKAGE_ROOT)/$(CONTENT_ID).gp4
PKG := $(PACKAGE_ROOT)/$(CONTENT_ID).pkg

# Files every OpenOrbis package needs, matching the toolchain's own samples:
# the libc / Fios2 runtime modules the ELF loads at startup, the about/right
# module and the app icon. Without sce_module/libc.prx the app cannot start
# (the PS4 reports it as corrupted).
PACKAGE_FILES := eboot.bin sce_sys/param.sfo sce_sys/icon0.png \
                 sce_sys/about/right.sprx \
                 sce_module/libc.prx sce_module/libSceFios2.prx
PACKAGE_SUPPORT := $(PACKAGE_ROOT)/sce_sys/icon0.png \
                   $(PACKAGE_ROOT)/sce_sys/about/right.sprx \
                   $(PACKAGE_ROOT)/sce_module/libc.prx \
                   $(PACKAGE_ROOT)/sce_module/libSceFios2.prx
OO_MODULES := $(TOOLCHAIN)/src/modules
OO_ICON    := $(TOOLCHAIN)/samples/hello_world/sce_sys/icon0.png
ifeq ($(PCPS4_ENABLE_OPENGNM),1)
PACKAGE_FILES += assets/shaders/tri.vert.sb assets/shaders/tri.frag.sb
endif

CREATE_FSELF := $(TOOLCHAIN)/bin/linux/create-fself
CREATE_GP4 := $(TOOLCHAIN)/bin/linux/create-gp4
PKGTOOL := $(TOOLCHAIN)/bin/linux/PkgTool.Core

LDFLAGS := -m elf_x86_64 -pie \
           --script $(TOOLCHAIN)/link.x \
           --eh-frame-hdr \
           -L$(TOOLCHAIN)/lib \
           -lc -lkernel -lc++ -lSceVideoOut \
           $(if $(PCPS4_ENABLE_GNM),-lSceGnmDriver,) \
           -lScePad -lSceUserService -lSceNet \
           $(if $(filter 1,$(PCPS4_ENABLE_OPENGNM)),-L$(PCPS4_OPENGNM_DIR) -lopengnm,) \
           $(TOOLCHAIN)/lib/crt1.o

.PHONY: all pkg eboot translate test test-host clean info

# Host-compiled unit tests (run on the CI host, not the PS4 target).
HOST_CC    ?= clang
HOST_CFLAGS := -std=c11 -Wall -Wextra -Werror -O2
PE_HOST_TEST := $(INTDIR)/pe_host_test
PE_MAP_HOST_TEST := $(INTDIR)/pe_map_host_test
PE_BIND_HOST_TEST := $(INTDIR)/pe_bind_host_test
DXBC_HOST_TEST := $(INTDIR)/dxbc_host_test

all: pkg

.PHONY: package-game

package-game:
	@rm -rf "$(PACKAGE_ROOT)/game"
	@if [ "$(PCPS4_BUNDLED_GAME)" = "1" ]; then \
		test -n "$(PCPS4_GAME_DIR)" || (echo "ERROR: PCPS4_GAME_DIR must be set when PCPS4_BUNDLED_GAME=1"; exit 1); \
		test -f "$(PCPS4_GAME_DIR)/game.exe" || (echo "ERROR: $(PCPS4_GAME_DIR)/game.exe not found"; exit 1); \
		mkdir -p "$(PACKAGE_ROOT)/game"; \
		cp -R "$(PCPS4_GAME_DIR)/." "$(PACKAGE_ROOT)/game/"; \
		echo "Bundled Windows game staged from $(PCPS4_GAME_DIR)"; \
	fi

translate: $(TRANSLATED)

$(GENERATED)/%.c: pc_source/%.c $(TRANSLATOR) compat/pcps4_compat.h compat/pcps4_xinput.h compat/pcps4_sys.h
	@mkdir -p "$(dir $@)"
	$(PYTHON) "$(TRANSLATOR)" "$<" --out "$@" --report "$(REPORT)" --strict

$(GENERATED)/%.cpp: pc_source/%.cpp $(TRANSLATOR) compat/pcps4_compat.h compat/pcps4_d3d11.h compat/pcps4_xinput.h compat/pcps4_sys.h
	@mkdir -p "$(dir $@)"
	$(PYTHON) "$(TRANSLATOR)" "$<" --out "$@" --report "$(REPORT)" --strict

$(INTDIR)/%.o: $(GENERATED)/%.c | $(INTDIR)
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -o "$@" "$<"

$(INTDIR)/%.cpp.o: $(GENERATED)/%.cpp | $(INTDIR)
	@mkdir -p "$(dir $@)"
	$(CXX) $(CXXFLAGS) -o "$@" "$<"

$(SYS_OBJ): compat/pcps4_sys.c compat/pcps4_sys.h
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -o "$@" "$<"

$(FAULT_OBJ): compat/pcps4_fault.c compat/pcps4_fault.h
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -o "$@" "$<"

$(GFX_OBJ): compat/pcps4_gfx.c compat/pcps4_gfx.h
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -o "$@" "$<"

$(GNM_OBJ): compat/pcps4_gnm.c compat/pcps4_gnm.h
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -o "$@" "$<"


$(GNM_RENDERER_OBJ): compat/pcps4_gnm_renderer.c compat/pcps4_gnm_renderer.h
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -o "$@" "$<"

$(GPU_OBJ): compat/pcps4_gpu.c compat/pcps4_gpu.h compat/pcps4_gfx.h
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -o "$@" "$<"

$(D3D11_OBJ): compat/pcps4_d3d11.cpp compat/pcps4_d3d11.h compat/pcps4_gpu.h compat/pcps4_shader.h
	@mkdir -p "$(dir $@)"
	$(CXX) $(CXXFLAGS) -o "$@" "$<"

$(INTDIR)/pcps4_xinput.o: compat/pcps4_xinput.c compat/pcps4_xinput.h compat/pcps4_compat.h
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -o "$@" "$<"

$(WINAPI_OBJ): compat/pcps4_winapi.c compat/pcps4_winapi.h compat/pcps4_compat.h compat/pcps4_xinput.h compat/pcps4_pe.h
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -o "$@" "$<"

$(GAME_OBJ): compat/pcps4_game.c compat/pcps4_game.h compat/pcps4_loader.h compat/pcps4_winapi.h
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -o "$@" "$<"

$(D3D11_COM_OBJ): compat/pcps4_d3d11_com.cpp compat/pcps4_d3d11_com.h compat/pcps4_d3d11.h
	@mkdir -p "$(dir $@)"
	$(CXX) $(CXXFLAGS) -o "$@" "$<"

$(SHADER_OBJ): compat/pcps4_shader.c compat/pcps4_shader.h
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -o "$@" "$<"

$(PE_OBJ): compat/pcps4_pe.c compat/pcps4_pe.h
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -o "$@" "$<"

$(LOADER_OBJ): compat/pcps4_loader.c compat/pcps4_loader.h compat/pcps4_pe.h
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -o "$@" "$<"

$(STATUS_OBJ): compat/pcps4_status.c compat/pcps4_status.h compat/pcps4_gfx.h
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -o "$@" "$<"

$(INTDIR):
	@mkdir -p "$(INTDIR)"

$(ELF): $(OBJS) $(SYS_OBJ) $(FAULT_OBJ) $(GFX_OBJ) $(GNM_OBJ) $(GNM_RENDERER_OBJ) $(GPU_OBJ) $(D3D11_OBJ) $(SHADER_OBJ) $(PE_OBJ) $(LOADER_OBJ) $(STATUS_OBJ) $(XINPUT_OBJ) $(WINAPI_OBJ) $(GAME_OBJ) $(D3D11_COM_OBJ)
	@test -n "$(OBJS)" || (echo "ERROR: no PC source files found in pc_source/."; exit 1)
	$(LD) $(OBJS) $(SYS_OBJ) $(FAULT_OBJ) $(GFX_OBJ) $(GNM_OBJ) \
	       $(GPU_OBJ) $(D3D11_OBJ) $(SHADER_OBJ) $(GNM_RENDERER_OBJ) $(PE_OBJ) $(LOADER_OBJ) $(STATUS_OBJ) $(XINPUT_OBJ) $(WINAPI_OBJ) $(GAME_OBJ) $(D3D11_COM_OBJ) -o "$@" $(LDFLAGS)

eboot.bin: $(ELF)
	@test -n "$(OO_PS4_TOOLCHAIN)" || (echo "ERROR: OO_PS4_TOOLCHAIN is not set."; exit 1)
	@test -x "$(CREATE_FSELF)" || (echo "ERROR: $(CREATE_FSELF) not found."; exit 1)
	"$(CREATE_FSELF)" -in="$(ELF)" -out="$(OELF)" --eboot "eboot.bin" \
	    --paid 0x3800000000000011

$(SFO):
	@mkdir -p "$(PACKAGE_ROOT)/sce_sys"
	@test -x "$(PKGTOOL)" || (echo "ERROR: $(PKGTOOL) not found."; exit 1)
	"$(PKGTOOL)" sfo_new "$@"
	"$(PKGTOOL)" sfo_setentry "$@" APP_TYPE --type Integer --maxsize 4 --value 1
	"$(PKGTOOL)" sfo_setentry "$@" APP_VER --type Utf8 --maxsize 8 --value "$(VERSION)"
	"$(PKGTOOL)" sfo_setentry "$@" ATTRIBUTE --type Integer --maxsize 4 --value 0
	"$(PKGTOOL)" sfo_setentry "$@" CATEGORY --type Utf8 --maxsize 4 --value "gd"
	"$(PKGTOOL)" sfo_setentry "$@" CONTENT_ID --type Utf8 --maxsize 48 --value "$(CONTENT_ID)"
	"$(PKGTOOL)" sfo_setentry "$@" DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
	"$(PKGTOOL)" sfo_setentry "$@" SYSTEM_VER --type Integer --maxsize 4 --value 0
	"$(PKGTOOL)" sfo_setentry "$@" TITLE --type Utf8 --maxsize 128 --value "$(TITLE)"
	"$(PKGTOOL)" sfo_setentry "$@" TITLE_ID --type Utf8 --maxsize 12 --value "$(TITLE_ID)"
	"$(PKGTOOL)" sfo_setentry "$@" VERSION --type Utf8 --maxsize 8 --value "$(VERSION)"

$(PACKAGE_EBOOT): eboot.bin
	@mkdir -p "$(PACKAGE_ROOT)"
	cp "$<" "$@"

$(PACKAGE_ROOT)/sce_module/%.prx: $(OO_MODULES)/%.prx
	@mkdir -p "$(dir $@)"
	cp "$<" "$@"

$(PACKAGE_ROOT)/sce_sys/about/right.sprx: $(OO_MODULES)/right.sprx
	@mkdir -p "$(dir $@)"
	cp "$<" "$@"

$(PACKAGE_ROOT)/sce_sys/icon0.png: $(OO_ICON)
	@mkdir -p "$(dir $@)"
	cp "$<" "$@"

$(GP4): $(SFO) $(PACKAGE_EBOOT) $(PACKAGE_SUPPORT) package-game
	@test -x "$(CREATE_GP4)" || (echo "ERROR: $(CREATE_GP4) not found."; exit 1)
	cd "$(PACKAGE_ROOT)" && \
	  GAME_FILES="$$(if [ -d game ]; then find game -type f -print | tr '\\n' ' '; fi)" && \
	  "$(CREATE_GP4)" \
	    -out "$(notdir $@)" \
	    --content-id="$(CONTENT_ID)" \
	    --files "$(PACKAGE_FILES)" $$GAME_FILES

$(PKG): $(GP4)
	@test -x "$(PKGTOOL)" || (echo "ERROR: $(PKGTOOL) not found."; exit 1)
	cd "$(PACKAGE_ROOT)" && "$(PKGTOOL)" pkg_build "$(notdir $<)" .
	@test -s "$@" || (echo "ERROR: PkgTool did not create $@"; exit 1)

pkg: $(PKG)
	@echo "PS4 package created: $(PKG)"
	@echo "Content ID: $(CONTENT_ID)"
	@echo "Title ID: $(TITLE_ID)"

test:
	$(PYTHON) -m unittest discover -s tests -v

test-host: $(PE_HOST_TEST) $(PE_MAP_HOST_TEST) $(PE_BIND_HOST_TEST) $(DXBC_HOST_TEST)
	"$(PE_HOST_TEST)"
	"$(PE_MAP_HOST_TEST)"
	"$(PE_BIND_HOST_TEST)"
	"$(DXBC_HOST_TEST)"

$(PE_HOST_TEST): compat/pcps4_pe.c compat/pcps4_pe.h tests/pe_host_test.c | $(INTDIR)
	$(HOST_CC) $(HOST_CFLAGS) -o "$@" compat/pcps4_pe.c tests/pe_host_test.c

$(PE_MAP_HOST_TEST): compat/pcps4_pe.c compat/pcps4_pe.h tests/pe_map_host_test.c | $(INTDIR)
	$(HOST_CC) $(HOST_CFLAGS) -o "$@" compat/pcps4_pe.c tests/pe_map_host_test.c

$(PE_BIND_HOST_TEST): compat/pcps4_pe.c compat/pcps4_pe.h tests/pe_bind_host_test.c | $(INTDIR)
	$(HOST_CC) $(HOST_CFLAGS) -o "$@" compat/pcps4_pe.c tests/pe_bind_host_test.c

$(DXBC_HOST_TEST): compat/pcps4_shader.c compat/pcps4_shader.h tests/dxbc_host_test.c | $(INTDIR)
	$(HOST_CC) $(HOST_CFLAGS) -o "$@" compat/pcps4_shader.c tests/dxbc_host_test.c

info:
	@test -n "$(OO_PS4_TOOLCHAIN)" || (echo "ERROR: OO_PS4_TOOLCHAIN is not set."; exit 1)
	@echo "OpenOrbis toolchain: $(TOOLCHAIN)"
	@echo "Target: x86_64-pc-freebsd12-elf"
	@echo "Translator: $(TRANSLATOR)"
	@echo "OS adapter: compat/pcps4_sys.[ch]"
	@echo "Graphics backend: compat/pcps4_gfx.[ch]"
	@echo "GPU resources: compat/pcps4_gpu.[ch]"
	@echo "D3D11 façade: compat/pcps4_d3d11.[h/cpp]"
	@echo "DXBC parser: compat/pcps4_shader.[h/c]"
	@echo "GNM bridge: compat/pcps4_gnm.[ch]"
	@echo "Real GNM renderer: compat/pcps4_gnm_renderer.[ch]"
	@echo "OpenGNM enabled: $(PCPS4_ENABLE_OPENGNM)"
	@echo "Interactive game demo: $(PCPS4_RUN_GAME)"
	@echo "Bundled Windows game: $(PCPS4_BUNDLED_GAME)"
	@echo "Bundled game directory: $(PCPS4_GAME_DIR)"
	@echo "Fault boundary: compat/pcps4_fault.[ch]"
	@echo "Content ID: $(CONTENT_ID)"
	@echo "Title ID: $(TITLE_ID)"

clean:
	rm -rf "$(INTDIR)" eboot.bin
