/* userland.ld — user-mode 프로그램 linker script
 *
 * ELF 가 가상 주소 0x40000000 부터 시작하도록 강제한다.
 * MyOS 의 ELF 로더가 vmm_map 으로 이 영역을 새 user 페이지에 매핑한다.
 * 4 KB 정렬을 보장 — load_segment 가 페이지 단위로 매핑하기 편하도록.
 */

ENTRY(_start)

SECTIONS {
    . = 0x40000000;

    .text ALIGN(4096) : {
        *(.text)
        *(.text.*)
    }

    .rodata ALIGN(4096) : {
        *(.rodata)
        *(.rodata.*)
    }

    .data ALIGN(4096) : {
        *(.data)
    }

    .bss ALIGN(4096) : {
        *(COMMON)
        *(.bss)
    }
}
