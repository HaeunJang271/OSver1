#include "elf.h"
#include "../mem/pmm.h"
#include "../mem/paging.h"
#include "../mem/kheap.h"
#include "../drivers/screen.h"

/* ── 작은 헬퍼 ──────────────────────────────────────────────────────────── */

static void *uimemset(void *dst, int v, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (uint32_t i = 0; i < n; i++) d[i] = (uint8_t)v;
    return dst;
}

static void *uimemcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

static int validate_ehdr(const elf32_ehdr_t *eh, uint32_t file_size) {
    if (file_size < sizeof(elf32_ehdr_t)) return 0;
    if (eh->e_ident[0] != ELF_MAGIC0 ||
        eh->e_ident[1] != ELF_MAGIC1 ||
        eh->e_ident[2] != ELF_MAGIC2 ||
        eh->e_ident[3] != ELF_MAGIC3) return 0;
    if (eh->e_ident[4] != ELFCLASS32)  return 0;
    if (eh->e_ident[5] != ELFDATA2LSB) return 0;
    if (eh->e_machine  != EM_386)      return 0;
    if (eh->e_type     != ET_EXEC)     return 0;
    if (eh->e_phentsize != sizeof(elf32_phdr_t)) return 0;
    if (eh->e_phoff + eh->e_phnum * sizeof(elf32_phdr_t) > file_size) return 0;
    return 1;
}

/* ── elf_unload — 매핑/페이지 회수 ──────────────────────────────────────── */

void elf_unload(user_image_t *image) {
    if (!image) return;
    for (int i = 0; i < image->seg_count; i++) {
        uint32_t v = image->segs[i].vaddr;
        for (uint32_t p = 0; p < image->segs[i].pages; p++) {
            uint32_t va = v + p * 0x1000u;
            uint32_t pa = vmm_get_phys(va);
            vmm_unmap(va);
            if (pa != 0xFFFFFFFFu) pmm_free((void *)(pa & 0xFFFFF000u));
        }
    }
    kfree(image);
}

/* ── 한 LOAD 세그먼트 적재 ──────────────────────────────────────────────
   - phdr 의 [p_vaddr, p_vaddr+p_memsz) 영역을 페이지 단위로 매핑한다.
   - 첫 페이지는 p_vaddr 의 하위 12비트 만큼 offset 이 있을 수 있어
     세심하게 처리한다.
   - p_filesz 만큼 file 에서 복사하고 그 뒤(p_memsz - p_filesz)는 0 으로
     채운다 (.bss). */
static int load_segment(const uint8_t *file, const elf32_phdr_t *ph,
                        user_image_t *img) {
    if (img->seg_count >= MAX_USER_SEGMENTS) return -1;

    uint32_t vstart = ph->p_vaddr & ~0xFFFu;
    uint32_t vend   = (ph->p_vaddr + ph->p_memsz + 0xFFFu) & ~0xFFFu;
    if (vend <= vstart) return -1;

    uint32_t pages  = (vend - vstart) / 0x1000u;
    user_seg_t *seg = &img->segs[img->seg_count++];
    seg->vaddr = vstart;
    seg->pages = 0;

    /* 페이지 하나씩 alloc + map + zero/copy */
    for (uint32_t i = 0; i < pages; i++) {
        uint32_t v   = vstart + i * 0x1000u;
        void    *phy = pmm_alloc();
        if (!phy) return -1;

        uint32_t flags = PAGE_RW | PAGE_USER;        /* (X 비트는 32-bit 페이징에 없음) */
        if (vmm_map(v, (uint32_t)phy, flags) < 0) {
            pmm_free(phy);
            return -1;
        }
        seg->pages++;

        /* phys 가 첫 4MB 안이라면 식별 매핑이라 phys 주소로 직접 쓰기 가능.
           그 가정 하에 페이지를 0 으로 초기화 후 file 데이터 복사. */
        uint8_t *page = (uint8_t *)phy;
        uimemset(page, 0, 0x1000u);

        /* 이 페이지가 file 영역과 겹치는 부분만 복사 */
        uint32_t page_v_start = v;
        uint32_t page_v_end   = v + 0x1000u;
        uint32_t file_v_start = ph->p_vaddr;
        uint32_t file_v_end   = ph->p_vaddr + ph->p_filesz;

        /* 겹치는 가상범위 [lo, hi) */
        uint32_t lo = page_v_start > file_v_start ? page_v_start : file_v_start;
        uint32_t hi = page_v_end   < file_v_end   ? page_v_end   : file_v_end;
        if (lo < hi) {
            uint32_t copy_len   = hi - lo;
            uint32_t file_off   = ph->p_offset + (lo - ph->p_vaddr);
            uint32_t page_off   = lo - page_v_start;
            uimemcpy(page + page_off, file + file_off, copy_len);
        }
    }
    return 0;
}

/* ── elf_load — 메인 진입 ─────────────────────────────────────────────── */

int elf_load(const uint8_t *file_data, uint32_t file_size,
             user_image_t **out_image) {
    if (!file_data || !out_image) return -1;
    const elf32_ehdr_t *eh = (const elf32_ehdr_t *)file_data;
    if (!validate_ehdr(eh, file_size)) return -2;

    user_image_t *img = (user_image_t *)kmalloc(sizeof(user_image_t));
    if (!img) return -3;
    img->seg_count = 0;
    img->entry     = eh->e_entry;

    const elf32_phdr_t *ph =
        (const elf32_phdr_t *)(file_data + eh->e_phoff);

    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD)         continue;
        if (ph[i].p_memsz == 0)               continue;
        if (ph[i].p_offset + ph[i].p_filesz > file_size) {
            elf_unload(img);
            return -4;
        }
        if (load_segment(file_data, &ph[i], img) < 0) {
            elf_unload(img);
            return -5;
        }
    }

    *out_image = img;
    return 0;
}
