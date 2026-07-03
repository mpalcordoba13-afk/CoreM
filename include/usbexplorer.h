#ifndef USBEXPLORER_H
#define USBEXPLORER_H
#include "keyboard.h"

/* Reusar las constantes de keyboard.h directamente */
#define KEY_UP_USB   KEY_UP
#define KEY_DOWN_USB KEY_DOWN
#define KEY_PGUP_USB 134
#define KEY_PGDN_USB 135

void usbexplorer_init(void);
void usbexplorer_draw(int wx, int wy, int ww, int wh);
void usbexplorer_key(int ch);
void usbexplorer_mouse(int bx_w, int by_w, int bw_w, int bh_w,
                        int px, int py, int click);
int  usbexplorer_selected_file(void);  /* índice del archivo seleccionado o -1 */

#endif
