#include "gallery.h"
#include "gui.h"
#include "framebuffer.h"
#include "scenes.h"
#include "wallpaper.h"
#include <stdint.h>

static int current = 0;

void gallery_draw(int wx,int wy,int ww,int wh) {
    (void)wh;
    int ox=wx+BORDER+10, oy=wy+TITLEBAR_H+10;
    int iw=ww-BORDER*2-20, ih=170;

    fb_draw_str(ox,oy,"Galeria de imagenes",fb_color(0x0f,0x34,0x60),fb_color(0xf2,0xf2,0xf2));
    oy+=20;

    fb_draw_rect(ox-1,oy-1,iw+2,ih+2,fb_color(0x88,0x88,0xaa));
    scene_draw(current,ox,oy,iw,ih);
    oy+=ih+10;

    /* Flechas */
    fb_fill_rect(ox,oy,50,28,fb_color(0x20,0x50,0x90));
    fb_draw_str(ox+18,oy+10,"<",fb_color(0xff,0xff,0xff),fb_color(0x20,0x50,0x90));

    fb_fill_rect(ox+iw-50,oy,50,28,fb_color(0x20,0x50,0x90));
    fb_draw_str(ox+iw-32,oy+10,">",fb_color(0xff,0xff,0xff),fb_color(0x20,0x50,0x90));

    /* Nombre */
    const char *name = scene_name(current);
    int nlen=0; while(name[nlen])nlen++;
    fb_draw_str(ox+iw/2-nlen*9/2,oy+10,name,fb_color(0x11,0x11,0x22),fb_color(0xf2,0xf2,0xf2));
    oy+=38;

    /* Boton usar como fondo */
    int active = (wallpaper_current() == (wallpaper_t)(WP_GALLERY0+current));
    uint32_t bcol = active ? fb_color(0x33,0xcc,0x66) : fb_color(0x20,0x80,0x40);
    fb_fill_rect(ox,oy,iw,30,bcol);
    fb_draw_str(ox+iw/2-70, oy+10, active?"Fondo actual":"Usar como fondo",
                fb_color(0xff,0xff,0xff),bcol);
}

int gallery_click(int wx,int wy,int ww,int wh,int mx,int my) {
    (void)wh;
    int ox=wx+BORDER+10, oy=wy+TITLEBAR_H+10;
    int iw=ww-BORDER*2-20, ih=170;
    oy+=20+ih+10;

    if (mx>=ox&&mx<ox+50&&my>=oy&&my<oy+28) {
        current = (current+SCENE_COUNT-1)%SCENE_COUNT;
        return 1;
    }
    if (mx>=ox+iw-50&&mx<ox+iw&&my>=oy&&my<oy+28) {
        current = (current+1)%SCENE_COUNT;
        return 1;
    }
    oy+=38;
    if (mx>=ox&&mx<ox+iw&&my>=oy&&my<oy+30) {
        wallpaper_set((wallpaper_t)(WP_GALLERY0+current));
        return 1;
    }
    return 0;
}
