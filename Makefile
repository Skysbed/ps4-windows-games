# PcPs4: PC source -> translation layer -> OpenOrbis -> eboot.bin -> PKG

PROJECT       := PcPs4
TITLE         := PcPs4
VERSION       := 1.00
TITLE_ID      := PCPS00001
CONTENT_ID    := IV0000-PCPS00001_00-PCPS4TRANSLATOR01

TOOLCHAIN     := $(OO_PS4_TOOLCHAIN)
INTDIR        := build
GENERATED     := $(INTDIR)/generated
PACKAGE_ROOT  := $(INTDIR)/package
REPORT        := $(INTDIR)/translation.json

PYTHON        := python3
TRANSLATOR    := tools/pcps4_translate.py

PC_C_SOURCES  := $(wildcard pc_source/*.c)
PC_CPP_SOURCES := $(wildcard pc_source/*.cpp)
TRANSLATED_C  := $(patsubst pc_source/%.c,$(GENERATED)/%.c,$(PC_C_SOURCES))
TRANSLATED_CPP := $(patsubst pc_source/%.cpp,$(GENERATED)/%.cpp,$(PC_CPP_SOURCES))
TRANSLATED    := $(TRANSLATED_C) $(TRANSLATED_CPP)

CC            := clang
CXX           := clang++
LD            := ld.lld

COMMONFLAGS   := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables \
                 -isysroot $(TOOLCHAIN) \
                 -isystem $(TOOLCHAIN)/include \
                 -Icompat
CFLAGS        := $(COMMONFLAGS) -c
CXXFLAGS      := $(COMMONFLAGS) -isystem $(TOOLCHAIN)/include/c++/v1 -c

OBJS_C        := $(patsubst $(GENERATED)/%.c,$(INTDIR)/%.o,$(TRANSLATED_C))
OBJS_CPP      := $(patsubst $(GENERATED)/%.cpp,$(INTDIR)/%.cpp.o,$(TRANSLATED_CPP))
OBJS          := $(OBJS_C) $(OBJS_CPP)

ELF           := $(INTDIR)/$(PROJECT).elf
OELF          := $(INTDIR)/$(PROJECT).oelf

SFO           := $(PACKAGE_ROOT)/sce_sys/param.sfo
PACKAGE_EBOOT := $(PACKAGE_ROOT)/eboot.bin
GP4           := $(PACKAGE_ROOT)/$(CONTENT_ID).gp4
PKG           := $(PACKAGE_ROOT)/$(CONTENT_ID).pkg

CREATE_FSELF  := $(TOOLCHAIN)/bin/linux/create-fself
CREATE_GP4    := $(TOOLCHAIN)/bin/linux/create-gp4
PKGTOOL       := $(TOOLCHAIN)/bin/linux/PkgTool.Core

LDFLAGS       := -m elf_x86_64 -pie \
                 --script $(TOOLCHAIN)/link.x \
                 --eh-frame-hdr \
                 -L$(TOOLCHAIN)/lib \
                 -lc -lkernel -lc++ $(TOOLCHAIN)/lib/crt1.o

.PHONY: all pkg eboot translate test clean info

all: pkg

translate: $(TRANSLATED)

$(GENERATED)/%.c: pc_source/%.c $(TRANSLATOR) compat/pcps4_compat.h
	@mkdir -p "$(dir $@)"
	$(PYTHON) "$(TRANSLATOR)" "$<" --out "$@" --report "$(REPORT)" --strict

$(GENERATED)/%.cpp: pc_source/%.cpp $(TRANSLATOR) compat/pcps4_compat.h
	@mkdir -p "$(dir $@)"
	$(PYTHON) "$(TRANSLATOR)" "$<" --out "$@" --report "$(REPORT)" --strict

$(INTDIR)/%.o: $(GENERATED)/%.c | $(INTDIR)
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) -o "$@" "$<"

$(INTDIR)/%.cpp.o: $(GENERATED)/%.cpp | $(INTDIR)
	@mkdir -p "$(dir $@)"
	$(CXX) $(CXXFLAGS) -o "$@" "$<"

$(INTDIR):
	@mkdir -p "$(INTDIR)"

$(ELF): $(OBJS)
	@test -n "$(OBJS)" || (echo "ERROR: no PC source files found in pc_source/."; exit 1)
	$(LD) $(OBJS) -o "$@" $(LDFLAGS)

eboot.bin: $(ELF)
	@test -n "$(OO_PS4_TOOLCHAIN)" || (echo "ERROR: OO_PS4_TOOLCHAIN is not set."; exit 1)
	@test -x "$(CREATE_FSELF)" || (echo "ERROR: $(CREATE_FSELF) not found."; exit 1)
	"$(CREATE_FSELF)" -in="$(ELF)" -out="$(OELF)" --eboot "eboot.bin" --paid 0x3800000000000011

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

$(GP4): $(SFO) $(PACKAGE_EBOOT)
	@test -x "$(CREATE_GP4)" || (echo "ERROR: $(CREATE_GP4) not found."; exit 1)
	cd "$(PACKAGE_ROOT)" && "$(CREATE_GP4)" -out "$(notdir $@)" --content-id="$(CONTENT_ID)" --files "eboot.bin sce_sys/param.sfo"

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

info:
	@test -n "$(OO_PS4_TOOLCHAIN)" || (echo "ERROR: OO_PS4_TOOLCHAIN is not set."; exit 1)
	@echo "OpenOrbis toolchain: $(TOOLCHAIN)"
	@echo "Target: x86_64-pc-freebsd12-elf"
	@echo "Translator: $(TRANSLATOR)"
	@echo "create-fself: $(CREATE_FSELF)"
	@echo "create-gp4: $(CREATE_GP4)"
	@echo "PkgTool.Core: $(PKGTOOL)"
	@echo "Content ID: $(CONTENT_ID)"
	@echo "Title ID: $(TITLE_ID)"

clean:
	rm -rf "$(INTDIR)" eboot.bin