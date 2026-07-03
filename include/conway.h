#ifndef CONWAY_H
#define CONWAY_H
void conway_init(void);
void conway_draw(int wx, int wy, int ww, int wh);
void conway_tick(int wx, int wy, int ww, int wh);
int  conway_click(int wx, int wy, int ww, int wh, int mx, int my);
void conway_key(char k);
#endif
