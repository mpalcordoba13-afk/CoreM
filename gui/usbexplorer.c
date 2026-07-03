/*
 * usbexplorer.c  –  Explorador de USB (FAT32) para MyOS
 *
 * Muestra el contenido de un USB FAT32 con navegación de directorios.
 * Permite abrir archivos de texto (.txt, .c, .h, .md, .cfg, .log).
 * Muestra imágenes BMP directamente (usa fb_draw_bmp si está disponible).
 */

#include "usbexplorer.h"
#include "fat32.h"
#include "usb_msd.h"
#include "framebuffer.h"
#include "gui.h"
#include "keyboard.h"
#include <stdint.h>

/* ---- Layout ------------------------------------------------------ */
#define PAD         6
#define ROW_H       22
#define CHAR_W      9
#define CHAR_H      12

/* ---- Estado ------------------------------------------------------ */
typedef enum {
    VIEW_DIR,    /* listado de directorio */
    VIEW_TEXT,   /* visor de texto */
} usb_view_t;

static usb_view_t  view        = VIEW_DIR;
static int         initialized = 0;
static int         scroll      = 0;
static int         selected    = 0;

/* Para el visor de texto */
#define TEXT_LINES   512
#define TEXT_LINE_W  90
static char  tlines[TEXT_LINES][TEXT_LINE_W+1];
static int   tline_count = 0;
static int   tscroll     = 0;
static char  tfile_name[FAT32_NAME_LEN];

/* Historial de directorios (para volver atrás) */
#define HIST_DEPTH  8
static uint32_t dir_hist[HIST_DEPTH];
static int       dir_hist_top = 0;

/* ---- Helpers ------------------------------------------------------ */
static int slen(const char *s){ int n=0; while(s[n]) n++; return n; }
static void scpy(char *d,const char *s,int max){
    int i=0; while(s[i]&&i<max-1){d[i]=s[i];i++;} d[i]='\0';
}

/* Devuelve extensión en minúsculas (puntero dentro de name) */
static const char* ext_of(const char *name){
    const char *dot = 0;
    for(int i=0;name[i];i++) if(name[i]=='.') dot=name+i;
    return dot ? dot+1 : "";
}

static int streq(const char *a,const char *b){
    while(*a&&*b) if(*a++!=*b++) return 0;
    return *a==*b;
}

static char tolower_c(char c){ return (c>='A'&&c<='Z') ? c+32 : c; }

static int ext_is_text(const char *e){
    static const char *texts[]={"txt","c","h","cpp","hpp","md","cfg",
                                  "log","asm","s","ini","sh","py","js","css","html",0};
    char el[8]; int i=0;
    while(e[i]&&i<7){ el[i]=tolower_c(e[i]); i++; } el[i]='\0';
    for(int k=0;texts[k];k++) if(streq(el,texts[k])) return 1;
    return 0;
}

/* Convierte raw bytes a líneas de texto */
static void buf_to_lines(const uint8_t *buf, uint32_t len){
    tline_count = 0; tscroll = 0;
    int col = 0;
    for(uint32_t i=0;i<len&&tline_count<TEXT_LINES;i++){
        char c = (char)buf[i];
        if(c=='\r') continue;
        if(c=='\n' || col>=TEXT_LINE_W){
            tlines[tline_count][col]='\0';
            tline_count++;
            col=0;
            if(c=='\n') continue;
        }
        if(tline_count<TEXT_LINES)
            tlines[tline_count][col++]=(c>=32&&c<127)?c:'?';
    }
    if(col>0&&tline_count<TEXT_LINES){
        tlines[tline_count][col]='\0';
        tline_count++;
    }
}

/* ================================================================== */
/* API pública                                                         */
/* ================================================================== */

void usbexplorer_init(void){
    initialized = 0;
    view = VIEW_DIR;
    scroll = 0; selected = 0;
    dir_hist_top = 0;

    if (!usb_msd_present()){
        /* Intentar inicializar ahora (puede que usb_msd_init no se llamó) */
        usb_msd_init();
    }
    if (!usb_msd_present()) return;

    if (fat32_init() != 0) return;

    initialized = 1;
    selected = 0;
    scroll = 0;
}

void usbexplorer_draw(int wx, int wy, int ww, int wh){
    /* Reintentar init si todavía no está listo */
    if (!initialized && usb_msd_present() && !fat32_ready()){
        if (fat32_init() == 0) initialized = 1;
    }
    if (!initialized && !usb_msd_present()){
        usb_msd_init();
        if (usb_msd_present() && fat32_init()==0) initialized=1;
    }
    int bx = wx + BORDER;
    int by = wy + TITLEBAR_H;
    int bw = ww - BORDER*2;
    int bh = wh - TITLEBAR_H - BORDER;
    if (bw < 100 || bh < 60) return;

    uint32_t BG       = fb_color(0x12,0x12,0x20);
    uint32_t HDR_BG   = fb_color(0x1e,0x1e,0x35);
    uint32_t TXT      = fb_color(0xe0,0xe0,0xf0);
    uint32_t DIM      = fb_color(0x66,0x66,0x88);
    uint32_t ACC      = fb_color(0x44,0xaa,0xff);
    uint32_t DIR_COL  = fb_color(0xff,0xcc,0x44);
    uint32_t SEL_BG   = fb_color(0x22,0x44,0x88);
    uint32_t ERR      = fb_color(0xff,0x55,0x55);
    uint32_t GRN      = fb_color(0x44,0xcc,0x66);

    fb_fill_rect(bx, by, bw, bh, BG);

    /* ---- Sin USB / no inicializado ---- */
    if (!usb_msd_present()){
        fb_fill_rect(bx,by,bw,32,HDR_BG);
        fb_draw_str(bx+PAD,by+10,"Explorador USB",ACC,HDR_BG);
        fb_draw_str(bx+PAD,by+44,"No se detectó ningún USB conectado.",ERR,BG);
        fb_draw_str(bx+PAD,by+62,"Conecta un pendrive y reabre esta ventana.",DIM,BG);
        fb_draw_str(bx+PAD,by+80,"(QEMU: Machine → USB → Add device)",DIM,BG);
        return;
    }
    if (!fat32_ready()){
        fb_fill_rect(bx,by,bw,32,HDR_BG);
        fb_draw_str(bx+PAD,by+10,"Explorador USB",ACC,HDR_BG);
        fb_draw_str(bx+PAD,by+44,"USB detectado pero no se pudo leer FAT32.",ERR,BG);
        fb_draw_str(bx+PAD,by+62,"Asegurate de formatear el USB como FAT32.",DIM,BG);
        return;
    }

    /* ---- Cabecera ---- */
    fb_fill_rect(bx,by,bw,28,HDR_BG);

    if (view == VIEW_DIR){
        /* Título con capacidad */
        uint32_t total_mb = (usb_msd_sector_count() / 2048);
        char tbuf[32];
        /* itoa manual */
        int tv=(int)total_mb; int ti=0;
        if(tv==0){tbuf[ti++]='0';}
        else{char tmp[10];int j=0;while(tv>0){tmp[j++]='0'+tv%10;tv/=10;}while(j>0)tbuf[ti++]=tmp[--j];}
        tbuf[ti++]=' ';tbuf[ti++]='M';tbuf[ti++]='B';tbuf[ti]='\0';

        fb_draw_str(bx+PAD,by+8,"USB FAT32 –",GRN,HDR_BG);
        fb_draw_str(bx+PAD+100,by+8,tbuf,DIM,HDR_BG);

        /* Hint teclas */
        fb_draw_str(bx+bw-200,by+8,"↑↓ Sel  Enter Abrir  BS Volver",DIM,HDR_BG);

        /* Lista de archivos */
        int cy = by + 32;
        int visible = (bh - 32) / ROW_H;
        int count = fat32_dir_count();

        /* Botón "↑ Directorio padre" si tenemos historial */
        if (dir_hist_top > 0){
            uint32_t row_bg = (selected == -1) ? SEL_BG : BG;
            fb_fill_rect(bx, cy, bw, ROW_H-2, row_bg);
            fb_draw_str(bx+PAD+18, cy+5, ".. (directorio padre)", DIM, row_bg);
            cy += ROW_H;
        }

        for (int i=scroll; i<count && cy<by+bh-4; i++){
            const fat32_entry_t *fe = fat32_dir_entry(i);
            if (!fe) break;

            uint32_t row_bg = (i==selected) ? SEL_BG : BG;
            fb_fill_rect(bx, cy, bw, ROW_H-2, row_bg);

            /* Icono */
            if (fe->is_dir){
                fb_draw_str(bx+PAD, cy+5, "[D]", DIR_COL, row_bg);
            } else {
                fb_draw_str(bx+PAD, cy+5, "   ", DIM, row_bg);
                /* Punto de color según tipo */
                const char *ex = ext_of(fe->name);
                uint32_t ic = ext_is_text(ex) ? GRN : DIM;
                fb_fill_rect(bx+PAD+2, cy+8, 6, 6, ic);
            }

            /* Nombre */
            fb_draw_str(bx+PAD+28, cy+5, fe->name,
                        fe->is_dir ? DIR_COL : TXT, row_bg);

            /* Tamaño (solo para archivos) */
            if (!fe->is_dir){
                uint32_t sz = fe->size;
                char sbuf[16]; int si=0;
                if(sz>=1024*1024){
                    int mb=(int)(sz/1048576);
                    if(mb==0){sbuf[si++]='0';}
                    else{char tmp[8];int j=0;while(mb>0){tmp[j++]='0'+mb%10;mb/=10;}while(j>0)sbuf[si++]=tmp[--j];}
                    sbuf[si++]=' ';sbuf[si++]='M';sbuf[si++]='B';
                } else if(sz>=1024){
                    int kb=(int)(sz/1024);
                    if(kb==0){sbuf[si++]='0';}
                    else{char tmp[8];int j=0;while(kb>0){tmp[j++]='0'+kb%10;kb/=10;}while(j>0)sbuf[si++]=tmp[--j];}
                    sbuf[si++]=' ';sbuf[si++]='K';sbuf[si++]='B';
                } else {
                    int bts=(int)sz;
                    if(bts==0){sbuf[si++]='0';}
                    else{char tmp[8];int j=0;while(bts>0){tmp[j++]='0'+bts%10;bts/=10;}while(j>0)sbuf[si++]=tmp[--j];}
                    sbuf[si++]=' ';sbuf[si++]='B';
                }
                sbuf[si]='\0';
                int sw = slen(sbuf)*CHAR_W;
                fb_draw_str(bx+bw-sw-PAD*2, cy+5, sbuf, DIM, row_bg);
            }

            cy += ROW_H;
            (void)visible;
        }

        /* Scrollbar */
        if (count > 0 && bh > 32){
            int area = bh-32;
            int sb_x = bx+bw-6;
            fb_fill_rect(sb_x,by+32,5,area,fb_color(0x1a,0x1a,0x28));
            if (count > 1){
                int th = area * (area/ROW_H) / count;
                if(th<8)th=8;
                int ty = by+32 + area*scroll/count;
                fb_fill_rect(sb_x,ty,5,th,fb_color(0x33,0x55,0x99));
            }
        }

    } else if (view == VIEW_TEXT){
        /* ---- Visor de texto ---- */
        fb_draw_str(bx+PAD,by+8,tfile_name,ACC,HDR_BG);
        fb_draw_str(bx+bw-160,by+8,"BS=Volver  ↑↓ Scroll",DIM,HDR_BG);

        int cy = by+32;
        int vlines = (bh-32)/CHAR_H;
        for(int i=tscroll; i<tline_count && cy<by+bh-4; i++){
            fb_draw_str(bx+PAD, cy, tlines[i], TXT, BG);
            cy += CHAR_H;
        }
        (void)vlines;
    }
}

void usbexplorer_key(int ch){
    if (!initialized) return;

    if (view == VIEW_TEXT){
        if (ch==8||ch==127||ch==27){ view=VIEW_DIR; return; }
        if (ch==KEY_UP_USB)   { if(tscroll>0) tscroll--; return; }
        if (ch==KEY_DOWN_USB) { tscroll++; return; }
        if (ch==KEY_PGUP_USB) { tscroll-=10; if(tscroll<0)tscroll=0; return; }
        if (ch==KEY_PGDN_USB) { tscroll+=10; return; }
        return;
    }

    /* VIEW_DIR */
    int count = fat32_dir_count();
    if (ch==KEY_UP_USB){
        if(selected>0) selected--;
        if(selected<scroll) scroll=selected;
        return;
    }
    if (ch==KEY_DOWN_USB){
        if(selected<count-1) selected++;
        return;
    }
    if (ch==KEY_PGUP_USB){ selected-=8; if(selected<0)selected=0; scroll=selected; return; }
    if (ch==KEY_PGDN_USB){ selected+=8; if(selected>=count)selected=count-1; return; }

    if (ch=='\n'||ch=='\r'){
        if (selected<0||selected>=count) return;
        const fat32_entry_t *fe = fat32_dir_entry(selected);
        if (!fe) return;

        if (fe->is_dir){
            /* Navegar al subdirectorio */
            if (dir_hist_top < HIST_DEPTH)
                dir_hist[dir_hist_top++] = fat32_dir_count() > 0 ?
                    fat32_dir_entry(0)->cluster : 0; /* guardamos cluster del padre */
            /* En FAT32, el cluster del directorio padre está en la entrada ".." —
             * lo hacemos más simple: guardamos el cluster actual */
            /* Simplificación: guardar cur_dir_clus (no expuesta; usamos el 1er entry) */
            fat32_read_dir(fe->cluster);
            selected=0; scroll=0;
        } else {
            /* Abrir archivo */
            uint8_t *buf;
            int n = fat32_read_file(fe->cluster, fe->size, &buf);
            if (n > 0){
                scpy(tfile_name, fe->name, FAT32_NAME_LEN);
                buf_to_lines(buf, (uint32_t)n);
                view = VIEW_TEXT;
                tscroll = 0;
            }
        }
        return;
    }

    /* Backspace = volver al directorio padre */
    if (ch==8||ch==127||ch==27){
        if (dir_hist_top > 0){
            dir_hist_top--;
            /* volver al raíz: fat32_read_dir(root) –
             * root_clus no está expuesto, re-init */
            fat32_init();  /* re-monta y vuelve al raíz */
            selected=0; scroll=0;
        }
    }
}

void usbexplorer_mouse(int bx_w,int by_w,int bw_w,int bh_w,int px,int py,int click){
    (void)bx_w;(void)by_w;(void)bw_w;(void)bh_w;(void)px;(void)py;(void)click;
    /* Selección por click: calcular row */
    if (!click || !initialized) return;
    if (view == VIEW_TEXT) return;

    int list_y = by_w + TITLEBAR_H + 28;
    if (py < list_y) return;
    int row = (py - list_y) / ROW_H + scroll;
    int count = fat32_dir_count();
    if (row >= 0 && row < count){
        if (row == selected){
            /* doble click simulado: abrir */
            usbexplorer_key('\n');
        } else {
            selected = row;
        }
    }
}

int usbexplorer_selected_file(void){
    if(view != VIEW_DIR) return -1;
    if(selected<0||selected>=fat32_dir_count()) return -1;
    const fat32_entry_t *fe=fat32_dir_entry(selected);
    if(!fe||fe->is_dir) return -1;
    return selected;
}
