#include "game2048.h"
#include "framebuffer.h"
#include "gui.h"
#include <stdint.h>

static int board[4][4];
static int score=0, best=0;
static int game_over=0, won=0;
static uint32_t rng_s=777;
static uint32_t rng(void){ rng_s=rng_s*1664525+1013904223; return rng_s; }

static void add_tile(void){
    int empty[16],n=0;
    for(int r=0;r<4;r++) for(int c=0;c<4;c++) if(!board[r][c]) empty[n++]=(r<<4)|c;
    if(!n) return;
    int idx=empty[rng()%n];
    board[idx>>4][idx&0xf]=(rng()%10<9)?2:4;
}

void game2048_init(void){
    for(int r=0;r<4;r++) for(int c=0;c<4;c++) board[r][c]=0;
    score=0; game_over=0; won=0; add_tile(); add_tile();
}

static int slide_row(int row[4]){
    int tmp[4]={0,0,0,0},n=0,moved=0;
    for(int i=0;i<4;i++) if(row[i]) tmp[n++]=row[i];
    for(int i=0;i<n-1;i++) if(tmp[i]==tmp[i+1]){ tmp[i]*=2; score+=tmp[i]; if(tmp[i]==2048)won=1; tmp[i+1]=0; i++; }
    int out[4]={0,0,0,0},k=0;
    for(int i=0;i<4;i++) if(tmp[i]) out[k++]=tmp[i];
    for(int i=0;i<4;i++){ if(row[i]!=out[i]) moved=1; row[i]=out[i]; }
    return moved;
}

void game2048_key(int key){
    if(game_over) return;
    int moved=0, tmp[4];
    if(key==2){ /* left */
        for(int r=0;r<4;r++) moved|=slide_row(board[r]);
    } else if(key==3){ /* right */
        for(int r=0;r<4;r++){ for(int c=0;c<4;c++)tmp[c]=board[r][3-c]; moved|=slide_row(tmp); for(int c=0;c<4;c++)board[r][3-c]=tmp[c]; }
    } else if(key==0){ /* up */
        for(int c=0;c<4;c++){ for(int r=0;r<4;r++)tmp[r]=board[r][c]; moved|=slide_row(tmp); for(int r=0;r<4;r++)board[r][c]=tmp[r]; }
    } else if(key==1){ /* down */
        for(int c=0;c<4;c++){ for(int r=0;r<4;r++)tmp[r]=board[3-r][c]; moved|=slide_row(tmp); for(int r=0;r<4;r++)board[3-r][c]=tmp[r]; }
    }
    if(moved) add_tile();
    if(score>best) best=score;
    /* Check game over */
    int has_empty=0;
    for(int r=0;r<4;r++) for(int c=0;c<4;c++){
        if(!board[r][c]){has_empty=1;break;}
        if(c<3&&board[r][c]==board[r][c+1]){has_empty=1;break;}
        if(r<3&&board[r][c]==board[r+1][c]){has_empty=1;break;}
    }
    if(!has_empty) game_over=1;
}

static uint32_t tile_color(int v){
    switch(v){
    case 2:    return fb_color(0xee,0xe4,0xda);
    case 4:    return fb_color(0xed,0xe0,0xc8);
    case 8:    return fb_color(0xf2,0xb1,0x79);
    case 16:   return fb_color(0xf5,0x95,0x63);
    case 32:   return fb_color(0xf6,0x7c,0x5f);
    case 64:   return fb_color(0xf6,0x5e,0x3b);
    case 128:  return fb_color(0xed,0xcf,0x72);
    case 256:  return fb_color(0xed,0xcc,0x61);
    case 512:  return fb_color(0xed,0xc8,0x50);
    case 1024: return fb_color(0xed,0xc5,0x3f);
    case 2048: return fb_color(0xed,0xc2,0x2e);
    default:   return fb_color(0x3c,0x3a,0x32);
    }
}

static void itoa10(int n,char*b){if(n==0){b[0]='0';b[1]='\0';return;}char t[10];int i=0;while(n>0){t[i++]='0'+n%10;n/=10;}int p=0;while(i>0)b[p++]=t[--i];b[p]='\0';}
static int slen(const char*s){int n=0;while(s[n])n++;return n;}

void game2048_draw(int wx,int wy,int ww,int wh){
    uint32_t bg   =fb_color(0xfa,0xf8,0xef);
    uint32_t grid =fb_color(0xbb,0xad,0xa0);
    uint32_t dark =fb_color(0x77,0x6e,0x65);
    uint32_t white=fb_color(0xff,0xff,0xff);
    int ox=wx+BORDER+8, oy=wy+TITLEBAR_H+8;
    int pw=ww-BORDER*2-16;
    fb_fill_rect(wx+BORDER,wy+TITLEBAR_H,ww-BORDER*2,wh-TITLEBAR_H-BORDER,bg);

    /* Score */
    fb_fill_rect(ox+pw-160,oy,76,40,grid);
    fb_draw_str(ox+pw-156,oy+4,"SCORE",white,grid);
    char sb[12]; itoa10(score,sb);
    fb_draw_str(ox+pw-80-slen(sb)*9/2,oy+18,sb,white,grid);
    fb_fill_rect(ox+pw-80,oy,76,40,grid);
    fb_draw_str(ox+pw-76,oy+4,"MEJOR",white,grid);
    char bb[12]; itoa10(best,bb);
    fb_draw_str(ox+pw-4-slen(bb)*9,oy+18,bb,white,grid);

    /* Título */
    fb_draw_str_scaled(ox,oy,game_over?"GAME OVER":(won?"GANASTE!":"2048"),dark,bg,2);

    /* Botón nuevo */
    fb_fill_rect(ox,oy+24,80,16,grid);
    fb_draw_str(ox+4,oy+27,"Nuevo juego",white,grid);

    /* Grid */
    int gy=oy+52;
    int cell=(pw-10)/4; if(cell>100)cell=100;
    int gw=cell*4+10;
    fb_fill_rect(ox,gy,gw,gw,grid);
    for(int r=0;r<4;r++) for(int c=0;c<4;c++){
        int cx2=ox+2+c*(cell+2), cy2=gy+2+r*(cell+2);
        int v=board[r][c];
        uint32_t cbg=v?tile_color(v):fb_color(0xcd,0xc1,0xb4);
        fb_fill_rect(cx2,cy2,cell,cell,cbg);
        if(v){
            char nb[8]; itoa10(v,nb);
            int nl=slen(nb);
            int scale=(v>=1000)?1:(v>=100?1:2);
            int tx=cx2+cell/2-nl*9*scale/2;
            int ty=cy2+cell/2-8*scale/2;
            uint32_t tc=(v<=4)?dark:white;
            fb_draw_str_scaled(tx,ty,nb,tc,cbg,scale);
        }
    }
    /* Instrucciones */
    fb_draw_str(ox,gy+gw+6,"WASD o flechas para mover",dark,bg);
    (void)wh;
}

int game2048_click(int wx,int wy,int ww,int wh,int mx,int my){
    int ox=wx+BORDER+8, oy=wy+TITLEBAR_H+8;
    if(mx>=ox&&mx<ox+80&&my>=oy+24&&my<oy+40){ game2048_init(); }
    (void)ww;(void)wh;
    return 0;
}
