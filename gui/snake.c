#include "snake.h"
#include "gui.h"
#include "framebuffer.h"
#include "sound.h"
#include <stdint.h>

#define GRID_W 22
#define GRID_H 16
#define CELL   14
#define MAX_LEN (GRID_W*GRID_H)

typedef struct { int x,y; } pt_t;

static pt_t snake[MAX_LEN];
static int  snake_len;
static int  dir_x, dir_y;
static int  next_dir_x, next_dir_y;
static pt_t food;
static int  score;
static int  game_over;
static uint32_t rng = 7919;
static int  tick_counter = 0;
#define TICK_SPEED 6  /* frames del loop principal por movimiento */

static uint32_t rnd(void){ rng=rng*1103515245+12345; return rng; }

static int collides_self(int x,int y){
    for(int i=0;i<snake_len;i++) if(snake[i].x==x&&snake[i].y==y) return 1;
    return 0;
}

static void place_food(void){
    do {
        food.x = rnd()%GRID_W;
        food.y = rnd()%GRID_H;
    } while(collides_self(food.x,food.y));
}

void snake_restart(void){
    snake_len=3;
    snake[0].x=10; snake[0].y=8;
    snake[1].x=9;  snake[1].y=8;
    snake[2].x=8;  snake[2].y=8;
    dir_x=1; dir_y=0;
    next_dir_x=1; next_dir_y=0;
    score=0;
    game_over=0;
    tick_counter=0;
    place_food();
}

void snake_init(void){ snake_restart(); }

void snake_key(int dir){
    if(game_over) return;
    /* 0=up 1=down 2=left 3=right, evitando giro de 180 */
    if(dir==0 && dir_y!=1){ next_dir_x=0; next_dir_y=-1; }
    else if(dir==1 && dir_y!=-1){ next_dir_x=0; next_dir_y=1; }
    else if(dir==2 && dir_x!=1){ next_dir_x=-1; next_dir_y=0; }
    else if(dir==3 && dir_x!=-1){ next_dir_x=1; next_dir_y=0; }
}

void snake_tick(void){
    if(game_over) return;
    tick_counter++;
    if(tick_counter < TICK_SPEED) return;
    tick_counter = 0;

    dir_x=next_dir_x; dir_y=next_dir_y;
    int newx = snake[0].x + dir_x;
    int newy = snake[0].y + dir_y;

    if(newx<0||newx>=GRID_W||newy<0||newy>=GRID_H){ game_over=1; sound_beep(180,300); return; }
    if(collides_self(newx,newy)){ game_over=1; sound_beep(180,300); return; }

    int ate = (newx==food.x && newy==food.y);

    for(int i=snake_len;i>0;i--) snake[i]=snake[i-1];
    snake[0].x=newx; snake[0].y=newy;

    if(ate){
        snake_len++;
        score+=10;
        sound_beep(900,40);
        place_food();
    }
}

void snake_draw(int wx,int wy,int ww,int wh){
    (void)ww;(void)wh;
    int ox=wx+BORDER+10, oy=wy+TITLEBAR_H+30;
    uint32_t bg = fb_color(0x10,0x18,0x10);
    fb_fill_rect(ox,oy,GRID_W*CELL,GRID_H*CELL,bg);
    fb_draw_rect(ox,oy,GRID_W*CELL,GRID_H*CELL,fb_color(0x44,0x88,0x44));

    /* comida */
    fb_fill_rect(ox+food.x*CELL+2,oy+food.y*CELL+2,CELL-4,CELL-4,fb_color(0xff,0x55,0x55));

    /* serpiente */
    for(int i=0;i<snake_len;i++){
        uint32_t c = (i==0) ? fb_color(0x55,0xff,0x55) : fb_color(0x22,0xcc,0x44);
        fb_fill_rect(ox+snake[i].x*CELL+1,oy+snake[i].y*CELL+1,CELL-2,CELL-2,c);
    }

    /* score */
    fb_draw_str(wx+BORDER+10,wy+TITLEBAR_H+8,"Puntaje:",fb_color(0x22,0x22,0x44),fb_color(0xf2,0xf2,0xf2));
    fb_draw_int(wx+BORDER+90,wy+TITLEBAR_H+8,score,fb_color(0x22,0x22,0x44),fb_color(0xf2,0xf2,0xf2));

    if(game_over){
        int cx=ox+GRID_W*CELL/2, cy=oy+GRID_H*CELL/2;
        fb_fill_rect(cx-90,cy-20,180,50,fb_color(0x00,0x00,0x00));
        fb_draw_rect(cx-90,cy-20,180,50,fb_color(0xff,0x55,0x55));
        fb_draw_str(cx-60,cy-12,"GAME OVER",fb_color(0xff,0x88,0x88),fb_color(0x00,0x00,0x00));
        fb_draw_str(cx-72,cy+6,"Click para reiniciar",fb_color(0xcc,0xcc,0xcc),fb_color(0x00,0x00,0x00));
    }
}
