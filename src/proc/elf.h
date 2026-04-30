#pragma once
#include "../include/types.h"

/* ── ELF32 구조체 (System V ABI) ──────────────────────────────────────── */

#define EI_NIDENT 16

#define ELF_MAGIC0 0x7F
#define ELF_MAGIC1 'E'
#define ELF_MAGIC2 'L'
#define ELF_MAGIC3 'F'

#define ELFCLASS32  1
#define ELFDATA2LSB 1
#define EM_386      3
#define ET_EXEC     2

#define PT_LOAD     1

#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

typedef struct {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) elf32_ehdr_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed)) elf32_phdr_t;

/* ── 로드 결과 — 적재된 LOAD 세그먼트들의 매핑 정보 ─────────────────────
   task 가 종료되면 이 정보로 vmm_unmap + pmm_free 해서 회수한다. */

#define MAX_USER_SEGMENTS 8

typedef struct {
    uint32_t vaddr;     /* 4KB-aligned */
    uint32_t pages;     /* 매핑된 페이지 수 */
} user_seg_t;

typedef struct {
    int        seg_count;
    user_seg_t segs[MAX_USER_SEGMENTS];
    uint32_t   entry;
} user_image_t;

/* ── 공개 API ─────────────────────────────────────────────────────────── */

/* file_data: 파일 전체가 들어있는 버퍼.
   on success: 0, *out_image 채움 (kmalloc).
   on failure: 음수, 부분 매핑된 페이지는 자동 회수. */
int  elf_load(const uint8_t *file_data, uint32_t file_size,
              user_image_t **out_image);

/* image 가 가리키는 모든 매핑을 풀고 PMM 페이지 반납 후 image 자체도 free.
   task_exit/reap 에서 호출됨. */
void elf_unload(user_image_t *image);
