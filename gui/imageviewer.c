#include "imageviewer.h"
#include "gui.h"
#include "framebuffer.h"
#include "assets.h"
#include "wallpaper.h"
#include <stdint.h>

static int current = 0;

static int slen(const char *s){int n=0;while(s[n])n++;return n;}

void imageviewer_draw(int wx,int wy,int ww,int wh) {
    (void)wh;
    int ox=wx+BORDER+10, oy=wy+TITLEBAR_H+10;
    int iw=ww-BORDER*2-20, ih=170;

    fb_draw_str(ox,oy,"Carpeta: /assets",fb_color(0x0f,0x34,0x60),fb_color(0xf2,0xf2,0xf2));
    oy+=20;

    fb_draw_rect(ox-1,oy-1,iw+2,ih+2,fb_color(0x88,0x88,0xaa));
    asset_draw(current,ox,oy,iw,ih);
    oy+=ih+10;

    fb_fill_rect(ox,oy,50,28,fb_color(0x20,0x50,0x90));
    fb_draw_str(ox+18,oy+10,"<",fb_color(0xff,0xff,0xff),fb_color(0x20,0x50,0x90));

    fb_fill_rect(ox+iw-50,oy,50,28,fb_color(0x20,0x50,0x90));
    fb_draw_str(ox+iw-32,oy+10,">",fb_color(0xff,0xff,0xff),fb_color(0x20,0x50,0x90));

    const char *name = asset_name(current);
    int nlen=slen(name);
    fb_draw_str(ox+iw/2-nlen*9/2,oy+10,name,fb_color(0x11,0x11,0x22),fb_color(0xf2,0xf2,0xf2));
    oy+=38;

    int active = (wallpaper_current() == (wallpaper_t)(WP_GALLERY0+4+current));
    uint32_t bcol = active ? fb_color(0x33,0xcc,0x66) : fb_color(0x20,0x80,0x40);
    fb_fill_rect(ox,oy,iw,30,bcol);
    fb_draw_str(ox+iw/2-70, oy+10, active?"Fondo actual":"Usar como fondo",
                fb_color(0xff,0xff,0xff),bcol);
}

int imageviewer_click(int wx,int wy,int ww,int wh,int mx,int my) {
    (void)wh;
    int ox=wx+BORDER+10, oy=wy+TITLEBAR_H+10;
    int iw=ww-BORDER*2-20, ih=170;
    oy+=20+ih+10;

    if (mx>=ox&&mx<ox+50&&my>=oy&&my<oy+28) { current=(current+ASSET_COUNT-1)%ASSET_COUNT; return 1; }
    if (mx>=ox+iw-50&&mx<ox+iw&&my>=oy&&my<oy+28) { current=(current+1)%ASSET_COUNT; return 1; }
    oy+=38;
    if (mx>=ox&&mx<ox+iw&&my>=oy&&my<oy+30) {
        wallpaper_set((wallpaper_t)(WP_GALLERY0+4+current));
        return 1;
    }
    return 0;
}

/* Buffer para imagen BMP cargada via drag & drop */
#define IV_BMP_MAX (256*1024)
static uint8_t iv_bmp_buf[IV_BMP_MAX];
static uint32_t iv_bmp_len = 0;
static int iv_bmp_valid = 0;

void imageviewer_load_bmp(const uint8_t *data, uint32_t len){
    uint32_t sz = len < IV_BMP_MAX ? len : IV_BMP_MAX;
    for(uint32_t k=0;k<sz;k++) iv_bmp_buf[k]=data[k];
    iv_bmp_len   = sz;
    iv_bmp_valid = 1;
}

void imageviewer_draw_bmp_overlay(int wx,int wy,int ww,int wh){
    if(!iv_bmp_valid) return;
    int ox=wx+BORDER+4, oy=wy+TITLEBAR_H+4;
    int iw=ww-BORDER*2-8, ih=wh-TITLEBAR_H-BORDER-8;
    extern void fb_draw_bmp(int,int,int,int,const uint8_t*,uint32_t);
    fb_draw_bmp(ox,oy,iw,ih,iv_bmp_buf,iv_bmp_len);
}
