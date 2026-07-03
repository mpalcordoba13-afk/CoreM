#ifndef IMAGEVIEWER_H
#define IMAGEVIEWER_H
#include <stdint.h>
void imageviewer_draw(int wx,int wy,int ww,int wh);
int  imageviewer_click(int wx,int wy,int ww,int wh,int mx,int my);
void imageviewer_load_bmp(const uint8_t *data, uint32_t len);
void imageviewer_draw_bmp_overlay(int wx,int wy,int ww,int wh);
#endif
