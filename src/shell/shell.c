#include "shell.h"
#include "../drivers/screen.h"
#include "../drivers/keyboard.h"
#include "../mem/pmm.h"
#include "../mem/paging.h"
#include "../fs/fat32.h"

#define LINE_MAX  256
#define ARGS_MAX  16

/* ── 문자열 유틸 ─────────────────────────────────────────────────────────── */

static int str_len(const char *s) {
    int n = 0; while (s[n]) n++; return n;
}

static int str_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == '\0' && *b == '\0';
}

/* "0x1A2B" 또는 "1234" → uint32_t */
static uint32_t parse_uint(const char *s) {
    uint32_t v = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        for (; *s; s++) {
            v <<= 4;
            if (*s >= '0' && *s <= '9') v |= (uint32_t)(*s - '0');
            else if (*s >= 'a' && *s <= 'f') v |= (uint32_t)(*s - 'a' + 10);
            else if (*s >= 'A' && *s <= 'F') v |= (uint32_t)(*s - 'A' + 10);
            else break;
        }
    } else {
        for (; *s >= '0' && *s <= '9'; s++)
            v = v * 10 + (uint32_t)(*s - '0');
    }
    return v;
}

/* ── 명령어 파서 ─────────────────────────────────────────────────────────── */
/* line을 공백 기준으로 나눠 argv에 채우고 argc 반환 (line 내용 변경됨) */

static int parse(char *line, char *argv[]) {
    int argc = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        if (argc == ARGS_MAX) break;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    return argc;
}

/* ── 내장 명령어 ─────────────────────────────────────────────────────────── */

typedef struct {
    const char *name;
    const char *desc;
    void (*fn)(int argc, char **argv);
} cmd_t;

static void cmd_help(int, char **);
static void cmd_clear(int, char **);
static void cmd_echo(int, char **);
static void cmd_meminfo(int, char **);
static void cmd_page(int, char **);
static void cmd_vmap(int, char **);
static void cmd_pftest(int, char **);
static void cmd_ls(int, char **);
static void cmd_cat(int, char **);
static void cmd_touch(int, char **);
static void cmd_write(int, char **);
static void cmd_rm(int, char **);
static void cmd_mkdir(int, char **);
static void cmd_rmdir(int, char **);
static void cmd_cd(int, char **);
static void cmd_pwd(int, char **);
static void cmd_version(int, char **);
static void cmd_halt(int, char **);

static const cmd_t cmds[] = {
    { "help",    "show this command list",                       cmd_help    },
    { "clear",   "clear the screen",                             cmd_clear   },
    { "echo",    "print text                echo <text>",        cmd_echo    },
    { "meminfo", "show memory usage",                            cmd_meminfo },
    { "page",    "alloc / free a page      page alloc|free <addr>", cmd_page },
    { "vmap",    "show v->p mapping        vmap <virt>",         cmd_vmap    },
    { "pf-test", "trigger a page fault",                         cmd_pftest  },
    { "ls",      "list files               ls [path]",           cmd_ls      },
    { "cat",     "print a file             cat <path>",          cmd_cat     },
    { "touch",   "create empty file        touch <path>",        cmd_touch   },
    { "write",   "overwrite file content   write <path> <text>", cmd_write   },
    { "rm",      "delete a file            rm <path>",           cmd_rm      },
    { "mkdir",   "create a directory       mkdir <path>",        cmd_mkdir   },
    { "rmdir",   "remove an empty dir      rmdir <path>",        cmd_rmdir   },
    { "cd",      "change directory         cd <path> | .. | /",  cmd_cd      },
    { "pwd",     "print current path",                           cmd_pwd     },
    { "version", "show OS version info",                         cmd_version },
    { "halt",    "halt the system",                              cmd_halt    },
};
#define NUM_CMDS ((int)(sizeof(cmds) / sizeof(cmds[0])))

/* help */
static void cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;
    kprint_color("Commands:\n", VGA_LIGHT_CYAN, VGA_BLACK);
    for (int i = 0; i < NUM_CMDS; i++) {
        kprint("  ");
        kprint_color(cmds[i].name, VGA_YELLOW, VGA_BLACK);
        int pad = 10 - str_len(cmds[i].name);
        for (int j = 0; j < pad; j++) kputchar(' ');
        kprint(cmds[i].desc);
        kputchar('\n');
    }
}

/* clear */
static void cmd_clear(int argc, char **argv) {
    (void)argc; (void)argv;
    clear_screen();
}

/* echo */
static void cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) kputchar(' ');
        kprint(argv[i]);
    }
    kputchar('\n');
}

/* meminfo */
static void cmd_meminfo(int argc, char **argv) {
    (void)argc; (void)argv;

    uint32_t total_kb = pmm_total_pages() * (PAGE_SIZE / 1024);
    uint32_t free_kb  = pmm_free_pages()  * (PAGE_SIZE / 1024);
    uint32_t used_kb  = total_kb - free_kb;

    kprint_color("Memory Info\n", VGA_LIGHT_CYAN, VGA_BLACK);
    kprint("  Total : "); kprint_dec(total_kb / 1024); kprint(" MB\n");
    kprint("  Used  : "); kprint_dec(used_kb  / 1024); kprint(" MB\n");
    kprint("  Free  : "); kprint_dec(free_kb  / 1024); kprint(" MB\n");

    /* 막대 그래프 */
    if (total_kb == 0) return;
    kprint("  [");
    int used_bars = (int)(used_kb * 30 / total_kb);
    for (int i = 0; i < 30; i++) {
        if (i < used_bars) kprint_color("|", VGA_LIGHT_RED,   VGA_BLACK);
        else               kprint_color("-", VGA_LIGHT_GREEN, VGA_BLACK);
    }
    kprint("] ");
    kprint_dec(used_kb * 100 / total_kb);
    kprint("% used\n");
}

/* page — PMM 직접 테스트 */
static void cmd_page(int argc, char **argv) {
    if (argc < 2) {
        kprint("usage: page alloc | page free <addr>\n");
        return;
    }
    if (str_eq(argv[1], "alloc")) {
        void *p = pmm_alloc();
        if (!p) {
            kprint_color("Out of memory!\n", VGA_LIGHT_RED, VGA_BLACK);
        } else {
            kprint("Allocated page at: ");
            kprint_hex((uint32_t)p);
            kprint("\n");
        }
    } else if (str_eq(argv[1], "free") && argc >= 3) {
        uint32_t addr = parse_uint(argv[2]);
        if (addr % PAGE_SIZE != 0) {
            kprint_color("Address must be page-aligned (multiple of 4096)\n",
                         VGA_LIGHT_RED, VGA_BLACK);
            return;
        }
        pmm_free((void *)addr);
        kprint("Freed page at: ");
        kprint_hex(addr);
        kprint("\n");
    } else {
        kprint("usage: page alloc | page free <addr>\n");
    }
}

/* vmap <virt> — 가상 주소의 매핑 조회 */
static void cmd_vmap(int argc, char **argv) {
    if (argc < 2) {
        kprint("usage: vmap <virt>\n");
        return;
    }
    uint32_t v = parse_uint(argv[1]);
    uint32_t p = vmm_get_phys(v);
    kprint("virt "); kprint_hex(v);
    if (p == 0xFFFFFFFFu) {
        kprint_color(" -> (unmapped)\n", VGA_LIGHT_RED, VGA_BLACK);
    } else {
        kprint(" -> phys "); kprint_hex(p); kprint("\n");
    }
}

/* pf-test — 매핑 없는 주소 읽기로 페이지 폴트 유발 */
static void cmd_pftest(int argc, char **argv) {
    (void)argc; (void)argv;
    kprint_color("Triggering page fault at 0xDEADC000 ...\n",
                 VGA_LIGHT_RED, VGA_BLACK);
    volatile uint32_t *bad = (volatile uint32_t *)0xDEADC000u;
    volatile uint32_t  v   = *bad;     /* PF! */
    (void)v;
    kprint("(unreachable)\n");
}

/* ── FAT32 셸 명령 ─────────────────────────────────────────────────────── */

/* "FOO     TXT" → "FOO.TXT" 의 출력용 변환 (12바이트 + NUL) */
static void short_name_to_str(const uint8_t raw[11], char out[13]) {
    int n = 0;
    for (int i = 0; i < 8 && raw[i] != ' '; i++) out[n++] = (char)raw[i];
    if (raw[8] != ' ') {
        out[n++] = '.';
        for (int i = 8; i < 11 && raw[i] != ' '; i++) out[n++] = (char)raw[i];
    }
    out[n] = '\0';
}

static void ls_visitor(const fat32_dirent_t *e, void *ctx) {
    (void)ctx;
    char name[13];
    short_name_to_str(e->name, name);

    if (e->attr & FAT_ATTR_DIRECTORY) {
        kprint_color("  <DIR>  ", VGA_LIGHT_CYAN, VGA_BLACK);
        kprint(name);
        kprint("\n");
    } else {
        kprint("         ");
        kprint(name);
        int pad = 14 - str_len(name);
        for (int i = 0; i < pad; i++) kputchar(' ');
        kprint_dec(e->size);
        kprint(" bytes\n");
    }
}

/* ── 깊은 경로 처리 헬퍼 ─────────────────────────────────────────────────── */

/* path 를 분해해 마지막 토큰 직전까지 cwd 를 이동시키고, 마지막 토큰을
   basename 에 복사한다. 호출자는 작업 후 fat32_set_cwd_cluster(saved) 로
   원래 cwd 로 복원해야 한다.

   path 예시:
     "foo.txt"        → cwd 그대로,        basename = "foo.txt"
     "a/b/foo.txt"    → cwd → a → b,       basename = "foo.txt"
     "/a/b/foo.txt"   → cwd → root → a → b, basename = "foo.txt"
     "/" 또는 ""       → basename = ""  (호출자가 빈 검사)
   성공 0, 실패 -1 (이 경우 cwd 가 부분 이동된 상태일 수 있음). */
static int path_descend(const char *path, char basename[16]) {
    const char *p = path;
    if (p[0] == '/') {
        fat32_chdir("/");
        p++;
    }

    char prev[16];
    int  prev_set = 0;
    char cur[16];
    int  ci = 0;

    while (1) {
        if (*p == '/' || *p == '\0') {
            if (ci > 0) {
                cur[ci] = '\0';
                if (prev_set) {
                    if (fat32_chdir(prev) < 0) return -1;
                }
                for (int i = 0; i <= ci; i++) prev[i] = cur[i];
                prev_set = 1;
                ci = 0;
            }
            if (*p == '\0') break;
            p++;
        } else {
            if (ci < 15) cur[ci++] = *p;
            p++;
        }
    }

    if (prev_set) {
        for (int i = 0; ; i++) {
            basename[i] = prev[i];
            if (!prev[i]) break;
        }
    } else {
        basename[0] = '\0';
    }
    return 0;
}

/* path → 부모로 cwd 이동 + basename 추출. 실패하면 cwd 복원하고 -1. */
static int path_to_parent_and_base(const char *path,
                                   uint32_t *saved_out,
                                   char basename[16]) {
    *saved_out = fat32_cwd_cluster();
    if (path_descend(path, basename) < 0 || basename[0] == '\0') {
        fat32_set_cwd_cluster(*saved_out);
        return -1;
    }
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────── */

static void cmd_ls(int argc, char **argv) {
    if (!fat32_is_mounted()) {
        kprint_color("FAT32 not mounted.\n", VGA_LIGHT_RED, VGA_BLACK);
        return;
    }

    uint32_t saved = fat32_cwd_cluster();
    if (argc >= 2) {
        if (fat32_chdir_path(argv[1]) < 0) {
            fat32_set_cwd_cluster(saved);
            kprint_color("ls: not a directory or not found: ",
                         VGA_LIGHT_RED, VGA_BLACK);
            kprint(argv[1]); kprint("\n");
            return;
        }
    }
    fat32_listdir(fat32_cwd_cluster(), ls_visitor, 0);
    fat32_set_cwd_cluster(saved);
}

#define CAT_BUF_SIZE 16384      /* 최대 16KB 텍스트 표시 */
static uint8_t cat_buf[CAT_BUF_SIZE];

static void cmd_cat(int argc, char **argv) {
    if (argc < 2) { kprint("usage: cat <path>\n"); return; }
    if (!fat32_is_mounted()) {
        kprint_color("FAT32 not mounted.\n", VGA_LIGHT_RED, VGA_BLACK); return;
    }

    uint32_t saved;
    char base[16];
    if (path_to_parent_and_base(argv[1], &saved, base) < 0) {
        kprint_color("cat: invalid path\n", VGA_LIGHT_RED, VGA_BLACK);
        return;
    }

    fat32_dirent_t e;
    int find_rc = fat32_find(base, &e);
    fat32_set_cwd_cluster(saved);

    if (find_rc < 0) {
        kprint_color("not found: ", VGA_LIGHT_RED, VGA_BLACK);
        kprint(argv[1]); kprint("\n");
        return;
    }
    if (e.attr & FAT_ATTR_DIRECTORY) {
        kprint_color("is a directory\n", VGA_LIGHT_RED, VGA_BLACK);
        return;
    }

    int n = fat32_read_file(&e, cat_buf, CAT_BUF_SIZE);
    if (n < 0) {
        kprint_color("read error\n", VGA_LIGHT_RED, VGA_BLACK);
        return;
    }
    for (int i = 0; i < n; i++) {
        char c = (char)cat_buf[i];
        if (c == '\r') continue;          /* CRLF → LF */
        kputchar(c);
    }
    if (n > 0 && cat_buf[n - 1] != '\n') kputchar('\n');
}

/* touch <path> — 빈 파일 생성 */
static void cmd_touch(int argc, char **argv) {
    if (argc < 2) { kprint("usage: touch <path>\n"); return; }
    if (!fat32_is_mounted()) {
        kprint_color("FAT32 not mounted.\n", VGA_LIGHT_RED, VGA_BLACK); return;
    }
    uint32_t saved;
    char base[16];
    if (path_to_parent_and_base(argv[1], &saved, base) < 0) {
        kprint_color("touch: invalid path\n", VGA_LIGHT_RED, VGA_BLACK); return;
    }
    int r = fat32_create(base);
    fat32_set_cwd_cluster(saved);
    if (r < 0) {
        kprint_color("touch failed (exists or directory full)\n",
                     VGA_LIGHT_RED, VGA_BLACK);
        return;
    }
    kprint("created: "); kprint(argv[1]); kprint("\n");
}

/* write <path> <text...> — argv[2..] 를 공백으로 합쳐 파일에 덮어쓰기 */
#define WRITE_BUF_SIZE 8192
static uint8_t write_buf[WRITE_BUF_SIZE];

static void cmd_write(int argc, char **argv) {
    if (argc < 3) { kprint("usage: write <path> <text...>\n"); return; }
    if (!fat32_is_mounted()) {
        kprint_color("FAT32 not mounted.\n", VGA_LIGHT_RED, VGA_BLACK); return;
    }

    uint32_t len = 0;
    for (int i = 2; i < argc; i++) {
        if (i > 2 && len < WRITE_BUF_SIZE) write_buf[len++] = ' ';
        const char *s = argv[i];
        while (*s && len < WRITE_BUF_SIZE) write_buf[len++] = (uint8_t)*s++;
    }
    if (len < WRITE_BUF_SIZE) write_buf[len++] = '\n';

    uint32_t saved;
    char base[16];
    if (path_to_parent_and_base(argv[1], &saved, base) < 0) {
        kprint_color("write: invalid path\n", VGA_LIGHT_RED, VGA_BLACK); return;
    }
    int r = fat32_write_file(base, write_buf, len);
    fat32_set_cwd_cluster(saved);
    if (r < 0) {
        kprint_color("write failed (disk or directory full?)\n",
                     VGA_LIGHT_RED, VGA_BLACK);
        return;
    }
    kprint("wrote "); kprint_dec(len);
    kprint(" bytes to "); kprint(argv[1]); kprint("\n");
}

/* rm <path> — 파일 삭제 */
static void cmd_rm(int argc, char **argv) {
    if (argc < 2) { kprint("usage: rm <path>\n"); return; }
    if (!fat32_is_mounted()) {
        kprint_color("FAT32 not mounted.\n", VGA_LIGHT_RED, VGA_BLACK); return;
    }
    uint32_t saved;
    char base[16];
    if (path_to_parent_and_base(argv[1], &saved, base) < 0) {
        kprint_color("rm: invalid path\n", VGA_LIGHT_RED, VGA_BLACK); return;
    }
    int r = fat32_remove(base);
    fat32_set_cwd_cluster(saved);
    if (r < 0) {
        kprint_color("rm failed (not found, or it's a directory?)\n",
                     VGA_LIGHT_RED, VGA_BLACK);
        return;
    }
    kprint("removed: "); kprint(argv[1]); kprint("\n");
}

/* ── 디렉토리 + cwd 추적 ────────────────────────────────────────────────── */

#define CWD_PATH_MAX 128
static char cwd_path[CWD_PATH_MAX] = "/";

/* 8.3 short name 을 셸 표기용으로 정리 ("FOO     TXT" → "FOO.TXT") */
static void short_to_display(const char *src, char dst[13]) {
    /* 입력은 사용자가 친 그대로(소문자 가능) — 우선 대문자로 변환 */
    int n = 0;
    int dot_seen = 0;
    for (int i = 0; src[i] && n < 12; i++) {
        char c = src[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c == '.') dot_seen = 1;
        dst[n++] = c;
    }
    (void)dot_seen;
    dst[n] = '\0';
}

static void path_push(const char *name) {
    char up[13];
    short_to_display(name, up);

    int len = str_len(cwd_path);
    int add = str_len(up);
    /* 슬래시 1개 + 이름 + NUL */
    if (len + 1 + add + 1 >= CWD_PATH_MAX) return;
    if (!(len == 1 && cwd_path[0] == '/')) {
        cwd_path[len++] = '/';
    }
    for (int i = 0; i < add; i++) cwd_path[len++] = up[i];
    cwd_path[len] = '\0';
}

static void path_pop(void) {
    int len = str_len(cwd_path);
    if (len <= 1) { cwd_path[0] = '/'; cwd_path[1] = '\0'; return; }
    /* 뒤에서 '/' 까지 자르기 */
    while (len > 1 && cwd_path[len - 1] != '/') len--;
    if (len > 1) len--;          /* 그 슬래시도 제거 */
    cwd_path[len] = '\0';
    if (cwd_path[0] == '\0') { cwd_path[0] = '/'; cwd_path[1] = '\0'; }
}

static void cmd_mkdir(int argc, char **argv) {
    if (argc < 2) { kprint("usage: mkdir <path>\n"); return; }
    if (!fat32_is_mounted()) {
        kprint_color("FAT32 not mounted.\n", VGA_LIGHT_RED, VGA_BLACK); return;
    }
    uint32_t saved;
    char base[16];
    if (path_to_parent_and_base(argv[1], &saved, base) < 0) {
        kprint_color("mkdir: invalid path\n", VGA_LIGHT_RED, VGA_BLACK); return;
    }
    int r = fat32_mkdir(base);
    fat32_set_cwd_cluster(saved);
    if (r < 0) {
        kprint_color("mkdir failed (exists, full, or out of space)\n",
                     VGA_LIGHT_RED, VGA_BLACK);
        return;
    }
    kprint("created dir: "); kprint(argv[1]); kprint("\n");
}

static void cmd_rmdir(int argc, char **argv) {
    if (argc < 2) { kprint("usage: rmdir <path>\n"); return; }
    if (!fat32_is_mounted()) {
        kprint_color("FAT32 not mounted.\n", VGA_LIGHT_RED, VGA_BLACK); return;
    }
    uint32_t saved;
    char base[16];
    if (path_to_parent_and_base(argv[1], &saved, base) < 0) {
        kprint_color("rmdir: invalid path\n", VGA_LIGHT_RED, VGA_BLACK); return;
    }
    int r = fat32_rmdir(base);
    fat32_set_cwd_cluster(saved);
    if (r < 0) {
        kprint_color("rmdir failed (not a dir, or not empty)\n",
                     VGA_LIGHT_RED, VGA_BLACK);
        return;
    }
    kprint("removed dir: "); kprint(argv[1]); kprint("\n");
}

static void cmd_cd(int argc, char **argv) {
    if (!fat32_is_mounted()) {
        kprint_color("FAT32 not mounted.\n", VGA_LIGHT_RED, VGA_BLACK); return;
    }
    const char *target = (argc < 2) ? "/" : argv[1];

    /* 실패 시 복원할 cwd + path 저장 */
    uint32_t saved_cluster = fat32_cwd_cluster();
    char saved_path[CWD_PATH_MAX];
    for (int i = 0; ; i++) {
        saved_path[i] = cwd_path[i];
        if (!cwd_path[i]) break;
    }

    /* 절대 경로면 root 부터 시작 */
    const char *p = target;
    if (p[0] == '/') {
        fat32_chdir("/");
        cwd_path[0] = '/'; cwd_path[1] = '\0';
        p++;
    }

    /* 토큰별 chdir + path 갱신 */
    char tok[16];
    int  ti = 0;
    while (1) {
        if (*p == '/' || *p == '\0') {
            if (ti > 0) {
                tok[ti] = '\0';
                if (fat32_chdir(tok) < 0) {
                    /* 원자적으로 원위치 복원 */
                    fat32_set_cwd_cluster(saved_cluster);
                    for (int i = 0; ; i++) {
                        cwd_path[i] = saved_path[i];
                        if (!saved_path[i]) break;
                    }
                    kprint_color("cd failed: ", VGA_LIGHT_RED, VGA_BLACK);
                    kprint(target); kprint("\n");
                    return;
                }
                if (tok[0] == '.' && tok[1] == '\0') {
                    /* "." → 변화 없음 */
                } else if (tok[0] == '.' && tok[1] == '.' && tok[2] == '\0') {
                    path_pop();
                } else {
                    path_push(tok);
                }
                ti = 0;
            }
            if (*p == '\0') return;
            p++;
        } else {
            if (ti < 15) tok[ti++] = *p;
            p++;
        }
    }
}

static void cmd_pwd(int argc, char **argv) {
    (void)argc; (void)argv;
    kprint(cwd_path); kprint("\n");
}

/* version */
static void cmd_version(int argc, char **argv) {
    (void)argc; (void)argv;
    kprint_color("MyOS v0.9\n", VGA_LIGHT_CYAN, VGA_BLACK);
    kprint("  Arch  : x86 (i686), 32-bit protected mode + paging\n");
    kprint("  Kernel: custom bootloader + C kernel\n");
    kprint("  Phases: boot / GDT+IDT / keyboard / PMM / shell / paging\n");
    kprint("        / FAT32 read+write + directories\n");
    kprint("  FS    : ls cat touch write rm mkdir rmdir cd pwd\n");
}

/* halt */
static void cmd_halt(int argc, char **argv) {
    (void)argc; (void)argv;
    kprint_color("System halted.\n", VGA_LIGHT_RED, VGA_BLACK);
    __asm__ volatile ("cli; hlt");
}

/* ── 한 줄 입력 ──────────────────────────────────────────────────────────── */

static void readline(char *buf, int max) {
    int len = 0;
    int pcol, prow;
    kget_cursor(&pcol, &prow);

    for (;;) {
        char c = keyboard_getchar();
        if (c == '\n') {
            kputchar('\n');
            buf[len] = '\0';
            return;
        }
        if (c == '\b') {
            if (len > 0) {
                int cc, cr;
                kget_cursor(&cc, &cr);
                if (cr > prow || (cr == prow && cc > pcol)) {
                    kputchar('\b');
                    len--;
                }
            }
        } else if (len < max - 1) {
            kputchar(c);
            buf[len++] = c;
        }
    }
}

/* ── 명령어 실행 ─────────────────────────────────────────────────────────── */

static void exec(char *line) {
    char *argv[ARGS_MAX];
    int   argc = parse(line, argv);
    if (argc == 0) return;

    for (int i = 0; i < NUM_CMDS; i++) {
        if (str_eq(argv[0], cmds[i].name)) {
            cmds[i].fn(argc, argv);
            return;
        }
    }
    kprint_color("unknown: ", VGA_LIGHT_RED, VGA_BLACK);
    kprint(argv[0]);
    kprint("  (type 'help')\n");
}

/* ── 셸 메인 루프 ────────────────────────────────────────────────────────── */

void shell_run(void) {
    char line[LINE_MAX];
    kprint_color("MyOS Shell - ", VGA_YELLOW, VGA_BLACK);
    kprint("type 'help' for commands\n\n");

    for (;;) {
        kprint_color(cwd_path, VGA_LIGHT_GREEN, VGA_BLACK);
        kprint_color("> ", VGA_LIGHT_GREEN, VGA_BLACK);
        readline(line, LINE_MAX);
        exec(line);
    }
}
