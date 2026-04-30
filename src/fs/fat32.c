#include "fat32.h"
#include "../drivers/ata.h"

/* ── BPB (FAT32 Boot Sector) 핵심 필드만 ──────────────────────────────────── */
typedef struct {
    uint8_t  jump[3];
    uint8_t  oem[8];
    uint16_t bytes_per_sector;     /* offset 0x0B */
    uint8_t  sectors_per_cluster;  /* offset 0x0D */
    uint16_t reserved_sectors;     /* offset 0x0E */
    uint8_t  num_fats;             /* offset 0x10 */
    uint16_t root_entries_16;      /* FAT32=0    */
    uint16_t total_sectors_16;
    uint8_t  media;
    uint16_t fat_size_16;          /* FAT32=0    */
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    /* FAT32 확장 BPB (offset 0x24~) */
    uint32_t fat_size_32;          /* offset 0x24 */
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;         /* offset 0x2C */
} __attribute__((packed)) bpb32_t;

/* 한 클러스터의 최대 크기 — 64KB까지 허용 (sectors_per_cluster<=128) */
#define FAT32_CLUSTER_BUF_MAX (128 * 512)

/* ── 마운트 상태 ──────────────────────────────────────────────────────────── */
static struct {
    uint8_t  drive;
    uint8_t  sectors_per_cluster;
    uint16_t bytes_per_sector;
    uint32_t fat_start_lba;
    uint32_t data_start_lba;
    uint32_t fat_size;
    uint32_t root_cluster;
    int      mounted;
} fs;

/* 한 클러스터를 통째로 담을 버퍼 (정적, 첫 4MB BSS 안에 위치) */
static uint8_t cluster_buf[FAT32_CLUSTER_BUF_MAX];
/* FAT 엔트리 조회용 1섹터 캐시 */
static uint8_t fat_sector_buf[512];
static uint32_t fat_sector_cached_lba = 0xFFFFFFFFu;

/* ── 작은 문자열 유틸 ─────────────────────────────────────────────────────── */
static char to_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static int str_eq_ci(const char *a, const char *b) {
    while (*a && *b) {
        if (to_upper(*a) != to_upper(*b)) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

/* "FOO     TXT" → "FOO.TXT" (대문자, 끝의 공백 제거, 확장자 없으면 점 생략) */
static void format_short_name(const uint8_t raw[11], char out[13]) {
    int n = 0;
    for (int i = 0; i < 8 && raw[i] != ' '; i++) out[n++] = (char)raw[i];
    if (raw[8] != ' ') {
        out[n++] = '.';
        for (int i = 8; i < 11 && raw[i] != ' '; i++) out[n++] = (char)raw[i];
    }
    out[n] = '\0';
}

/* ── 클러스터 ↔ LBA 변환 / FAT 체인 추적 ──────────────────────────────────── */

static uint32_t cluster_to_lba(uint32_t cluster) {
    return fs.data_start_lba + (cluster - 2) * fs.sectors_per_cluster;
}

static int is_eoc(uint32_t cluster) {
    /* EOC: 0x0FFFFFF8 이상 (하위 28비트 기준) */
    return (cluster & 0x0FFFFFFFu) >= 0x0FFFFFF8u;
}

static uint32_t next_cluster(uint32_t cluster) {
    uint32_t fat_byte_offset = cluster * 4;
    uint32_t fat_lba = fs.fat_start_lba + fat_byte_offset / 512;
    uint32_t in_sector_off = fat_byte_offset % 512;

    if (fat_lba != fat_sector_cached_lba) {
        if (ata_read(fs.drive, fat_lba, 1, fat_sector_buf) < 0)
            return 0x0FFFFFFFu;
        fat_sector_cached_lba = fat_lba;
    }
    uint32_t v = *(uint32_t *)(fat_sector_buf + in_sector_off);
    return v & 0x0FFFFFFFu;
}

static int read_cluster(uint32_t cluster) {
    uint32_t lba = cluster_to_lba(cluster);
    return ata_read(fs.drive, lba, fs.sectors_per_cluster, cluster_buf);
}

/* ── 공개 API ─────────────────────────────────────────────────────────────── */

int fat32_mount(uint8_t drive) {
    uint8_t boot[512];
    if (ata_read(drive, 0, 1, boot) < 0) return -1;

    bpb32_t *bpb = (bpb32_t *)boot;
    if (bpb->bytes_per_sector != 512) return -1;
    if (bpb->sectors_per_cluster == 0)        return -1;
    if (bpb->fat_size_32 == 0)                return -1;     /* FAT32 아님 */
    if ((uint32_t)bpb->sectors_per_cluster * 512 > FAT32_CLUSTER_BUF_MAX)
        return -1;                                            /* 너무 큰 cluster */

    fs.drive               = drive;
    fs.bytes_per_sector    = bpb->bytes_per_sector;
    fs.sectors_per_cluster = bpb->sectors_per_cluster;
    fs.fat_start_lba       = bpb->reserved_sectors;
    fs.fat_size            = bpb->fat_size_32;
    fs.data_start_lba      = fs.fat_start_lba + bpb->num_fats * fs.fat_size;
    fs.root_cluster        = bpb->root_cluster;
    fs.mounted             = 1;
    fat_sector_cached_lba  = 0xFFFFFFFFu;
    return 0;
}

int fat32_is_mounted(void)        { return fs.mounted; }
uint32_t fat32_root_cluster(void) { return fs.root_cluster; }

void fat32_listdir(uint32_t start_cluster, fat32_visitor_t visit, void *ctx) {
    if (!fs.mounted) return;

    uint32_t cluster = start_cluster;
    while (!is_eoc(cluster) && cluster >= 2) {
        if (read_cluster(cluster) < 0) return;

        uint32_t bytes = (uint32_t)fs.sectors_per_cluster * 512;
        fat32_dirent_t *e = (fat32_dirent_t *)cluster_buf;
        uint32_t n = bytes / sizeof(fat32_dirent_t);

        for (uint32_t i = 0; i < n; i++, e++) {
            uint8_t first = e->name[0];
            if (first == 0x00) return;            /* 디렉토리 끝 */
            if (first == 0xE5) continue;          /* 삭제됨        */
            if (e->attr == FAT_ATTR_LFN) continue;/* LFN 보조 엔트리 */
            if (e->attr & FAT_ATTR_VOLUME_ID) continue;
            visit(e, ctx);
        }
        cluster = next_cluster(cluster);
    }
}

/* ── 루트에서 이름 검색 ──────────────────────────────────────────────────── */
typedef struct {
    const char *target;
    int found;
    fat32_dirent_t out;
} find_ctx_t;

static void find_visitor(const fat32_dirent_t *e, void *vctx) {
    find_ctx_t *ctx = (find_ctx_t *)vctx;
    if (ctx->found) return;
    char name[13];
    format_short_name(e->name, name);
    if (str_eq_ci(name, ctx->target)) {
        ctx->out   = *e;
        ctx->found = 1;
    }
}

int fat32_find_in_root(const char *name83, fat32_dirent_t *out) {
    if (!fs.mounted) return -1;
    find_ctx_t ctx = { name83, 0, {0} };
    fat32_listdir(fs.root_cluster, find_visitor, &ctx);
    if (!ctx.found) return -1;
    *out = ctx.out;
    return 0;
}

/* ── 파일 읽기 ───────────────────────────────────────────────────────────── */
int fat32_read_file(const fat32_dirent_t *e, void *buf, uint32_t max) {
    if (!fs.mounted) return -1;

    uint32_t cluster = ((uint32_t)e->cluster_hi << 16) | e->cluster_lo;
    uint32_t size    = e->size;
    if (size > max) size = max;

    uint8_t *dst    = (uint8_t *)buf;
    uint32_t copied = 0;

    while (!is_eoc(cluster) && cluster >= 2 && copied < size) {
        if (read_cluster(cluster) < 0) return -1;

        uint32_t bytes  = (uint32_t)fs.sectors_per_cluster * 512;
        uint32_t remain = size - copied;
        uint32_t n      = (remain < bytes) ? remain : bytes;

        for (uint32_t i = 0; i < n; i++) dst[copied + i] = cluster_buf[i];
        copied += n;
        cluster = next_cluster(cluster);
    }
    return (int)copied;
}
