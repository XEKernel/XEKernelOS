#pragma once

#define KB_DATA   0x60
#define KB_STATUS 0x64
#define CMD_BUF   64

void kb_init(void);
char kb_getchar(void);
void kb_readline(char *b, int max);
void kb_flush(void);
