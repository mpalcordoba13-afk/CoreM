#include "tetris.h"
#include "gui.h"
#include "framebuffer.h"
#include "sound.h"
#include <stdint.h>

#define TW 10
#define TH 18
#define CELL 16

static int board[TH][TW]; /* 0 = vacio, 1..7 = color de pieza */

typedef struct { int cells[4][2]; } shape_t;

/* 7 piezas: I O T S Z J L, rotacion 0 (base) */
static const int shapes[7][4][2] = {
    {{0,0},{1,0},{2,0},{3,0}}, /* I */
    {{0,0},{1,0},{0,1},{1,1}}, /* O */
    {{0,0},{1,0},{2,0},{1,1}}, /* T */
    {{1,0},{2,0},{0,1},{1,1}}, /* S */
    {{0,0},{1,0},{1,1},{2,1}}, /* Z */
    {{0,0},{0,1},{1,1},{2,1}}, /* J */
    {{2,0},{0,1},{1,1},{2,1}}, /* L */
};

static int cur_shape;
static int cur_x, cur_y;
static int cur_cells[4][2];
static int score, level, lines_cleared;
static int game_over;
static uint32_t rng = 424243;
static int tick_counter = 0;
static int tick_speed = 14;

static uint32_t rnd(void){ rng=rng*1103515245+12345; return rng; }

static void load_shape(int s){
    cur_shape=s;
    for(int i=0;i<4;i++){ cur_cells[i][0]=shapes[s][i][0]; cur_cells[i][1]=shapes[s][i][1]; }
    cur_x = TW/2-2;
    cur_y = 0;
}

static int check_collision(int dx,int dy,int rotated[4][2]){
    for(int i=0;i<4;i++){
        int x = cur_x+rotated[i][0]+dx;
        int y = cur_y+rotated[i][1]+dy;
        if(x<0||x>=TW||y>=TH) return 1;
        if(y>=0 && board[y][x]) return 1;
    }
    return 0;
}

static void rotate_piece(void){
    if(cur_shape==1) return; /* O no rota */
    int rotated[4][2];
    /* rotacion simple alrededor del primer bloque */
    int px=cur_cells[0][0], py=cur_cells[0][1];
    for(int i=0;i<4;i++){
        int rx = cur_cells[i][1]-py;
        int ry = -(cur_cells[i][0]-px);
        rotated[i][0]=px+rx;
        rotated[i][1]=py+ry;
    }
    if(!check_collision(0,0,rotated)){
        for(int i=0;i<4;i++){ cur_cells[i][0]=rotated[i][0]; cur_cells[i][1]=rotated[i][1]; }
    }
}

static void lock_piece(void){
    for(int i=0;i<4;i++){
        int x=cur_x+cur_cells[i][0], y=cur_y+cur_cells[i][1];
        if(y>=0&&y<TH&&x>=0&&x<TW) board[y][x]=cur_shape+1;
    }
    /* limpiar lineas completas */
    int cleared=0;
    for(int y=TH-1;y>=0;y--){
        int full=1;
        for(int x=0;x<TW;x++) if(!board[y][x]){ full=0; break; }
        if(full){
            cleared++;
            for(int yy=y;yy>0;yy--)
                for(int x=0;x<TW;x++) board[yy][x]=board[yy-1][x];
            for(int x=0;x<TW;x++) board[0][x]=0;
            y++; /* re-chequear misma fila */
        }
    }
    if(cleared){
        const int pts[5]={0,40,100,300,1200};
        score += pts[cleared]*(level+1);
        lines_cleared += cleared;
        level = lines_cleared/10;
        if(tick_speed>4) tick_speed = 14 - level;
        if(tick_speed<4) tick_speed=4;
        sound_beep(700+cleared*100,80);
    }
    load_shape(rnd()%7);
    if(check_collision(0,0,cur_cells)){
        game_over=1;
        sound_beep(180,400);
    }
}

void tetris_restart(void){
    for(int y=0;y<TH;y++) for(int x=0;x<TW;x++) board[y][x]=0;
    score=0; level=0; lines_cleared=0; game_over=0; tick_speed=14; tick_counter=0;
    load_shape(rnd()%7);
}

void tetris_init(void){ tetris_restart(); }

void tetris_key(int action){
    if(game_over) return;
    if(action==0){ if(!check_collision(-1,0,cur_cells)) cur_x--; }
    else if(action==1){ if(!check_collision(1,0,cur_cells)) cur_x++; }
    else if(action==2){ rotate_piece(); }
    else if(action==3){ if(!check_collision(0,1,cur_cells)) cur_y++; else lock_piece(); }
    else if(action==4){
        while(!check_collision(0,1,cur_cells)) cur_y++;
        lock_piece();
        sound_beep(500,30);
    }
}

void tetris_tick(void){
    if(game_over) return;
    tick_counter++;
    if(tick_counter < tick_speed) return;
    tick_counter=0;
    if(!check_collision(0,1,cur_cells)) cur_y++;
    else lock_piece();
}

static uint32_t piece_color(int v){
    static const uint32_t cols[8] = {
        0, /* vacio */
    };
    (void)cols;
    switch(v){
        case 1: return fb_color(0x00,0xcc,0xcc); /* I cyan */
        case 2: return fb_color(0xcc,0xcc,0x00); /* O amarillo */
        case 3: return fb_color(0xaa,0x33,0xcc); /* T purpura */
        case 4: return fb_color(0x33,0xcc,0x33); /* S verde */
        case 5: return fb_color(0xcc,0x33,0x33); /* Z rojo */
        case 6: return fb_color(0x33,0x55,0xcc); /* J azul */
        case 7: return fb_color(0xcc,0x77,0x22); /* L naranja */
        default: return fb_color(0,0,0);
    }
}

void tetris_draw(int wx,int wy,int ww,int wh){
    (void)ww;(void)wh;
    int ox=wx+BORDER+10, oy=wy+TITLEBAR_H+10;
    uint32_t bg=fb_color(0x10,0x10,0x18);
    fb_fill_rect(ox,oy,TW*CELL,TH*CELL,bg);
    fb_draw_rect(ox,oy,TW*CELL,TH*CELL,fb_color(0x66,0x66,0x99));

    for(int y=0;y<TH;y++)
        for(int x=0;x<TW;x++)
            if(board[y][x])
                fb_fill_rect(ox+x*CELL+1,oy+y*CELL+1,CELL-2,CELL-2,piece_color(board[y][x]));

    if(!game_over){
        uint32_t pc = piece_color(cur_shape+1);
        for(int i=0;i<4;i++){
            int x=cur_x+cur_cells[i][0], y=cur_y+cur_cells[i][1];
            if(y>=0) fb_fill_rect(ox+x*CELL+1,oy+y*CELL+1,CELL-2,CELL-2,pc);
        }
    }

    int px = ox+TW*CELL+20;
    fb_draw_str(px,oy,"Puntaje:",fb_color(0x22,0x22,0x44),fb_color(0xf2,0xf2,0xf2));
    fb_draw_int(px,oy+16,score,fb_color(0x22,0x22,0x44),fb_color(0xf2,0xf2,0xf2));
    fb_draw_str(px,oy+40,"Nivel:",fb_color(0x22,0x22,0x44),fb_color(0xf2,0xf2,0xf2));
    fb_draw_int(px,oy+56,level,fb_color(0x22,0x22,0x44),fb_color(0xf2,0xf2,0xf2));
    fb_draw_str(px,oy+80,"Lineas:",fb_color(0x22,0x22,0x44),fb_color(0xf2,0xf2,0xf2));
    fb_draw_int(px,oy+96,lines_cleared,fb_color(0x22,0x22,0x44),fb_color(0xf2,0xf2,0xf2));

    fb_draw_str(px,oy+130,"Flechas: mover",fb_color(0x55,0x55,0x77),fb_color(0xf2,0xf2,0xf2));
    fb_draw_str(px,oy+146,"Arriba: rotar",fb_color(0x55,0x55,0x77),fb_color(0xf2,0xf2,0xf2));
    fb_draw_str(px,oy+162,"Espacio: caer",fb_color(0x55,0x55,0x77),fb_color(0xf2,0xf2,0xf2));

    if(game_over){
        int cx=ox+TW*CELL/2, cy=oy+TH*CELL/2;
        fb_fill_rect(cx-90,cy-20,180,50,fb_color(0x00,0x00,0x00));
        fb_draw_rect(cx-90,cy-20,180,50,fb_color(0xff,0x55,0x55));
        fb_draw_str(cx-60,cy-12,"GAME OVER",fb_color(0xff,0x88,0x88),fb_color(0x00,0x00,0x00));
        fb_draw_str(cx-72,cy+6,"Click para reiniciar",fb_color(0xcc,0xcc,0xcc),fb_color(0x00,0x00,0x00));
    }
}
