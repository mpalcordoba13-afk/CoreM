#ifndef CODE_H
#define CODE_H

/* Teclas especiales usadas por el editor */
#define KEY_HOME     132
#define KEY_END      133
#define KEY_PGUP     134
#define KEY_PGDN     135
#define KEY_CTRL     200   /* press */
#define KEY_CTRL_REL 201   /* release */

void code_init(void);
void code_load(const char *filename);
void code_draw(int wx, int wy, int ww, int wh);
void code_key (int ch);
void code_mouse(int bx, int by, int bw, int bh, int px, int py, int click);

#endif
