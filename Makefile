# ─── Tools ────────────────────────────────────────────────────────────────────
# i686-linux-gnu-gcc: sudo apt install gcc-i686-linux-gnu nasm qemu-system-x86
CC      := i686-linux-gnu-gcc
LD      := i686-linux-gnu-ld
OBJCOPY := i686-linux-gnu-objcopy
ASM     := nasm
QEMU    := qemu-system-i386

# ─── Flags ────────────────────────────────────────────────────────────────────
CFLAGS  := -ffreestanding -fno-builtin -fno-stack-protector -fno-pie \
           -nostdlib -Wall -Wextra -m32 -O2 -I src
LDFLAGS := -T linker.ld -melf_i386 --no-pie

# ─── Paths ────────────────────────────────────────────────────────────────────
BUILD := build

BOOT_SRC    := src/boot/boot.asm
KENTRY_SRC  := src/kernel/kernel_entry.asm
C_SRCS      := src/kernel/kernel.c \
               src/drivers/screen.c

C_OBJS      := $(patsubst src/%.c, $(BUILD)/%.o, $(C_SRCS))
KENTRY_OBJ  := $(BUILD)/kernel_entry.o

# ─── Targets ──────────────────────────────────────────────────────────────────
.PHONY: all run debug clean

all: $(BUILD)/os.img

# Create build directory tree
$(BUILD)/kernel $(BUILD)/drivers:
	mkdir -p $@

$(BUILD):
	mkdir -p $(BUILD)/kernel $(BUILD)/drivers

# Bootloader → raw binary
$(BUILD)/boot.bin: $(BOOT_SRC) | $(BUILD)
	$(ASM) -f bin -o $@ $<

# Kernel entry point → ELF object
$(KENTRY_OBJ): $(KENTRY_SRC) | $(BUILD)
	$(ASM) -f elf32 -o $@ $<

# C sources → ELF objects
$(BUILD)/kernel/%.o: src/kernel/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/drivers/%.o: src/drivers/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

# Link kernel (kernel_entry.o MUST be first — places _start at 0x10000)
$(BUILD)/kernel.elf: $(KENTRY_OBJ) $(C_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

# Strip to flat binary
$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@

# Build floppy disk image (1.44 MB = 2880 sectors × 512 bytes)
$(BUILD)/os.img: $(BUILD)/boot.bin $(BUILD)/kernel.bin
	dd if=/dev/zero        of=$@ bs=512 count=2880 2>/dev/null
	dd if=$(BUILD)/boot.bin   of=$@ conv=notrunc         2>/dev/null
	dd if=$(BUILD)/kernel.bin of=$@ seek=1 conv=notrunc  2>/dev/null
	@echo "Image built: $@"

# ─── Run / Debug ──────────────────────────────────────────────────────────────
QEMU_DRIVE := -drive file=$(BUILD)/os.img,format=raw,if=ide

run: $(BUILD)/os.img
	$(QEMU) $(QEMU_DRIVE) -display sdl -monitor stdio

# VNC fallback: connect with any VNC viewer to 127.0.0.1:5900
run-vnc: $(BUILD)/os.img
	@echo "VNC 뷰어로 127.0.0.1:5900 에 접속하세요"
	$(QEMU) $(QEMU_DRIVE) -display vnc=127.0.0.1:0 -monitor stdio

# Launch QEMU in debug mode, then attach GDB
debug: $(BUILD)/os.img
	@echo "Run in another terminal: gdb -ex 'target remote :1234'"
	$(QEMU) $(QEMU_DRIVE) -display sdl -s -S -monitor stdio

# ─── Clean ────────────────────────────────────────────────────────────────────
clean:
	rm -rf $(BUILD)
