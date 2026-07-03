#include "screensaver.h"
#include "framebuffer.h"
#include "timer.h"
#include <stdint.h>

#define SS_TIMEOUT 1800  /* 30 segundos a 60fps */
#define N_STARS    80
#define N_BALLS    8

static uint32_t last_activity=0;
static int      ss_on=0;

/* Estrellas volando (starfield) */
typedef struct { int x,y,z; } star_t;
static star_t stars[N_STARS];
static uint32_t frame=0;

/* Bolas rebotando */
typedef struct { int x,y,vx,vy; uint32_t color; int r; } ball_t;
static ball_t balls[N_BALLS];

static uint32_t rng_state=12345;
static uint32_t rng(void){ rng_state=rng_state*1664525+1013904223; return rng_state; }

void screensaver_init(void){
    last_activity=timer_ticks(); ss_on=0; frame=0;
    int sw=fb_width(), sh=fb_height();
    for(int i=0;i<N_STARS;i++){
        stars[i].x=(int)(rng()%(uint32_t)sw)-(sw/2);
        stars[i].y=(int)(rng()%(uint32_t)sh)-(sh/2);
        stars[i].z=(int)(rng()%100)+1;
    }
    for(int i=0;i<N_BALLS;i++){
        balls[i].x=(int)(rng()%(uint32_t)(sw-40))+20;
        balls[i].y=(int)(rng()%(uint32_t)(sh-40))+20;
        balls[i].vx=((int)(rng()%6))-3; if(balls[i].vx==0)balls[i].vx=2;
        balls[i].vy=((int)(rng()%6))-3; if(balls[i].vy==0)balls[i].vy=2;
        balls[i].r=10+(int)(rng()%12);
        balls[i].color=fb_color((uint8_t)(rng()%200+55),(uint8_t)(rng()%200+55),(uint8_t)(rng()%200+55));
    }
}

void screensaver_reset(void){ last_activity=timer_ticks(); if(ss_on){ ss_on=0; } }
int  screensaver_active(void){ return ss_on; }

void screensaver_tick(void){
    if(!ss_on && timer_ticks()-last_activity > SS_TIMEOUT){ ss_on=1; }
    if(!ss_on) return;
    frame++;
    /* Update balls */
    int sw=fb_width(), sh=fb_height();
    for(int i=0;i<N_BALLS;i++){
        balls[i].x+=balls[i].vx; balls[i].y+=balls[i].vy;
        if(balls[i].x-balls[i].r<0||balls[i].x+balls[i].r>=sw){ balls[i].vx=-balls[i].vx; balls[i].x+=balls[i].vx*2; }
        if(balls[i].y-balls[i].r<0||balls[i].y+balls[i].r>=sh){ balls[i].vy=-balls[i].vy; balls[i].y+=balls[i].vy*2; }
    }
}

void screensaver_draw(void){
    if(!ss_on) return;
    int sw=fb_width(), sh=fb_height();
    /* Fondo negro */
    fb_fill_rect(0,0,sw,sh,fb_color(0,0,0));

    /* Starfield */
    int cx=sw/2, cy=sh/2;
    for(int i=0;i<N_STARS;i++){
        int px=stars[i].x*256/stars[i].z+cx;
        int py=stars[i].y*256/stars[i].z+cy;
        stars[i].z-=2; if(stars[i].z<=0){ stars[i].z=100; stars[i].x=(int)(rng()%(uint32_t)sw)-cx; stars[i].y=(int)(rng()%(uint32_t)sh)-cy; }
        int bright=255-stars[i].z*2; if(bright<50)bright=50;
        uint32_t sc=fb_color((uint8_t)bright,(uint8_t)bright,(uint8_t)bright);
        int sz=(bright>200)?2:1;
        fb_fill_rect(px-sz/2,py-sz/2,sz,sz,sc);
    }

    /* Bolas rebotando */
    for(int i=0;i<N_BALLS;i++){
        /* Sombra */
        fb_fill_circle(balls[i].x+3,balls[i].y+3,balls[i].r,fb_color(0x10,0x10,0x10));
        fb_fill_circle(balls[i].x,balls[i].y,balls[i].r,balls[i].color);
        /* Brillo */
        fb_fill_circle(balls[i].x-balls[i].r/3,balls[i].y-balls[i].r/3,balls[i].r/4,fb_color(0xff,0xff,0xff));
    }

    /* Texto "CoreM" pulsante */
    int alpha=(int)(frame%60); if(alpha>30)alpha=60-alpha;
    uint32_t tc=fb_color((uint8_t)(alpha*8),(uint8_t)(alpha*5),(uint8_t)255);
    fb_draw_str_scaled(sw/2-45,sh-40,"CoreM OS",tc,0,2);
    fb_draw_str(sw/2-80,sh-16,"Mueve el mouse para continuar",fb_color(0x44,0x44,0x66),0);
}
