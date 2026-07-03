#ifndef TETRIS_H
#define TETRIS_H

void tetris_init(void);
void tetris_tick(void);
void tetris_draw(int wx,int wy,int ww,int wh);
void tetris_key(int action); /* 0=left 1=right 2=rotate 3=down(soft) 4=drop */
void tetris_restart(void);

#endif
