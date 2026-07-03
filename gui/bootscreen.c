#include "bootscreen.h"
#include "framebuffer.h"
#include "sound.h"
#include <stdint.h>

static int slen(const char *s){int n=0;while(s[n])n++;return n;}

static void wait(int loops) {
    for (volatile int i=0;i<loops;i++);
}

void bootscreen_show(void) {
    uint32_t bg     = fb_color(0x05,0x05,0x14);
    uint32_t accent = fb_color(0x4a,0x9e,0xff);
    uint32_t dim    = fb_color(0x55,0x55,0x77);

    fb_fill_rect(0,0,fb_width(),fb_height(),bg);
    fb_flush();
    wait(800000);

    int cx = fb_width()/2;
    int scale = 7;
    const char *logo = "CoreM";
    int logo_w = slen(logo)*9*scale;

    /* Marco decorativo */
    fb_draw_rect(cx-logo_w/2-30, 130, logo_w+60, 8*scale+60, fb_color(0x22,0x44,0x88));

    fb_draw_str_scaled(cx-logo_w/2, 155, logo, accent, bg, scale);
    fb_draw_str(cx-110, 230, "S i s t e m a   O p e r a t i v o", dim, bg);
    fb_flush();

    sound_beep(523,90); wait(300000);
    sound_beep(659,90); wait(300000);
    sound_beep(784,140);

    const char *steps[] = {
        "Inicializando GDT...",
        "Configurando IDT y PIC...",
        "Iniciando temporizador PIT...",
        "Detectando modo de video VBE...",
        "Inicializando controlador PS/2...",
        "Montando sistema de archivos...",
        "Cargando base de usuarios...",
        "Iniciando entorno grafico..."
    };
    int n = 8;
    int bar_w = 440, bar_h = 18;
    int bx = cx-bar_w/2, by = 360;

    fb_draw_rect(bx-2,by-2,bar_w+4,bar_h+4,fb_color(0x44,0x66,0xaa));

    for (int i=0;i<n;i++) {
        fb_fill_rect(bx,by-26,bar_w,16,bg);
        fb_draw_str(bx,by-26,steps[i],fb_color(0xaa,0xaa,0xcc),bg);
        int fillw = (bar_w-4)*(i+1)/n;
        fb_fill_rect(bx+2,by+2,fillw,bar_h-4,accent);
        fb_flush();
        wait(900000);
    }

    fb_fill_rect(bx,by-26,bar_w,16,bg);
    fb_draw_str(bx,by-26,"Listo.",fb_color(0x66,0xff,0x99),bg);
    fb_flush();
    wait(900000);
}
