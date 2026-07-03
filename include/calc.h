#ifndef CALC_H
#define CALC_H
#include <stdint.h>

void calc_init(void);
void calc_draw(int wx, int wy, int ww);
int  calc_click(int wx, int wy, int ww, int mx, int my);

#endif
