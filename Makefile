# XEKernelOS Makefile — Multi-File Build
#   Boot:      boot.asm + stage2.asm (NASM)
#   ISR:       isr.asm (NASM elf32)
#   Kernel:    kernel.c + idt.c + mm.c + keyboard.c + pic.c + shell.c

NASM     = nasm
CLANG    = clang
LD       = ld.lld
OBJCOPY  = llvm-objcopy
TARGET   = i686-elf

SRCDIR   = src
BLDDIR   = build

BOOT_SRC   = $(SRCDIR)/boot/boot.asm
STAGE2_SRC = $(SRCDIR)/boot/stage2.asm
ISR_SRC    = $(SRCDIR)/kernel/isr.asm
LINKER_SRC = $(SRCDIR)/linker.ld

KERN_C   = $(SRCDIR)/kernel/kernel.c
ISR_C    = $(SRCDIR)/kernel/isr.c
IDT_C    = $(SRCDIR)/kernel/idt.c
MM_C     = $(SRCDIR)/kernel/mm.c
PAGING_C = $(SRCDIR)/kernel/paging.c
USER_C   = $(SRCDIR)/kernel/user.c
PANIC_C  = $(SRCDIR)/kernel/panic.c
SYSCALL_C = $(SRCDIR)/kernel/syscall.c
TASK_C    = $(SRCDIR)/kernel/task.c
LOADER_C  = $(SRCDIR)/kernel/loader.c
KB_C     = $(SRCDIR)/drivers/keyboard.c
PIC_C    = $(SRCDIR)/drivers/pic.c
PIT_C    = $(SRCDIR)/drivers/pit.c
MOUSE_C  = $(SRCDIR)/drivers/mouse.c
ATA_C    = $(SRCDIR)/drivers/ata.c
GFX_C    = $(SRCDIR)/drivers/gfx.c
SERIAL_C = $(SRCDIR)/drivers/serial.c
FAT_C    = $(SRCDIR)/fs/fat12.c
SHELL_C  = $(SRCDIR)/shell/shell.c
HEAP_C   = $(SRCDIR)/lib/heap.c
STRUTIL_C = $(SRCDIR)/lib/strutil.c

C_SRCS   = $(KERN_C) $(ISR_C) $(IDT_C) $(MM_C) $(PAGING_C) $(USER_C) $(PANIC_C) $(SYSCALL_C) $(TASK_C) $(LOADER_C) $(KB_C) $(PIC_C) $(PIT_C) $(MOUSE_C) $(ATA_C) $(GFX_C) $(SERIAL_C) $(FAT_C) $(SHELL_C) $(HEAP_C) $(STRUTIL_C)
C_OBJS   = $(patsubst $(SRCDIR)/%.c,$(BLDDIR)/%.o,$(C_SRCS))

BOOT_BIN   = $(BLDDIR)/boot.bin
STAGE2_BIN = $(BLDDIR)/stage2.bin
ISR_OBJ    = $(BLDDIR)/isr.o
KERNEL_ELF = $(BLDDIR)/kernel.elf
KERNEL_BIN = $(BLDDIR)/kernel.bin
IMG        = $(BLDDIR)/xekernelos.img

CFLAGS  = -target $(TARGET) -ffreestanding -nostdlib -Wall -Wextra -O1 \
          -mno-sse -mno-mmx -mno-sse2 -I $(SRCDIR)
LDFLAGS = -m elf_i386

.PHONY: all run clean

all: $(IMG)

$(BOOT_BIN): $(BOOT_SRC) | $(BLDDIR)
	$(NASM) -f bin $< -o $@

$(STAGE2_BIN): $(STAGE2_SRC) | $(BLDDIR)
	$(NASM) -f bin $< -o $@

$(ISR_OBJ): $(ISR_SRC) | $(BLDDIR)
	$(NASM) -f elf32 -w-label-orphan $< -o $@

$(BLDDIR)/kernel/%.o: $(SRCDIR)/kernel/%.c | $(BLDDIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BLDDIR)/drivers/%.o: $(SRCDIR)/drivers/%.c | $(BLDDIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BLDDIR)/shell/%.o: $(SRCDIR)/shell/%.c | $(BLDDIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BLDDIR)/lib/%.o: $(SRCDIR)/lib/%.c | $(BLDDIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BLDDIR)/fs/%.o: $(SRCDIR)/fs/%.c | $(BLDDIR)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(KERNEL_ELF): $(ISR_OBJ) $(C_OBJS) $(LINKER_SRC)
	$(LD) $(LDFLAGS) -T $(LINKER_SRC) $(ISR_OBJ) $(C_OBJS) -o $@

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
	mkdir -p $(BLDDIR) $(BLDDIR)/kernel $(BLDDIR)/drivers $(BLDDIR)/shell $(BLDDIR)/fs $(BLDDIR)/lib

run: $(IMG)
	qemu-system-i386 -fda $< -hda $(BLDDIR)/disk.img -m 32

run-debug: $(IMG)
	qemu-system-i386 -fda $< -hda $(BLDDIR)/disk.img -monitor stdio -d cpu_reset,int -m 32

clean:
	rm -rf $(BLDDIR)
