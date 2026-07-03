#ifndef SNAKE_H
#define SNAKE_H

void snake_init(void);
void snake_tick(void);              /* avanza la logica del juego */
void snake_draw(int wx,int wy,int ww,int wh);
void snake_key(int dir);            /* 0=up 1=down 2=left 3=right */
void snake_restart(void);

#endif
