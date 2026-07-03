#ifndef BROWSER_H
#define BROWSER_H
#include <stdint.h>

/*
 * browser.h - Navegador de texto HTTP para MyOS.
 *
 * Ventana con una barra de URL editable, boton "Ir", area de contenido
 * scrollable y navegacion con teclado. Muestra texto plano o HTML
 * simplificado (quita tags, expande entidades basicas).
 */

void browser_init(void);
void browser_draw(int wx, int wy, int ww, int wh);
void browser_key(int ch);
void browser_mouse(int bx, int by, int bw, int bh, int px, int py, int click);
void browser_load_bmp(const uint8_t *data, uint32_t len);

#endif
