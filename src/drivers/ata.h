#pragma once
#include "../include/types.h"

#define ATA_DRIVE_MASTER 0
#define ATA_DRIVE_SLAVE  1

void ata_init(void);

/* drive(0=master, 1=slave) 의 lba 섹터부터 count개를 buf 로 읽는다.
   count 는 1~255. 성공 0, 실패 -1. */
int ata_read(uint8_t drive, uint32_t lba, uint8_t count, void *buf);
