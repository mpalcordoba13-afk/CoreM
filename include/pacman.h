#ifndef PACMAN_H
#define PACMAN_H

void pacman_init(void);
void pacman_tick(void);
void pacman_draw(int wx, int wy, int ww, int wh);
void pacman_key(int dir); /* 0=up 1=down 2=left 3=right */
void pacman_restart(void);

#endif
