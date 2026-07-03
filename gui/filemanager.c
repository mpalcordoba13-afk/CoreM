/*
 * filemanager.c – Explorador de archivos estilo Windows Explorer
 *
 * Layout:
 *   - Barra de ruta  (arriba)
 *   - Panel izquierdo: árbol de ubicaciones (Este equipo, Archivos, Papelera)
 *   - Panel derecho:   iconos grandes de archivos/carpetas
 *   - Barra de estado (abajo): número de archivos
 *
 * Doble click en archivo -> abre en Notas
 * Botón "Eliminar" (click derecho simulado con botón rojo) -> papelera
 * Botón "Nuevo" -> crea archivo vacío
 */

#include "filemanager.h"
#include "gui.h"
#include "framebuffer.h"
#include "fs.h"
#include "notepad.h"
#include "trash.h"
#include <stdint.h>

/* ---- Constantes de layout ---- */
#define FM_TOOLBAR_H   28    /* altura barra superior (ruta) */
#define FM_STATUSBAR_H 22    /* altura barra de estado inferior */
#define FM_SIDEBAR_W   110   /* ancho panel izquierdo */
#define FM_ICON_W      80    /* ancho celda icono */
#define FM_ICON_H      72    /* alto  celda icono */
#define FM_ICON_IMG    32    /* tamaño icono pixel-art */

/* ---- Colores ---- */
#define C_BG        fb_color(0xf2,0xf3,0xf5)
#define C_SIDEBAR   fb_color(0xe8,0xe8,0xf0)
#define C_TOOLBAR   fb_color(0xdf,0xe1,0xe8)
#define C_STATUS    fb_color(0xd8,0xd8,0xe0)
#define C_DIVIDER   fb_color(0xcc,0xcc,0xdd)
#define C_TEXT      fb_color(0x11,0x11,0x22)
#define C_TEXT2     fb_color(0x55,0x55,0x77)
#define C_BLUE      fb_color(0x0f,0x5f,0xc0)
#define C_SEL_BG    fb_color(0xcc,0xdd,0xff)
#define C_SEL_BOR   fb_color(0x55,0x88,0xdd)
#define C_BTN_RED   fb_color(0xcc,0x33,0x33)
#define C_BTN_GRN   fb_color(0x22,0x88,0x44)
#define C_HOVER_BG  fb_color(0xe0,0xe8,0xff)

/* ---- Estado ---- */
static int fm_sel    = -1;  /* índice del archivo seleccionado (-1 = ninguno) */
static int fm_last_click = -1;
static int fm_view   = 0;   /* 0=Archivos FS, 1=Papelera */
static int fm_scroll = 0;   /* scroll del panel derecho (filas ocultas arriba) */
static int counter   = 1;

/* ---- Helpers ---- */
static void itoa10(int n, char *buf){
    int i=0;
    if(n==0){ buf[i++]='0'; }
    else {
        char t[12]; int j=0;
        while(n>0){ t[j++]='0'+n%10; n/=10; }
        while(j>0) buf[i++]=t[--j];
    }
    buf[i]='\0';
}
static int slen(const char *s){ int n=0; while(s[n]) n++; return n; }
static void scpy(char *d, const char *s, int n){
    int i=0; while(s[i]&&i<n-1){d[i]=s[i];i++;} d[i]='\0';
}

/* ---- Dibujar icono pixel-art de archivo ---- */
static void draw_file_art(int x, int y, const char *name){
    /* Hoja de papel con doblez en esquina superior derecha */
    uint32_t white = fb_color(0xff,0xff,0xff);
    uint32_t fold  = fb_color(0xcc,0xcc,0xff);
    uint32_t edge  = fb_color(0x88,0x88,0xbb);
    uint32_t line  = fb_color(0xaa,0xbb,0xdd);

    fb_fill_rect(x,   y,   26, 30, white);
    fb_fill_rect(x+20,y,    6,  6, fold);
    fb_fill_rect(x+20,y,    6,  1, edge);
    fb_fill_rect(x+20,y,    1,  6, edge);
    fb_draw_rect(x,   y,   26, 30, edge);

    /* líneas de texto simuladas */
    fb_fill_rect(x+3, y+8,  14, 2, line);
    fb_fill_rect(x+3, y+12, 18, 2, line);
    fb_fill_rect(x+3, y+16, 16, 2, line);
    fb_fill_rect(x+3, y+20, 10, 2, line);

    /* extensión con color si la hay */
    const char *dot=0;
    for(int i=0;name[i];i++) if(name[i]=='.') dot=name+i+1;
    if(dot){
        uint32_t ext_col = fb_color(0x22,0x66,0xcc);
        /* pequeño badge de extensión abajo */
        int ew=slen(dot)*6+4;
        if(ew>24) ew=24;
        fb_fill_rect(x+2, y+22, ew, 6, ext_col);
        /* solo mostramos si cabe — usamos solo primeras letras */
    }
}

/* ---- Dibujar icono de carpeta ---- */
static void draw_folder_art(int x, int y){
    uint32_t body = fb_color(0xff,0xcc,0x00);
    uint32_t tab  = fb_color(0xff,0xaa,0x00);
    uint32_t inner= fb_color(0xff,0xee,0x88);
    fb_fill_rect(x+1, y+8,  14, 4,  tab);
    fb_fill_rect(x+1, y+10, 24, 18, body);
    fb_fill_rect(x+3, y+13, 20, 13, inner);
}

/* ---- Calcular layout del panel derecho ---- */
static void fm_panel_rect(int wx,int wy,int ww,int wh,
                           int *px,int *py,int *pw,int *ph){
    *px = wx + BORDER + FM_SIDEBAR_W + 1;
    *py = wy + TITLEBAR_H + FM_TOOLBAR_H;
    *pw = ww - BORDER*2 - FM_SIDEBAR_W - 1;
    *ph = wh - TITLEBAR_H - BORDER - FM_TOOLBAR_H - FM_STATUSBAR_H;
}

/* ================================================================== */
/* DRAW                                                                  */
/* ================================================================== */
void filemanager_draw(int wx, int wy, int ww, int wh){
    uint32_t white = fb_color(0xff,0xff,0xff);

    /* ---- Toolbar / barra de ruta ---- */
    int tbx = wx+BORDER, tby = wy+TITLEBAR_H;
    int tbw = ww-BORDER*2, tbh = FM_TOOLBAR_H;
    fb_fill_rect(tbx, tby, tbw, tbh, C_TOOLBAR);
    fb_draw_rect(tbx, tby+tbh-1, tbw, 1, C_DIVIDER);

    /* Ruta actual */
    const char *ruta = (fm_view==0) ? "Este equipo  >  Archivos" : "Este equipo  >  Papelera";
    fb_fill_rect(tbx+4, tby+4, tbw-8, tbh-8, white);
    fb_draw_rect(tbx+4, tby+4, tbw-8, tbh-8, C_DIVIDER);
    fb_draw_str(tbx+10, tby+9, ruta, C_TEXT, white);

    /* ---- Panel izquierdo (sidebar) ---- */
    int sbx = wx+BORDER, sby = wy+TITLEBAR_H+FM_TOOLBAR_H;
    int sbh = wh-TITLEBAR_H-BORDER-FM_TOOLBAR_H-FM_STATUSBAR_H;
    fb_fill_rect(sbx, sby, FM_SIDEBAR_W, sbh, C_SIDEBAR);
    fb_draw_rect(sbx+FM_SIDEBAR_W-1, sby, 1, sbh, C_DIVIDER);

    /* Items del sidebar */
    const char *nav_items[] = { "Este equipo", "Archivos", "Papelera" };
    int nav_count = 3;
    for(int i=0; i<nav_count; i++){
        int iy = sby + 8 + i*28;
        int is_sel = (i==1 && fm_view==0) || (i==2 && fm_view==1);
        uint32_t ibg = is_sel ? C_SEL_BG : C_SIDEBAR;
        fb_fill_rect(sbx+2, iy-2, FM_SIDEBAR_W-4, 24, ibg);
        if(is_sel) fb_draw_rect(sbx+2, iy-2, FM_SIDEBAR_W-4, 24, C_SEL_BOR);

        /* Icono pequeño */
        uint32_t ic_col = (i==2) ? fb_color(0xcc,0x44,0x44) : fb_color(0xff,0xcc,0x00);
        if(i==0) ic_col = fb_color(0x22,0x66,0xcc);
        fb_fill_rect(sbx+6, iy+2, 16, 14, ic_col);
        fb_draw_str(sbx+26, iy+4, nav_items[i], is_sel ? C_BLUE : C_TEXT, ibg);
    }

    /* Botón "Nuevo archivo" en sidebar */
    int nb_y = sby + sbh - 38;
    fb_fill_rect(sbx+4, nb_y, FM_SIDEBAR_W-8, 26, C_BTN_GRN);
    fb_draw_str(sbx+8, nb_y+8, "+ Nuevo", white, C_BTN_GRN);

    /* ---- Panel derecho (área de iconos) ---- */
    int px, py, pw, ph;
    fm_panel_rect(wx,wy,ww,wh,&px,&py,&pw,&ph);
    fb_fill_rect(px, py, pw, ph, C_BG);

    /* Calcular cuántos iconos caben por fila */
    int cols = pw / FM_ICON_W;
    if(cols < 1) cols = 1;

    /* Leer entradas según vista */
    char names[32][FS_NAME_LEN];
    int  sizes[32];
    int  file_count = 0;

    if(fm_view == 0){
        /* Vista archivos FS */
        char nm[FS_NAME_LEN]; int sz;
        for(int k=0; k<32; k++){
            if(!fs_get_entry(k, nm, &sz)) break;
            scpy(names[file_count], nm, FS_NAME_LEN);
            sizes[file_count] = sz;
            file_count++;
        }
    } else {
        /* Vista papelera */
        char nm[FS_NAME_LEN]; int sz;
        for(int k=0; k<32; k++){
            if(!trash_get_entry(k, nm, &sz)) break;
            scpy(names[file_count], nm, FS_NAME_LEN);
            sizes[file_count] = sz;
            file_count++;
        }
    }

    /* Texto de "carpeta vacía" si no hay archivos */
    if(file_count == 0){
        fb_draw_str(px+20, py+20, "Esta carpeta esta vacia", C_TEXT2, C_BG);
    }

    /* Dibujar iconos */
    int scroll_skip = fm_scroll * cols;
    int visible = 0;
    for(int k=0; k<file_count; k++){
        if(k < scroll_skip) continue;
        int vi = k - scroll_skip;
        int col = vi % cols;
        int row = vi / cols;
        int ix = px + col * FM_ICON_W + (FM_ICON_W - FM_ICON_IMG)/2;
        int iy = py + row * FM_ICON_H + 4;

        if(iy + FM_ICON_H > py + ph) break; /* fuera del área visible */
        visible++;

        /* Fondo de selección */
        uint32_t ibg = (k==fm_sel) ? C_SEL_BG : C_BG;
        fb_fill_rect(px+col*FM_ICON_W, py+row*FM_ICON_H, FM_ICON_W, FM_ICON_H, ibg);
        if(k==fm_sel)
            fb_draw_rect(px+col*FM_ICON_W+1, py+row*FM_ICON_H+1, FM_ICON_W-2, FM_ICON_H-2, C_SEL_BOR);

        /* Arte del icono */
        draw_file_art(ix, iy, names[k]);

        /* Nombre del archivo — centrado bajo el icono */
        int nlen = slen(names[k]);
        if(nlen > 9) nlen = 9;
        char short_name[10]; scpy(short_name, names[k], 10);
        int tx = px + col*FM_ICON_W + (FM_ICON_W - nlen*9)/2;
        if(tx < px+col*FM_ICON_W+2) tx = px+col*FM_ICON_W+2;
        fb_draw_str(tx, iy + FM_ICON_IMG + 6, short_name, C_TEXT, ibg);

        /* Tamaño pequeño debajo del nombre */
        char sb[10]; itoa10(sizes[k], sb);
        int slen2 = slen(sb);
        int tx2 = px + col*FM_ICON_W + (FM_ICON_W - (slen2+1)*9)/2;
        fb_draw_str(tx2, iy + FM_ICON_IMG + 18, sb, C_TEXT2, ibg);

        /* Botón X (eliminar) — esquina superior derecha del icono */
        if(k==fm_sel && fm_view==0){
            int bx2 = px+col*FM_ICON_W+FM_ICON_W-18;
            int by2 = py+row*FM_ICON_H+2;
            fb_fill_rect(bx2, by2, 16, 16, C_BTN_RED);
            fb_draw_str(bx2+4, by2+3, "x", white, C_BTN_RED);
        }
    }
    (void)visible;

    /* ---- Barra de estado ---- */
    int stx = wx+BORDER, sty = wy+wh-BORDER-FM_STATUSBAR_H;
    int stw = ww-BORDER*2;
    fb_fill_rect(stx, sty, stw, FM_STATUSBAR_H, C_STATUS);
    fb_draw_rect(stx, sty, stw, 1, C_DIVIDER);

    char st_buf[48]; int p=0;
    char num_buf[8]; itoa10(file_count, num_buf);
    for(int i=0; num_buf[i]; i++) st_buf[p++]=num_buf[i];
    const char *suf = (fm_view==0) ? " archivos" : " elementos en papelera";
    for(int i=0; suf[i]; i++) st_buf[p++]=suf[i];
    st_buf[p]='\0';
    fb_draw_str(stx+8, sty+6, st_buf, C_TEXT2, C_STATUS);

    /* Info de selección */
    if(fm_sel>=0 && fm_sel<file_count){
        char sel_buf[48]; int sp=0;
        const char *sl = "Seleccionado: ";
        for(int i=0; sl[i]; i++) sel_buf[sp++]=sl[i];
        for(int i=0; names[fm_sel][i]&&sp<46; i++) sel_buf[sp++]=names[fm_sel][i];
        sel_buf[sp]='\0';
        int info_x = stx+stw-slen(sel_buf)*9-12;
        if(info_x > stx+200) fb_draw_str(info_x, sty+6, sel_buf, C_BLUE, C_STATUS);
    }
}

/* ================================================================== */
/* CLICK                                                                 */
/* ================================================================== */
int filemanager_click(int wx, int wy, int ww, int wh, int mx, int my){
    /* ---- Sidebar ---- */
    int sbx = wx+BORDER, sby = wy+TITLEBAR_H+FM_TOOLBAR_H;
    int sbh = wh-TITLEBAR_H-BORDER-FM_TOOLBAR_H-FM_STATUSBAR_H;

    /* Items de navegación */
    const int nav_count = 3;
    for(int i=0; i<nav_count; i++){
        int iy = sby+8+i*28;
        if(mx>=sbx+2 && mx<sbx+FM_SIDEBAR_W-2 && my>=iy-2 && my<iy+22){
            if(i==1){ fm_view=0; fm_sel=-1; fm_scroll=0; fm_last_click=-1; return 0; }
            if(i==2){ fm_view=1; fm_sel=-1; fm_scroll=0; fm_last_click=-1; return 0; }
            /* i==0: "Este equipo" → ir a archivos */
            fm_view=0; fm_sel=-1; fm_scroll=0; fm_last_click=-1; return 0;
        }
    }

    /* Botón "Nuevo" */
    int nb_y = sby + sbh - 38;
    if(mx>=sbx+4 && mx<sbx+FM_SIDEBAR_W-4 && my>=nb_y && my<nb_y+26){
        if(fm_view==0){
            char fname[FS_NAME_LEN];
            char nb[8]; itoa10(counter++, nb);
            int p=0;
            const char *pref="nuevo"; for(int i=0;pref[i];i++) fname[p++]=pref[i];
            for(int i=0;nb[i];i++) fname[p++]=nb[i];
            const char *ext=".txt"; for(int i=0;ext[i];i++) fname[p++]=ext[i];
            fname[p]='\0';
            fs_write(fname,"",0);
            notepad_load(fname);
            fm_sel=-1;
            return 1; /* abre notas */
        }
        return 0;
    }

    /* ---- Panel de iconos ---- */
    int px, py, pw, ph;
    fm_panel_rect(wx,wy,ww,wh,&px,&py,&pw,&ph);

    if(mx<px||mx>=px+pw||my<py||my>=py+ph) return 0;

    int cols = pw / FM_ICON_W;
    if(cols < 1) cols = 1;

    /* Leer lista actual */
    char names[32][FS_NAME_LEN]; int sizes[32]; int file_count=0;
    if(fm_view==0){
        char nm[FS_NAME_LEN]; int sz;
        for(int k=0;k<32;k++){
            if(!fs_get_entry(k,nm,&sz)) break;
            scpy(names[file_count],nm,FS_NAME_LEN);
            sizes[file_count]=sz;
            file_count++;
        }
    } else {
        char nm[FS_NAME_LEN]; int sz;
        for(int k=0;k<32;k++){
            if(!trash_get_entry(k,nm,&sz)) break;
            scpy(names[file_count],nm,FS_NAME_LEN);
            sizes[file_count]=sz;
            file_count++;
        }
    }

    int scroll_skip = fm_scroll * cols;

    for(int k=0; k<file_count; k++){
        if(k<scroll_skip) continue;
        int vi = k - scroll_skip;
        int col = vi % cols;
        int row = vi / cols;
        int cx = px + col * FM_ICON_W;
        int cy = py + row * FM_ICON_H;
        if(cy+FM_ICON_H > py+ph) break;

        if(mx>=cx && mx<cx+FM_ICON_W && my>=cy && my<cy+FM_ICON_H){
            /* Botón eliminar (solo si ya estaba seleccionado y es vista FS) */
            if(k==fm_sel && fm_view==0){
                int bx2 = cx+FM_ICON_W-18;
                int by2 = cy+2;
                if(mx>=bx2&&mx<bx2+16&&my>=by2&&my<by2+16){
                    /* Eliminar -> papelera */
                    char buf[FS_DATA_LEN];
                    int r=fs_read(names[k],buf,sizeof(buf));
                    trash_add(names[k], r>0?buf:"", r>0?r:0);
                    fs_delete(names[k]);
                    fm_sel=-1; fm_last_click=-1;
                    return 2; /* señal: no abrir notas */
                }
            }

            /* Doble click -> abrir */
            if(k==fm_last_click){
                if(fm_view==0){
                    notepad_load(names[k]);
                    fm_last_click=-1;
                    return 1;
                }
                fm_last_click=-1;
                return 0;
            }
            /* Primer click -> seleccionar */
            fm_sel=k;
            fm_last_click=k;
            return 0;
        }
    }

    /* Click en área vacía → deseleccionar */
    fm_sel=-1;
    fm_last_click=-1;
    return 0;
}
