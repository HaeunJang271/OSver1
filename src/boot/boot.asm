; boot.asm — Stage 1 Bootloader (512 bytes, loaded at 0x7C00 by BIOS)
;
; Responsibilities:
;   1. Real mode setup
;   2. Load kernel from disk via BIOS LBA extension (int 13h, ah=42h)
;   3. Set up minimal GDT
;   4. Switch CPU to 32-bit protected mode
;   5. Jump to kernel at physical 0x10000

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

    mov [boot_drive], dl        ; BIOS passes boot drive number in DL

    mov si, msg_booting
    call print16

; ─── Load Kernel from Disk ───────────────────────────────────────────────────
; Read 50 sectors (25 KB) starting at LBA 1 (sector after MBR)
; into physical address 0x10000 (segment 0x1000, offset 0x0000)
    mov ah, 0x42
    mov dl, [boot_drive]
    mov si, disk_packet
    int 0x13
    jc .disk_error
    jmp .load_ok

.disk_error:
    mov si, msg_disk_err
    call print16
    jmp $

.load_ok:
    mov si, msg_loaded
    call print16

; ─── Load GDT and Enter Protected Mode ───────────────────────────────────────
    cli
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 0x01
    mov cr0, eax

    jmp CODE_SEG:pm_entry       ; Far jump: flushes pipeline, loads CS

; ─── 16-bit Helpers ──────────────────────────────────────────────────────────
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

; BIOS Disk Address Packet (DAP) for int 13h ah=42h
disk_packet:
    db 0x10             ; Packet size (16 bytes)
    db 0x00             ; Reserved
    dw 50               ; Sectors to read
    dw 0x0000           ; Buffer offset
    dw 0x1000           ; Buffer segment → physical 0x10000
    dd 0x00000001       ; LBA start (sector index 1 = second sector on disk)
    dd 0x00000000       ; LBA high 32 bits

; ─── Global Descriptor Table ─────────────────────────────────────────────────
align 8
gdt_start:
    ; Null descriptor (required by x86 spec)
    dq 0

    ; Code segment: base=0, limit=4 GB, ring 0, exec/read
gdt_code:
    dw 0xFFFF           ; Limit[0:15]
    dw 0x0000           ; Base[0:15]
    db 0x00             ; Base[16:23]
    db 10011010b        ; P=1, DPL=0, S=1, Type=1010
    db 11001111b        ; G=1, D/B=1, Limit[16:19]=0xF
    db 0x00             ; Base[24:31]

    ; Data segment: base=0, limit=4 GB, ring 0, read/write
gdt_data:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b        ; P=1, DPL=0, S=1, Type=0010
    db 11001111b
    db 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG equ gdt_code - gdt_start   ; = 0x08
DATA_SEG equ gdt_data - gdt_start   ; = 0x10

; ─── Protected Mode Entry ────────────────────────────────────────────────────
[bits 32]
pm_entry:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9FC00    ; Stack below BIOS data area (~640 KB mark)

    jmp 0x10000         ; Jump to kernel entry point

; ─── Boot Signature ──────────────────────────────────────────────────────────
times 510 - ($ - $$) db 0
dw 0xAA55
