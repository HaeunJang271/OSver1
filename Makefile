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
          src/drivers/serial.c     \
          src/drivers/timer.c      \
          src/drivers/keyboard.c   \
          src/drivers/ata.c        \
          src/cpu/gdt.c            \
          src/cpu/idt.c            \
          src/cpu/isr.c            \
          src/cpu/pic.c            \
          src/cpu/tss.c            \
          src/mem/pmm.c            \
          src/mem/paging.c         \
          src/mem/kheap.c          \
          src/fs/fat32.c           \
          src/proc/task.c          \
          src/proc/syscall.c       \
          src/proc/user_demo.c     \
          src/proc/elf.c           \
          src/shell/shell.c

# Extra ASM objects (beyond kernel_entry.o which is handled separately)
CPU_ASM_SRCS := src/cpu/gdt_flush.asm \
                src/cpu/idt_flush.asm  \
                src/cpu/isr_stubs.asm \
                src/proc/switch.asm

C_OBJS      := $(patsubst src/%.c,   $(BUILD)/%.o, $(C_SRCS))
CPU_ASM_OBJS := $(patsubst src/%.asm, $(BUILD)/%.o, $(CPU_ASM_SRCS))
KENTRY_OBJ  := $(BUILD)/kernel_entry.o

# ─── Build dirs ───────────────────────────────────────────────────────────────
BUILD_DIRS := $(BUILD)/kernel $(BUILD)/drivers $(BUILD)/cpu $(BUILD)/mem \
              $(BUILD)/fs $(BUILD)/proc $(BUILD)/shell $(BUILD)/userland

# ─── Userland (Phase 12: 별도 ELF 사용자 프로그램) ─────────────────────────
USER_CFLAGS  := -ffreestanding -fno-builtin -fno-stack-protector -fno-pie \
                -nostdlib -Wall -Wextra -m32 -O2 -I src
USER_LDFLAGS := -T src/userland/userland.ld -melf_i386 --no-pie

USER_PROGS := $(BUILD)/userland/hello.elf

.PHONY: all run run-vnc debug clean data userland

all: $(BUILD)/os.img $(BUILD)/data.img

userland: $(USER_PROGS)

$(BUILD_DIRS):
	mkdir -p $@

# ─── Bootloader ───────────────────────────────────────────────────────────────
$(BUILD)/boot.bin: src/boot/boot.asm | $(BUILD_DIRS)
	$(ASM) -f bin -o $@ $<

# ─── Kernel entry (must link first → lands at 0x10000) ───────────────────────
$(KENTRY_OBJ): src/kernel/kernel_entry.asm | $(BUILD_DIRS)
	$(ASM) -f elf32 -o $@ $<

# ─── CPU / proc assembly stubs ────────────────────────────────────────────────
$(BUILD)/cpu/%.o: src/cpu/%.asm | $(BUILD_DIRS)
	$(ASM) -f elf32 -o $@ $<

$(BUILD)/proc/%.o: src/proc/%.asm | $(BUILD_DIRS)
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

$(BUILD)/fs/%.o: src/fs/%.c | $(BUILD_DIRS)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/proc/%.o: src/proc/%.c | $(BUILD_DIRS)
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

# ─── Userland ELF 빌드 룰 ──────────────────────────────────────────────────
# crt0.S + 각 프로그램의 main.c → 정적 링크된 ELF (가상 0x40000000 시작)

$(BUILD)/userland/crt0.o: src/userland/crt0.S | $(BUILD_DIRS)
	$(CC) $(USER_CFLAGS) -c -o $@ $<

$(BUILD)/userland/%.o: src/userland/%.c | $(BUILD_DIRS)
	$(CC) $(USER_CFLAGS) -c -o $@ $<

$(BUILD)/userland/%.elf: $(BUILD)/userland/crt0.o $(BUILD)/userland/%.o \
                         src/userland/userland.ld | $(BUILD_DIRS)
	$(LD) $(USER_LDFLAGS) -o $@ $(BUILD)/userland/crt0.o $(BUILD)/userland/$*.o
	@echo "Built userland: $@"

# ─── FAT32 데이터 디스크 (64 MB, secondary IDE) ───────────────────────────────
# 필요 도구: dosfstools(mkfs.fat) + mtools(mcopy)
#   sudo apt install dosfstools mtools
DATA_FILES := $(wildcard disk_files/*)

$(BUILD)/data.img: $(DATA_FILES) $(USER_PROGS) | $(BUILD_DIRS)
	dd if=/dev/zero of=$@ bs=1M count=64 status=none
	mkfs.fat -F 32 -n MYOSDATA $@ >/dev/null
	@for f in $(DATA_FILES); do mcopy -i $@ "$$f" ::; done
	@for f in $(USER_PROGS); do mcopy -i $@ "$$f" ::; done
	@echo "Built: $@  (FAT32 64MB, $(words $(DATA_FILES)) data + $(words $(USER_PROGS)) elf)"

data: $(BUILD)/data.img

# ─── Run ──────────────────────────────────────────────────────────────────────
QEMU_DRIVE := -drive file=$(BUILD)/os.img,format=raw,if=ide \
              -drive file=$(BUILD)/data.img,format=raw,if=ide

# `run` — SDL 창에 화면, 호스트 터미널엔 시리얼 콘솔(부팅 로그/uptime/디버그) 미러링
run: $(BUILD)/os.img $(BUILD)/data.img
	$(QEMU) $(QEMU_DRIVE) -display sdl -serial stdio

# VNC fallback — VNC viewer 로 127.0.0.1:5900 접속.
# stdio 는 monitor 가 잡고 있으므로 시리얼은 파일로 떨어뜨린다.
run-vnc: $(BUILD)/os.img $(BUILD)/data.img
	@echo "VNC 뷰어로 127.0.0.1:5900 에 접속하세요"
	@echo "시리얼 로그: $(BUILD)/serial.log"
	$(QEMU) $(QEMU_DRIVE) -display vnc=127.0.0.1:0 -monitor stdio \
	    -serial file:$(BUILD)/serial.log

debug: $(BUILD)/os.img $(BUILD)/data.img
	@echo "다른 터미널에서: gdb -ex 'target remote :1234'"
	$(QEMU) $(QEMU_DRIVE) -display sdl -s -S -monitor stdio \
	    -serial file:$(BUILD)/serial.log

# ─── Clean ────────────────────────────────────────────────────────────────────
clean:
	rm -rf $(BUILD)
