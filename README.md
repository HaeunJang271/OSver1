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

## 참고 자료

- [OSDev Wiki](https://wiki.osdev.org/)
- [Intel x86 Manual](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [NASM Documentation](https://nasm.us/doc/)
