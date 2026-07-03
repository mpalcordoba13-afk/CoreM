#ifndef SCREENSAVER_H
#define SCREENSAVER_H
#include <stdint.h>
void screensaver_init(void);
void screensaver_tick(void);
void screensaver_reset(void);
int  screensaver_active(void);
void screensaver_draw(void);
#endif
