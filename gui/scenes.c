#include "scenes.h"
#include "framebuffer.h"
#include <stdint.h>

static const char *names[SCENE_COUNT] = { "Atardecer", "Bosque", "Espacio", "Neon" };

const char* scene_name(int idx) {
    if (idx<0||idx>=SCENE_COUNT) return "?";
    return names[idx];
}

/* PRNG determinista para estrellas (siempre igual) */
static uint32_t srng;
static uint32_t snext(void){ srng=srng*1664525+1013904223; return srng; }

static void scene_sunset(int x,int y,int w,int h){
    fb_fill_gradient(x,y,w,h/2, fb_color(0xff,0x90,0x20), fb_color(0x80,0x10,0x50));
    fb_fill_gradient(x,y+h/2,w,h-h/2, fb_color(0x40,0x00,0x60), fb_color(0x05,0x05,0x20));
    int sx=x+w/2, sy=y+h/2;
    int r=h/8; if(r<6)r=6;
    fb_fill_circle(sx,sy,r,fb_color(0xff,0xe0,0x40));
    fb_fill_circle(sx,sy,r-3,fb_color(0xff,0xf4,0x99));
    for(int i=0;i<6;i++){
        int ry=sy+r+8+i*(r/2);
        if(ry>=y+h) break;
        int rw=r*2-(i*r/6);
        if(rw<2)rw=2;
        fb_fill_rect(sx-rw/2,ry,rw,2,fb_color(0xff,0xaa,0x40));
    }
    /* Montanas */
    for(int i=0;i<w;i++){
        int mh = (i%37<19)? (h/6 + (i%37)*2) : (h/6 + (37-(i%37))*2);
        fb_fill_rect(x+i,y+h-mh,1,mh,fb_color(0x20,0x08,0x30));
    }
}

static void scene_forest(int x,int y,int w,int h){
    fb_fill_gradient(x,y,w,h, fb_color(0x60,0xb0,0xd0), fb_color(0xb0,0xe0,0xa0));
    fb_fill_rect(x,y+h*2/3,w,h/3,fb_color(0x3a,0x6b,0x35));
    srng=4242;
    for(int i=0;i<10;i++){
        int tx = x + (snext()%w);
        int th = h/4 + (int)(snext()%(h/4));
        int tw = th/2;
        int ty = y+h*2/3;
        /* tronco */
        fb_fill_rect(tx-2,ty-th/3,4,th/3,fb_color(0x5a,0x3a,0x20));
        /* copa: 3 triangulos apilados via circulos */
        fb_fill_circle(tx,ty-th/3,tw/2,fb_color(0x2f,0x7a,0x35));
        fb_fill_circle(tx,ty-th/3-tw/3,tw/2-2,fb_color(0x35,0x8a,0x3d));
        fb_fill_circle(tx,ty-th/3-2*tw/3,tw/2-4,fb_color(0x3c,0x9a,0x45));
    }
    /* sol */
    fb_fill_circle(x+w-w/6,y+h/6,h/10,fb_color(0xff,0xf0,0x80));
}

static void scene_space(int x,int y,int w,int h){
    fb_fill_rect(x,y,w,h,fb_color(0x03,0x03,0x10));
    srng=99991;
    for(int i=0;i<150;i++){
        int sx=x+(int)(snext()%w);
        int sy=y+(int)(snext()%h);
        uint8_t b=120+(snext()%135);
        fb_put_pixel(sx,sy,fb_color(b,b,b));
    }
    /* planeta */
    int px=x+w*2/3, py=y+h/2, pr=h/4;
    fb_fill_circle(px,py,pr,fb_color(0xd0,0x70,0x40));
    fb_fill_circle(px-pr/3,py-pr/3,pr/3,fb_color(0xe0,0x90,0x60));
    /* anillo */
    fb_draw_circle(px,py,pr+pr/3,fb_color(0xc0,0xc0,0xe0));
    fb_draw_circle(px,py,pr+pr/3+1,fb_color(0x90,0x90,0xb0));
    /* luna pequena */
    fb_fill_circle(x+w/5,y+h/5,h/16,fb_color(0xe0,0xe0,0xf0));
}

static void scene_neon(int x,int y,int w,int h){
    fb_fill_rect(x,y,w,h,fb_color(0x05,0x05,0x12));
    uint32_t gc=fb_color(0x20,0x10,0x40);
    int step = w/16; if(step<8)step=8;
    for(int gx=x;gx<x+w;gx+=step) fb_draw_line(gx,y,gx,y+h,gc);
    for(int gy=y;gy<y+h;gy+=step) fb_draw_line(x,gy,x+w,gy,gc);

    int cx=x+w/2, cy=y+h/2;
    fb_draw_circle(cx,cy,h/3,fb_color(0xff,0x40,0xb0));
    fb_draw_circle(cx,cy,h/3-3,fb_color(0xff,0x80,0xd0));
    fb_draw_circle(cx,cy,h/4,fb_color(0x40,0xe0,0xff));
    fb_draw_circle(cx,cy,h/6,fb_color(0xb0,0xff,0x60));
    fb_fill_circle(cx,cy,4,fb_color(0xff,0xff,0xff));
}

void scene_draw(int idx,int x,int y,int w,int h){
    switch(idx){
        case 0: scene_sunset(x,y,w,h); break;
        case 1: scene_forest(x,y,w,h); break;
        case 2: scene_space(x,y,w,h);  break;
        case 3: scene_neon(x,y,w,h);   break;
        default: fb_fill_rect(x,y,w,h,fb_color(0x20,0x20,0x20));
    }
}
