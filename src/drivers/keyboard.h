#pragma once
#include "../include/types.h"

void keyboard_init(void);
int  keyboard_haschar(void);
char keyboard_getchar(void);  /* blocks until a key is available */
