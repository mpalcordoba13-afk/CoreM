#include "assets.h"
#include "framebuffer.h"
#include <stdint.h>

static const char *names[ASSET_COUNT] = {
    "corazon.img","estrella.img","sonrisa.img","casa.img","arbol.img","sol.img"
};

const char* asset_name(int idx){
    if(idx<0||idx>=ASSET_COUNT) return "?";
    return names[idx];
}

static void draw_heart(int x,int y,int w,int h){
    fb_fill_rect(x,y,w,h,fb_color(0xff,0xff,0xff));
    int cx=x+w/2, cy=y+h/2, r=h/5;
    fb_fill_circle(cx-r,cy-r/2,r,fb_color(0xe0,0x30,0x50));
    fb_fill_circle(cx+r,cy-r/2,r,fb_color(0xe0,0x30,0x50));
    for(int i=0;i<r*2;i++){
        int rw = r*2 - i;
        if(rw<2) rw=2;
        fb_fill_rect(cx-rw/2, cy-r/2+i, rw, 2, fb_color(0xe0,0x30,0x50));
    }
}

static void draw_star(int x,int y,int w,int h){
    fb_fill_rect(x,y,w,h,fb_color(0x10,0x10,0x30));
    int cx=x+w/2, cy=y+h/2, r=h/3;
    static const int sx[10]={0,7,30,12,18,0,-18,-12,-30,-7};
    static const int sy[10]={-30,-9,-9,7,28,14,28,7,-9,-9};
    for(int i=0;i<10;i++){
        int j=(i+1)%10;
        int x1=cx+sx[i]*r/30, y1=cy+sy[i]*r/30;
        int x2=cx+sx[j]*r/30, y2=cy+sy[j]*r/30;
        fb_draw_line(x1,y1,x2,y2,fb_color(0xff,0xdd,0x33));
    }
    fb_fill_circle(cx,cy,3,fb_color(0xff,0xee,0x88));
}

static void draw_smiley(int x,int y,int w,int h){
    fb_fill_rect(x,y,w,h,fb_color(0xff,0xff,0xff));
    int cx=x+w/2, cy=y+h/2, r=h/3;
    fb_fill_circle(cx,cy,r,fb_color(0xff,0xcc,0x22));
    fb_fill_circle(cx-r/3,cy-r/4,r/8,fb_color(0x20,0x20,0x20));
    fb_fill_circle(cx+r/3,cy-r/4,r/8,fb_color(0x20,0x20,0x20));
    for(int i=-r/2;i<=r/2;i++){
        int yy = cy+r/4 + (r/2 - (i<0?-i:i))/3;
        fb_put_pixel(cx+i,yy,fb_color(0x20,0x20,0x20));
        fb_put_pixel(cx+i,yy+1,fb_color(0x20,0x20,0x20));
    }
}

static void draw_house(int x,int y,int w,int h){
    fb_fill_gradient(x,y,w,h,fb_color(0x80,0xc0,0xf0),fb_color(0xc0,0xe8,0xff));
    int gy=y+h*2/3;
    fb_fill_rect(x,gy,w,h-h*2/3,fb_color(0x60,0xa0,0x50));
    int hx=x+w/2, hw=w/3, hh=h/4;
    fb_fill_rect(hx-hw/2,gy-hh,hw,hh,fb_color(0xd0,0xa0,0x70));
    /* techo */
    for(int i=0;i<hw/2+6;i++){
        fb_fill_rect(hx-hw/2-6+i, gy-hh-i*hh/(hw/2+6), hw+12-i*2, 2, fb_color(0xa0,0x40,0x30));
    }
    fb_fill_rect(hx-6,gy-hh/2,12,hh/2,fb_color(0x60,0x30,0x20));
}

static void draw_tree(int x,int y,int w,int h){
    fb_fill_gradient(x,y,w,h,fb_color(0x70,0xb0,0xe0),fb_color(0xb0,0xd8,0xa0));
    int cx=x+w/2, gy=y+h*3/4;
    fb_fill_rect(cx-4,gy-h/3,8,h/3,fb_color(0x5a,0x38,0x20));
    fb_fill_circle(cx,gy-h/3,h/5,fb_color(0x2f,0x7a,0x35));
    fb_fill_circle(cx,gy-h/3-h/6,h/6,fb_color(0x3c,0x9a,0x45));
}

static void draw_sun(int x,int y,int w,int h){
    fb_fill_gradient(x,y,w,h,fb_color(0x40,0x80,0xff),fb_color(0xa0,0xd0,0xff));
    int cx=x+w/2, cy=y+h/2, r=h/4;
    for(int i=0;i<12;i++){
        int ang = i*30;
        int dx = (ang%90<45) ? r+10 : r+10;
        (void)dx;
        int lx = cx + ((ang*2)%(r*3)-r); /* aproximacion simple de rayos */
        (void)lx;
    }
    /* rayos simples en 8 direcciones fijas */
    static const int rx[8]={1,1,0,-1,-1,-1,0,1};
    static const int ry[8]={0,1,1,1,0,-1,-1,-1};
    for(int i=0;i<8;i++){
        fb_draw_line(cx+rx[i]*r,cy+ry[i]*r,cx+rx[i]*(r+14),cy+ry[i]*(r+14),fb_color(0xff,0xdd,0x44));
    }
    fb_fill_circle(cx,cy,r,fb_color(0xff,0xcc,0x22));
}

void asset_draw(int idx,int x,int y,int w,int h){
    switch(idx){
        case 0: draw_heart(x,y,w,h); break;
        case 1: draw_star(x,y,w,h); break;
        case 2: draw_smiley(x,y,w,h); break;
        case 3: draw_house(x,y,w,h); break;
        case 4: draw_tree(x,y,w,h); break;
        case 5: draw_sun(x,y,w,h); break;
        default: fb_fill_rect(x,y,w,h,fb_color(0x20,0x20,0x20));
    }
}
