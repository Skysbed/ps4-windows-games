# PcPs4 OpenOrbis build.
# Based on the build flow used by the official OpenOrbis Linux samples.

PROJECT     := PcPs4
TOOLCHAIN   := $(OO_PS4_TOOLCHAIN)
INTDIR      := build

CC          := clang
LD          := ld.lld

SRC         := src/main.c
OBJ         := $(INTDIR)/main.o
ELF         := $(INTDIR)/$(PROJECT).elf
OELF        := $(INTDIR)/$(PROJECT).oelf

CFLAGS      := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -c \
               -isysroot $(TOOLCHAIN) \
               -isystem $(TOOLCHAIN)/include \
               -Icompat

LDFLAGS     := -m elf_x86_64 -pie \
               --script $(TOOLCHAIN)/link.x \
               --eh-frame-hdr \
               -L$(TOOLCHAIN)/lib \
               -lc -lkernel $(TOOLCHAIN)/lib/crt1.o

CREATE_FSELF := $(TOOLCHAIN)/bin/linux/create-fself

.PHONY: all clean info

all: eboot.bin

$(INTDIR):
	@mkdir -p $(INTDIR)

$(OBJ): $(SRC) compat/pcps4_compat.h | $(INTDIR)
	$(CC) $(CFLAGS) -o $@ $(SRC)

$(ELF): $(OBJ)
	$(LD) $(OBJ) -o $@ $(LDFLAGS)

eboot.bin: $(ELF)
	@test -n "$(OO_PS4_TOOLCHAIN)" || (echo "ERROR: OO_PS4_TOOLCHAIN is not set."; exit 1)
	@test -x "$(CREATE_FSELF)" || (echo "ERROR: $(CREATE_FSELF) not found. Check OO_PS4_TOOLCHAIN."; exit 1)
	"$(CREATE_FSELF)" -in=$(ELF) -out=$(OELF) --eboot "eboot.bin" --paid 0x3800000000000011

info:
	@test -n "$(OO_PS4_TOOLCHAIN)" || (echo "ERROR: OO_PS4_TOOLCHAIN is not set."; exit 1)
	@echo "OpenOrbis toolchain: $(OO_PS4_TOOLCHAIN)"
	@echo "Target: x86_64-pc-freebsd12-elf"
	@echo "Compiler: $(CC)"
	@echo "Linker: $(LD)"
	@echo "Linker script: $(TOOLCHAIN)/link.x"
	@echo "create-fself: $(CREATE_FSELF)"

clean:
	rm -rf $(INTDIR) eboot.bin
