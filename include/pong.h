#ifndef PONG_H
#define PONG_H

/* Jugador 1: W/S   |   Jugador 2: UP/DOWN */

void pong_init(void);
void pong_tick(void);
void pong_draw(int wx, int wy, int ww, int wh);
/* key: 0=p1 arriba, 1=p1 abajo, 2=p2 arriba, 3=p2 abajo, 4=restart */
void pong_key(int key);
void pong_restart(void);

#endif
