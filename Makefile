# ─── Tools ────────────────────────────────────────────────────────────────────
# Install: sudo apt install gcc-i686-linux-gnu nasm qemu-system-x86
CC      := i686-linux-gnu-gcc
LD      := i686-linux-gnu-ld
OBJCOPY := i686-linux-gnu-objcopy
ASM     := nasm
QEMU    := qemu-system-i386

# ─── Flags ────────────────────────────────────────────────────────────────────
CFLAGS  := -ffreestanding -fno-builtin -fno-stack-protector -fno-pie \
           -nostdlib -Wall -Wextra -m32 -O2 -I src
LDFLAGS := -T linker.ld -melf_i386 --no-pie

# ─── Source files ─────────────────────────────────────────────────────────────
BUILD := build

C_SRCS := src/kernel/kernel.c      \
          src/drivers/screen.c     \
          src/drivers/keyboard.c   \
          src/cpu/gdt.c            \
          src/cpu/idt.c            \
          src/cpu/isr.c            \
          src/cpu/pic.c            \
          src/mem/pmm.c            \
          src/shell/shell.c

# Extra ASM objects (beyond kernel_entry.o which is handled separately)
CPU_ASM_SRCS := src/cpu/gdt_flush.asm \
                src/cpu/idt_flush.asm  \
                src/cpu/isr_stubs.asm

C_OBJS      := $(patsubst src/%.c,   $(BUILD)/%.o, $(C_SRCS))
CPU_ASM_OBJS := $(patsubst src/%.asm, $(BUILD)/%.o, $(CPU_ASM_SRCS))
KENTRY_OBJ  := $(BUILD)/kernel_entry.o

# ─── Build dirs ───────────────────────────────────────────────────────────────
BUILD_DIRS := $(BUILD)/kernel $(BUILD)/drivers $(BUILD)/cpu $(BUILD)/mem $(BUILD)/shell

.PHONY: all run run-vnc debug clean

all: $(BUILD)/os.img

$(BUILD_DIRS):
	mkdir -p $@

# ─── Bootloader ───────────────────────────────────────────────────────────────
$(BUILD)/boot.bin: src/boot/boot.asm | $(BUILD_DIRS)
	$(ASM) -f bin -o $@ $<

# ─── Kernel entry (must link first → lands at 0x10000) ───────────────────────
$(KENTRY_OBJ): src/kernel/kernel_entry.asm | $(BUILD_DIRS)
	$(ASM) -f elf32 -o $@ $<

# ─── CPU assembly stubs ───────────────────────────────────────────────────────
$(BUILD)/cpu/%.o: src/cpu/%.asm | $(BUILD_DIRS)
	$(ASM) -f elf32 -o $@ $<

# ─── C sources ────────────────────────────────────────────────────────────────
$(BUILD)/kernel/%.o: src/kernel/%.c | $(BUILD_DIRS)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/drivers/%.o: src/drivers/%.c | $(BUILD_DIRS)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/cpu/%.o: src/cpu/%.c | $(BUILD_DIRS)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/mem/%.o: src/mem/%.c | $(BUILD_DIRS)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/shell/%.o: src/shell/%.c | $(BUILD_DIRS)
	$(CC) $(CFLAGS) -c -o $@ $<

# ─── Link ─────────────────────────────────────────────────────────────────────
$(BUILD)/kernel.elf: $(KENTRY_OBJ) $(CPU_ASM_OBJS) $(C_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@

# ─── Disk image (1.44 MB hard-disk style) ─────────────────────────────────────
$(BUILD)/os.img: $(BUILD)/boot.bin $(BUILD)/kernel.bin
	dd if=/dev/zero           of=$@ bs=512 count=2880 2>/dev/null
	dd if=$(BUILD)/boot.bin   of=$@ conv=notrunc         2>/dev/null
	dd if=$(BUILD)/kernel.bin of=$@ seek=1 conv=notrunc  2>/dev/null
	@echo "Built: $@"

# ─── Run ──────────────────────────────────────────────────────────────────────
QEMU_DRIVE := -drive file=$(BUILD)/os.img,format=raw,if=ide

run: $(BUILD)/os.img
	$(QEMU) $(QEMU_DRIVE) -display sdl -monitor stdio

# VNC fallback — connect with any VNC viewer to 127.0.0.1:5900
run-vnc: $(BUILD)/os.img
	@echo "VNC 뷰어로 127.0.0.1:5900 에 접속하세요"
	$(QEMU) $(QEMU_DRIVE) -display vnc=127.0.0.1:0 -monitor stdio

debug: $(BUILD)/os.img
	@echo "다른 터미널에서: gdb -ex 'target remote :1234'"
	$(QEMU) $(QEMU_DRIVE) -display sdl -s -S -monitor stdio

# ─── Clean ────────────────────────────────────────────────────────────────────
clean:
	rm -rf $(BUILD)
