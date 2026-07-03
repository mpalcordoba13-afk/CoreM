#include "minesweeper.h"
#include "framebuffer.h"
#include "gui.h"
#include "timer.h"
#include <stdint.h>

#define MS_COLS 16
#define MS_ROWS 16
#define MS_MINES 30
#define CELL 20

typedef struct { int mine,revealed,flagged,adj; } ms_cell_t;
static ms_cell_t board[MS_ROWS][MS_COLS];
static int ms_state=0; /* 0=playing,1=won,2=lost */
static int cells_left=0;
static uint32_t start_tick=0;
static int initialized_board=0;

static uint32_t rng_s=99991;
static uint32_t rng(void){ rng_s=rng_s*6364136223846793005ULL+1442695040888963407ULL; return rng_s; }

static void ms_reset(void){
    for(int r=0;r<MS_ROWS;r++) for(int c=0;c<MS_COLS;c++){
        board[r][c].mine=board[r][c].revealed=board[r][c].flagged=board[r][c].adj=0;
    }
    int placed=0;
    while(placed<MS_MINES){
        int r=(int)(rng()%MS_ROWS),c=(int)(rng()%MS_COLS);
        if(!board[r][c].mine){ board[r][c].mine=1; placed++; }
    }
    for(int r=0;r<MS_ROWS;r++) for(int c=0;c<MS_COLS;c++){
        if(board[r][c].mine) continue;
        int cnt=0;
        for(int dr=-1;dr<=1;dr++) for(int dc=-1;dc<=1;dc++){
            int nr=r+dr,nc=c+dc;
            if(nr>=0&&nr<MS_ROWS&&nc>=0&&nc<MS_COLS&&board[nr][nc].mine) cnt++;
        }
        board[r][c].adj=cnt;
    }
    ms_state=0; cells_left=MS_ROWS*MS_COLS-MS_MINES;
    start_tick=timer_ticks(); initialized_board=1;
}

static void ms_reveal(int r,int c){
    if(r<0||r>=MS_ROWS||c<0||c>=MS_COLS) return;
    if(board[r][c].revealed||board[r][c].flagged) return;
    board[r][c].revealed=1; cells_left--;
    if(board[r][c].adj==0&&!board[r][c].mine)
        for(int dr=-1;dr<=1;dr++) for(int dc=-1;dc<=1;dc++) ms_reveal(r+dr,c+dc);
}

void minesweeper_init(void){ initialized_board=0; ms_reset(); }

void minesweeper_draw(int wx,int wy,int ww,int wh){
    if(!initialized_board) ms_reset();
    uint32_t bg=fb_color(0xc0,0xc0,0xc0);
    uint32_t hidden=fb_color(0xaa,0xaa,0xaa);
    uint32_t revealed_bg=fb_color(0xe0,0xe0,0xe0);
    uint32_t mine_col=fb_color(0xff,0x00,0x00);
    uint32_t flag_col=fb_color(0xff,0x44,0x00);
    uint32_t border_l=fb_color(0xff,0xff,0xff), border_d=fb_color(0x55,0x55,0x55);
    uint32_t num_cols[]={0,fb_color(0x00,0x00,0xff),fb_color(0x00,0x88,0x00),fb_color(0xff,0x00,0x00),
                           fb_color(0x00,0x00,0x88),fb_color(0x88,0x00,0x00),fb_color(0x00,0x88,0x88),
                           fb_color(0x00,0x00,0x00),fb_color(0x88,0x88,0x88)};

    int ox=wx+BORDER+4, oy=wy+TITLEBAR_H+4;
    int pw=ww-BORDER*2-8;
    fb_fill_rect(wx+BORDER,wy+TITLEBAR_H,ww-BORDER*2,wh-TITLEBAR_H-BORDER,bg);

    /* Header */
    fb_fill_rect(ox,oy,pw,28,bg);
    fb_draw_rect(ox,oy,pw,28,border_d);
    /* Contador minas */
    int flags=0; for(int r=0;r<MS_ROWS;r++) for(int c=0;c<MS_COLS;c++) if(board[r][c].flagged) flags++;
    char mc[8]; int mv=MS_MINES-flags; if(mv<0)mv=0;
    mc[0]='0'+(mv/100)%10; mc[1]='0'+(mv/10)%10; mc[2]='0'+mv%10; mc[3]='\0';
    fb_fill_rect(ox+4,oy+4,32,20,fb_color(0x00,0x00,0x00));
    fb_draw_str(ox+6,oy+8,mc,fb_color(0xff,0x00,0x00),fb_color(0,0,0));
    /* Tiempo */
    uint32_t elapsed=(ms_state==0)?(timer_ticks()-start_tick)/60:0;
    if(elapsed>999)elapsed=999;
    char tc[8]; tc[0]='0'+(elapsed/100)%10; tc[1]='0'+(elapsed/10)%10; tc[2]='0'+elapsed%10; tc[3]='\0';
    fb_fill_rect(ox+pw-36,oy+4,32,20,fb_color(0,0,0));
    fb_draw_str(ox+pw-34,oy+8,tc,fb_color(0xff,0x00,0x00),fb_color(0,0,0));
    /* Botón reset */
    int bx=ox+pw/2-12, by=oy+4;
    fb_fill_rect(bx,by,24,20,bg);
    fb_draw_rect(bx,by,24,20,border_l);
    const char *face=(ms_state==1)?"=D":(ms_state==2)?"x_":":)";
    fb_draw_str(bx+3,by+6,face,fb_color(0,0,0),bg);

    /* Grid */
    int gy=oy+34;
    for(int r=0;r<MS_ROWS;r++){
        for(int c=0;c<MS_COLS;c++){
            int cx2=ox+c*CELL, cy2=gy+r*CELL;
            ms_cell_t *cell=&board[r][c];
            if(cell->revealed){
                fb_fill_rect(cx2,cy2,CELL,CELL,revealed_bg);
                fb_draw_rect(cx2,cy2,CELL,CELL,border_d);
                if(cell->mine){
                    fb_fill_circle(cx2+CELL/2,cy2+CELL/2,CELL/2-3,mine_col);
                } else if(cell->adj>0){
                    char nb[3]; nb[0]='0'+cell->adj; nb[1]='\0';
                    fb_draw_str(cx2+6,cy2+6,nb,num_cols[cell->adj],revealed_bg);
                }
            } else {
                fb_fill_rect(cx2,cy2,CELL,CELL,hidden);
                fb_fill_rect(cx2,cy2,CELL,1,border_l);
                fb_fill_rect(cx2,cy2,1,CELL,border_l);
                fb_fill_rect(cx2+CELL-1,cy2,1,CELL,border_d);
                fb_fill_rect(cx2,cy2+CELL-1,CELL,1,border_d);
                if(cell->flagged){
                    fb_fill_rect(cx2+7,cy2+4,2,10,fb_color(0x44,0x22,0x00));
                    fb_fill_rect(cx2+5,cy2+4,6,6,flag_col);
                }
            }
            (void)border_l;
        }
    }
    /* Estado */
    if(ms_state==1) fb_draw_str(ox,gy+MS_ROWS*CELL+4,"GANASTE!",fb_color(0,0x88,0),bg);
    if(ms_state==2) fb_draw_str(ox,gy+MS_ROWS*CELL+4,"PERDISTE!",fb_color(0xff,0,0),bg);
    (void)ww;(void)wh;
}

int minesweeper_click(int wx,int wy,int ww,int wh,int mx,int my,int right){
    int ox=wx+BORDER+4, oy=wy+TITLEBAR_H+4;
    int pw=ww-BORDER*2-8;
    /* Reset button */
    int bx=ox+pw/2-12,by=oy+4;
    if(mx>=bx&&mx<bx+24&&my>=by&&my<by+20){ ms_reset(); return 0; }
    if(ms_state!=0) return 0;
    int gy=oy+34;
    int c=(mx-ox)/CELL, r=(my-gy)/CELL;
    if(c<0||c>=MS_COLS||r<0||r>=MS_ROWS) return 0;
    if(right){
        if(!board[r][c].revealed) board[r][c].flagged=!board[r][c].flagged;
    } else {
        if(board[r][c].flagged) return 0;
        if(board[r][c].mine){ board[r][c].revealed=1; ms_state=2;
            for(int rr=0;rr<MS_ROWS;rr++) for(int cc=0;cc<MS_COLS;cc++) if(board[rr][cc].mine) board[rr][cc].revealed=1;
        } else {
            ms_reveal(r,c);
            if(cells_left==0) ms_state=1;
        }
    }
    (void)ww;(void)wh;
    return 0;
}
