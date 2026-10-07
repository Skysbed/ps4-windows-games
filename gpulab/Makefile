# PcPs4 GPU Lab (round 2) - builds on a Mac or Linux with the OpenOrbis toolchain.
#
# Required (environment or command line):
#   OO_PS4_TOOLCHAIN   OpenOrbis toolchain root
#   VENDOR             freegnm-examples checkout (shared helper sources)
#   OGNM               opengnm checkout (headers)
#   PSBC_INC           opengnm-psbc/libpsbc (psbc_compile.h)
#   VKHDR              Vulkan-Headers checkout
#   LIBS               directory holding libopengnm.a, libpsbc.orbis.a, libvulkan_ps4.a
#   PSBC               the host shader compiler binary (opengnm-psbc)

TITLE ?= PcPs4 GPU Lab
VERSION = 1.00
TITLE_ID ?= PCPSGPU01
CONTENT_ID ?= IV0000-PCPSGPU01_00-GPULABROUNDTWO00

TOOLCHAIN = $(OO_PS4_TOOLCHAIN)
SHARED = $(VENDOR)/shared
CC ?= clang
LD ?= ld.lld
GLSLC ?= glslc
MKFSELF ?= $(TOOLCHAIN)/bin/linux/create-fself
MKGP4 ?= $(TOOLCHAIN)/bin/linux/create-gp4
PKGTOOL ?= $(TOOLCHAIN)/bin/linux/PkgTool.Core
RUNTIME_MODULE_DIR ?= $(TOOLCHAIN)/src/modules

SRCS = $(SHARED)/src/u/fs.c $(SHARED)/src/u/utility.c $(SHARED)/src/displayctx.c \
       $(SHARED)/src/memalloc.c $(SHARED)/src/misc.c src/main.c src/psbc_link_stubs.c
OBJS = $(SRCS:%.c=%.o)
EXE = lab

CFLAGS = -std=c11 -Wall -Wextra -D_BSD_SOURCE -DUSE_OPENGNM -DCGLM_ALL_UNALIGNED \
         -DCGLM_FORCE_LEFT_HANDED -DCGLM_FORCE_DEPTH_ZERO_TO_ONE \
         -I$(SHARED)/src -I$(OGNM)/include -I$(PSBC_INC) -I$(VKHDR)/include
LDLIBS = -lc -lc++ -lkernel -lSceGnmDriver -lSceVideoOut \
         $(LIBS)/libvulkan_ps4.a $(LIBS)/libpsbc.orbis.a $(LIBS)/libopengnm.a

# Shaders compiled for the base PS4 GPU (-4) and for the PS4 Pro (-n).
BASE_SB = assets/misc/tri.vert.sb assets/misc/tri.frag.sb assets/misc/clear.frag.sb \
          assets/misc/solid.vert.sb assets/misc/solid.frag.sb assets/misc/clearsolid.frag.sb
NEO_SB  = assets/misc/solid.vert.neo.sb assets/misc/solid.frag.neo.sb \
          assets/misc/clearsolid.frag.neo.sb
# SPIR-V shipped as-is, for the on-console compile test and the Vulkan test.
SPV_BIN = assets/misc/rt.vert.bin assets/misc/rt.frag.bin assets/misc/rtclear.frag.bin
ASSETS = $(BASE_SB) $(NEO_SB) $(SPV_BIN)

RUNTIME_MODULES = sce_module/libc.prx sce_module/libSceFios2.prx
PKG_FILES = eboot.bin sce_sys/about/right.sprx sce_sys/param.sfo sce_sys/icon0.png \
            $(RUNTIME_MODULES) $(ASSETS)

.PRECIOUS: %.spv
all: $(CONTENT_ID).pkg

%.o: %.c
	$(CC) --target=x86_64-ps4-elf -fPIC -c -isysroot $(TOOLCHAIN) \
		-isystem $(TOOLCHAIN)/include $(CFLAGS) -o $@ $<

$(EXE): $(OBJS)
	$(LD) -o $@ $(OBJS) -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x \
		--eh-frame-hdr -z muldefs -L$(TOOLCHAIN)/lib $(LDLIBS) $(TOOLCHAIN)/lib/crt1.o \
		$(TOOLCHAIN)/lib/crti.o $(TOOLCHAIN)/lib/crtn.o

%.vert.spv: %.vert.glsl
	$(GLSLC) -fshader-stage=vertex $< -o $@
%.frag.spv: %.frag.glsl
	$(GLSLC) -fshader-stage=fragment $< -o $@

# PS4 (base GFX7) shaders: -4. NEO (PS4 Pro GFX8) shaders: -n.
%.vert.sb: %.vert.spv
	$(PSBC) -4 -s vertex -f $< -o $@
%.frag.sb: %.frag.spv
	$(PSBC) -4 -s fragment -f $< -o $@
%.vert.neo.sb: %.vert.spv
	$(PSBC) -n -s vertex -f $< -o $@
%.frag.neo.sb: %.frag.spv
	$(PSBC) -n -s fragment -f $< -o $@

assets/misc/rt.vert.bin: assets/misc/solid.vert.spv
	cp $< $@
assets/misc/rt.frag.bin: assets/misc/solid.frag.spv
	cp $< $@
assets/misc/rtclear.frag.bin: assets/misc/clearsolid.frag.spv
	cp $< $@

eboot.bin: $(EXE)
	$(MKFSELF) -in=$(EXE) -out=$(EXE).oelf -eboot=eboot.bin --paid 0x3800000000000011

sce_module sce_sys:
	mkdir -p $@
	mkdir -p sce_sys/about

sce_sys/icon0.png: $(VENDOR)/videoout-linear/sce_sys/icon0.png | sce_sys
	cp $< $@
sce_sys/about/right.sprx: $(VENDOR)/videoout-linear/sce_sys/about/right.sprx | sce_sys
	cp $< $@
sce_module/libc.prx: $(RUNTIME_MODULE_DIR)/libc.prx | sce_module
	cp $< $@
sce_module/libSceFios2.prx: $(RUNTIME_MODULE_DIR)/libSceFios2.prx | sce_module
	cp $< $@

sce_sys/param.sfo: Makefile | sce_sys
	$(PKGTOOL) sfo_new $@
	$(PKGTOOL) sfo_setentry $@ APP_TYPE --type Integer --maxsize 4 --value 1
	$(PKGTOOL) sfo_setentry $@ APP_VER --type Utf8 --maxsize 8 --value '$(VERSION)'
	$(PKGTOOL) sfo_setentry $@ ATTRIBUTE --type Integer --maxsize 4 --value 0
	$(PKGTOOL) sfo_setentry $@ CATEGORY --type Utf8 --maxsize 4 --value 'gd'
	$(PKGTOOL) sfo_setentry $@ CONTENT_ID --type Utf8 --maxsize 48 --value '$(CONTENT_ID)'
	$(PKGTOOL) sfo_setentry $@ DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
	$(PKGTOOL) sfo_setentry $@ SYSTEM_VER --type Integer --maxsize 4 --value 0
	$(PKGTOOL) sfo_setentry $@ TITLE --type Utf8 --maxsize 128 --value '$(TITLE)'
	$(PKGTOOL) sfo_setentry $@ TITLE_ID --type Utf8 --maxsize 12 --value '$(TITLE_ID)'
	$(PKGTOOL) sfo_setentry $@ VERSION --type Utf8 --maxsize 8 --value '$(VERSION)'

pkg.gp4: $(PKG_FILES)
	$(MKGP4) -out=$@ -content-id=$(CONTENT_ID) -files "$(PKG_FILES)"

$(CONTENT_ID).pkg: pkg.gp4
	$(PKGTOOL) pkg_build $< .

clean:
	rm -f $(OBJS) $(EXE) $(EXE).oelf eboot.bin pkg.gp4 $(ASSETS) assets/misc/*.spv *.pkg
	rm -rf sce_sys sce_module
