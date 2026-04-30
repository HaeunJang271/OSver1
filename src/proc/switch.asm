; switch.asm — 커널 컨텍스트 스위치 + 새 task 진입 트램폴린
;
; cdecl 호출 규약 가정. caller-saved (eax/ecx/edx) 는 컴파일러가 알아서 저장.
; 여기서는 callee-saved (ebx/esi/edi/ebp) 만 push/pop 해서 esp 만 바꾼다.

[bits 32]
section .text

; ── void context_switch(uint32_t **old_esp_ptr, uint32_t *new_esp) ───────
;
; 진입 시 스택:
;   [esp + 0]  ret_addr
;   [esp + 4]  old_esp_ptr   (uint32_t **)
;   [esp + 8]  new_esp       (uint32_t *)
;
; callee-saved 4개 push 후:
;   [esp + 0]  edi
;   [esp + 4]  esi
;   [esp + 8]  ebx
;   [esp + 12] ebp
;   [esp + 16] ret_addr
;   [esp + 20] old_esp_ptr
;   [esp + 24] new_esp
;
global context_switch
context_switch:
    push ebp
    push ebx
    push esi
    push edi

    ; *old_esp_ptr = esp
    mov     eax, [esp + 20]
    mov     [eax], esp

    ; esp = new_esp
    mov     esp, [esp + 24]

    ; 새 task 의 callee-saved 복원 후 ret 으로 점프
    pop     edi
    pop     esi
    pop     ebx
    pop     ebp
    ret


; ── 새 task 진입 트램폴린 ────────────────────────────────────────────────
; task_create() 가 새 task 의 스택을 다음과 같이 셋업한다:
;
;   high addr ───────────────
;       ...                    (task 의 작업 스택)
;       (entry function ptr)   <- pop 하면 eax 에 들어감
;       (task_start_trampoline) <- context_switch 의 ret 이 여기로 점프
;       0  (ebp)
;       0  (ebx)
;       0  (esi)
;       0  (edi)               <- 새 esp
;   low  addr ───────────────
;
; 트램폴린은:
;   1. sti  — 인터럽트가 cli 상태에서 진입했어도 강제 활성
;   2. entry() 호출
;   3. entry 가 반환하면 task_exit() 호출 (반환 안 함)
;
extern task_exit

global task_start_trampoline
task_start_trampoline:
    sti                     ; 새 task 는 항상 IF=1 상태로 시작
    pop     eax             ; entry function pointer
    call    eax             ; entry()
    call    task_exit       ; entry 가 반환하면 자동 종료 (no-return)
.hang:
    hlt
    jmp     .hang


; ── ring 3 진입 트램폴린 ──────────────────────────────────────────────────
; task_create_user() 가 새 task 의 커널 스택을 다음과 같이 셋업한다:
;
;   high addr ─────────────────
;       SS3      (USER_DATA_SEL | 3 = 0x23)
;       ESP3     (user stack top)
;       EFLAGS   (0x202 → IF=1, reserved bit 1)
;       CS3      (USER_CODE_SEL | 3 = 0x1B)
;       EIP      (entry function pointer)
;       ─── iret 5-frame ──────
;       user_iret_trampoline   ← context_switch 의 ret 점프 대상
;       0  (ebp)
;       0  (ebx)
;       0  (esi)
;       0  (edi)               ← 저장된 esp
;   low  addr ─────────────────
;
; 트램폴린은 ds/es/fs/gs 를 user data segment (RPL=3) 로 갈아끼고 iret.
; iret 이 SS3:ESP3 / EFLAGS / CS3:EIP 를 자동 복원해서 ring 3 로 점프한다.

global user_iret_trampoline
user_iret_trampoline:
    mov     ax, 0x23        ; USER_DATA_SEL | RPL 3
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    iret                    ; ring 3 진입 — 절대 반환 안 함
