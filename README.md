# MyOS

x86 베어메탈 운영체제 — QEMU에서 부팅되는 진짜 OS.

## 아키텍처

```
[BIOS] → boot.asm (MBR, 0x7C00) → protected mode → kernel_entry.asm (0x10000) → kmain()
```

## 프로젝트 구조

```
OSver1/
├── src/
│   ├── boot/
│   │   └── boot.asm          # 부트로더: 디스크 로드 + 보호 모드 전환
│   ├── kernel/
│   │   ├── kernel_entry.asm  # 커널 진입점 (_start @ 0x10000)
│   │   └── kernel.c          # kmain()
│   └── drivers/
│       ├── screen.c          # VGA 텍스트 드라이버
│       └── screen.h
├── linker.ld                 # 커널 링커 스크립트
├── Makefile
└── README.md
```

## 개발 환경 설정 (Windows)

### 1. WSL 설치
```powershell
wsl --install
```

### 2. WSL 안에서 크로스 컴파일러 설치
```bash
sudo apt update
sudo apt install -y nasm qemu-system-x86 make

# i686-elf 크로스 컴파일러 빌드 (OSDev Wiki 권장)
# https://wiki.osdev.org/GCC_Cross-Compiler
# 또는 prebuilt 사용:
sudo apt install -y gcc-multilib gcc
```

### 3. QEMU (Windows 네이티브) — 선택사항
QEMU for Windows: https://www.qemu.org/download/#windows

## 빌드 및 실행

```bash
# WSL 터미널에서 프로젝트 디렉토리로 이동 후
make          # 빌드 → build/os.img 생성
make run      # QEMU로 실행
make clean    # 빌드 결과물 삭제
make debug    # GDB 디버그 모드 (포트 1234)
```

## 구현 로드맵

- [x] **Phase 1** — 부트로더 + 보호 모드 + VGA 드라이버
- [x] **Phase 2** — GDT / IDT / 인터럽트 핸들러
- [x] **Phase 3** — 키보드 드라이버 (PS/2)
- [x] **Phase 4** — 물리 메모리 관리자
- [x] **Phase 5** — 기본 셸 (명령어 입력)
- [x] **Phase 6** — 가상 메모리 / 페이징 (첫 4 MB identity map + PF 핸들러)
- [x] **Phase 7** — 파일시스템 (FAT32 read+write + 디렉토리, ATA PIO,
      ls/cat/touch/write/rm/mkdir/rmdir/cd/pwd)
- [x] **Phase 8** — 시간 / 디버깅 (COM1 시리얼 콘솔, PIT 100Hz 타이머,
      `uptime`/`sleep` 명령)
- [x] **Phase 9** — 커널 힙 (`kmalloc`/`kfree`, doubly-linked freelist +
      coalesce, 정적 `cat_buf`/`write_buf` 를 동적 할당으로 전환)
- [x] **Phase 10** — 멀티태스킹 (`task_t`, `context_switch`, 협력 yield +
      PIT IRQ 기반 선점 라운드로빈, 50ms time slice)
- [x] **Phase 11** — 유저 모드 + 시스템콜 (Ring 3, GDT user descriptors,
      TSS, `int 0x80` 시스템콜 디스패처)
- [ ] **Phase 12** — ELF 로더 + 사용자 프로그램 실행
      (FAT32에서 ELF32 적재 → ring 3 진입, `exec /bin/hello.elf`)
      → **여기까지가 mini-Unix**
- [ ] **Phase 13** — VESA 그래픽 모드 + 비트맵 폰트 렌더러
      (320×200 또는 1024×768, 픽셀 출력, 한글 폰트)
- [ ] **Phase 14** — 마우스 드라이버(PS/2) + 이벤트 시스템
- [ ] **Phase 15** — 윈도우 매니저 (창/포커스/합성기, dirty rect, double buffer)
- [ ] **Phase 16** — GUI 위젯 (버튼/텍스트박스/스크롤바, 레이아웃)
- [ ] **Phase 17** — libc 포팅 (newlib 또는 자체) → 응용 빌드 가능
- [ ] **Phase 18** — 네트워크 스택 (RTL8139 NIC + ARP/IP/UDP/TCP)
- [ ] **Phase 19** — 기본 응용 (텍스트 에디터, 터미널 에뮬레이터,
      파일 매니저, 시계, 계산기)
      → **여기까지 가면 ToaruOS급 GUI 데스크톱 OS**

### 사이드 트랙 (선택)

- FAT32 보강 (LFN, `mkdir -p`, `cp`/`mv`, 디렉토리 클러스터 자동 확장)
- ext2 파일시스템 마이그레이션 (응용 포팅 본격화 시 자연스러움)
- APIC + HPET (PIC/PIT 의 현대적 후계자, 멀티코어로 가는 길)
- PCI 열거 (네트워크/AHCI 가기 전 디딤돌)
- AHCI/SATA 드라이버 (실제 하드웨어용)
- 셸 향상 (입력 history, 탭 자동완성, `|` 파이프, `>` 리다이렉션)

## 예상 기간

| 단계 | 풀타임 | 파트타임(퇴근 후/주말) |
|---|---|---|
| Phase 8~9 (시리얼 / PIT / 힙) | 1~2주 | 1~2개월 |
| Phase 10 (멀티태스킹) | 2~4주 | 2~3개월 |
| Phase 11 (유저모드 + 시스템콜) | 1~2주 | 1~2개월 |
| Phase 12 (ELF 로더) | 1주 | 1개월 |
| **여기까지 = mini-Unix** | **~2개월** | **6~12개월** |
| Phase 13 (VESA + 폰트) | 1~2주 | 1~2개월 |
| Phase 14 (마우스 + 이벤트) | 1주 | 2~3주 |
| Phase 15 (윈도우 매니저) | 1~3개월 | 6~12개월 |
| Phase 16 (GUI 위젯) | 1~2개월 | 3~6개월 |
| Phase 17 (libc 포팅) | 1~2개월 | 6~12개월 |
| Phase 18 (네트워크 스택) | 2~6개월 | 1~2년 |
| Phase 19 (기본 응용) | 2~6개월 | 1~2년 |
| **여기까지 = ToaruOS급 GUI 데스크톱 OS** | **1.5~3년** | **3~7년** |
| 디버깅 / 안정화 / 성능 | 지속 | 지속 |

> 한 사람 OS 프로젝트의 99%는 3~6개월에 멈춥니다. 속도보다 **꾸준함**이 핵심.

## 참고 자료

- [OSDev Wiki](https://wiki.osdev.org/)
- [Intel x86 Manual](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [NASM Documentation](https://nasm.us/doc/)
