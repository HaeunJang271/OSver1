MyOS - tiny educational OS

Source layout:
  src/boot/      Stage-1 bootloader (NASM, 512 bytes)
  src/kernel/    Kernel entry + main
  src/cpu/       GDT, IDT, ISRs, PIC
  src/drivers/   VGA text, PS/2 keyboard, ATA PIO
  src/mem/       Physical memory manager + paging
  src/fs/        FAT32 read-only driver
  src/shell/     Minimal interactive shell

Type 'help' to see all commands.
