#ifndef KEYBOARD_H
#define KEYBOARD_H
#include <stdint.h>

#define KEY_UP    128
#define KEY_DOWN  129
#define KEY_LEFT  130
#define KEY_RIGHT 131

void keyboard_init(void);
int  keyboard_has_key(void);
char keyboard_getchar(void);
int  keyboard_poll(void);
int  keyboard_readline(char *buf, int maxlen);
void keyboard_flush(void);
uint32_t keyboard_key_count(void);

#endif
