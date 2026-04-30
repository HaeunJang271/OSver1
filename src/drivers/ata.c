#include "ata.h"
#include "../include/io.h"

/* ── ATA primary bus (IDE 0) PIO 레지스터 ─────────────────────────────────── */
#define ATA_IO_BASE       0x1F0
#define ATA_CTRL_BASE     0x3F6

#define ATA_REG_DATA       (ATA_IO_BASE + 0)
#define ATA_REG_FEATURES   (ATA_IO_BASE + 1)
#define ATA_REG_SECCOUNT   (ATA_IO_BASE + 2)
#define ATA_REG_LBA0       (ATA_IO_BASE + 3)
#define ATA_REG_LBA1       (ATA_IO_BASE + 4)
#define ATA_REG_LBA2       (ATA_IO_BASE + 5)
#define ATA_REG_HDDEVSEL   (ATA_IO_BASE + 6)
#define ATA_REG_STATUS     (ATA_IO_BASE + 7)
#define ATA_REG_COMMAND    (ATA_IO_BASE + 7)

/* status flags */
#define ATA_SR_BSY  0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

#define ATA_CMD_READ_PIO 0x20

/* 400ns 안정화: alt-status 4번 읽기 (Intel 권장) */
static void ata_io_wait(void) {
    for (int i = 0; i < 4; i++) (void)inb(ATA_CTRL_BASE);
}

/* BSY가 풀리고 DRQ가 올라올 때까지 대기. ERR가 뜨면 실패. */
static int ata_poll_data_ready(void) {
    ata_io_wait();
    while (1) {
        uint8_t s = inb(ATA_REG_STATUS);
        if (s & ATA_SR_ERR)        return -1;
        if (!(s & ATA_SR_BSY) && (s & ATA_SR_DRQ)) return 0;
    }
}

void ata_init(void) {
    /* 인터럽트 비활성화 (폴링 PIO 만 사용)
       Device Control 레지스터의 nIEN(비트 1) 설정 */
    outb(ATA_CTRL_BASE, 0x02);
}

int ata_read(uint8_t drive, uint32_t lba, uint8_t count, void *buf) {
    if (count == 0) return 0;
    if (lba & 0xF0000000) return -1;            /* LBA28 한계 (256 GB) */

    /* 1) drive select + LBA 상위 4비트 */
    outb(ATA_REG_HDDEVSEL,
         0xE0 | ((drive & 1) << 4) | ((lba >> 24) & 0x0F));
    ata_io_wait();

    /* 2) 셀렉트 직후 BSY 풀림 대기 */
    while (inb(ATA_REG_STATUS) & ATA_SR_BSY) { /* spin */ }

    /* 3) sector count + LBA[23:0] */
    outb(ATA_REG_FEATURES, 0);
    outb(ATA_REG_SECCOUNT, count);
    outb(ATA_REG_LBA0,  lba        & 0xFF);
    outb(ATA_REG_LBA1, (lba >> 8)  & 0xFF);
    outb(ATA_REG_LBA2, (lba >> 16) & 0xFF);

    /* 4) READ SECTORS */
    outb(ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    /* 5) 섹터마다 DRQ 대기 후 256 워드(=512B) 읽기 */
    uint8_t *p = (uint8_t *)buf;
    for (uint32_t s = 0; s < count; s++) {
        if (ata_poll_data_ready() < 0) return -1;
        insw(ATA_REG_DATA, p, 256);
        p += 512;
    }
    return 0;
}
