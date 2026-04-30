; kernel_entry.asm — 커널 진입점 (0x10000)
; kmain() 호출 전에 BSS 섹션을 0으로 초기화한다.
; (BSS는 ELF에 내용이 없으므로 부트로더가 로드하지 않아 쓰레기값이 들어있음)

[bits 32]
[global _start]
[extern kmain]
[extern bss_start]
[extern bss_end]

_start:
    ; ── BSS 제로화 ──────────────────────────────────────────────────────────
    mov edi, bss_start
    mov ecx, bss_end
    sub ecx, edi            ; byte count
    xor eax, eax
    rep stosb               ; fill with 0

    call kmain

    cli
.halt:
    hlt
    jmp .halt
