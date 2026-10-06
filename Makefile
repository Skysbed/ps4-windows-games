# PcPs4: PC source -> translation layer -> OpenOrbis -> eboot.bin

PROJECT       := PcPs4
TOOLCHAIN     := $(OO_PS4_TOOLCHAIN)
INTDIR        := build
GENERATED     := $(INTDIR)/generated
REPORT        := $(INTDIR)/translation.json

PYTHON        := python3
TRANSLATOR    := tools/pcps4_translate.py

PC_C_SOURCES := $(wildcard pc_source/*.c)
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
CREATE_FSELF  := $(TOOLCHAIN)/bin/linux/create-fself

LDFLAGS       := -m elf_x86_64 -pie \
                 --script $(TOOLCHAIN)/link.x \
                 --eh-frame-hdr \
                 -L$(TOOLCHAIN)/lib \
                 -lc -lkernel -lc++ $(TOOLCHAIN)/lib/crt1.o

.PHONY: all translate test clean info

all: eboot.bin

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

test:
	$(PYTHON) -m unittest discover -s tests -v

info:
	@test -n "$(OO_PS4_TOOLCHAIN)" || (echo "ERROR: OO_PS4_TOOLCHAIN is not set."; exit 1)
	@echo "OpenOrbis toolchain: $(TOOLCHAIN)"
	@echo "Target: x86_64-pc-freebsd12-elf"
	@echo "C sources: $(PC_C_SOURCES)"
	@echo "C++ sources: $(PC_CPP_SOURCES)"
	@echo "Translator: $(TRANSLATOR)"
	@echo "create-fself: $(CREATE_FSELF)"

clean:
	rm -rf "$(INTDIR)" eboot.bin
