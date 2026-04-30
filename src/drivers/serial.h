#pragma once
#include "../include/types.h"

/* COM1 시리얼 포트 출력 드라이버 (115200 8N1).
   QEMU 의 -serial stdio 또는 -serial file:foo.log 와 결합되면
   부팅 로그/디버그 메시지를 호스트에서 그대로 볼 수 있다. */

void serial_init(void);
int  serial_is_ready(void);
void serial_putc(char c);
void serial_puts(const char *s);
