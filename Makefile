# PcPs4: PC source -> translation layer -> OpenOrbis -> eboot.bin

PROJECT       := PcPs4
TOOLCHAIN     := $(OO_PS4_TOOLCHAIN)
INTDIR        := build
GENERATED     := $(INTDIR)/generated
REPORT        := $(INTDIR)/translation.json

PYTHON        := python3
TRANSLATOR    := tools/pcps4_translate.py
PC_SOURCE     := pc_source/main.c
TRANSLATED    := $(GENERATED)/main.c

CC            := clang
LD            := ld.lld

CFLAGS        := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -c \
                 -isysroot $(TOOLCHAIN) \
                 -isystem $(TOOLCHAIN)/include \
                 -Icompat

LDFLAGS       := -m elf_x86_64 -pie \
                 --script $(TOOLCHAIN)/link.x \
                 --eh-frame-hdr \
                 -L$(TOOLCHAIN)/lib \
                 -lc -lkernel $(TOOLCHAIN)/lib/crt1.o

CREATE_FSELF  := $(TOOLCHAIN)/bin/linux/create-fself
OBJ           := $(INTDIR)/main.o
ELF           := $(INTDIR)/$(PROJECT).elf
OELF          := $(INTDIR)/$(PROJECT).oelf

.PHONY: all translate test clean info

all: eboot.bin

translate: $(TRANSLATED)

$(TRANSLATED): $(PC_SOURCE) $(TRANSLATOR) compat/pcps4_compat.h
	@mkdir -p "$(GENERATED)"
	$(PYTHON) "$(TRANSLATOR)" "$(PC_SOURCE)" --out "$@" --report "$(REPORT)" --strict

$(INTDIR):
	@mkdir -p "$(INTDIR)"

$(OBJ): $(TRANSLATED) compat/pcps4_compat.h | $(INTDIR)
	$(CC) $(CFLAGS) -o "$@" "$(TRANSLATED)"

$(ELF): $(OBJ)
	$(LD) "$(OBJ)" -o "$@" $(LDFLAGS)

eboot.bin: $(ELF)
	@test -n "$(OO_PS4_TOOLCHAIN)" || (echo "ERROR: OO_PS4_TOOLCHAIN is not set."; exit 1)
	@test -x "$(CREATE_FSELF)" || (echo "ERROR: $(CREATE_FSELF) not found."; exit 1)
	"$(CREATE_FSELF)" -in="$(ELF)" -out="$(OELF)" --eboot "eboot.bin" --paid 0x3800000000000011

test:
	$(PYTHON) -m unittest discover -s tests -v

info:
	@test -n "$(OO_PS4_TOOLCHAIN)" || (echo "ERROR: OO_PS4_TOOLCHAIN is not set."; exit 1)
	@echo "OpenOrbis toolchain: $(TOOLCHAIN)"
	@echo "Target: x86_64-pc-freebsd12-elf"
	@echo "Translator: $(TRANSLATOR)"
	@echo "create-fself: $(CREATE_FSELF)"

clean:
	rm -rf "$(INTDIR)" eboot.bin
