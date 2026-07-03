#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H
#include <stdint.h>

void fb_init(uint32_t *addr, uint32_t width, uint32_t height, uint32_t pitch);
void fb_flush(void);

uint32_t fb_color(uint8_t r, uint8_t g, uint8_t b);
uint32_t fb_width(void);
uint32_t fb_height(void);

void fb_put_pixel(int x, int y, uint32_t color);
void fb_fill_rect(int x, int y, int w, int h, uint32_t color);
void fb_draw_rect(int x, int y, int w, int h, uint32_t color);
void fb_draw_line(int x0, int y0, int x1, int y1, uint32_t color);
void fb_draw_circle(int cx, int cy, int r, uint32_t color);
void fb_fill_circle(int cx, int cy, int r, uint32_t color);
void fb_fill_gradient(int x, int y, int w, int h, uint32_t c1, uint32_t c2);

void fb_draw_char(int x, int y, unsigned char c, uint32_t fg, uint32_t bg);
void fb_draw_str(int x, int y, const char *s, uint32_t fg, uint32_t bg);
void fb_draw_int(int x, int y, int n, uint32_t fg, uint32_t bg);

void fb_draw_char_scaled(int x, int y, unsigned char c, uint32_t fg, uint32_t bg, int scale);
void fb_draw_str_scaled(int x, int y, const char *s, uint32_t fg, uint32_t bg, int scale);

/* Decodifica y dibuja un BMP 24/32bpp sin compresión.
 * data/data_len : buffer completo del archivo BMP.
 * dx,dy         : posición en pantalla.
 * max_w,max_h   : tamaño máximo; la imagen se escala (factor entero) para caber. */
void fb_draw_bmp(int dx, int dy, int max_w, int max_h,
                  const uint8_t *data, uint32_t data_len);

#endif
