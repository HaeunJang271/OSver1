/* crt0.S — userland C 런타임 진입점
 *
 * ELF 의 entry point (e_entry) 가 _start 를 가리킨다.
 * _start 는 main() 호출 후 SYS_EXIT 시스템콜로 자동 종료한다.
 * main 의 반환값은 exit code 로 전달.
 */

.intel_syntax noprefix
.text
.global _start
_start:
    call    main
    /* eax = main() 의 반환값. SYS_EXIT(eax) 호출. */
    mov     ebx, eax    /* arg = exit code */
    mov     eax, 0      /* SYS_EXIT */
    int     0x80
.hang:
    jmp     .hang
