/*
 * desktop.c  –  Escritorio estilo Windows para MyOS v2
 *
 * - Iconos arrastrables con posición persistente
 * - Papelera en esquina inferior derecha
 * - Drag & drop: archivos USB → apps, iconos → papelera
 * - Menú contextual por icono
 * - Pixel-art 32x32 por app
 */

#include "desktop.h"
#include "framebuffer.h"
#include "gui.h"
#include "wallpaper.h"
#include "fs.h"
#include "fat32.h"
#include "usb_msd.h"
#include "notepad.h"
#include "sound.h"
#include "trash.h"
#include "timer.h"
#include <stdint.h>

/* ---- Dimensiones ---- */
#define ICON_W      76
#define ICON_H      84
#define ICON_IMG_W  32
#define ICON_IMG_H  32
#define ICON_NAME_MAX 11

/* ---- Grilla de escritorio ---- */
#define GRID_COL_W  (ICON_W + 8)   /* ancho de celda de grilla */
#define GRID_ROW_H  (ICON_H + 8)   /* alto  de celda de grilla */
#define GRID_OX     8               /* margen izquierdo */
#define GRID_OY     8               /* margen superior  */

/* ---- Apps del sistema ---- */
typedef struct {
    const char *app_name;
    const char *label;
} app_def_t;

static const app_def_t APPS[] = {
    { "Terminal",      "Terminal"   },
    { "Navegador",     "Navegador"  },
    { "Archivos",      "Archivos"   },
    { "Notas",         "Notas"      },
    { "Calculadora",   "Calc"       },
    { "Reloj",         "Reloj"      },
    { "Imagenes",      "Imágenes"   },
    { "Musica",        "Música"     },
    { "USB",           "USB"        },
    { "Code",          "Code"       },
    { "Dispositivos",  "Dispositiv" },
    { "Monitor",       "Monitor"    },
    { "Configuracion", "Config"     },
    { "Snake",         "Snake"      },
    { "Tetris",        "Tetris"     },
    { "Pacman",        "Pacman"     },
    { "Pong",          "Pong"       },
    { "Conway",        "Conway"     },
};
#define N_APPS 18

/* ---- Iconos (apps + archivos FS) ---- */
#define MAX_ICONS  (N_APPS + 12)

typedef struct {
    int  x, y;          /* posición actual en pantalla */
    int  is_file;       /* 1 = archivo del FS, 0 = app */
    int  app_idx;       /* índice en APPS[] si !is_file */
    char name[24];      /* nombre para archivos FS */
    int  active;
} desk_icon_t;

static desk_icon_t icons[MAX_ICONS];
static int         icon_count = 0;

/* ---- Estado drag/drop ---- */
static int drag_icon     = -1;   /* índice del icono siendo arrastrado */
static int drag_off_x    = 0;
static int drag_off_y    = 0;
static int drag_moved    = 0;    /* se movió suficiente para ser drag */

/* Drag de archivo USB */
static int   usb_drag_active = 0;
static int   usb_drag_idx    = -1;
static int   usb_drag_x      = 0;
static int   usb_drag_y      = 0;
static char  usb_drag_name[64];

/* ---- Selección / doble click ---- */
static int      sel_icon       = -1;
static int      last_click_idx = -1;
static uint32_t last_click_t   = 0;

/* ---- Papelera ---- */
static int trash_hover = 0;   /* 1 si el drag está sobre la papelera */

/* ---- FS icons ---- */
static char fs_names[12][24];
static int  fs_count = 0;

/* ================================================================== */
/* Helpers                                                              */
/* ================================================================== */
static int slen(const char *s){ int n=0; while(s[n]) n++; return n; }
static void scpy(char *d,const char *s,int n){int i=0;while(s[i]&&i<n-1){d[i]=s[i];i++;}d[i]='\0';}

static uint32_t pal(uint8_t r,uint8_t g,uint8_t b){ return fb_color(r,g,b); }

/* ================================================================== */
/* Helpers de grilla                                                    */
/* ================================================================== */
static void snap_to_grid(int *x, int *y){
    int col = (*x - GRID_OX + GRID_COL_W/2) / GRID_COL_W;
    int row = (*y - GRID_OY + GRID_ROW_H/2) / GRID_ROW_H;
    int sh  = fb_height() - 30;
    int max_rows = (sh - GRID_OY) / GRID_ROW_H;
    int max_cols = (fb_width() - GRID_OX) / GRID_COL_W - 1;
    if(col < 0) col = 0;
    if(row < 0) row = 0;
    if(row >= max_rows) row = max_rows - 1;
    if(col > max_cols) col = max_cols;
    *x = GRID_OX + col * GRID_COL_W;
    *y = GRID_OY + row * GRID_ROW_H;
}

static int grid_cell_taken(int gx, int gy, int excl_idx){
    for(int i=0;i<icon_count;i++){
        if(i==excl_idx||!icons[i].active) continue;
        if(icons[i].x==gx && icons[i].y==gy) return 1;
    }
    return 0;
}

static void find_free_grid_cell(int *out_x, int *out_y, int excl_idx){
    int sh = fb_height() - 30;
    int max_rows = (sh - GRID_OY) / GRID_ROW_H;
    int max_cols = (fb_width() - GRID_OX) / GRID_COL_W - 1;
    for(int col=0; col<=max_cols; col++){
        for(int row=0; row<max_rows; row++){
            int gx = GRID_OX + col * GRID_COL_W;
            int gy = GRID_OY + row * GRID_ROW_H;
            if(!grid_cell_taken(gx, gy, excl_idx)){
                *out_x = gx;
                *out_y = gy;
                return;
            }
        }
    }
    *out_x = GRID_OX;
    *out_y = GRID_OY;
}

/* ================================================================== */
/* Pixel-art por tipo de app                                            */
/* ================================================================== */
static void draw_icon_art(int x, int y, int app_idx){
    switch(app_idx){
    case 0: /* Terminal */
        fb_fill_rect(x,y,32,32,pal(0x08,0x08,0x10));
        fb_draw_rect(x,y,32,32,pal(0x22,0xcc,0x22));
        fb_draw_str(x+3,y+4,">_",pal(0x00,0xff,0x00),pal(0x08,0x08,0x10));
        fb_fill_rect(x+3,y+16,18,2,pal(0x00,0xaa,0x00));
        fb_fill_rect(x+3,y+20,12,2,pal(0x00,0x77,0x00));
        fb_fill_rect(x+3,y+24,15,2,pal(0x00,0x55,0x00));
        break;
    case 1: /* Navegador */
        fb_fill_rect(x,y,32,32,pal(0x11,0x22,0x55));
        fb_fill_rect(x,y,32,7,pal(0x22,0x44,0xaa));
        fb_fill_rect(x+2,y+1,22,5,pal(0xee,0xee,0xff));
        fb_draw_str(x+3,y+1,"www",pal(0x44,0x44,0x88),pal(0xee,0xee,0xff));
        /* globo */
        fb_fill_rect(x+7,y+10,18,18,pal(0x22,0x88,0xdd));
        fb_fill_rect(x+15,y+10,2,18,pal(0x88,0xcc,0xff));
        fb_fill_rect(x+7,y+18,18,2,pal(0x88,0xcc,0xff));
        fb_draw_rect(x+7,y+10,18,18,pal(0x11,0x55,0xaa));
        break;
    case 2: /* Archivos - carpeta */
        fb_fill_rect(x+1,y+8,14,5,pal(0xff,0xcc,0x00));
        fb_fill_rect(x+1,y+11,30,17,pal(0xff,0xcc,0x00));
        fb_fill_rect(x+3,y+13,26,13,pal(0xff,0xee,0x88));
        fb_fill_rect(x+5,y+15,8,2,pal(0xcc,0x99,0x00));
        fb_fill_rect(x+5,y+19,14,2,pal(0xcc,0x99,0x00));
        fb_fill_rect(x+5,y+23,10,2,pal(0xcc,0x99,0x00));
        break;
    case 3: /* Notas */
        fb_fill_rect(x+3,y+1,24,29,pal(0xff,0xff,0xff));
        fb_fill_rect(x+3,y+1,24,5,pal(0x44,0x66,0xff));
        fb_fill_rect(x+21,y+1,6,6,pal(0xcc,0xcc,0xff));
        fb_fill_rect(x+6,y+9,18,2,pal(0x88,0xaa,0xdd));
        fb_fill_rect(x+6,y+13,18,2,pal(0x88,0xaa,0xdd));
        fb_fill_rect(x+6,y+17,18,2,pal(0x88,0xaa,0xdd));
        fb_fill_rect(x+6,y+21,14,2,pal(0xbb,0xbb,0xdd));
        fb_fill_rect(x+6,y+25,10,2,pal(0xbb,0xbb,0xdd));
        break;
    case 4: /* Calc */
        fb_fill_rect(x+2,y+1,28,30,pal(0x33,0x33,0x44));
        fb_fill_rect(x+4,y+3,24,8,pal(0x11,0x22,0x44));
        fb_draw_str(x+6,y+4,"123",pal(0x44,0xff,0x88),pal(0x11,0x22,0x44));
        for(int r=0;r<3;r++) for(int c=0;c<3;c++)
            fb_fill_rect(x+4+c*8,y+13+r*6,6,4,pal(0x44,0x88,0xcc));
        fb_fill_rect(x+4,y+27,14,4,pal(0xff,0x77,0x22));
        break;
    case 5: /* Reloj */
        fb_fill_rect(x+4,y+3,24,26,pal(0xff,0xff,0xff));
        fb_draw_rect(x+4,y+3,24,26,pal(0x22,0x44,0x88));
        fb_fill_rect(x+15,y+6,2,10,pal(0x22,0x44,0x88));
        fb_fill_rect(x+15,y+12,7,2,pal(0xff,0x44,0x44));
        fb_fill_rect(x+14,y+15,4,4,pal(0xff,0x44,0x44));
        fb_fill_rect(x+12,y+3,8,2,pal(0x88,0x88,0xaa));
        break;
    case 6: /* Imágenes */
        fb_fill_rect(x+1,y+1,30,30,pal(0xee,0xee,0xff));
        fb_draw_rect(x+1,y+1,30,30,pal(0x88,0x88,0xaa));
        fb_fill_rect(x+18,y+4,10,10,pal(0xff,0xdd,0x22));
        for(int r=0;r<10;r++){
            fb_fill_rect(x+1+r,y+21+r/2,12-r/2,1,pal(0x33,0xaa,0x44));
            fb_fill_rect(x+13+r,y+28-r/2,18-r,1,pal(0x44,0x77,0xcc));
        }
        break;
    case 7: /* Música */
        fb_fill_rect(x+1,y+1,30,30,pal(0x11,0x11,0x22));
        fb_fill_rect(x+10,y+4,4,18,pal(0xff,0x44,0x88));
        fb_fill_rect(x+19,y+2,4,18,pal(0xff,0x44,0x88));
        fb_fill_rect(x+10,y+4,13,3,pal(0xcc,0x00,0x55));
        fb_fill_rect(x+7,y+22,10,6,pal(0xff,0x66,0xaa));
        fb_fill_rect(x+16,y+20,10,6,pal(0xff,0x66,0xaa));
        break;
    case 8: /* USB */
        fb_fill_rect(x+13,y+1,6,5,pal(0x00,0xaa,0xff));
        fb_fill_rect(x+15,y+6,2,16,pal(0x00,0xaa,0xff));
        fb_fill_rect(x+8,y+13,16,3,pal(0x00,0xaa,0xff));
        fb_fill_rect(x+7,y+11,5,7,pal(0xff,0xff,0xff));
        fb_fill_rect(x+20,y+13,5,7,pal(0xff,0xff,0xff));
        fb_fill_rect(x+13,y+22,6,7,pal(0x00,0x66,0xcc));
        break;
    case 9: /* Code editor */
        fb_fill_rect(x,y,32,32,pal(0x1e,0x1e,0x2e));
        fb_fill_rect(x,y,32,5,pal(0x2d,0x2d,0x44));
        fb_draw_str(x+1,y,"{;}",pal(0x88,0xcc,0xff),pal(0x2d,0x2d,0x44));
        /* líneas de código */
        fb_fill_rect(x+2,y+7,6,2,pal(0xcc,0x88,0xff));   /* keyword */
        fb_fill_rect(x+10,y+7,12,2,pal(0x88,0xdd,0xff));  /* identifier */
        fb_fill_rect(x+4,y+11,14,2,pal(0xcc,0x88,0xff));
        fb_fill_rect(x+20,y+11,8,2,pal(0xff,0xcc,0x44));  /* string */
        fb_fill_rect(x+2,y+15,4,2,pal(0xcc,0x88,0xff));
        fb_fill_rect(x+8,y+15,10,2,pal(0x88,0xdd,0xff));
        fb_fill_rect(x+4,y+19,18,2,pal(0x66,0xcc,0x66));  /* comment */
        fb_fill_rect(x+2,y+23,8,2,pal(0xcc,0x88,0xff));
        fb_fill_rect(x+12,y+23,6,2,pal(0xff,0x88,0x44));
        /* cursor parpadeante */
        fb_fill_rect(x+20,y+23,2,8,pal(0xff,0xff,0xff));
        break;
    case 10: /* Dispositivos */
        fb_fill_rect(x+6,y+6,20,20,pal(0x11,0x44,0x22));
        fb_fill_rect(x+8,y+8,16,16,pal(0x22,0x88,0x44));
        for(int p=0;p<4;p++){
            fb_fill_rect(x+1,y+9+p*4,5,2,pal(0x88,0xff,0x88));
            fb_fill_rect(x+26,y+9+p*4,5,2,pal(0x88,0xff,0x88));
            fb_fill_rect(x+9+p*4,y+1,2,5,pal(0x88,0xff,0x88));
            fb_fill_rect(x+9+p*4,y+26,2,5,pal(0x88,0xff,0x88));
        }
        fb_fill_rect(x+11,y+11,10,10,pal(0x44,0xcc,0x66));
        break;
    case 11: /* Monitor */
        fb_fill_rect(x+1,y+3,30,20,pal(0x22,0x33,0x55));
        fb_draw_rect(x+1,y+3,30,20,pal(0xaa,0xaa,0xcc));
        fb_fill_rect(x+4,y+17,3,5,pal(0x44,0xff,0x88));
        fb_fill_rect(x+9,y+11,3,11,pal(0xff,0xcc,0x00));
        fb_fill_rect(x+14,y+14,3,8,pal(0x44,0xff,0x88));
        fb_fill_rect(x+19,y+8,3,14,pal(0xff,0x55,0x55));
        fb_fill_rect(x+13,y+23,6,4,pal(0xaa,0xaa,0xcc));
        fb_fill_rect(x+8,y+27,16,2,pal(0xaa,0xaa,0xcc));
        break;
    case 12: /* Config */
        fb_fill_rect(x+10,y+1,12,4,pal(0x88,0x88,0xaa));
        fb_fill_rect(x+10,y+27,12,4,pal(0x88,0x88,0xaa));
        fb_fill_rect(x+1,y+10,4,12,pal(0x88,0x88,0xaa));
        fb_fill_rect(x+27,y+10,4,12,pal(0x88,0x88,0xaa));
        fb_fill_rect(x+5,y+4,5,5,pal(0x88,0x88,0xaa));
        fb_fill_rect(x+22,y+4,5,5,pal(0x88,0x88,0xaa));
        fb_fill_rect(x+5,y+23,5,5,pal(0x88,0x88,0xaa));
        fb_fill_rect(x+22,y+23,5,5,pal(0x88,0x88,0xaa));
        fb_fill_rect(x+6,y+6,20,20,pal(0x88,0x88,0xaa));
        fb_fill_rect(x+10,y+10,12,12,pal(0x44,0x44,0x66));
        fb_fill_rect(x+12,y+12,8,8,pal(0xcc,0xcc,0xff));
        break;
    case 13: /* Snake */
        fb_fill_rect(x,y,32,32,pal(0x08,0x10,0x08));
        fb_fill_rect(x+3,y+16,16,4,pal(0x00,0xdd,0x00));
        fb_fill_rect(x+3,y+12,4,8,pal(0x00,0xdd,0x00));
        fb_fill_rect(x+3,y+8,16,4,pal(0x00,0xaa,0x00));
        fb_fill_rect(x+15,y+8,4,12,pal(0x00,0xaa,0x00));
        fb_fill_rect(x+22,y+14,4,2,pal(0xff,0xaa,0x00));
        fb_fill_rect(x+22,y+18,4,2,pal(0xff,0xaa,0x00));
        break;
    case 14: /* Tetris */
        fb_fill_rect(x,y,32,32,pal(0x08,0x08,0x18));
        fb_fill_rect(x+2,y+2,8,8,pal(0xff,0x00,0x00));
        fb_fill_rect(x+12,y+2,8,8,pal(0x00,0xff,0x00));
        fb_fill_rect(x+22,y+2,8,8,pal(0x00,0x00,0xff));
        fb_fill_rect(x+2,y+12,8,8,pal(0xff,0xff,0x00));
        fb_fill_rect(x+12,y+12,8,8,pal(0xff,0x00,0xff));
        fb_fill_rect(x+22,y+12,8,8,pal(0x00,0xff,0xff));
        fb_fill_rect(x+2,y+24,28,6,pal(0x55,0x55,0x77));
        break;
    case 15: /* Pacman */
        fb_fill_rect(x,y,32,32,pal(0x00,0x00,0x22));
        fb_fill_rect(x+5,y+7,22,18,pal(0xff,0xff,0x00));
        fb_fill_rect(x+3,y+11,26,10,pal(0xff,0xff,0x00));
        fb_fill_rect(x+7,y+5,18,22,pal(0xff,0xff,0x00));
        fb_fill_rect(x+17,y+14,15,8,pal(0x00,0x00,0x22));
        fb_fill_rect(x+10,y+8,4,4,pal(0x00,0x00,0x00));
        fb_fill_rect(x+24,y+9,5,4,pal(0xff,0x44,0x44));
        fb_fill_rect(x+24,y+15,5,4,pal(0xff,0x44,0x44));
        break;
    case 16: /* Pong */
        fb_fill_rect(x,y,32,32,pal(0x11,0x11,0x22));
        fb_fill_rect(x+2,y+5,4,22,pal(0xff,0xff,0xff));
        fb_fill_rect(x+26,y+5,4,22,pal(0xff,0xff,0xff));
        fb_fill_rect(x+13,y+13,6,6,pal(0xff,0xff,0xff));
        for(int r=0;r<5;r++) fb_fill_rect(x+15,y+2+r*6,2,3,pal(0x55,0x55,0x77));
        break;
    default: /* Conway - cuadrícula de células vivas */
        fb_fill_rect(x,y,32,32,pal(0x06,0x0e,0x12));
        fb_draw_rect(x,y,32,32,pal(0x10,0x30,0x20));
        /* Simular un pequeño glider + células */
        fb_fill_rect(x+10,y+4, 4,4,pal(0x44,0xff,0x88));
        fb_fill_rect(x+18,y+8, 4,4,pal(0x00,0xdd,0x44));
        fb_fill_rect(x+6, y+8, 4,4,pal(0x00,0xdd,0x44));
        fb_fill_rect(x+6, y+12,4,4,pal(0x00,0xaa,0x22));
        fb_fill_rect(x+10,y+12,4,4,pal(0x00,0xaa,0x22));
        fb_fill_rect(x+18,y+12,4,4,pal(0xff,0xcc,0x00));
        fb_fill_rect(x+14,y+16,4,4,pal(0xff,0x88,0x00));
        fb_fill_rect(x+22,y+16,4,4,pal(0xff,0x44,0x44));
        fb_fill_rect(x+6, y+20,4,4,pal(0x44,0xff,0x88));
        fb_fill_rect(x+18,y+20,4,4,pal(0x00,0xdd,0x44));
        fb_fill_rect(x+10,y+24,4,4,pal(0x00,0xaa,0x22));
        fb_fill_rect(x+22,y+24,4,4,pal(0xff,0xcc,0x00));
        break;
    }
}

static void draw_file_icon_art(int x, int y, const char *name){
    fb_fill_rect(x+2,y+1,24,28,pal(0xff,0xff,0xff));
    fb_fill_rect(x+2,y+1,24,4,pal(0x44,0x66,0xff));
    fb_fill_rect(x+20,y+1,6,6,pal(0xcc,0xcc,0xff));
    /* extensión */
    const char *dot=0;
    for(int i=0;name[i];i++) if(name[i]=='.') dot=name+i+1;
    if(dot) fb_draw_str(x+5,y+13,dot,pal(0x33,0x33,0xcc),pal(0xff,0xff,0xff));
}

/* ================================================================== */
/* Posición de papelera                                                 */
/* ================================================================== */
static void trash_icon_pos(int *ox, int *oy){
    *ox = fb_width()  - ICON_W - 8;
    *oy = fb_height() - 30 - ICON_H - 4;
}

static void draw_trash_icon(int ox, int oy, int hover, int has_items){
    uint32_t bg = hover ? fb_color(0x44,0x22,0x22) : 0;
    if(hover) fb_fill_rect(ox,oy,ICON_W,ICON_H,bg);

    int ix=ox+(ICON_W-ICON_IMG_W)/2, iy=oy+4;
    /* Cubo de basura */
    uint32_t body = has_items ? pal(0xcc,0x44,0x44) : pal(0x88,0x88,0xaa);
    uint32_t lid  = pal(0x66,0x66,0x88);
    fb_fill_rect(ix+4,iy+6,24,22,body);
    fb_fill_rect(ix+2,iy+4,28,4,lid);
    fb_fill_rect(ix+10,iy+1,12,4,lid);
    /* líneas */
    fb_fill_rect(ix+10,iy+8,2,16,pal(0x55,0x55,0x77));
    fb_fill_rect(ix+15,iy+8,2,16,pal(0x55,0x55,0x77));
    fb_fill_rect(ix+20,iy+8,2,16,pal(0x55,0x55,0x77));

    /* Label */
    const char *lbl = "Papelera";
    int nlen=slen(lbl);
    int tx=ox+(ICON_W-nlen*9)/2;
    fb_draw_str(tx+1,oy+4+ICON_IMG_H+5,lbl,fb_color(0,0,0),0);
    fb_draw_str(tx,  oy+4+ICON_IMG_H+4,lbl,fb_color(0xff,0xff,0xff),0);
}

/* ================================================================== */
/* Init                                                                 */
/* ================================================================== */
static void refresh_fs_icons(void){
    fs_count=0;
    char nm[24]; int sz;
    for(int k=0;k<12;k++){
        if(!fs_get_entry(k,nm,&sz)) break;
        scpy(fs_names[fs_count],nm,24);
        fs_count++;
    }
}

static void build_icons(void){
    icon_count=0;

    /* Apps */
    for(int i=0;i<N_APPS&&icon_count<MAX_ICONS;i++){
        desk_icon_t *ic=&icons[icon_count];
        ic->is_file=0;
        ic->app_idx=i;
        ic->active=1;
        scpy(ic->name,APPS[i].label,24);
        /* Colocar en la primera celda libre de la grilla */
        find_free_grid_cell(&ic->x, &ic->y, icon_count);
        icon_count++;
    }
    /* Archivos FS */
    refresh_fs_icons();
    for(int i=0;i<fs_count&&icon_count<MAX_ICONS;i++){
        desk_icon_t *ic=&icons[icon_count];
        ic->is_file=1;
        ic->app_idx=-1;
        ic->active=1;
        scpy(ic->name,fs_names[i],24);
        find_free_grid_cell(&ic->x, &ic->y, icon_count);
        icon_count++;
    }
}

void desktop_init(void){
    sel_icon=-1; drag_icon=-1; last_click_idx=-1;
    usb_drag_active=0; trash_hover=0;
    build_icons();
}

/* Remove all file icons from desktop and rebuild icons (keeps apps) */
void desktop_clean(void){
    /* mark all fs icons inactive */
    for(int i=0;i<icon_count;i++){
        if(icons[i].is_file) icons[i].active=0;
    }
    /* compact icons array keeping apps first */
    int write=0;
    for(int i=0;i<icon_count;i++){
        if(icons[i].active){
            if(i!=write) icons[write]=icons[i];
            write++;
        }
    }
    icon_count = write;
    /* refresh fs entries and rebuild icons for any new files */
    refresh_fs_icons();
    for(int i=0;i<fs_count && icon_count<MAX_ICONS;i++){
        desk_icon_t *ic=&icons[icon_count];
        ic->is_file=1; ic->app_idx=-1; ic->active=1;
        scpy(ic->name,fs_names[i],24);
        find_free_grid_cell(&ic->x,&ic->y,icon_count);
        icon_count++;
    }
    wallpaper_invalidate();
}

/* ================================================================== */
/* Dibujar icono normal                                                 */
/* ================================================================== */
static void draw_one_icon(int idx){
    desk_icon_t *ic=&icons[idx];
    if(!ic->active) return;

    int ox=ic->x, oy=ic->y;
    int ix=ox+(ICON_W-ICON_IMG_W)/2, iy=oy+4;

    /* Selección */
    if(idx==sel_icon)
        fb_fill_rect(ox,oy,ICON_W,ICON_H,fb_color(0x33,0x55,0xaa));

    /* Arte */
    if(ic->is_file) draw_file_icon_art(ix,iy,ic->name);
    else            draw_icon_art(ix,iy,ic->app_idx);

    /* Label con sombra */
    const char *lbl=ic->is_file ? ic->name : APPS[ic->app_idx].label;
    char sl[ICON_NAME_MAX+1]; int k=0;
    while(lbl[k]&&k<ICON_NAME_MAX){sl[k]=lbl[k];k++;}sl[k]='\0';
    int nlen=slen(sl);
    int tx=ox+(ICON_W-nlen*9)/2;
    int ty=oy+4+ICON_IMG_H+5;
    fb_draw_str(tx+1,ty+1,sl,fb_color(0,0,0),0);
    fb_draw_str(tx,  ty,   sl,fb_color(0xff,0xff,0xff),0);
}

/* ================================================================== */
/* Draw público                                                          */
/* ================================================================== */
void desktop_draw(void){
    /* No llamar refresh_fs_icons() cada frame — solo cuando hay cambios */

    /* Papelera */
    int tx,ty; trash_icon_pos(&tx,&ty);
    char tn[24]; int ts;
    int has_trash=trash_get_entry(0,tn,&ts);
    draw_trash_icon(tx,ty,trash_hover,has_trash);

    /* Iconos */
    for(int i=0;i<icon_count;i++){
        if(i==drag_icon) continue;  /* se dibuja encima */
        draw_one_icon(i);
    }

    /* Icono siendo arrastrado — encima de todo */
    if(drag_icon>=0){
        desk_icon_t *ic=&icons[drag_icon];
        int ox=ic->x,oy=ic->y;
        int ix=ox+(ICON_W-ICON_IMG_W)/2,iy=oy+4;
        fb_fill_rect(ox,oy,ICON_W,ICON_H,fb_color(0x22,0x44,0x88));
        if(ic->is_file) draw_file_icon_art(ix,iy,ic->name);
        else            draw_icon_art(ix,iy,ic->app_idx);
        const char *lbl=ic->is_file?ic->name:APPS[ic->app_idx].label;
        char sl[ICON_NAME_MAX+1]; int k=0;
        while(lbl[k]&&k<ICON_NAME_MAX){sl[k]=lbl[k];k++;}sl[k]='\0';
        fb_draw_str(ox+(ICON_W-slen(sl)*9)/2,oy+4+ICON_IMG_H+5,sl,fb_color(0xff,0xff,0xff),0);
    }

    /* Drag USB */
    if(usb_drag_active){
        fb_fill_rect(usb_drag_x-4,usb_drag_y-4,ICON_W,28,fb_color(0x33,0x55,0x99));
        fb_draw_str(usb_drag_x,usb_drag_y,usb_drag_name,fb_color(0xff,0xff,0xff),fb_color(0x33,0x55,0x99));
    }
}

/* ================================================================== */
/* Hit tests                                                            */
/* ================================================================== */
static int icon_at(int mx,int my){
    for(int i=icon_count-1;i>=0;i--){
        desk_icon_t *ic=&icons[i];
        if(!ic->active) continue;
        if(mx>=ic->x&&mx<ic->x+ICON_W&&my>=ic->y&&my<ic->y+ICON_H) return i;
    }
    return -1;
}

static int over_trash(int mx,int my){
    int tx,ty; trash_icon_pos(&tx,&ty);
    return mx>=tx&&mx<tx+ICON_W&&my>=ty&&my<ty+ICON_H;
}

/* ================================================================== */
/* Mouse                                                                */
/* ================================================================== */
void desktop_mouse_down(int mx,int my){
    int idx=icon_at(mx,my);
    if(idx<0){ sel_icon=-1; return; }
    sel_icon=idx;
    drag_icon=idx;
    drag_off_x=mx-icons[idx].x;
    drag_off_y=my-icons[idx].y;
    drag_moved=0;
}

void desktop_mouse_move(int mx,int my){
    /* Drag de icono */
    if(drag_icon>=0){
        icons[drag_icon].x=mx-drag_off_x;
        icons[drag_icon].y=my-drag_off_y;
        drag_moved=1;
        /* Hover en papelera */
        trash_hover=over_trash(mx,my)?1:0;
    }
    /* Drag USB */
    if(usb_drag_active){
        usb_drag_x=mx+4;
        usb_drag_y=my-8;
        trash_hover=over_trash(mx,my)?1:0;
    }
}

void desktop_mouse_up(int mx,int my){
    if(drag_icon>=0){
        if(drag_moved){
            /* Soltar en papelera */
            if(over_trash(mx,my) && icons[drag_icon].is_file){
                /* Leer archivo y mandarlo a la papelera */
                static char fdata[1024];
                int flen=fs_read(icons[drag_icon].name,fdata,sizeof(fdata)-1);
                if(flen<0)flen=0;
                trash_add(icons[drag_icon].name,fdata,flen);
                fs_delete(icons[drag_icon].name);
                icons[drag_icon].active=0;
                sound_click();
            }
            /* Snap a la celda de grilla más cercana */
            snap_to_grid(&icons[drag_icon].x, &icons[drag_icon].y);
            /* Si la celda ya la ocupa otro icono, buscar la primera libre */
            if(grid_cell_taken(icons[drag_icon].x, icons[drag_icon].y, drag_icon)){
                int fx, fy;
                find_free_grid_cell(&fx, &fy, drag_icon);
                icons[drag_icon].x = fx;
                icons[drag_icon].y = fy;
            }
        } else {
            /* Click simple o doble */
            uint32_t now=timer_ticks();
            if(drag_icon==last_click_idx && now-last_click_t<50){
                /* Doble click — abrir */
                desk_icon_t *ic=&icons[drag_icon];
                if(ic->is_file){
                    notepad_load(ic->name);
                    open_window_by_title("Notas");
                } else {
                    open_window_by_title(APPS[ic->app_idx].app_name);
                }
                sound_click();
                last_click_idx=-1;
            } else {
                last_click_idx=drag_icon;
                last_click_t=now;
            }
        }
        drag_icon=-1;
        trash_hover=0;
    }

    /* Drop de archivo USB */
    if(usb_drag_active){
        usb_drag_active=0;
        trash_hover=0;
        if(over_trash(mx,my)){
            /* Enviar a papelera */
            const fat32_entry_t *fe=fat32_dir_entry(usb_drag_idx);
            if(fe){
                uint8_t *buf; int n=fat32_read_file(fe->cluster,fe->size,&buf);
                if(n>0) trash_add(usb_drag_name,(const char*)buf,n);
            }
            return;
        }
        /* Soltar sobre una ventana */
        const fat32_entry_t *fe=fat32_dir_entry(usb_drag_idx);
        if(!fe) return;
        uint8_t *buf; int n=fat32_read_file(fe->cluster,fe->size,&buf);
        if(n<=0) return;

        int is_bmp=(n>=2&&buf[0]=='B'&&buf[1]=='M');
        int widx=gui_window_at(mx,my);
        if(widx<0) return;
        window_t *w=gui_get_window(widx);
        if(!w) return;
        if(!is_bmp && w->type==WIN_NOTEPAD){
            static char tb[8192];
            int len=n<8191?n:8191;
            for(int k=0;k<len;k++) tb[k]=(char)buf[k];
            tb[len]='\0';
            notepad_set_content(tb);
            open_window_by_title("Notas");
            sound_click();
        }
        if(is_bmp && w->type==WIN_IMAGEVIEWER){
            imageviewer_load_bmp(buf,(uint32_t)n);
            open_window_by_title("Imagenes");
            sound_click();
        }
        if(is_bmp && w->type==WIN_BROWSER){
            browser_load_bmp(buf,(uint32_t)n);
            open_window_by_title("Navegador");
            sound_click();
        }
    }
}

/* Click derecho sobre icono */
int desktop_icon_at(int mx,int my){ return icon_at(mx,my); }

/* Para papelera */
int desktop_over_trash(int mx,int my){ return over_trash(mx,my); }

const char* desktop_icon_appname(int idx){
    if(idx<0||idx>=icon_count) return "";
    desk_icon_t *ic=&icons[idx];
    if(ic->is_file) return ic->name;
    return APPS[ic->app_idx].app_name;
}
int desktop_icon_is_file(int idx){
    if(idx<0||idx>=icon_count) return 0;
    return icons[idx].is_file;
}

/* USB drag */
void desktop_drag_start_usb(int file_idx,int mx,int my){
    if(!fat32_ready()) return;
    const fat32_entry_t *fe=fat32_dir_entry(file_idx);
    if(!fe||fe->is_dir) return;
    usb_drag_active=1;
    usb_drag_idx=file_idx;
    usb_drag_x=mx+4; usb_drag_y=my-8;
    scpy(usb_drag_name,fe->name,64);
}
void desktop_drag_move(int mx,int my){ desktop_mouse_move(mx,my); }
int  desktop_drag_drop(int mx,int my){ desktop_mouse_up(mx,my); return 1; }
int  desktop_drag_active(void){ return usb_drag_active||drag_icon>=0; }
