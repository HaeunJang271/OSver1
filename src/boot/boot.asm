; boot.asm — Stage 1 Bootloader (512 bytes)
;
; 1. Real mode setup
; 2. BIOS e820 memory detection → store at 0x500
; 3. Load kernel from disk (LBA, int 13h ext)
; 4. Set up GDT, switch to 32-bit protected mode
; 5. Jump to kernel at 0x10000
;
; Memory map output layout (for PMM):
;   0x0500: uint16_t  entry_count
;   0x0502: e820_entry[N]  (each 20 bytes, max 20 entries)

[org 0x7C00]
[bits 16]

; ─── Real Mode Setup ─────────────────────────────────────────────────────────
start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [boot_drive], dl

; ─── BIOS e820 Memory Detection ──────────────────────────────────────────────
; Stores entries at 0x0502, count word at 0x0500.
    xor ax, ax
    mov es, ax                  ; es = 0 for es:di buffer
    mov di, 0x0502
    xor ebx, ebx                ; continuation value (must start at 0)
    xor bp, bp                  ; entry counter

.e820_loop:
    mov eax, 0x0000E820
    mov edx, 0x534D4150         ; 'SMAP' signature
    mov ecx, 20
    int 0x15
    jc  .e820_done              ; carry = error or no more entries
    cmp eax, 0x534D4150
    jne .e820_done
    test ecx, ecx
    jz  .e820_skip
    inc  bp
    add  di, 20
    cmp  bp, 20                 ; cap at 20 entries
    jge  .e820_done
.e820_skip:
    test ebx, ebx               ; ebx = 0 means last entry
    jz   .e820_done
    jmp  .e820_loop
.e820_done:
    mov [0x0500], bp            ; store count

; ─── Load Kernel from Disk ───────────────────────────────────────────────────
    mov si, msg_booting
    call print16

    mov ah, 0x42
    mov dl, [boot_drive]
    mov si, disk_packet
    int 0x13
    jc .disk_error

    mov si, msg_loaded
    call print16

; ─── Enter Protected Mode ────────────────────────────────────────────────────
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or  eax, 0x01
    mov cr0, eax
    jmp CODE_SEG:pm_entry

.disk_error:
    mov si, msg_disk_err
    call print16
    jmp $

; ─── 16-bit Print ────────────────────────────────────────────────────────────
print16:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0e
    int 0x10
    jmp print16
.done:
    ret

; ─── Data ────────────────────────────────────────────────────────────────────
msg_booting  db 'Booting...', 13, 10, 0
msg_loaded   db 'Kernel loaded.', 13, 10, 0
msg_disk_err db 'Disk read failed!', 0
boot_drive   db 0

disk_packet:
    db 0x10, 0x00               ; DAP size, reserved
    dw 100                      ; sectors to read (100 × 512 = 50 KB)
    dw 0x0000                   ; buffer offset
    dw 0x1000                   ; buffer segment → physical 0x10000
    dd 0x00000001               ; LBA sector 1 (right after MBR)
    dd 0x00000000

; ─── GDT ─────────────────────────────────────────────────────────────────────
align 8
gdt_start:
    dq 0                        ; null descriptor
gdt_code:
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00
gdt_data:
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

; ─── Protected Mode Entry ────────────────────────────────────────────────────
[bits 32]
pm_entry:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9FC00
    jmp 0x10000

; ─── Boot Signature ──────────────────────────────────────────────────────────
times 510 - ($ - $$) db 0
dw 0xAA55
