#ifndef PAINT_H
#define PAINT_H
#include <stdint.h>
void paint_init(void);
void paint_draw(int wx,int wy,int ww,int wh);
void paint_mouse(int wx,int wy,int ww,int wh,int mx,int my,int btn);
int  paint_click(int wx,int wy,int ww,int wh,int mx,int my);
#endif
