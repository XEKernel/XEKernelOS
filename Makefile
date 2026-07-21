# XEKernelOS Makefile — C++ Build
#   Boot:      boot.asm + stage2.asm (NASM)
#   ISR asm:   isr.asm (NASM elf32)
#   Kernel:    C++ sources

NASM     = nasm
CXX      = clang++
LD       = ld.lld
OBJCOPY  = llvm-objcopy
TARGET   = i686-elf

SRCDIR   = src
BLDDIR   = build

BOOT_SRC   = $(SRCDIR)/boot/boot.asm
STAGE2_SRC = $(SRCDIR)/boot/stage2.asm
ISR_ASM    = $(SRCDIR)/kernel/isr.asm
LINKER_SRC = $(SRCDIR)/linker.ld

# All C++ sources
CXX_SRCS := $(SRCDIR)/kernel/kernel.cpp \
            $(SRCDIR)/kernel/isr.cpp \
            $(SRCDIR)/kernel/idt.cpp \
            $(SRCDIR)/kernel/mm.cpp \
            $(SRCDIR)/kernel/paging.cpp \
            $(SRCDIR)/kernel/user.cpp \
            $(SRCDIR)/kernel/panic.cpp \
            $(SRCDIR)/kernel/syscall.cpp \
            $(SRCDIR)/kernel/task.cpp \
            $(SRCDIR)/kernel/loader.cpp \
            $(SRCDIR)/kernel/elf.cpp \
            $(SRCDIR)/drivers/keyboard.cpp \
            $(SRCDIR)/drivers/pic.cpp \
            $(SRCDIR)/drivers/pit.cpp \
            $(SRCDIR)/drivers/mouse.cpp \
            $(SRCDIR)/drivers/ata.cpp \
            $(SRCDIR)/drivers/gfx.cpp \
            $(SRCDIR)/drivers/serial.cpp \
            $(SRCDIR)/fs/fat12.cpp \
            $(SRCDIR)/shell/shell.cpp \
            $(SRCDIR)/lib/heap.cpp \
            $(SRCDIR)/lib/strutil.cpp \
            $(SRCDIR)/lib/cpprt.cpp

CXX_OBJS = $(patsubst $(SRCDIR)/%.cpp,$(BLDDIR)/%.o,$(CXX_SRCS))

BOOT_BIN   = $(BLDDIR)/boot.bin
STAGE2_BIN = $(BLDDIR)/stage2.bin
ISR_OBJ    = $(BLDDIR)/isr.o
KERNEL_ELF = $(BLDDIR)/kernel.elf
KERNEL_BIN = $(BLDDIR)/kernel.bin
IMG        = $(BLDDIR)/xekernelos.img
HELLO_BIN  = $(BLDDIR)/hello.bin

USER_ASM   = $(SRCDIR)/user/hello.asm

CXXFLAGS = -target $(TARGET) -ffreestanding -nostdlib -Wall -Wextra -O1 \
           -fno-exceptions -fno-rtti -fno-use-cxa-atexit -std=c++17 \
           -mno-sse -mno-mmx -mno-sse2 -I $(SRCDIR)
LDFLAGS = -m elf_i386

.PHONY: all run clean

TEST_ELF_ASM = $(SRCDIR)/user/test_elf.asm
TEST_ELF_O   = $(BLDDIR)/test_elf.o
TEST_ELF     = $(BLDDIR)/test_elf.elf

all: $(IMG) $(HELLO_BIN) $(TEST_ELF)

$(BOOT_BIN): $(BOOT_SRC) | $(BLDDIR)
	$(NASM) -f bin $< -o $@

$(STAGE2_BIN): $(STAGE2_SRC) | $(BLDDIR)
	$(NASM) -f bin $< -o $@

$(ISR_OBJ): $(ISR_ASM) | $(BLDDIR)
	$(NASM) -f elf32 -w-label-orphan $< -o $@

$(BLDDIR)/%.o: $(SRCDIR)/%.cpp | $(BLDDIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(KERNEL_ELF): $(ISR_OBJ) $(CXX_OBJS) $(LINKER_SRC)
	$(LD) $(LDFLAGS) -T $(LINKER_SRC) $(ISR_OBJ) $(CXX_OBJS) -o $@

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

$(IMG): $(BOOT_BIN) $(STAGE2_BIN) $(KERNEL_BIN) build_img.py
	python build_img.py $(BLDDIR)
	@echo "=== Image ==="
	@ls -la $@
	@echo "=== Sections ==="
	@echo "  Boot:    512B"
	@echo "  Stage2:  $$(wc -c < $(STAGE2_BIN))B"
	@echo "  Kernel:  $$(wc -c < $(KERNEL_BIN))B"

$(BLDDIR):
	mkdir -p $(BLDDIR)

$(HELLO_BIN): $(USER_ASM) | $(BLDDIR)
	$(NASM) -f bin $< -o $@
	@echo "  User hello: $$(wc -c < $@)B"

$(TEST_ELF_O): $(TEST_ELF_ASM) | $(BLDDIR)
	$(NASM) -f elf32 $< -o $@

$(TEST_ELF): $(TEST_ELF_O)
	$(LD) $(LDFLAGS) -Ttext=0x10000000 $< -o $@
	@echo "  ELF test: $$(wc -c < $@)B"

run: $(IMG)
	qemu-system-i386 -fda $< -hda $(BLDDIR)/disk.img -m 32

run-debug: $(IMG)
	qemu-system-i386 -fda $< -hda $(BLDDIR)/disk.img -monitor stdio -d cpu_reset,int -m 32

clean:
	rm -rf $(BLDDIR)
