#include "wallpaper.h"
#include "framebuffer.h"
#include "scenes.h"
#include "assets.h"
#include <stdint.h>

static wallpaper_t current = WP_SOLID; /* Sólido por defecto — el más rápido */
static int wp_dirty = 1; /* 1 = necesita redibujar el fondo */

static uint32_t rng_wp = 12345;
static uint32_t rand_next(void){ rng_wp=rng_wp*1664525+1013904223; return rng_wp; }

static void draw_solid(void){
    fb_fill_rect(0,0,fb_width(),fb_height()-28,fb_color(0x0d,0x10,0x2a));
}
static void draw_gradient(void){
    /* Solo se llama cuando wp_dirty=1 — no cada frame */
    fb_fill_gradient(0,0,fb_width(),fb_height()-28,
        fb_color(0x0d,0x0d,0x2b),fb_color(0x1a,0x05,0x3a));
}
static void draw_stars(void){
    fb_fill_rect(0,0,fb_width(),fb_height()-28,fb_color(0x00,0x00,0x08));
    rng_wp=99991;
    int sw=fb_width(),sh=fb_height()-28;
    for(int i=0;i<300;i++){
        int x=rand_next()%sw, y=rand_next()%sh;
        uint8_t b=100+rand_next()%155;
        uint32_t col=fb_color(b,b,b);
        fb_put_pixel(x,y,col);
        if(b>200) fb_put_pixel(x+1,y,col);
    }
    fb_fill_circle(fb_width()-120,80,35,fb_color(0xff,0xf0,0xc0));
    fb_fill_circle(fb_width()-105,70,30,fb_color(0x00,0x00,0x08));
}
static void draw_grid(void){
    fb_fill_rect(0,0,fb_width(),fb_height()-28,fb_color(0x05,0x10,0x20));
    uint32_t gc=fb_color(0x00,0x40,0x60);
    for(int x=0;x<(int)fb_width();x+=40) fb_draw_line(x,0,x,fb_height()-28,gc);
    for(int y=0;y<(int)fb_height()-28;y+=40) fb_draw_line(0,y,fb_width(),y,gc);
    int cx=fb_width()/2,cy=(fb_height()-28)/2;
    fb_draw_circle(cx,cy,80,fb_color(0x00,0x80,0xaa));
    fb_draw_circle(cx,cy,40,fb_color(0x00,0xaa,0xff));
    fb_fill_circle(cx,cy,5,fb_color(0x00,0xff,0xff));
}
static void draw_sunset(void){
    fb_fill_gradient(0,0,fb_width(),(fb_height()-28)/2,
        fb_color(0xff,0x80,0x00),fb_color(0x80,0x00,0x40));
    fb_fill_gradient(0,(fb_height()-28)/2,fb_width(),(fb_height()-28)/2,
        fb_color(0x40,0x00,0x60),fb_color(0x00,0x10,0x40));
    int sx=fb_width()/2,sy=(fb_height()-28)/2;
    fb_fill_circle(sx,sy,50,fb_color(0xff,0xdd,0x00));
    fb_fill_circle(sx,sy,40,fb_color(0xff,0xee,0x88));
    for(int i=0;i<8;i++)
        fb_fill_rect(sx-5+i*2,sy+5+i*8,8,6,fb_color(0xff,0xaa,0x00));
}

/* Redibuja el fondo cada frame para limpiar el cursor y cualquier artefacto. */
void wallpaper_draw(void){
    switch(current){
        case WP_SOLID:    draw_solid();   break;
        case WP_GRADIENT: draw_gradient(); break;
        case WP_STARS:    draw_stars();   break;
        case WP_GRID:     draw_grid();    break;
        case WP_SUNSET:   draw_sunset();  break;
        case WP_GALLERY0: scene_draw(0,0,0,fb_width(),fb_height()-28); break;
        case WP_GALLERY1: scene_draw(1,0,0,fb_width(),fb_height()-28); break;
        case WP_GALLERY2: scene_draw(2,0,0,fb_width(),fb_height()-28); break;
        case WP_GALLERY3: scene_draw(3,0,0,fb_width(),fb_height()-28); break;
        case WP_ASSET0:   asset_draw(0,0,0,fb_width(),fb_height()-28); break;
        case WP_ASSET1:   asset_draw(1,0,0,fb_width(),fb_height()-28); break;
        case WP_ASSET2:   asset_draw(2,0,0,fb_width(),fb_height()-28); break;
        case WP_ASSET3:   asset_draw(3,0,0,fb_width(),fb_height()-28); break;
        case WP_ASSET4:   asset_draw(4,0,0,fb_width(),fb_height()-28); break;
        case WP_ASSET5:   asset_draw(5,0,0,fb_width(),fb_height()-28); break;
    }
}

void wallpaper_set(wallpaper_t type){ current=type; wp_dirty=1; }
wallpaper_t wallpaper_current(void){ return current; }
wallpaper_t wallpaper_next(void){
    current=(wallpaper_t)((current+1)%WP_COUNT);
    wp_dirty=1;
    return current;
}
void wallpaper_invalidate(void){ wp_dirty=1; }
