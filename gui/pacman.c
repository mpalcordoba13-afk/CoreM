#include "pacman.h"
#include "gui.h"
#include "framebuffer.h"
#include "sound.h"
#include <stdint.h>

#define MAP_W  19
#define MAP_H  21
#define CELL   14
#define GHOST_COUNT 4

/* 0=vacio 1=pared 2=punto 3=power */
static const uint8_t map_template[MAP_H][MAP_W] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,2,2,2,2,2,2,2,2,1,2,2,2,2,2,2,2,2,1},
    {1,3,1,1,2,1,1,1,2,1,2,1,1,1,2,1,1,3,1},
    {1,2,1,1,2,1,1,1,2,1,2,1,1,1,2,1,1,2,1},
    {1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1},
    {1,2,1,1,2,1,2,1,1,1,1,1,2,1,2,1,1,2,1},
    {1,2,2,2,2,1,2,2,2,1,2,2,2,1,2,2,2,2,1},
    {1,1,1,1,2,1,1,1,0,0,0,1,1,1,2,1,1,1,1},
    {1,1,1,1,2,1,0,0,0,0,0,0,0,1,2,1,1,1,1},
    {1,1,1,1,2,1,0,1,1,0,1,1,0,1,2,1,1,1,1},
    {0,0,0,0,2,0,0,1,0,0,0,1,0,0,2,0,0,0,0},
    {1,1,1,1,2,1,0,1,1,1,1,1,0,1,2,1,1,1,1},
    {1,1,1,1,2,1,0,0,0,0,0,0,0,1,2,1,1,1,1},
    {1,1,1,1,2,1,0,1,1,1,1,1,0,1,2,1,1,1,1},
    {1,2,2,2,2,2,2,2,2,1,2,2,2,2,2,2,2,2,1},
    {1,2,1,1,2,1,1,1,2,1,2,1,1,1,2,1,1,2,1},
    {1,3,2,1,2,2,2,2,2,0,2,2,2,2,2,1,2,3,1},
    {1,1,2,1,2,1,2,1,1,1,1,1,2,1,2,1,2,1,1},
    {1,2,2,2,2,1,2,2,2,1,2,2,2,1,2,2,2,2,1},
    {1,2,1,1,1,1,1,1,2,1,2,1,1,1,1,1,1,2,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};

static uint8_t map[MAP_H][MAP_W];
static int total_dots;

/* Pacman */
static int px, py;          /* posicion (celda) */
static int pdx, pdy;        /* direccion actual */
static int next_dx, next_dy;/* siguiente direccion pedida */
static int pmove_tick;      /* subtick de movimiento */
static int pmouth;          /* animacion boca */

/* Fantasmas */
typedef struct {
    int x, y;
    int dx, dy;
    int scared;
    uint32_t color;
} ghost_t;

static ghost_t ghosts[GHOST_COUNT];
static int scared_timer;
static int score;
static int lives;
static int game_over;
static int win;
static int tick_cnt;
#define PACMAN_SPEED 6
#define GHOST_SPEED  10
#define SCARED_TIME  80

static uint32_t rng2 = 31337;
static uint32_t rnd2(void){ rng2=rng2*1103515245+12345; return rng2; }

static int can_move(int x, int y){
    if(x<0||x>=MAP_W||y<0||y>=MAP_H) return 0;
    return map[y][x] != 1;
}

static void reset_positions(void){
    px=9; py=16; pdx=0; pdy=0; next_dx=-1; next_dy=0;
    pmove_tick=0; pmouth=0;

    static const int gx[4]={8,9,10,9};
    static const int gy[4]={9,9,9,10};
    static const uint32_t gc[4]={
        0xFF2222FF, 0xFF44FFFF, 0xFFFF88FF, 0xFFFFAA44
    };
    for(int i=0;i<GHOST_COUNT;i++){
        ghosts[i].x=gx[i]; ghosts[i].y=gy[i];
        ghosts[i].dx=0; ghosts[i].dy=0;
        ghosts[i].scared=0;
        ghosts[i].color=gc[i];
    }
    scared_timer=0;
}

static void reset_map(void){
    total_dots=0;
    for(int y=0;y<MAP_H;y++)
        for(int x=0;x<MAP_W;x++){
            map[y][x]=map_template[y][x];
            if(map[y][x]==2||map[y][x]==3) total_dots++;
        }
}

void pacman_restart(void){
    reset_map();
    reset_positions();
    score=0; lives=3; game_over=0; win=0; tick_cnt=0;
}

void pacman_init(void){ pacman_restart(); }

void pacman_key(int dir){
    if(game_over||win) return;
    if(dir==0){ next_dx=0;next_dy=-1; }
    else if(dir==1){ next_dx=0;next_dy=1; }
    else if(dir==2){ next_dx=-1;next_dy=0; }
    else if(dir==3){ next_dx=1;next_dy=0; }
}

static void ghost_move(ghost_t *g){
    /* Intenta ir hacia pacman o aleatoriamente si asustado */
    int dirs[4][2]={{0,-1},{0,1},{-1,0},{1,0}};
    int best=-1, bestd=9999;

    for(int d=0;d<4;d++){
        int nx=g->x+dirs[d][0], ny=g->y+dirs[d][1];
        if(!can_move(nx,ny)) continue;
        /* No ir al revés si es posible */
        if(dirs[d][0]==-g->dx && dirs[d][1]==-g->dy && best>=0) continue;

        int dist;
        if(g->scared){
            /* Huir de pacman */
            int ddx=nx-px, ddy=ny-py;
            dist=-(ddx*ddx+ddy*ddy);
        } else {
            /* Perseguir pacman con algo de azar */
            int ddx=nx-px, ddy=ny-py;
            dist=ddx*ddx+ddy*ddy;
            if(rnd2()%5==0) dist=rnd2()%100;
        }
        if(dist<bestd){ bestd=dist; best=d; }
    }
    if(best>=0){ g->dx=dirs[best][0]; g->dy=dirs[best][1];
                 g->x+=g->dx; g->y+=g->dy; }
}

void pacman_tick(void){
    if(game_over||win) return;
    tick_cnt++;

    /* Mover pacman */
    pmove_tick++;
    if(pmove_tick>=PACMAN_SPEED){
        pmove_tick=0;
        pmouth=(pmouth+1)%4;

        /* Intentar cambio de direccion */
        if(can_move(px+next_dx,py+next_dy)){ pdx=next_dx; pdy=next_dy; }

        if(pdx!=0||pdy!=0){
            int nx=px+pdx, ny=py+pdy;
            /* Tunel lateral */
            if(nx<0) nx=MAP_W-1;
            if(nx>=MAP_W) nx=0;
            if(can_move(nx,ny)){
                px=nx; py=ny;
                if(map[py][px]==2){
                    map[py][px]=0; score+=10;
                    total_dots--;
                    sound_beep(600+(total_dots%3)*100,6);
                } else if(map[py][px]==3){
                    map[py][px]=0; score+=50;
                    total_dots--;
                    scared_timer=SCARED_TIME;
                    for(int i=0;i<GHOST_COUNT;i++) ghosts[i].scared=1;
                    sound_beep(400,50);
                }
                if(total_dots<=0){ win=1; sound_beep(1046,200); }
            }
        }
    }

    /* Temporizador de miedo */
    if(scared_timer>0){
        scared_timer--;
        if(scared_timer==0)
            for(int i=0;i<GHOST_COUNT;i++) ghosts[i].scared=0;
    }

    /* Mover fantasmas */
    if(tick_cnt%GHOST_SPEED==0){
        for(int i=0;i<GHOST_COUNT;i++) ghost_move(&ghosts[i]);
    }

    /* Colision con fantasmas */
    for(int i=0;i<GHOST_COUNT;i++){
        if(ghosts[i].x==px && ghosts[i].y==py){
            if(ghosts[i].scared){
                ghosts[i].x=9; ghosts[i].y=9;
                ghosts[i].scared=0;
                score+=200;
                sound_beep(880,80);
            } else {
                lives--;
                sound_beep(180,300);
                if(lives<=0){ game_over=1; }
                else { reset_positions(); }
            }
        }
    }
}

/* ---- Dibujo ---- */
static void draw_cell(int ox,int oy,int cx,int cy){
    int px2=ox+cx*CELL, py2=oy+cy*CELL;
    switch(map[cy][cx]){
        case 1:
            fb_fill_rect(px2,py2,CELL,CELL,fb_color(0x00,0x00,0xaa));
            fb_draw_rect(px2,py2,CELL,CELL,fb_color(0x22,0x22,0xff));
            break;
        case 2:
            fb_put_pixel(px2+CELL/2,py2+CELL/2,fb_color(0xff,0xee,0xaa));
            fb_put_pixel(px2+CELL/2+1,py2+CELL/2,fb_color(0xff,0xee,0xaa));
            fb_put_pixel(px2+CELL/2,py2+CELL/2+1,fb_color(0xff,0xee,0xaa));
            fb_put_pixel(px2+CELL/2+1,py2+CELL/2+1,fb_color(0xff,0xee,0xaa));
            break;
        case 3:
            fb_fill_circle(px2+CELL/2,py2+CELL/2,4,fb_color(0xff,0xbb,0x00));
            break;
    }
}

void pacman_draw(int wx,int wy,int ww,int wh){
    (void)ww;(void)wh;
    int ox=wx+BORDER+4, oy=wy+TITLEBAR_H+22;

    /* Fondo negro */
    fb_fill_rect(ox,oy,MAP_W*CELL,MAP_H*CELL,fb_color(0x00,0x00,0x00));

    /* Mapa */
    for(int y=0;y<MAP_H;y++)
        for(int x=0;x<MAP_W;x++)
            draw_cell(ox,oy,x,y);

    /* Pacman */
    int pcx=ox+px*CELL+CELL/2, pcy=oy+py*CELL+CELL/2;
    fb_fill_circle(pcx,pcy,CELL/2-1,fb_color(0xff,0xff,0x00));
    /* Boca */
    if(pmouth<2){
        int msize=pmouth*3+1;
        /* Boca segun direccion */
        int mdx=pdx, mdy=pdy;
        if(mdx==0&&mdy==0) mdx=-1;
        for(int i=0;i<msize;i++){
            fb_draw_line(pcx,pcy,
                pcx+mdx*(CELL/2-1)-mdy*i,
                pcy+mdy*(CELL/2-1)+mdx*i,
                fb_color(0x00,0x00,0x00));
            fb_draw_line(pcx,pcy,
                pcx+mdx*(CELL/2-1)+mdy*i,
                pcy+mdy*(CELL/2-1)-mdx*i,
                fb_color(0x00,0x00,0x00));
        }
    }
    /* Ojo */
    fb_fill_circle(pcx-pdy*3-pdx*2,pcy-pdx*3-pdy*2,2,fb_color(0x00,0x00,0x00));

    /* Fantasmas */
    for(int i=0;i<GHOST_COUNT;i++){
        ghost_t *g=&ghosts[i];
        int gcx=ox+g->x*CELL+CELL/2, gcy=oy+g->y*CELL+CELL/2;
        int scared_flash = (scared_timer>0 && scared_timer<20 && (scared_timer/3)%2);
        uint32_t gc2 = g->scared ?
            (scared_flash ? fb_color(0xff,0xff,0xff) : fb_color(0x22,0x22,0xff))
            : fb_color((g->color>>16)&0xFF,(g->color>>8)&0xFF,g->color&0xFF);
        fb_fill_circle(gcx,gcy-1,CELL/2-1,gc2);
        fb_fill_rect(gcx-CELL/2+1,gcy,CELL-2,CELL/2-1,gc2);
        /* Ondas abajo */
        for(int w=0;w<3;w++){
            int wbx=gcx-CELL/2+1+w*(CELL-2)/3;
            fb_fill_circle(wbx+2,gcy+CELL/2-2,2,fb_color(0,0,0));
        }
        /* Ojos */
        if(!g->scared){
            fb_fill_circle(gcx-3,gcy-2,2,fb_color(0xff,0xff,0xff));
            fb_fill_circle(gcx+3,gcy-2,2,fb_color(0xff,0xff,0xff));
            fb_fill_circle(gcx-3+g->dx,gcy-2+g->dy,1,fb_color(0x00,0x00,0xff));
            fb_fill_circle(gcx+3+g->dx,gcy-2+g->dy,1,fb_color(0x00,0x00,0xff));
        }
    }

    /* HUD */
    fb_fill_rect(wx+BORDER,wy+TITLEBAR_H,ww-BORDER*2,20,fb_color(0x00,0x00,0x00));
    fb_draw_str(wx+BORDER+4,wy+TITLEBAR_H+6,"Score:",fb_color(0xff,0xff,0xff),fb_color(0,0,0));
    fb_draw_int(wx+BORDER+56,wy+TITLEBAR_H+6,score,fb_color(0xff,0xff,0x00),fb_color(0,0,0));
    fb_draw_str(wx+BORDER+130,wy+TITLEBAR_H+6,"Vidas:",fb_color(0xff,0xff,0xff),fb_color(0,0,0));
    for(int i=0;i<lives;i++)
        fb_fill_circle(wx+BORDER+186+i*14,wy+TITLEBAR_H+10,5,fb_color(0xff,0xff,0x00));

    if(game_over){
        int cx=ox+MAP_W*CELL/2, cy=oy+MAP_H*CELL/2;
        fb_fill_rect(cx-90,cy-22,180,52,fb_color(0x00,0x00,0x00));
        fb_draw_rect(cx-90,cy-22,180,52,fb_color(0xff,0x00,0x00));
        fb_draw_str(cx-60,cy-14,"GAME OVER",fb_color(0xff,0x44,0x44),fb_color(0,0,0));
        fb_draw_str(cx-72,cy+4,"Click para reiniciar",fb_color(0xcc,0xcc,0xcc),fb_color(0,0,0));
    }
    if(win){
        int cx=ox+MAP_W*CELL/2, cy=oy+MAP_H*CELL/2;
        fb_fill_rect(cx-90,cy-22,180,52,fb_color(0x00,0x00,0x00));
        fb_draw_rect(cx-90,cy-22,180,52,fb_color(0xff,0xff,0x00));
        fb_draw_str(cx-36,cy-14,"GANASTE!",fb_color(0xff,0xff,0x00),fb_color(0,0,0));
        fb_draw_str(cx-72,cy+4,"Click para reiniciar",fb_color(0xcc,0xcc,0xcc),fb_color(0,0,0));
    }
}
