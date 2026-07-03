#ifndef TOAST_H
#define TOAST_H
#include <stdint.h>
void toast_init(void);
void toast_show(const char *msg);
void toast_draw(void);
void toast_tick(void);
#endif
