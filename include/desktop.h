#ifndef DESKTOP_H
#define DESKTOP_H
#include "imageviewer.h"
#include "browser.h"
#include <stdint.h>

void desktop_init(void);
void desktop_draw(void);
void desktop_clean(void);

/* Mouse events — llamar desde gui.c */
void desktop_mouse_down(int mx, int my);
void desktop_mouse_move(int mx, int my);
void desktop_mouse_up  (int mx, int my);

/* Consultas */
int         desktop_icon_at      (int mx, int my);   /* -1 si nada */
int         desktop_over_trash   (int mx, int my);   /* 1 si sobre papelera */
const char* desktop_icon_appname (int idx);
int         desktop_icon_is_file (int idx);

/* USB drag & drop */
void desktop_drag_start_usb(int file_idx, int mx, int my);
void desktop_drag_move     (int mx, int my);
int  desktop_drag_drop     (int mx, int my);
int  desktop_drag_active   (void);

#endif
