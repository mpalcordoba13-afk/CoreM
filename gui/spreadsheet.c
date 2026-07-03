#include "spreadsheet.h"
#include "framebuffer.h"
#include "gui.h"
#include <stdint.h>

#define SS_COLS 8
#define SS_ROWS 20
#define SS_CELL_W 72
#define SS_CELL_H 18
#define SS_HEADER_W 28
#define SS_HEADER_H 18

static char cells[SS_ROWS][SS_COLS][24];
static int sel_r=0, sel_c=0;
static char edit_buf[24]; static int edit_len=0; static int editing=0;

static void scpy(char*d,const char*s,int n){int i=0;while(s[i]&&i<n-1){d[i]=s[i];i++;}d[i]='\0';}
static int slen(const char*s){int n=0;while(s[n])n++;return n;}
static int is_digit(char c){return c>='0'&&c<='9';}

/* Evalúa una celda: si empieza con '=' hace suma simple de rango A1:A5 etc */
static int eval_cell(const char *s){
    if(!s[0]) return 0;
    if(s[0]!='='){ /* número literal */
        int v=0,neg=0,i=0;
        if(s[0]=='-'){neg=1;i++;}
        for(;s[i];i++) if(is_digit(s[i])) v=v*10+(s[i]-'0');
        return neg?-v:v;
    }
    /* =SUM(A1:A5) simplificado */
    int sum=0;
    for(int r=0;r<SS_ROWS;r++)
        for(int c=0;c<SS_COLS;c++){
            const char *cs=cells[r][c];
            if(cs[0]&&cs[0]!='='){
                int v=0,i=0;
                for(;cs[i];i++) if(is_digit(cs[i])) v=v*10+(cs[i]-'0');
                sum+=v;
            }
        }
    return sum;
}

static void itoa10(int n,char*buf){
    if(n==0){buf[0]='0';buf[1]='\0';return;}
    char t[12];int i=0,neg=0;
    if(n<0){neg=1;n=-n;}
    while(n>0){t[i++]='0'+n%10;n/=10;}
    int p=0; if(neg)buf[p++]='-';
    while(i>0)buf[p++]=t[--i]; buf[p]='\0';
}

void spreadsheet_init(void){
    for(int r=0;r<SS_ROWS;r++) for(int c=0;c<SS_COLS;c++) cells[r][c][0]='\0';
    scpy(cells[0][0],"Producto",24); scpy(cells[0][1],"Precio",24); scpy(cells[0][2],"Cant",24); scpy(cells[0][3],"Total",24);
    scpy(cells[1][0],"Manzanas",24); scpy(cells[1][1],"150",24); scpy(cells[1][2],"5",24);
    scpy(cells[2][0],"Naranjas",24); scpy(cells[2][1],"200",24); scpy(cells[2][2],"3",24);
    edit_buf[0]='\0'; edit_len=0; editing=0;
}

void spreadsheet_draw(int wx,int wy,int ww,int wh){
    uint32_t hdr_bg=fb_color(0xd0,0xd8,0xf0);
    uint32_t sel_bg=fb_color(0xcc,0xdd,0xff);
    uint32_t bg    =fb_color(0xf8,0xf8,0xff);
    uint32_t grid  =fb_color(0xbb,0xbb,0xcc);
    uint32_t txt   =fb_color(0x11,0x11,0x22);
    uint32_t hdr_t =fb_color(0x22,0x22,0x55);

    int ox=wx+BORDER, oy=wy+TITLEBAR_H;
    int pw=ww-BORDER*2, ph=wh-TITLEBAR_H-BORDER;

    /* Barra de fórmula */
    fb_fill_rect(ox,oy,pw,22,hdr_bg);
    char ref[8]; ref[0]=(char)('A'+sel_c); ref[1]=(char)('0'+sel_r+1); ref[2]='\0';
    fb_draw_str(ox+4,oy+6,ref,hdr_t,hdr_bg);
    fb_fill_rect(ox+30,oy+3,pw-34,16,fb_color(0xff,0xff,0xff));
    fb_draw_rect(ox+30,oy+3,pw-34,16,grid);
    const char *shown=editing?edit_buf:cells[sel_r][sel_c];
    fb_draw_str(ox+34,oy+6,shown,txt,fb_color(0xff,0xff,0xff));
    if(editing){ /* cursor */
        int cx2=ox+34+slen(shown)*9;
        fb_fill_rect(cx2,oy+5,2,12,txt);
    }

    int grid_oy=oy+24;
    int visible_rows=(ph-24)/SS_CELL_H; if(visible_rows>SS_ROWS) visible_rows=SS_ROWS;
    int visible_cols=(pw-SS_HEADER_W)/SS_CELL_W; if(visible_cols>SS_COLS) visible_cols=SS_COLS;

    /* Cabecera columnas */
    fb_fill_rect(ox+SS_HEADER_W,grid_oy,visible_cols*SS_CELL_W,SS_HEADER_H,hdr_bg);
    for(int c=0;c<visible_cols;c++){
        char ch[3]; ch[0]=(char)('A'+c); ch[1]='\0';
        int cx2=ox+SS_HEADER_W+c*SS_CELL_W;
        fb_draw_rect(cx2,grid_oy,SS_CELL_W,SS_HEADER_H,grid);
        fb_draw_str(cx2+SS_CELL_W/2-4,grid_oy+5,ch,hdr_t,hdr_bg);
    }

    /* Cabecera filas + celdas */
    for(int r=0;r<visible_rows;r++){
        int ry=grid_oy+SS_HEADER_H+r*SS_CELL_H;
        /* Número de fila */
        fb_fill_rect(ox,ry,SS_HEADER_W,SS_CELL_H,hdr_bg);
        fb_draw_rect(ox,ry,SS_HEADER_W,SS_CELL_H,grid);
        char nb[4]; itoa10(r+1,nb);
        fb_draw_str(ox+4,ry+5,nb,hdr_t,hdr_bg);
        /* Celdas */
        for(int c=0;c<visible_cols;c++){
            int cx2=ox+SS_HEADER_W+c*SS_CELL_W;
            int is_sel=(r==sel_r&&c==sel_c);
            uint32_t cbg=is_sel?sel_bg:bg;
            fb_fill_rect(cx2,ry,SS_CELL_W,SS_CELL_H,cbg);
            fb_draw_rect(cx2,ry,SS_CELL_W,SS_CELL_H,grid);
            if(is_sel) fb_draw_rect(cx2,ry,SS_CELL_W,SS_CELL_H,fb_color(0x44,0x88,0xff));
            /* Contenido */
            const char *val=(is_sel&&editing)?edit_buf:cells[r][c];
            if(val[0]=='='){
                char nb2[12]; itoa10(eval_cell(val),nb2);
                fb_draw_str(cx2+3,ry+5,nb2,fb_color(0x22,0x55,0xaa),cbg);
            } else {
                /* Truncar si es muy largo */
                char tmp[9]; scpy(tmp,val,9);
                fb_draw_str(cx2+3,ry+5,tmp,txt,cbg);
            }
        }
    }
}

int spreadsheet_click(int wx,int wy,int ww,int wh,int mx,int my){
    if(editing){ scpy(cells[sel_r][sel_c],edit_buf,24); editing=0; }
    int ox=wx+BORDER, oy=wy+TITLEBAR_H+24+SS_HEADER_H;
    int c=(mx-(ox+SS_HEADER_W))/SS_CELL_W;
    int r=(my-oy)/SS_CELL_H;
    if(c>=0&&c<SS_COLS&&r>=0&&r<SS_ROWS){ sel_r=r; sel_c=c; }
    (void)ww;(void)wh;
    return 0;
}

void spreadsheet_key(char c){
    if(!editing){ editing=1; scpy(edit_buf,cells[sel_r][sel_c],24); edit_len=slen(edit_buf); }
    if(c=='\n'||c=='\r'){ scpy(cells[sel_r][sel_c],edit_buf,24); editing=0; if(sel_r<SS_ROWS-1)sel_r++; return; }
    if(c=='\b'){ if(edit_len>0){edit_len--;edit_buf[edit_len]='\0';} return; }
    if(c=='\t'){ scpy(cells[sel_r][sel_c],edit_buf,24); editing=0; if(sel_c<SS_COLS-1)sel_c++; return; }
    if(c>=' '&&c<127&&edit_len<23){ edit_buf[edit_len++]=c; edit_buf[edit_len]='\0'; }
}
