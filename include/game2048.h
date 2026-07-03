#ifndef GAME2048_H
#define GAME2048_H
void game2048_init(void);
void game2048_draw(int wx,int wy,int ww,int wh);
void game2048_key(int key); /* 0=up,1=down,2=left,3=right */
int  game2048_click(int wx,int wy,int ww,int wh,int mx,int my);
#endif
