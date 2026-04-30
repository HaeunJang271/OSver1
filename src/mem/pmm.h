#pragma once
#include "../include/types.h"

#define PAGE_SIZE 4096

void     pmm_init(void);
void    *pmm_alloc(void);        /* 빈 페이지 하나 할당 → 물리 주소 반환, 없으면 0 */
void     pmm_free(void *addr);   /* 페이지 반납 */
uint32_t pmm_free_pages(void);
uint32_t pmm_total_pages(void);
