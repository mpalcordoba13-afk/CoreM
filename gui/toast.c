/*
 * toast.c – Notificaciones tipo toast (esquina inferior derecha)
 * Duración: ~3 segundos. Se apilan hasta 4.
 */
#include "toast.h"
#include "framebuffer.h"
#include "timer.h"
#include <stdint.h>

#define TOAST_MAX    4
#define TOAST_W      260
#define TOAST_H      32
#define TOAST_TTL    180   /* ticks ~3 seg */
#define TOAST_FADE   40    /* últimos ticks se "atenúa" (solo visual) */

typedef struct {
    char     msg[64];
    uint32_t born;   /* timer_ticks() al crear */
    int      active;
} toast_t;

static toast_t toasts[TOAST_MAX];

void toast_init(void){
    for(int i=0;i<TOAST_MAX;i++) toasts[i].active=0;
}

static void scpy(char *d, const char *s, int n){
    int i=0; while(s[i]&&i<n-1){d[i]=s[i];i++;} d[i]='\0';
}

void toast_show(const char *msg){
    /* Reutilizar slot vacío o el más viejo */
    int slot=-1;
    uint32_t oldest=0xFFFFFFFF;
    for(int i=0;i<TOAST_MAX;i++){
        if(!toasts[i].active){ slot=i; break; }
        if(toasts[i].born < oldest){ oldest=toasts[i].born; slot=i; }
    }
    toasts[slot].active=1;
    toasts[slot].born=timer_ticks();
    scpy(toasts[slot].msg, msg, 64);
}

void toast_tick(void){
    uint32_t now=timer_ticks();
    for(int i=0;i<TOAST_MAX;i++){
        if(!toasts[i].active) continue;
        if(now - toasts[i].born > TOAST_TTL) toasts[i].active=0;
    }
}

void toast_draw(void){
    int sw=fb_width(), sh=fb_height();
    int row=0;
    for(int i=0;i<TOAST_MAX;i++){
        if(!toasts[i].active) continue;
        uint32_t age = timer_ticks() - toasts[i].born;
        /* Opacidad simulada con brillo */
        uint32_t bg, border;
        if(age > TOAST_TTL - TOAST_FADE){
            bg     = fb_color(0x30,0x30,0x40);
            border = fb_color(0x55,0x55,0x77);
        } else {
            bg     = fb_color(0x18,0x28,0x48);
            border = fb_color(0x44,0x88,0xcc);
        }
        int tx = sw - TOAST_W - 12;
        int ty = sh - 32 - (row+1)*(TOAST_H+6);
        fb_fill_rect(tx+3, ty+3, TOAST_W, TOAST_H, fb_color(0,0,0)); /* sombra */
        fb_fill_rect(tx, ty, TOAST_W, TOAST_H, bg);
        fb_draw_rect(tx, ty, TOAST_W, TOAST_H, border);
        /* Barra de progreso de tiempo */
        int bar = (int)((TOAST_TTL - age) * (TOAST_W-4) / TOAST_TTL);
        if(bar>0) fb_fill_rect(tx+2, ty+TOAST_H-3, bar, 2, border);
        /* Ícono pequeño */
        fb_fill_rect(tx+6, ty+8, 14, 14, fb_color(0x22,0x88,0xff));
        fb_draw_str(tx+9, ty+10, "i", fb_color(0xff,0xff,0xff), fb_color(0x22,0x88,0xff));
        /* Mensaje */
        fb_draw_str(tx+26, ty+11, toasts[i].msg, fb_color(0xee,0xee,0xff), bg);
        row++;
    }
}
