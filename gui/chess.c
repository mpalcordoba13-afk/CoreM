#include "chess.h"
#include "framebuffer.h"
#include "gui.h"
#include <stdint.h>

/* Piezas: mayúscula=blancas, minúscula=negras
   K/k=rey Q/q=reina R/r=torre B/b=alfil N/n=caballo P/p=peón 0=vacío */
static char board[8][8];
static int sel_r=-1,sel_c=-1;
static int turn=0; /* 0=blancas,1=negras */
static char status[48];
static int game_ended=0;

static void scpy(char*d,const char*s,int n){int i=0;while(s[i]&&i<n-1){d[i]=s[i];i++;}d[i]='\0';}

static void chess_reset(void){
    char init[8][8]={
        {'r','n','b','q','k','b','n','r'},
        {'p','p','p','p','p','p','p','p'},
        {0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0},
        {'P','P','P','P','P','P','P','P'},
        {'R','N','B','Q','K','B','N','R'}
    };
    for(int r=0;r<8;r++) for(int c=0;c<8;c++) board[r][c]=init[r][c];
    sel_r=-1; sel_c=-1; turn=0; game_ended=0;
    scpy(status,"Turno: Blancas",48);
}

void chess_init(void){ chess_reset(); }

static int is_white(char p){ return p>='A'&&p<='Z'; }
static int is_black(char p){ return p>='a'&&p<='z'; }
static int is_mine(char p){ return turn==0?is_white(p):is_black(p); }
static int is_enemy(char p){ return turn==0?is_black(p):is_white(p); }

/* Validación de movimiento simplificada */
static int valid_move(int r1,int c1,int r2,int c2){
    if(r2<0||r2>7||c2<0||c2>7) return 0;
    char p=board[r1][c1];
    char t=board[r2][c2];
    if(t&&is_mine(t)) return 0; /* no comer propio */
    char pl=p>='a'?p-32:p; /* pieza normalizada a mayúscula */
    int dr=r2-r1,dc=c2-c1;
    int adr=dr<0?-dr:dr, adc=dc<0?-dc:dc;
    switch(pl){
    case 'P':
        if(turn==0){ /* blancas suben (r decrece) */
            if(dc==0&&dr==-1&&!t) return 1;
            if(dc==0&&dr==-2&&r1==6&&!board[r1-1][c1]&&!t) return 1;
            if(adc==1&&dr==-1&&is_enemy(t)) return 1;
        } else {
            if(dc==0&&dr==1&&!t) return 1;
            if(dc==0&&dr==2&&r1==1&&!board[r1+1][c1]&&!t) return 1;
            if(adc==1&&dr==1&&is_enemy(t)) return 1;
        }
        return 0;
    case 'N': return (adr==2&&adc==1)||(adr==1&&adc==2);
    case 'K': return adr<=1&&adc<=1;
    case 'R':
        if(dr&&dc) return 0;
        if(dr==0){ int s=dc>0?1:-1; for(int i=c1+s;i!=c2;i+=s) if(board[r1][i]) return 0; return 1; }
        { int s=dr>0?1:-1; for(int i=r1+s;i!=r2;i+=s) if(board[i][c1]) return 0; return 1; }
    case 'B':
        if(adr!=adc) return 0;
        { int sr=dr>0?1:-1,sc=dc>0?1:-1; int nr=r1+sr,nc=c1+sc; while(nr!=r2){ if(board[nr][nc]) return 0; nr+=sr;nc+=sc; } return 1; }
    case 'Q':
        if(dr==0||dc==0){ /* como torre */
            if(dr==0){ int s=dc>0?1:-1; for(int i=c1+s;i!=c2;i+=s) if(board[r1][i]) return 0; return 1; }
            int s=dr>0?1:-1; for(int i=r1+s;i!=r2;i+=s) if(board[i][c1]) return 0; return 1;
        }
        if(adr==adc){ int sr=dr>0?1:-1,sc=dc>0?1:-1; int nr=r1+sr,nc=c1+sc; while(nr!=r2){if(board[nr][nc])return 0;nr+=sr;nc+=sc;} return 1; }
        return 0;
    }
    return 0;
}

void chess_draw(int wx,int wy,int ww,int wh){
    int sq=44;
    int ox=wx+BORDER+28, oy=wy+TITLEBAR_H+28;
    uint32_t light=fb_color(0xf0,0xd9,0xb5), dark2=fb_color(0xb5,0x88,0x63);
    uint32_t sel_col=fb_color(0x44,0xff,0x44);
    uint32_t txt=fb_color(0x22,0x22,0x22), bg=fb_color(0xee,0xe8,0xd5);

    fb_fill_rect(wx+BORDER,wy+TITLEBAR_H,ww-BORDER*2,wh-TITLEBAR_H-BORDER,bg);

    /* Letras/números */
    for(int i=0;i<8;i++){
        char lb[2]; lb[0]='A'+i; lb[1]='\0'; fb_draw_str(ox+i*sq+sq/2-4,oy-18,lb,txt,bg);
        char nb[2]; nb[0]='8'-i; nb[1]='\0'; fb_draw_str(ox-18,oy+i*sq+sq/2-6,nb,txt,bg);
    }

    for(int r=0;r<8;r++) for(int c=0;c<8;c++){
        int cx2=ox+c*sq, cy2=oy+r*sq;
        uint32_t sbg=(r+c)%2?dark2:light;
        if(r==sel_r&&c==sel_c) sbg=sel_col;
        fb_fill_rect(cx2,cy2,sq,sq,sbg);
        /* Highlight movimientos validi */
        if(sel_r>=0&&valid_move(sel_r,sel_c,r,c)){
            fb_fill_rect(cx2+sq/2-5,cy2+sq/2-5,10,10,fb_color(0x44,0xcc,0xff));
        }
        char p=board[r][c];
        if(!p) continue;
        /* Pieza como letra (escala 2) */
        char ps[2]; ps[0]=p; ps[1]='\0';
        uint32_t pc=is_white(p)?fb_color(0xff,0xff,0xff):fb_color(0x11,0x11,0x11);
        uint32_t shadow=is_white(p)?fb_color(0x88,0x88,0x88):fb_color(0x66,0x44,0x22);
        fb_draw_str_scaled(cx2+sq/2-7+1,cy2+sq/2-8+1,ps,shadow,sbg,2);
        fb_draw_str_scaled(cx2+sq/2-7,cy2+sq/2-8,ps,pc,sbg,2);
    }
    /* Status */
    fb_fill_rect(ox,oy+8*sq+4,8*sq,18,bg);
    fb_draw_str(ox,oy+8*sq+6,status,fb_color(0x33,0x22,0x00),bg);
    /* Reset btn */
    fb_fill_rect(ox+8*sq-70,oy-22,68,20,fb_color(0x88,0x44,0x22));
    fb_draw_str(ox+8*sq-64,oy-18,"Reiniciar",fb_color(0xff,0xff,0xff),fb_color(0x88,0x44,0x22));
    (void)ww;(void)wh;
}

int chess_click(int wx,int wy,int ww,int wh,int mx,int my){
    int sq=44, ox=wx+BORDER+28, oy=wy+TITLEBAR_H+28;
    /* Reset */
    if(mx>=ox+8*sq-70&&mx<ox+8*sq-2&&my>=oy-22&&my<oy-2){ chess_reset(); return 0; }
    if(game_ended) return 0;
    int c=(mx-ox)/sq, r=(my-oy)/sq;
    if(c<0||c>7||r<0||r>7) return 0;
    if(sel_r<0){
        if(board[r][c]&&is_mine(board[r][c])){ sel_r=r; sel_c=c; }
    } else {
        if(valid_move(sel_r,sel_c,r,c)){
            char captured=board[r][c];
            board[r][c]=board[sel_r][sel_c];
            board[sel_r][sel_c]=0;
            /* Promoción peón */
            if((board[r][c]=='P'&&r==0)) board[r][c]='Q';
            if((board[r][c]=='p'&&r==7)) board[r][c]='q';
            if(captured=='K'||captured=='k'){ game_ended=1; scpy(status,turn==0?"Blancas ganan!":"Negras ganan!",48); }
            else { turn=1-turn; scpy(status,turn==0?"Turno: Blancas":"Turno: Negras",48); }
            sel_r=-1; sel_c=-1;
        } else if(board[r][c]&&is_mine(board[r][c])){ sel_r=r; sel_c=c; }
        else { sel_r=-1; sel_c=-1; }
    }
    (void)ww;(void)wh;
    return 0;
}
