#ifndef BREAKOUT_H
#define BREAKOUT_H
void breakout_init(void);
void breakout_draw(int wx,int wy,int ww,int wh);
void breakout_tick(int wx,int wy,int ww,int wh);
int  breakout_click(int wx,int wy,int ww,int wh,int mx,int my);
void breakout_mouse(int wx,int wy,int ww,int wh,int mx);
#endif
