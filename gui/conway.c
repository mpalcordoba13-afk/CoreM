#include "conway.h"
#include "framebuffer.h"
#include "gui.h"
#include "sound.h"
#include <stdint.h>

/* Tamaño de la cuadrícula */
#define CELL_SIZE   8
#define MAX_COLS    80
#define MAX_ROWS    60

/* Estados */
#define CW_PAUSED   0
#define CW_RUNNING  1
#define CW_PLACING  2   /* primer inicio: usuario coloca células */

static uint8_t grid[MAX_ROWS][MAX_COLS];
static uint8_t next[MAX_ROWS][MAX_COLS];

static int cw_cols, cw_rows;       /* dimensiones reales según ventana */
static int cw_state = CW_PAUSED;
static int cw_gen   = 0;
static int cw_pop   = 0;
static int cw_tick_div = 0;        /* divisor para bajar velocidad */
static int cw_speed = 4;           /* 1=lento … 8=rápido */

/* Paleta de colores según "edad" (generaciones vivo) */
static uint8_t cell_age[MAX_ROWS][MAX_COLS];

/* ------------------------------------------------------------------ */
/* Helpers de texto */
/* ------------------------------------------------------------------ */
static void itoa10(int n, char *b){
    if(n==0){ b[0]='0'; b[1]='\0'; return; }
    char t[12]; int i=0;
    while(n>0){ t[i++]='0'+n%10; n/=10; }
    int p=0; while(i>0) b[p++]=t[--i]; b[p]='\0';
}
static void scat(char *d, const char *s){
    int i=0; while(d[i]) i++;
    int j=0; while(s[j]) d[i++]=s[j++]; d[i]='\0';
}

/* ------------------------------------------------------------------ */
/* Patrón inicial: planeador + R-pentomino + pistola de planeadores    */
/* ------------------------------------------------------------------ */
static void place_glider(int r, int c){
    if(r+2>=MAX_ROWS||c+2>=MAX_COLS) return;
    grid[r][c+1]=1;
    grid[r+1][c+2]=1;
    grid[r+2][c]=1; grid[r+2][c+1]=1; grid[r+2][c+2]=1;
}

static void place_rpentomino(int r, int c){
    if(r+2>=MAX_ROWS||c+2>=MAX_COLS) return;
    grid[r][c+1]=1;   grid[r][c+2]=1;
    grid[r+1][c]=1;   grid[r+1][c+1]=1;
    grid[r+2][c+1]=1;
}

static void place_blinker(int r, int c){
    if(r>=MAX_ROWS||c+2>=MAX_COLS) return;
    grid[r][c]=1; grid[r][c+1]=1; grid[r][c+2]=1;
}

static void place_lwss(int r, int c){
    /* Lightweight spaceship */
    if(r+3>=MAX_ROWS||c+4>=MAX_COLS) return;
    grid[r][c+1]=1; grid[r][c+4]=1;
    grid[r+1][c]=1;
    grid[r+2][c]=1; grid[r+2][c+4]=1;
    grid[r+3][c]=1; grid[r+3][c+1]=1; grid[r+3][c+2]=1; grid[r+3][c+3]=1;
}

static void cw_seed(void){
    int r = cw_rows, c = cw_cols;
    /* Planeadores en distintas posiciones */
    place_glider(2,  2);
    place_glider(2,  20);
    place_glider(2,  40);
    /* R-pentominos caóticos */
    place_rpentomino(r/2-2, c/2);
    place_rpentomino(r/3,   c/3);
    place_rpentomino(r*2/3, c*2/3);
    /* Blinkers */
    place_blinker(r-5, 4);
    place_blinker(r-5, c-8);
    /* LWSS */
    place_lwss(r/2, 4);
    place_lwss(r/4, c-10);
}

/* ------------------------------------------------------------------ */
/* Inicialización                                                       */
/* ------------------------------------------------------------------ */
void conway_init(void){
    for(int r=0;r<MAX_ROWS;r++)
        for(int c=0;c<MAX_COLS;c++){
            grid[r][c]=0; next[r][c]=0; cell_age[r][c]=0;
        }
    cw_state = CW_PAUSED;
    cw_gen   = 0;
    cw_pop   = 0;
    cw_tick_div = 0;
    cw_speed = 4;
    cw_cols  = 40; /* valores por defecto, se recalculan en draw */
    cw_rows  = 30;
    cw_seed();
}

/* ------------------------------------------------------------------ */
/* Lógica de un paso                                                    */
/* ------------------------------------------------------------------ */
static int count_neighbors(int r, int c){
    int cnt=0;
    for(int dr=-1;dr<=1;dr++) for(int dc=-1;dc<=1;dc++){
        if(dr==0&&dc==0) continue;
        int nr=r+dr, nc=c+dc;
        if(nr<0||nr>=cw_rows||nc<0||nc>=cw_cols) continue;
        cnt+=grid[nr][nc];
    }
    return cnt;
}

static void cw_step(void){
    cw_pop=0;
    for(int r=0;r<cw_rows;r++) for(int c=0;c<cw_cols;c++){
        int n=count_neighbors(r,c);
        if(grid[r][c]){
            next[r][c]=(n==2||n==3)?1:0;
        } else {
            next[r][c]=(n==3)?1:0;
        }
        if(next[r][c]){
            cell_age[r][c]++;
            cw_pop++;
        } else {
            cell_age[r][c]=0;
        }
    }
    for(int r=0;r<cw_rows;r++) for(int c=0;c<cw_cols;c++)
        grid[r][c]=next[r][c];
    cw_gen++;
}

/* ------------------------------------------------------------------ */
/* Color según edad de la célula                                        */
/* ------------------------------------------------------------------ */
static uint32_t age_color(int age){
    if(age<=1)  return fb_color(0x44,0xff,0x88);  /* verde brillante: recién nacida */
    if(age<=4)  return fb_color(0x00,0xdd,0x44);
    if(age<=10) return fb_color(0x00,0xaa,0x22);
    if(age<=20) return fb_color(0xff,0xcc,0x00);  /* dorado: veterana */
    if(age<=40) return fb_color(0xff,0x88,0x00);  /* naranja: anciana */
                return fb_color(0xff,0x44,0x44);   /* rojo: muy antigua */
}

/* ------------------------------------------------------------------ */
/* Tick (llamado cada frame)                                            */
/* ------------------------------------------------------------------ */
void conway_tick(int wx, int wy, int ww, int wh){
    (void)wx;(void)wy;(void)ww;(void)wh;
    if(cw_state!=CW_RUNNING) return;
    cw_tick_div++;
    int threshold = 9 - cw_speed;  /* speed=8 -> cada 1 tick; speed=1 -> cada 8 */
    if(threshold<1) threshold=1;
    if(cw_tick_div >= threshold){
        cw_tick_div=0;
        cw_step();
    }
}

/* ------------------------------------------------------------------ */
/* Dibujo                                                               */
/* ------------------------------------------------------------------ */
void conway_draw(int wx, int wy, int ww, int wh){
    int ox = wx + BORDER + 2;
    int oy = wy + TITLEBAR_H + 20;  /* dejar espacio para HUD */
    int pw = ww - BORDER*2 - 4;
    int ph = wh - TITLEBAR_H - BORDER - 22;

    /* Recalcular dimensiones */
    cw_cols = pw / CELL_SIZE; if(cw_cols>MAX_COLS) cw_cols=MAX_COLS;
    cw_rows = ph / CELL_SIZE; if(cw_rows>MAX_ROWS) cw_rows=MAX_ROWS;

    /* Fondo */
    uint32_t bg = fb_color(0x06,0x0e,0x12);
    fb_fill_rect(ox, oy, pw, ph, bg);

    /* Grid de fondo (opcional, sutil) */
    uint32_t gc = fb_color(0x10,0x18,0x1e);
    for(int r=0;r<=cw_rows;r++)
        fb_draw_line(ox, oy+r*CELL_SIZE, ox+cw_cols*CELL_SIZE, oy+r*CELL_SIZE, gc);
    for(int c=0;c<=cw_cols;c++)
        fb_draw_line(ox+c*CELL_SIZE, oy, ox+c*CELL_SIZE, oy+cw_rows*CELL_SIZE, gc);

    /* Células */
    for(int r=0;r<cw_rows;r++) for(int c=0;c<cw_cols;c++){
        if(!grid[r][c]) continue;
        int cx = ox + c*CELL_SIZE + 1;
        int cy = oy + r*CELL_SIZE + 1;
        int cs = CELL_SIZE - 2;
        uint32_t col = age_color(cell_age[r][c]);
        fb_fill_rect(cx, cy, cs, cs, col);
    }

    /* ---- HUD ---- */
    uint32_t hud_bg = fb_color(0x08,0x10,0x16);
    fb_fill_rect(ox, wy+TITLEBAR_H+2, pw, 18, hud_bg);

    /* Generación */
    char buf[64];
    buf[0]='G'; buf[1]='e'; buf[2]='n'; buf[3]=':'; buf[4]=' '; buf[5]='\0';
    itoa10(cw_gen, buf+5);
    fb_draw_str(ox+4, wy+TITLEBAR_H+6, buf, fb_color(0x44,0xff,0x88), hud_bg);

    /* Población */
    char pb[32];
    pb[0]='P'; pb[1]='o'; pb[2]='b'; pb[3]=':'; pb[4]=' '; pb[5]='\0';
    itoa10(cw_pop, pb+5);
    fb_draw_str(ox+90, wy+TITLEBAR_H+6, pb, fb_color(0xff,0xcc,0x44), hud_bg);

    /* Velocidad */
    char sb[24];
    sb[0]='V'; sb[1]='e'; sb[2]='l'; sb[3]=':'; sb[4]=' '; sb[5]='\0';
    itoa10(cw_speed, sb+5);
    fb_draw_str(ox+180, wy+TITLEBAR_H+6, sb, fb_color(0x88,0xcc,0xff), hud_bg);

    /* Estado */
    const char *est = (cw_state==CW_RUNNING) ? "CORRIENDO" : "PAUSADO";
    uint32_t ec = (cw_state==CW_RUNNING) ? fb_color(0x44,0xff,0x44) : fb_color(0xff,0x88,0x44);
    fb_draw_str(ox+pw-80, wy+TITLEBAR_H+6, est, ec, hud_bg);

    /* Controles (overlay si pausado) */
    if(cw_state==CW_PAUSED){
        uint32_t tc = fb_color(0x88,0x88,0xaa);
        fb_draw_str(ox+4, oy+ph-36, "Click: cel | Espacio: pausa/play", tc, bg);
        fb_draw_str(ox+4, oy+ph-20, "+/-: velocidad  R: reiniciar  C: limpiar", tc, bg);
    }
}

/* ------------------------------------------------------------------ */
/* Click: toggle célula bajo el cursor                                  */
/* ------------------------------------------------------------------ */
int conway_click(int wx, int wy, int ww, int wh, int mx, int my){
    int ox = wx + BORDER + 2;
    int oy = wy + TITLEBAR_H + 20;
    int pw = ww - BORDER*2 - 4;
    int ph = wh - TITLEBAR_H - BORDER - 22;

    if(mx<ox||mx>=ox+cw_cols*CELL_SIZE||my<oy||my>=oy+cw_rows*CELL_SIZE) return 0;
    int c = (mx-ox)/CELL_SIZE;
    int r = (my-oy)/CELL_SIZE;
    if(r<0||r>=cw_rows||c<0||c>=cw_cols) return 0;

    grid[r][c] = grid[r][c] ? 0 : 1;
    cell_age[r][c] = grid[r][c] ? 1 : 0;
    if(grid[r][c]) cw_pop++;
    else if(cw_pop>0) cw_pop--;

    (void)pw;(void)ph;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Teclado — llamado desde gui.c al procesar teclado con esta ventana  */
/* ------------------------------------------------------------------ */
void conway_key(char k){
    if(k==' '){
        cw_state = (cw_state==CW_RUNNING) ? CW_PAUSED : CW_RUNNING;
        sound_click();
    } else if(k=='+' || k=='='){
        if(cw_speed<8) cw_speed++;
        sound_click();
    } else if(k=='-'){
        if(cw_speed>1) cw_speed--;
        sound_click();
    } else if(k=='r' || k=='R'){
        conway_init();
        cw_state=CW_RUNNING;
        sound_click();
    } else if(k=='c' || k=='C'){
        for(int r=0;r<MAX_ROWS;r++) for(int col=0;col<MAX_COLS;col++){
            grid[r][col]=0; cell_age[r][col]=0;
        }
        cw_gen=0; cw_pop=0;
        cw_state=CW_PAUSED;
        sound_click();
    }
}
