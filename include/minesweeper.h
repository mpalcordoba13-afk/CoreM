#ifndef MINESWEEPER_H
#define MINESWEEPER_H
void minesweeper_init(void);
void minesweeper_draw(int wx,int wy,int ww,int wh);
int  minesweeper_click(int wx,int wy,int ww,int wh,int mx,int my,int right);
#endif
