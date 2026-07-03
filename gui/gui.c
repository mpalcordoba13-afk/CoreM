#include "gui.h"
#include "framebuffer.h"
#include "mouse.h"
#include "keyboard.h"
#include "rtc.h"
#include "sound.h"
#include "wallpaper.h"
#include "calc.h"
#include "terminal.h"
#include "filemanager.h"
#include "notepad.h"
#include "sysmonitor.h"
#include "settings.h"
#include "fs.h"
#include "snake.h"
#include "tetris.h"
#include "trash.h"
#include "volume.h"
#include "tips.h"
#include "gallery.h"
#include "imageviewer.h"
#include "musicplayer.h"
#include "net.h"
#include "guide.h"
#include "pacman.h"
#include "pong.h"
#include "browser.h"
#include "dhcp.h"
#include "pciviewer.h"
#include "users.h"
#include "usbexplorer.h"
#include "desktop.h"
#include "code.h"
#include "toast.h"
#include "syslog.h"
#include "lockscreen.h"
#include "screensaver.h"
#include "paint.h"
#include "spreadsheet.h"
#include "calendar.h"
#include "alarm.h"
#include "minesweeper.h"
#include "game2048.h"
#include "chess.h"
#include "breakout.h"
#include "conway.h"
#include "timer.h"
#include <stdint.h>
#include <stddef.h>

#define COL(r,g,b) fb_color(r,g,b)

static uint32_t C_WIN_BG, C_TITLEBAR, C_TITLE_FOC, C_TITLE_TXT;
static uint32_t C_BORDER_COL, C_BTN_CLOSE, C_TEXT, C_TASKBAR;
static uint32_t C_TASKBAR_TXT, C_SEP, C_BTN_MIN, C_BTN_MAX, C_STARTMENU, C_DESKICON_BG;

static void apply_theme(void){
    if (g_dark_theme) {
        C_WIN_BG     = COL(0x26,0x26,0x33);
        C_TITLEBAR   = COL(0x18,0x2a,0x44);
        C_TITLE_FOC  = COL(0xb0,0x35,0x4a);
        C_TITLE_TXT  = COL(0xff,0xff,0xff);
        C_BORDER_COL = COL(0x10,0x10,0x1c);
        C_BTN_CLOSE  = COL(0xa8,0x22,0x33);
        C_BTN_MIN    = COL(0x1e,0x55,0x2c);
        C_BTN_MAX    = COL(0x33,0x55,0x88);
        C_TEXT       = COL(0xea,0xea,0xf4);
        C_TASKBAR    = COL(0x08,0x08,0x12);
        C_TASKBAR_TXT= COL(0xcc,0xcc,0xdd);
        C_SEP        = COL(0x28,0x44,0x77);
        C_STARTMENU  = COL(0x0c,0x12,0x28);
        C_DESKICON_BG= COL(0x20,0x20,0x36);
    } else {
        C_WIN_BG     = COL(0xf2,0xf2,0xf2);
        C_TITLEBAR   = COL(0x0f,0x34,0x60);
        C_TITLE_FOC  = COL(0xe9,0x45,0x60);
        C_TITLE_TXT  = COL(0xff,0xff,0xff);
        C_BORDER_COL = COL(0x22,0x22,0x44);
        C_BTN_CLOSE  = COL(0xcc,0x22,0x33);
        C_BTN_MIN    = COL(0x22,0x66,0x33);
        C_BTN_MAX    = COL(0x44,0x77,0xaa);
        C_TEXT       = COL(0x11,0x11,0x22);
        C_TASKBAR    = COL(0x0a,0x0a,0x1a);
        C_TASKBAR_TXT= COL(0xcc,0xcc,0xdd);
        C_SEP        = COL(0x30,0x50,0x90);
        C_STARTMENU  = COL(0x10,0x18,0x38);
        C_DESKICON_BG= COL(0xff,0xff,0xff);
    }
}

static window_t windows[MAX_WINDOWS];
static int win_count=0;
static int mx=200,my=200;
static int btn_left=0,prev_btn_left=0;
static int btn_right=0,prev_btn_right=0;
static int startmenu_open=0;

/* Menu contextual del escritorio */
static int cmenu_open=0, cmenu_x=0, cmenu_y=0;
static int cmenu_target_win=-1;   /* ventana sobre la que se hizo click derecho */
static int cmenu_target_icon=-1;  /* icono de escritorio (-1 si no) */
#define CMENU_W 180
static int taskbar_start_index = 0; /* scroll index for visible window tabs */

/* Items dinámicos según contexto */
#define CMENU_MAX 8
static char  cmenu_item_labels[CMENU_MAX][32];
static int   cmenu_item_count=0;

typedef enum {
    CMENU_ACT_WALLPAPER=0,
    CMENU_ACT_NUEVA_NOTA,
    CMENU_ACT_CONFIG,
    CMENU_ACT_ACERCA,
    CMENU_ACT_CERRAR_WIN,
    CMENU_ACT_MINIMIZAR,
    CMENU_ACT_MAXIMIZAR,
    CMENU_ACT_ABRIR_ICON,
    CMENU_ACT_NONE,
} cmenu_action_t;
static cmenu_action_t cmenu_actions[CMENU_MAX];

/* ---- Menu WiFi en taskbar ---- */
#include "ndis.h"
#include "dhcp.h"
static int wifi_menu_open = 0;
/* Campos editables: IP, mascara, gateway, DNS (4 octetos c/u en texto) */
#define WFIELD_COUNT 4
static char wf_ip[16]   = "10.0.2.15";
static char wf_mask[16] = "255.255.255.0";
static char wf_gw[16]   = "10.0.2.2";
static char wf_dns[16]  = "10.0.2.3";
static int  wf_active   = -1; /* campo actualmente seleccionado para editar */
static char *wfields[WFIELD_COUNT] = {wf_ip, wf_mask, wf_gw, wf_dns};
static const char *wflabels[WFIELD_COUNT] = {"IP:","Mascara:","Gateway:","DNS:"};
static int  wf_dhcp_status = 0;  /* 0=no intentado, 1=OK, -1=fallo */
static char wf_status_msg[48] = "";
#define WMENU_W  240
#define WMENU_H  262

/* ---- utils ---- */
static void scpy(char *d,const char *s,int max){
    int i=0;while(s[i]&&i<max-1){d[i]=s[i];i++;}d[i]='\0';
}
static int slen(const char *s){int n=0;while(s[n])n++;return n;}
static int seq(const char *a,const char *b){
    while(*a&&*b){if(*a!=*b)return 0;a++;b++;}return *a==*b;
}
static int strequal(const char *a,const char *b){ int i=0; while(a[i]&&b[i]&&a[i]==b[i]) i++; return a[i]==b[i]; }

/* ---- cursor ---- */
static void draw_cursor(int x,int y){
    uint32_t w=COL(0xff,0xff,0xff),b=COL(0x00,0x00,0x00);
    for(int i=0;i<14;i++){fb_put_pixel(x,y+i,b);fb_put_pixel(x+1,y+i,b);}
    for(int i=0;i<12;i++){fb_put_pixel(x+i,y,b);fb_put_pixel(x+i,y+1,b);}
    for(int i=2;i<11;i++){fb_put_pixel(x+i+1,y+i,b);fb_put_pixel(x+i+2,y+i,b);}
    for(int i=2;i<13;i++)fb_put_pixel(x+2,y+i,w);
    for(int i=2;i<11;i++)fb_put_pixel(x+i,y+2,w);
    for(int i=3;i<11;i++)fb_put_pixel(x+i,y+i,w);
}

/* ---- helpers para activar/enfocar una ventana por titulo ---- */

/* Devuelve 1 si la ventana en (tx,ty,tw,th) se superpone con alguna ventana activa
   excepto la de índice excl */
static int win_overlaps_any(int tx, int ty, int tw, int th, int excl){
    for(int i=0;i<win_count;i++){
        if(i==excl) continue;
        if(!windows[i].active||windows[i].minimized) continue;
        window_t *o=&windows[i];
        /* AABB overlap */
        if(tx < o->x+o->w && tx+tw > o->x &&
           ty < o->y+o->h && ty+th > o->y) return 1;
    }
    return 0;
}

/* Busca una posición libre para la ventana de tamaño (tw,th) y la coloca ahí.
   Si no encuentra sitio, aplica un desplazamiento en cascada de 24px. */
static void place_window_no_overlap(window_t *win){
    if(win->maximized) return;
    int sw=fb_width(), sh=fb_height()-28;
    int tw=win->w, th=win->h;
    /* Intentar posiciones en una cuadrícula de 60x40 px */
    int step_x=60, step_y=40;
    for(int ty=0; ty+th<=sh; ty+=step_y){
        for(int tx=0; tx+tw<=sw; tx+=step_x){
            if(!win_overlaps_any(tx,ty,tw,th, (int)(win-windows))){
                win->x=tx; win->y=ty;
                return;
            }
        }
    }
    /* No se encontró lugar libre: cascada de 24px */
    int base_x=40, base_y=40;
    /* Contar cuántas ventanas activas hay para el offset */
    int n=0;
    for(int i=0;i<win_count;i++) if(windows[i].active&&!windows[i].minimized) n++;
    win->x=(base_x + n*24) % (sw-tw > 0 ? sw-tw : 1);
    win->y=(base_y + n*18) % (sh-th > 0 ? sh-th : 1);
}

/* Tabla de init lazy: cada app se inicializa la primera vez que se abre */
static int lazy_inited[MAX_WINDOWS];

static void lazy_init_window(int i){
    if(lazy_inited[i]) return;
    lazy_inited[i]=1;
    win_type_t t=windows[i].type;
    if(t==WIN_SNAKE)        snake_init();
    else if(t==WIN_TETRIS)  tetris_init();
    else if(t==WIN_PACMAN)  pacman_init();
    else if(t==WIN_PONG)    pong_init();
    else if(t==WIN_TRASH)   trash_init();
    else if(t==WIN_MUSICPLAYER) musicplayer_init();
    else if(t==WIN_SETTINGS)    settings_init();
    else if(t==WIN_BROWSER)     browser_init();
    else if(t==WIN_USBEXPLORER) usbexplorer_init();
    else if(t==WIN_CODEEDITOR)  code_init();
    else if(t==WIN_PAINT)       paint_init();
    else if(t==WIN_SPREADSHEET) spreadsheet_init();
    else if(t==WIN_CALENDAR)    calendar_init();
    else if(t==WIN_ALARM)       alarm_init();
    else if(t==WIN_SYSLOG)      syslog_init();
    else if(t==WIN_MINESWEEPER) minesweeper_init();
    else if(t==WIN_2048)        game2048_init();
    else if(t==WIN_CHESS)       chess_init();
    else if(t==WIN_BREAKOUT)    breakout_init();
    else if(t==WIN_CONWAY)      conway_init();
    /* Terminal, Notepad, Calc ya inicializados en gui_init */
    /* Logging */
    syslog_write(windows[i].title);
    toast_show(windows[i].title);
}

void open_window_by_title(const char *title){
    for(int i=0;i<win_count;i++){
        if(seq(windows[i].title,title)){
            int was_inactive = !windows[i].active || windows[i].minimized;
            lazy_init_window(i);
            windows[i].active=1;
            windows[i].minimized=0;
            for(int j=0;j<win_count;j++)windows[j].focused=0;
            windows[i].focused=1;
            if(was_inactive){ place_window_no_overlap(&windows[i]); wallpaper_invalidate(); }
            return;
        }
    }
}

/* ---- ventana ---- */
static void draw_window(int i){
    window_t *w=&windows[i];
    if(!w->active||w->minimized)return;
    int x=w->x,y=w->y,ww=w->w,wh=w->h;
    uint32_t tb=w->focused?C_TITLE_FOC:C_TITLEBAR;

    fb_fill_rect(x+5,y+5,ww,wh,COL(0x05,0x05,0x10));
    fb_fill_rect(x,y,ww,wh,C_BORDER_COL);
    fb_fill_rect(x+BORDER,y+TITLEBAR_H,ww-BORDER*2,wh-TITLEBAR_H-BORDER,C_WIN_BG);
    fb_fill_rect(x+BORDER,y+BORDER,ww-BORDER*2,TITLEBAR_H-BORDER,tb);

    int tlen=slen(w->title)*9;
    fb_draw_str(x+BORDER+(ww-BORDER*2-tlen)/2,y+BORDER+5,w->title,C_TITLE_TXT,tb);

    /* Botones: minimizar _, maximizar [], cerrar x (de derecha a izquierda) */
    int cbx=x+ww-BORDER-16, cby=y+BORDER+2;
    fb_fill_rect(cbx,cby,15,15,C_BTN_CLOSE);
    fb_draw_str(cbx+4,cby+3,"x",C_TITLE_TXT,C_BTN_CLOSE);

    int xbx=x+ww-BORDER-34, xby=y+BORDER+2;
    fb_fill_rect(xbx,xby,15,15,C_BTN_MAX);
    fb_draw_rect(xbx+3,xby+3,9,9,C_TITLE_TXT);

    int mbx=x+ww-BORDER-52, mby=y+BORDER+2;
    fb_fill_rect(mbx,mby,15,15,C_BTN_MIN);
    fb_draw_str(mbx+4,mby+8,"_",C_TITLE_TXT,C_BTN_MIN);

    /* Manija de redimension (esquina inferior derecha) */
    if(!w->maximized){
        for(int k=0;k<3;k++)
            fb_fill_rect(x+ww-6-k*4,y+wh-3,3,3,COL(0x88,0x88,0xaa));
    }

    /* Contenido por tipo */
    if(w->type==WIN_NORMAL){
        fb_draw_str(x+BORDER+8,y+TITLEBAR_H+10,w->content,C_TEXT,C_WIN_BG);
    }
    else if(w->type==WIN_CALC){
        calc_draw(x,y,ww);
    }
    else if(w->type==WIN_TERMINAL){
        terminal_draw(x,y,ww,wh);
    }
    else if(w->type==WIN_FILEMANAGER){
        filemanager_draw(x,y,ww,wh);
    }
    else if(w->type==WIN_NOTEPAD){
        notepad_draw(x,y,ww,wh);
    }
    else if(w->type==WIN_SYSMONITOR){
        sysmonitor_draw(x,y,ww,wh);
    }
    else if(w->type==WIN_SETTINGS){
        settings_draw(x,y,ww,wh);
    }
    else if(w->type==WIN_SNAKE){
        snake_draw(x,y,ww,wh);
    }
    else if(w->type==WIN_TETRIS){
        tetris_draw(x,y,ww,wh);
    }
    else if(w->type==WIN_TRASH){
        trash_draw(x,y,ww,wh);
    }
    else if(w->type==WIN_GALLERY){
        gallery_draw(x,y,ww,wh);
    }
    else if(w->type==WIN_IMAGEVIEWER){
        imageviewer_draw(x,y,ww,wh);
    }
    else if(w->type==WIN_MUSICPLAYER){
        musicplayer_draw(x,y,ww,wh);
    }
    else if(w->type==WIN_GUIDE){
        guide_draw(x,y,ww,wh);
    }
    else if(w->type==WIN_PACMAN){
        pacman_draw(x,y,ww,wh);
    }
    else if(w->type==WIN_PONG){
        pong_draw(x,y,ww,wh);
    }
    else if(w->type==WIN_BROWSER){
        browser_draw(x,y,ww,wh);
    }
    else if(w->type==WIN_USBEXPLORER){
        usbexplorer_draw(x,y,ww,wh);
    }
    else if(w->type==WIN_CODEEDITOR){
        code_draw(x,y,ww,wh);
    }
    else if(w->type==WIN_PAINT){
        paint_draw(x,y,ww,wh);
    }
    else if(w->type==WIN_SPREADSHEET){
        spreadsheet_draw(x,y,ww,wh);
    }
    else if(w->type==WIN_CALENDAR){
        calendar_draw(x,y,ww,wh);
    }
    else if(w->type==WIN_ALARM){
        alarm_draw(x,y,ww,wh);
    }
    else if(w->type==WIN_SYSLOG){
        syslog_draw(x,y,ww,wh);
    }
    else if(w->type==WIN_MINESWEEPER){
        minesweeper_draw(x,y,ww,wh);
    }
    else if(w->type==WIN_2048){
        game2048_draw(x,y,ww,wh);
    }
    else if(w->type==WIN_CHESS){
        chess_draw(x,y,ww,wh);
    }
    else if(w->type==WIN_BREAKOUT){
        breakout_draw(x,y,ww,wh);
    }
    else if(w->type==WIN_CONWAY){
        conway_draw(x,y,ww,wh);
    }
    else if(w->type==WIN_PCIVIEWER){
        pciviewer_draw(x,y,ww,wh);
    }
    else if(w->type==WIN_WALLPAPER){
        const char *names[]={"Solido","Gradiente","Estrellas","Grilla","Atardecer"};
        for(int k=0;k<5;k++){
            int bx2=x+BORDER+10,by2=y+TITLEBAR_H+10+k*34;
            fb_fill_rect(bx2,by2,150,26,COL(0x20,0x50,0x90));
            fb_draw_rect(bx2,by2,150,26,COL(0x44,0x88,0xcc));
            fb_draw_str(bx2+8,by2+8,names[k],COL(0xff,0xff,0xff),COL(0x20,0x50,0x90));
        }
        fb_draw_str(x+BORDER+10,y+TITLEBAR_H+186,"Click para cambiar",C_TEXT,C_WIN_BG);
    }
    else if(w->type==WIN_CLOCK){
        char tbuf[16]; rtc_get_str_fmt(tbuf,g_clock_12h);
        int cx2=x+ww/2,cy2=y+TITLEBAR_H+78;
        fb_fill_circle(cx2,cy2,68,COL(0x22,0x22,0x44));
        fb_draw_circle(cx2,cy2,68,COL(0x88,0xaa,0xff));
        static const int hx[12]={0,30,52,60,52,30,0,-30,-52,-60,-52,-30};
        static const int hy[12]={-60,-52,-30,0,30,52,60,52,30,0,-30,-52};
        for(int h=0;h<12;h++) fb_fill_circle(cx2+hx[h]*68/60,cy2+hy[h]*68/60,3,COL(0xaa,0xcc,0xff));

        rtc_time_t rt; rtc_read(&rt);
        int hour12 = rt.hour % 12;
        int min_idx = (rt.min/5)%12;
        fb_draw_line(cx2,cy2,cx2+hx[hour12]*4/10,cy2+hy[hour12]*4/10,COL(0xff,0xff,0xff));
        fb_draw_line(cx2,cy2,cx2+hx[min_idx]*9/10,cy2+hy[min_idx]*9/10,COL(0xff,0xcc,0x66));
        fb_fill_circle(cx2,cy2,3,COL(0xff,0xff,0xff));

        int tlen2=slen(tbuf)*9;
        fb_fill_rect(x+BORDER+8,y+TITLEBAR_H+156,ww-BORDER*2-16,18,C_WIN_BG);
        fb_draw_str(cx2-tlen2/2,cy2+78,tbuf,C_TEXT,C_WIN_BG);
        fb_draw_int(x+BORDER+10,y+TITLEBAR_H+160,rt.day,C_TEXT,C_WIN_BG);
        fb_draw_str(x+BORDER+28,y+TITLEBAR_H+160,"/",C_TEXT,C_WIN_BG);
        fb_draw_int(x+BORDER+36,y+TITLEBAR_H+160,rt.month,C_TEXT,C_WIN_BG);
        fb_draw_str(x+BORDER+54,y+TITLEBAR_H+160,"/",C_TEXT,C_WIN_BG);
        fb_draw_int(x+BORDER+62,y+TITLEBAR_H+160,rt.year,C_TEXT,C_WIN_BG);
    }
    else if(w->type==WIN_ABOUT){
        int ax=x+BORDER+10,ay=y+TITLEBAR_H+10;
        fb_draw_str(ax,ay,   "CoreM v1.0",COL(0x0f,0x34,0x60),C_WIN_BG);
        fb_draw_str(ax,ay+18,"Arquitectura: x86 32-bit",C_TEXT,C_WIN_BG);
        fb_draw_str(ax,ay+36,"Video: VBE 1280x720",C_TEXT,C_WIN_BG);
        fb_draw_str(ax,ay+54,"Mouse: PS/2 + USB tablet",C_TEXT,C_WIN_BG);
        fb_draw_str(ax,ay+72,"Audio: PC Speaker",C_TEXT,C_WIN_BG);
        fb_draw_str(ax,ay+90,"Filesystem en RAM",C_TEXT,C_WIN_BG);
        fb_draw_str(ax,ay+108,"100% C y Assembly",C_TEXT,C_WIN_BG);
        fb_fill_circle(x+ww-55,y+TITLEBAR_H+55,38,COL(0x0f,0x34,0x60));
        fb_draw_str(x+ww-78,y+TITLEBAR_H+48,"CoreM",COL(0xff,0xff,0xff),COL(0x0f,0x34,0x60));
    }
}

/* ---- Menu inicio ---- */
#define SMENU_W  210
static const char *smenu_labels[] = {
    "Bienvenido","Info","Reloj","Fondo","Acerca de",
    "Calculadora","Terminal","Archivos","Notas","Monitor","Configuracion",
    "Snake","Tetris","Pacman","Pong","Navegador","Papelera","Galeria","Imagenes","Musica","Guia",
    "Dispositivos","USB","Code",
    /* Nuevas apps */
    "Paint","Hoja de Calculo","Calendario","Alarma",
    "Logs del Sistema","Buscaminas","2048","Ajedrez","Breakout","Conway",
    "Limpiar escritorio",
    "Bloquear"
};
#define SMENU_ITEMS (sizeof(smenu_labels)/sizeof(smenu_labels[0]))
#define SMENU_H (28+SMENU_ITEMS*24)

static int smenu_start_index = 0; /* for scrolling */
static void draw_startmenu(void){
    if(!startmenu_open)return;
    int sh=fb_height();
    int menu_h = SMENU_H;
    if(menu_h > sh - 28) menu_h = sh - 28;
    if(menu_h < 28 + 24) menu_h = 28 + 24;
    int sx=2,sy=sh-28-menu_h;
    if(sy<0) sy=0;
    fb_fill_rect(sx,sy,SMENU_W,menu_h,C_STARTMENU);
    fb_draw_rect(sx,sy,SMENU_W,menu_h,C_SEP);
    fb_fill_rect(sx,sy,SMENU_W,24,C_TITLE_FOC);
    fb_draw_str(sx+8,sy+7,"CoreM",C_TITLE_TXT,C_TITLE_FOC);
    int visible = (menu_h-28)/24;
    if(visible<1) visible=1;
    int max_start = SMENU_ITEMS - visible;
    if(max_start < 0) max_start = 0;
    if(smenu_start_index < 0) smenu_start_index = 0;
    if(smenu_start_index > max_start) smenu_start_index = max_start;
    for(int i=0;i<visible;i++){
        int idx = smenu_start_index + i;
        if(idx>=SMENU_ITEMS) break;
        int iy=sy+26+i*24;
        fb_fill_rect(sx+4,iy,SMENU_W-8,22,C_STARTMENU);
        fb_draw_str(sx+14,iy+6,smenu_labels[idx],C_TASKBAR_TXT,C_STARTMENU);
        fb_fill_rect(sx+4,iy+22,SMENU_W-8,1,COL(0x20,0x30,0x50));
    }
}

static void startmenu_click(int px,int py){
    int sh=fb_height();
    int menu_h = SMENU_H;
    if(menu_h > sh - 28) menu_h = sh - 28;
    if(menu_h < 28 + 24) menu_h = 28 + 24;
    int sx=2,sy=sh-28-menu_h; if(sy<0)sy=0;
    if(px<sx||px>=sx+SMENU_W||py<sy+26||py>=sy+menu_h)return;
    int item=(py-(sy+26))/24;
    int visible = (menu_h-28)/24; if(visible<1) visible=1;
    int max_start = SMENU_ITEMS - visible;
    if(max_start < 0) max_start = 0;
    if(smenu_start_index < 0) smenu_start_index = 0;
    if(smenu_start_index > max_start) smenu_start_index = max_start;
    int idx = smenu_start_index + item;
    if(item<0||item>=visible||idx<0||idx>=(int)SMENU_ITEMS) return;
    /* Bloquear pantalla es especial */
    if(idx==SMENU_ITEMS-1){ lockscreen_lock(); startmenu_open=0; return; }
    /* Limpiar escritorio (entrada por etiqueta) */
    if(strequal(smenu_labels[idx],"Limpiar escritorio")){
        desktop_clean(); startmenu_open=0; sound_click(); return;
    }
    open_window_by_title(smenu_labels[idx]);
    sound_click();
}

/* ---- menu contextual inteligente ---- */
static void scpy_cm(char *d,const char *s,int max){int i=0;while(s[i]&&i<max-1){d[i]=s[i];i++;}d[i]='\0';}

static void build_cmenu(int win_idx, int icon_idx){
    cmenu_item_count=0;
    cmenu_target_win=win_idx;
    cmenu_target_icon=icon_idx;

    if(win_idx>=0){
        /* Menú de ventana */
        window_t *w=&windows[win_idx];
        scpy_cm(cmenu_item_labels[cmenu_item_count],"Cerrar",32);
        cmenu_actions[cmenu_item_count++]=CMENU_ACT_CERRAR_WIN;
        scpy_cm(cmenu_item_labels[cmenu_item_count],
                w->minimized?"Restaurar":"Minimizar",32);
        cmenu_actions[cmenu_item_count++]=CMENU_ACT_MINIMIZAR;
        scpy_cm(cmenu_item_labels[cmenu_item_count],
                w->maximized?"Restaurar tamanio":"Maximizar",32);
        cmenu_actions[cmenu_item_count++]=CMENU_ACT_MAXIMIZAR;
    } else if(icon_idx>=0){
        /* Menú de icono de escritorio */
        scpy_cm(cmenu_item_labels[cmenu_item_count],"Abrir",32);
        cmenu_actions[cmenu_item_count++]=CMENU_ACT_ABRIR_ICON;
        if(desktop_icon_is_file(icon_idx)){
            scpy_cm(cmenu_item_labels[cmenu_item_count],"Abrir en Notas",32);
            cmenu_actions[cmenu_item_count++]=CMENU_ACT_NUEVA_NOTA;
        }
        scpy_cm(cmenu_item_labels[cmenu_item_count],"Cambiar fondo",32);
        cmenu_actions[cmenu_item_count++]=CMENU_ACT_WALLPAPER;
    } else {
        /* Menú de escritorio */
        scpy_cm(cmenu_item_labels[cmenu_item_count],"Cambiar fondo",32);
        cmenu_actions[cmenu_item_count++]=CMENU_ACT_WALLPAPER;
        scpy_cm(cmenu_item_labels[cmenu_item_count],"Nueva nota",32);
        cmenu_actions[cmenu_item_count++]=CMENU_ACT_NUEVA_NOTA;
        scpy_cm(cmenu_item_labels[cmenu_item_count],"Configuracion",32);
        cmenu_actions[cmenu_item_count++]=CMENU_ACT_CONFIG;
        scpy_cm(cmenu_item_labels[cmenu_item_count],"Acerca de",32);
        cmenu_actions[cmenu_item_count++]=CMENU_ACT_ACERCA;
    }
}

static void draw_cmenu(void){
    if(!cmenu_open) return;
    int h=cmenu_item_count*28+8;
    /* Sombra */
    fb_fill_rect(cmenu_x+3,cmenu_y+3,CMENU_W,h,fb_color(0,0,0));
    fb_fill_rect(cmenu_x,cmenu_y,CMENU_W,h,C_STARTMENU);
    fb_draw_rect(cmenu_x,cmenu_y,CMENU_W,h,C_SEP);
    for(int i=0;i<cmenu_item_count;i++){
        int iy=cmenu_y+4+i*28;
        /* Hover: resaltar si el mouse está encima */
        if(mx>=cmenu_x&&mx<cmenu_x+CMENU_W&&my>=iy&&my<iy+28){
            fb_fill_rect(cmenu_x+2,iy,CMENU_W-4,28,COL(0x22,0x44,0x88));
        }
        fb_draw_str(cmenu_x+14,iy+8,cmenu_item_labels[i],C_TASKBAR_TXT,
                    (mx>=cmenu_x&&mx<cmenu_x+CMENU_W&&my>=iy&&my<iy+28)?
                    COL(0x22,0x44,0x88):C_STARTMENU);
        if(i<cmenu_item_count-1)
            fb_fill_rect(cmenu_x+4,iy+28,CMENU_W-8,1,COL(0x20,0x30,0x50));
    }
}

static void cmenu_click(int px,int py){
    int h=cmenu_item_count*28+8;
    if(px<cmenu_x||px>=cmenu_x+CMENU_W||py<cmenu_y||py>=cmenu_y+h)return;
    int item=(py-(cmenu_y+4))/28;
    if(item<0||item>=cmenu_item_count)return;

    cmenu_action_t act=cmenu_actions[item];
    if(act==CMENU_ACT_WALLPAPER) open_window_by_title("Fondo");
    else if(act==CMENU_ACT_NUEVA_NOTA){
        if(cmenu_target_icon>=0 && desktop_icon_is_file(cmenu_target_icon)){
            extern void notepad_load(const char*);
            notepad_load(desktop_icon_appname(cmenu_target_icon));
            open_window_by_title("Notas");
        } else {
            static int counter=100;
            char fname[32]="nota";
            char rev[8]; int rj=0,c=counter++;
            if(!c){rev[rj++]='0';}
            else{int cc=c;while(cc>0){rev[rj++]='0'+cc%10;cc/=10;}}
            int p=4;
            for(int q=rj-1;q>=0;q--) fname[p++]=rev[q];
            fname[p++]='.';fname[p++]='t';fname[p++]='x';fname[p++]='t';fname[p]='\0';
            fs_write(fname,"",0);
            notepad_load(fname);
            open_window_by_title("Notas");
        }
    }
    else if(act==CMENU_ACT_CONFIG)   open_window_by_title("Configuracion");
    else if(act==CMENU_ACT_ACERCA)   open_window_by_title("Acerca de");
    else if(act==CMENU_ACT_CERRAR_WIN && cmenu_target_win>=0){
        windows[cmenu_target_win].active=0; wallpaper_invalidate();
        windows[cmenu_target_win].minimized=0;
        windows[cmenu_target_win].maximized=0;
        sound_close_win();
        return;
    }
    else if(act==CMENU_ACT_MINIMIZAR && cmenu_target_win>=0){
        window_t *w=&windows[cmenu_target_win];
        w->minimized=!w->minimized;
        if(w->minimized) w->focused=0;
    }
    else if(act==CMENU_ACT_MAXIMIZAR && cmenu_target_win>=0){
        /* toggle_maximize es static en gui.c, se declara adelante */
        { window_t *w2=&windows[cmenu_target_win];
          if(w2->maximized){ w2->x=w2->prev_x;w2->y=w2->prev_y;w2->w=w2->prev_w;w2->h=w2->prev_h;w2->maximized=0; }
          else { w2->prev_x=w2->x;w2->prev_y=w2->y;w2->prev_w=w2->w;w2->prev_h=w2->h;
                 w2->x=0;w2->y=0;w2->w=fb_width();w2->h=fb_height()-28;w2->maximized=1; }
        }
    }
    else if(act==CMENU_ACT_ABRIR_ICON && cmenu_target_icon>=0){
        open_window_by_title(desktop_icon_appname(cmenu_target_icon));
    }
    sound_click();
}

/* ---- iconos de escritorio (archivos del FS) ---- */
#define DESKICON_W 80
#define DESKICON_H 80

static void draw_desktop_icons(void){
    int sx=fb_width()-DESKICON_W-10;
    int sy=14;
    char name[24]; int size;
    for(int k=0;;k++){
        if(!fs_get_entry(k,name,&size)) break;
        int iy=sy+k*(DESKICON_H+6);
        if(iy+DESKICON_H > (int)fb_height()-30) break;
        fb_fill_rect(sx,iy,DESKICON_W,40,C_DESKICON_BG);
        fb_draw_rect(sx,iy,DESKICON_W,40,COL(0x55,0x55,0x88));
        fb_fill_rect(sx+8,iy+8,16,16,COL(0x20,0x55,0x95));
        fb_draw_str(sx+8,iy+9,"T",COL(0xff,0xff,0xff),COL(0x20,0x55,0x95));
        /* nombre recortado debajo */
        char short_name[10];
        scpy(short_name,name,10);
        fb_draw_str(sx+2,iy+44,short_name,C_TASKBAR_TXT,COL(0,0,0));
    }
}

static int desktop_icon_click(int px,int py){
    int sx=fb_width()-DESKICON_W-10;
    int sy=14;
    char name[24]; int size;
    for(int k=0;;k++){
        if(!fs_get_entry(k,name,&size)) break;
        int iy=sy+k*(DESKICON_H+6);
        if(iy+DESKICON_H > (int)fb_height()-30) break;
        if(px>=sx&&px<sx+DESKICON_W&&py>=iy&&py<iy+40){
            notepad_load(name);
            open_window_by_title("Notas");
            sound_click();
            return 1;
        }
    }
    return 0;
}

/* ---- WiFi menu helpers ---- */

/* Convierte "a.b.c.d" a 4 octetos. Devuelve 1 si OK. */
static int parse_ip(const char *s, uint8_t out[4]){
    int i=0, v=0, dot=0;
    out[0]=out[1]=out[2]=out[3]=0;
    while(s[i]){
        if(s[i]>='0'&&s[i]<='9'){
            v=v*10+(s[i]-'0');
        } else if(s[i]=='.'){
            if(dot>=3) return 0;
            out[dot++]=(uint8_t)(v>255?255:v);
            v=0;
        } else return 0;
        i++;
    }
    if(dot!=3) return 0;
    out[3]=(uint8_t)(v>255?255:v);
    return 1;
}

static void draw_wifi_icon(int x, int y, int connected){
    /* Arcos simples WiFi: 3 arcos concentricos + punto */
    uint32_t col = connected ? fb_color(0x44,0xee,0x66) : fb_color(0x88,0x88,0xaa);
    uint32_t bg  = fb_color(0x0f,0x1f,0x3a);
    fb_fill_rect(x,y,20,20,bg);
    /* punto central */
    fb_fill_rect(x+9,y+16,3,3,col);
    /* arco pequeño */
    fb_fill_rect(x+6,y+12,2,2,col);
    fb_fill_rect(x+13,y+12,2,2,col);
    fb_fill_rect(x+7,y+11,7,2,col);
    /* arco mediano */
    fb_fill_rect(x+3,y+8,2,2,col);
    fb_fill_rect(x+16,y+8,2,2,col);
    fb_fill_rect(x+4,y+6,13,2,col);
    /* arco grande */
    fb_fill_rect(x+1,y+4,2,2,col);
    fb_fill_rect(x+18,y+4,2,2,col);
    fb_fill_rect(x+2,y+2,17,2,col);
}

static void draw_wifi_menu(int sw, int sh){
    if(!wifi_menu_open) return;
    int mx2 = sw - 200;
    int my2 = sh - 28 - WMENU_H;
    uint32_t mbg  = fb_color(0x0c,0x14,0x2c);
    uint32_t mfg  = fb_color(0xcc,0xcc,0xee);
    uint32_t msel = fb_color(0x1a,0x3a,0x66);
    uint32_t mact = fb_color(0x20,0x50,0x95);
    uint32_t mok  = fb_color(0x1a,0x88,0x44);
    uint32_t msep = fb_color(0x22,0x44,0x77);

    fb_fill_rect(mx2, my2, WMENU_W, WMENU_H, mbg);
    fb_draw_rect(mx2, my2, WMENU_W, WMENU_H, msep);

    /* Titulo con nombre del chip */
    fb_fill_rect(mx2, my2, WMENU_W, 22, mact);
    int connected = net_is_configured();
    const char *nic = ndis_present() ? ndis_adapter_name() : "Sin NIC";
    /* Construir titulo */
    static char title[48]; int ti=0;
    const char *st = connected ? "Conectado - " : "Red - ";
    for(int i=0;st[i];i++) title[ti++]=st[i];
    for(int i=0;nic[i]&&ti<46;i++) title[ti++]=nic[i];
    title[ti]='\0';
    fb_draw_str(mx2+8, my2+7, title, fb_color(0xff,0xff,0xff), mact);

    int oy = my2+28;

    /* IP actual */
    if(connected){
        fb_draw_str(mx2+8, oy, "IP:", fb_color(0xaa,0xaa,0xcc), mbg);
        fb_draw_str(mx2+36, oy, net_get_ip_str(), fb_color(0x55,0xff,0x88), mbg);
    } else {
        fb_draw_str(mx2+8, oy, "Sin conexion", fb_color(0xff,0x88,0x44), mbg);
    }
    oy += 18;
    fb_fill_rect(mx2+4, oy, WMENU_W-8, 1, msep);
    oy += 6;

    /* Boton DHCP automatico */
    uint32_t dhcp_col = ndis_present() ? fb_color(0x1a,0x66,0x99) : fb_color(0x33,0x33,0x44);
    fb_fill_rect(mx2+8, oy, WMENU_W-16, 22, dhcp_col);
    fb_draw_rect(mx2+8, oy, WMENU_W-16, 22, fb_color(0x33,0x99,0xcc));
    fb_draw_str(mx2+WMENU_W/2-52, oy+7, "DHCP Automatico", fb_color(0xff,0xff,0xff), dhcp_col);
    oy += 28;

    /* Mensaje de estado DHCP */
    if(wf_status_msg[0]){
        uint32_t sc = (wf_dhcp_status==1) ? fb_color(0x44,0xee,0x66) :
                      (wf_dhcp_status==-1)? fb_color(0xff,0x66,0x44) :
                                            fb_color(0xaa,0xaa,0x66);
        fb_draw_str(mx2+8, oy, wf_status_msg, sc, mbg);
    }
    oy += 16;
    fb_fill_rect(mx2+4, oy, WMENU_W-8, 1, msep);
    oy += 6;

    /* Campos IP manuales */
    fb_draw_str(mx2+8, oy, "Manual:", fb_color(0x88,0x88,0xaa), mbg);
    oy += 14;
    for(int i=0;i<WFIELD_COUNT;i++){
        fb_draw_str(mx2+8, oy+5, wflabels[i], mfg, mbg);
        uint32_t fbg2 = (wf_active==i) ? msel : fb_color(0x10,0x22,0x44);
        fb_fill_rect(mx2+80, oy, WMENU_W-92, 18, fbg2);
        fb_draw_rect(mx2+80, oy, WMENU_W-92, 18, (wf_active==i)?fb_color(0x55,0xaa,0xff):msep);
        fb_draw_str(mx2+84, oy+5, wfields[i], fb_color(0xff,0xff,0xff), fbg2);
        if(wf_active==i){
            int cpos=0; while(wfields[i][cpos]) cpos++;
            fb_fill_rect(mx2+84+cpos*9, oy+3, 1, 12, fb_color(0xff,0xff,0xff));
        }
        oy += 22;
    }
    oy += 2;
    /* Boton Aplicar manual */
    fb_fill_rect(mx2+8, oy, WMENU_W-16, 22, mok);
    fb_draw_rect(mx2+8, oy, WMENU_W-16, 22, fb_color(0x44,0xcc,0x66));
    fb_draw_str(mx2+WMENU_W/2-30, oy+7, "Aplicar IP", fb_color(0xff,0xff,0xff), mok);
}

static int wifi_menu_hit(int px, int py, int sw, int sh){
    if(!wifi_menu_open) return 0;
    int mx2 = sw - 200;
    int my2 = sh - 28 - WMENU_H;
    if(px<mx2||px>=mx2+WMENU_W||py<my2||py>=my2+WMENU_H){ wifi_menu_open=0; return 1; }

    int oy = my2 + 28 + 18 + 7; /* despues del titulo + IP + separador */

    /* Boton DHCP */
    if(px>=mx2+8&&px<mx2+WMENU_W-8&&py>=oy&&py<oy+22){
        if(ndis_present()){
            /* Correr DHCP (bloqueante hasta 3s) */
            wf_dhcp_status = dhcp_request(3000) ? 1 : -1;
            if(wf_dhcp_status == 1){
                const char *ip = net_get_ip_str();
                int p=0; const char *pre="OK: ";
                for(int k=0;pre[k];k++) wf_status_msg[p++]=pre[k];
                int i=0; while(ip[i]&&p<46) wf_status_msg[p++]=ip[i++];
                wf_status_msg[p]='\0';
                /* Rellenar campos con la IP obtenida */
                int q=0; while(ip[q]&&q<15){ wf_ip[q]=ip[q]; q++; } wf_ip[q]='\0';
            } else {
                const char *msg="Sin respuesta del router";
                int i=0; while(msg[i]&&i<46){ wf_status_msg[i]=msg[i]; i++; }
                wf_status_msg[i]='\0';
            }
        }
        return 1;
    }
    oy += 28 + 16 + 7; /* boton DHCP + status + separador */

    oy += 14; /* label "Manual:" */
    /* Campos editables */
    for(int i=0;i<WFIELD_COUNT;i++){
        if(px>=mx2+80&&px<mx2+WMENU_W-12&&py>=oy&&py<oy+18){
            wf_active = i; return 1;
        }
        oy+=22;
    }
    oy+=2;
    /* Boton Aplicar IP manual */
    if(px>=mx2+8&&px<mx2+WMENU_W-8&&py>=oy&&py<oy+22){
        uint8_t ip[4],mask[4],gw[4],dns[4];
        if(parse_ip(wf_ip,ip)&&parse_ip(wf_mask,mask)&&parse_ip(wf_gw,gw)&&parse_ip(wf_dns,dns)){
            net_set_ip(ip[0],ip[1],ip[2],ip[3],
                       mask[0],mask[1],mask[2],mask[3],
                       gw[0],gw[1],gw[2],gw[3],
                       dns[0],dns[1],dns[2],dns[3]);
            const char *msg="IP manual aplicada";
            int i=0; while(msg[i]&&i<46){ wf_status_msg[i]=msg[i]; i++; }
            wf_status_msg[i]='\0';
            wf_dhcp_status=1;
        }
        wifi_menu_open=0; wf_active=-1;
        return 1;
    }
    return 1;
}

static void wifi_menu_key(int ch){
    if(!wifi_menu_open||wf_active<0) return;
    char *buf = wfields[wf_active];
    int len=0; while(buf[len]) len++;
    if(ch==8||ch==127){ /* backspace */
        if(len>0) buf[len-1]='\0';
    } else if((ch>='0'&&ch<='9')||ch=='.'){
        if(len<15){ buf[len]=(char)ch; buf[len+1]='\0'; }
    }
}

/* ---- taskbar ---- */
#define TAB_W 92
#define TAB_GAP 4

static void draw_taskbar(void){
    int sw=fb_width(),sh=fb_height();
    fb_fill_rect(0,sh-28,sw,28,C_TASKBAR);
    fb_fill_rect(0,sh-29,sw,1,C_SEP);

    uint32_t sbg=startmenu_open?C_TITLE_FOC:COL(0x0f,0x34,0x60);
    fb_fill_rect(2,sh-26,52,24,sbg);
    fb_draw_str(8,sh-20,"Core",C_TITLE_TXT,sbg);
    fb_fill_rect(56,sh-24,1,18,C_SEP);

    int tab_cw = g_clock_12h ? 100 : 84;
    int tab_vx = sw-tab_cw-110;
    int tab_active_count = 0;
    for(int i=0;i<win_count;i++) if(windows[i].active) tab_active_count++;
    int reserved_right = 110 + 70 + 46 + 26 + 10; /* volume + window count + battery + wifi + gap */
    int tab_area_end = sw - tab_cw - reserved_right;
    int available = tab_area_end - 62;
    int visible_tabs = 0;
    if(available >= TAB_W) visible_tabs = 1 + (available - TAB_W) / (TAB_W + TAB_GAP);
    int max_tab_start = tab_active_count - visible_tabs;
    if(max_tab_start < 0) max_tab_start = 0;
    if(taskbar_start_index < 0) taskbar_start_index = 0;
    if(taskbar_start_index > max_tab_start) taskbar_start_index = max_tab_start;

    int tx=62;
    int shown = 0;
    for(int i=0;i<win_count && shown < visible_tabs;i++){
        if(!windows[i].active) continue;
        if(shown < taskbar_start_index){ shown++; continue; }
        uint32_t bg = windows[i].focused ? C_TITLE_FOC :
                      windows[i].minimized ? COL(0x16,0x16,0x2c) : COL(0x20,0x28,0x50);
        fb_fill_rect(tx,sh-23,TAB_W,18,bg);
        fb_draw_rect(tx,sh-23,TAB_W,18,COL(0x33,0x33,0x55));
        uint32_t txtcol = windows[i].minimized ? COL(0x88,0x88,0xaa) : C_TASKBAR_TXT;
        char short_title[12];
        scpy(short_title,windows[i].title,12);
        if(slen(windows[i].title)*9 > TAB_W-8) short_title[9]='\0';
        fb_draw_str(tx+4,sh-19,short_title,txtcol,bg);
        tx+=TAB_W+TAB_GAP;
        shown++;
    }

    char tbuf[16]; rtc_get_str_fmt(tbuf,g_clock_12h);
    fb_fill_rect(sw-tab_cw-2,sh-24,tab_cw,20,COL(0x0f,0x1f,0x3a));
    fb_draw_str(sw-tab_cw+2,sh-20,tbuf,COL(0xff,0xff,0xff),COL(0x0f,0x1f,0x3a));

    /* Control de volumen */
    fb_fill_rect(tab_vx,sh-24,100,20,COL(0x0f,0x1f,0x3a));
    fb_draw_str(tab_vx+4,sh-20,"Vol",COL(0xaa,0xaa,0xcc),COL(0x0f,0x1f,0x3a));
    fb_fill_rect(tab_vx+32,sh-22,12,16,COL(0x33,0x55,0x77));
    fb_draw_str(tab_vx+35,sh-19,"-",COL(0xff,0xff,0xff),COL(0x33,0x55,0x77));
    for(int b=0;b<g_volume;b++)
        fb_fill_rect(tab_vx+48+b*4,sh-22+(10-(b+1)),3,(b+1),COL(0x44,0xcc,0x66));
    fb_fill_rect(tab_vx+88,sh-22,12,16,COL(0x33,0x55,0x77));
    fb_draw_str(tab_vx+91,sh-19,"+",COL(0xff,0xff,0xff),COL(0x33,0x55,0x77));

    /* Contador de ventanas activas */
    int wx2 = tab_vx - 70;
    fb_fill_rect(wx2,sh-24,60,20,COL(0x0f,0x1f,0x3a));
    fb_draw_str(wx2+4,sh-20,"Ven:",COL(0xaa,0xaa,0xcc),COL(0x0f,0x1f,0x3a));
    fb_draw_int(wx2+38,sh-20,tab_active_count,COL(0xff,0xff,0xff),COL(0x0f,0x1f,0x3a));

    /* Bateria decorativa */
    int bx2 = wx2 - 46;
    fb_draw_rect(bx2,sh-21,30,14,COL(0x88,0x88,0xaa));
    fb_fill_rect(bx2+30,sh-17,3,6,COL(0x88,0x88,0xaa));
    fb_fill_rect(bx2+2,sh-19,26,10,COL(0x44,0xcc,0x66));

    /* Icono WiFi (clickeable) - justo a la izquierda de la bateria */
    int wix = bx2 - 26;
    draw_wifi_icon(wix, sh-24, net_is_configured());
}

static void draw_tip_widget(void){
    const char *tip = tips_get_today();
    int sh=fb_height();
    int tw = slen(tip)*9+20;
    fb_fill_rect(10,sh-28-30,tw,24,COL(0x10,0x18,0x30));
    fb_draw_rect(10,sh-28-30,tw,24,COL(0x33,0x55,0x88));
    fb_draw_str(20,sh-28-23,tip,COL(0xaa,0xcc,0xff),COL(0x10,0x18,0x30));
}

/* ---- redibujado ---- */
static void redraw_all(void){
    apply_theme();
    wallpaper_draw();
    desktop_draw();
    for(int i=0;i<win_count;i++)draw_window(i);
    draw_startmenu();
    draw_cmenu();
    draw_tip_widget();
    draw_taskbar();
    draw_wifi_menu(fb_width(), fb_height());
    toast_draw();
    draw_cursor(mx,my);
    fb_flush();
}

/* ---- hit tests ---- */
static int in_titlebar(window_t *w,int px,int py){
    return px>=w->x+BORDER&&px<w->x+w->w-BORDER-54&&py>=w->y+BORDER&&py<w->y+TITLEBAR_H;
}
static int in_close(window_t *w,int px,int py){
    int bx=w->x+w->w-BORDER-16,by=w->y+BORDER+2;
    return px>=bx&&px<bx+15&&py>=by&&py<by+15;
}
static int in_maximize(window_t *w,int px,int py){
    int bx=w->x+w->w-BORDER-34,by=w->y+BORDER+2;
    return px>=bx&&px<bx+15&&py>=by&&py<by+15;
}
static int in_minimize(window_t *w,int px,int py){
    int bx=w->x+w->w-BORDER-52,by=w->y+BORDER+2;
    return px>=bx&&px<bx+15&&py>=by&&py<by+15;
}
static int in_resize(window_t *w,int px,int py){
    if(w->maximized) return 0;
    return px>=w->x+w->w-12&&px<w->x+w->w&&py>=w->y+w->h-12&&py<w->y+w->h;
}
static int in_win(window_t *w,int px,int py){
    return w->active&&!w->minimized&&
           px>=w->x&&px<w->x+w->w&&py>=w->y&&py<w->y+w->h;
}

static void toggle_maximize(window_t *w){
    int sw=fb_width(), sh=fb_height()-28;
    if(!w->maximized){
        w->prev_x=w->x; w->prev_y=w->y; w->prev_w=w->w; w->prev_h=w->h;
        w->x=0; w->y=0; w->w=sw; w->h=sh;
        w->maximized=1;
    } else {
        w->x=w->prev_x; w->y=w->prev_y; w->w=w->prev_w; w->h=w->prev_h;
        w->maximized=0;
    }
}

/* ---- mouse ---- */
static void handle_mouse(void){
    mouse_get_pos(&mx,&my);
    btn_left=mouse_left_pressed();
    btn_right=mouse_right_pressed();
    int click=btn_left&&!prev_btn_left;
    int rclick=btn_right&&!prev_btn_right;
    int release=!btn_left&&prev_btn_left;
    int sh=fb_height();
    screensaver_reset(); /* cualquier movimiento de mouse desactiva el protector */
    int sw=fb_width();

    /* Si la pantalla esta bloqueada, no procesar nada de escritorio/ventanas.
       Solo dejamos pasar prev_btn_* actualizados para no perder el "release"
       cuando se desbloquee. */
    if(lockscreen_active()){
        prev_btn_left=btn_left;
        prev_btn_right=btn_right;
        /* consume wheel even when locked */
        int wh = mouse_get_wheel(); (void)wh;
        return;
    }

    /* Drag & drop desde USB explorer y drag de iconos */
    if(desktop_drag_active()){
        desktop_drag_move(mx,my);
        if(release) desktop_drag_drop(mx,my);
    } else {
        desktop_mouse_move(mx,my);
    }

    /* Arrastre */
    for(int i=0;i<win_count;i++){
        if(windows[i].dragging){
            windows[i].x=mx-windows[i].drag_ox;
            windows[i].y=my-windows[i].drag_oy;
            if(windows[i].x<0)windows[i].x=0;
            if(windows[i].y<0)windows[i].y=0;
            if(windows[i].x+windows[i].w>(int)fb_width())
                windows[i].x=fb_width()-windows[i].w;
            if(windows[i].y+windows[i].h>(int)fb_height()-28)
                windows[i].y=fb_height()-28-windows[i].h;
            if(release){
                windows[i].dragging=0;
                wallpaper_invalidate();
                if(mx<=2){
                    windows[i].prev_x=windows[i].x; windows[i].prev_y=windows[i].y;
                    windows[i].prev_w=windows[i].w; windows[i].prev_h=windows[i].h;
                    windows[i].x=0; windows[i].y=0;
                    windows[i].w=fb_width()/2; windows[i].h=fb_height()-28;
                    windows[i].maximized=1;
                } else if(mx>=(int)fb_width()-2){
                    windows[i].prev_x=windows[i].x; windows[i].prev_y=windows[i].y;
                    windows[i].prev_w=windows[i].w; windows[i].prev_h=windows[i].h;
                    windows[i].x=fb_width()/2; windows[i].y=0;
                    windows[i].w=fb_width()/2; windows[i].h=fb_height()-28;
                    windows[i].maximized=1;
                } else if(my<=2){
                    toggle_maximize(&windows[i]);
                } else {
                    /* Sin snap a borde: verificar que no quede encima de otra */
                    int tries=0;
                    while(win_overlaps_any(windows[i].x,windows[i].y,
                                           windows[i].w,windows[i].h,i) && tries<8){
                        windows[i].x+=22; windows[i].y+=16;
                        int sw2=(int)fb_width(), sh2=(int)fb_height()-28;
                        if(windows[i].x+windows[i].w>sw2) windows[i].x=sw2-windows[i].w;
                        if(windows[i].y+windows[i].h>sh2) windows[i].y=sh2-windows[i].h;
                        if(windows[i].x<0) windows[i].x=0;
                        if(windows[i].y<0) windows[i].y=0;
                        tries++;
                    }
                }
            }
            break;
        }
        if(windows[i].resizing){
            int neww=mx-windows[i].x, newh=my-windows[i].y;
            if(neww<160)neww=160;
            if(newh<120)newh=120;
            if(windows[i].x+neww>(int)fb_width()) neww=fb_width()-windows[i].x;
            if(windows[i].y+newh>(int)fb_height()-28) newh=fb_height()-28-windows[i].y;
            windows[i].w=neww; windows[i].h=newh;
            if(release) windows[i].resizing=0;
            break;
        }
    }

    /* Click derecho: menu contextual inteligente */
    if(rclick){
        int hit=-1;
        for(int i=win_count-1;i>=0;i--) if(in_win(&windows[i],mx,my)){hit=i;break;}
        int icon_idx = (hit<0) ? desktop_icon_at(mx,my) : -1;
        if(my<sh-28){
            build_cmenu(hit, icon_idx);
            cmenu_open=1; cmenu_x=mx; cmenu_y=my;
            if(cmenu_x+CMENU_W>(int)fb_width()) cmenu_x=fb_width()-CMENU_W;
            if(cmenu_y+cmenu_item_count*28+8>sh) cmenu_y=sh-(cmenu_item_count*28+8);
        }
    }

    if(click){
        /* Si el menu contextual esta abierto, resolver ahi */
        if(cmenu_open){
            cmenu_click(mx,my);
            cmenu_open=0;
            prev_btn_left=btn_left; prev_btn_right=btn_right;
            return;
        }

        /* Menu WiFi abierto: consume el click */
        if(wifi_menu_open){
            wifi_menu_hit(mx,my,sw,sh);
            sound_click();
            prev_btn_left=btn_left; prev_btn_right=btn_right;
            return;
        }

        /* Control de volumen */
        char tbuf[16]; rtc_get_str_fmt(tbuf,g_clock_12h);
        int cw = g_clock_12h ? 100 : 84;
        int vx = sw-cw-110;
        if(my>=sh-22&&my<sh-6){
            if(mx>=vx+32&&mx<vx+44){ volume_down(); prev_btn_left=btn_left; prev_btn_right=btn_right; return; }
            if(mx>=vx+88&&mx<vx+100){ volume_up(); prev_btn_left=btn_left; prev_btn_right=btn_right; return; }
        }

        /* Click en icono WiFi (junto a la bateria) */
        {
            int wx2_v = sw-cw-110-70; /* posicion de Ven: */
            int bx2_v = wx2_v - 46;   /* posicion bateria */
            int wix_v = bx2_v - 26;   /* posicion icono wifi */
            if(mx>=wix_v&&mx<wix_v+20&&my>=sh-24&&my<sh-4){
                wifi_menu_open = !wifi_menu_open;
                if(!wifi_menu_open) wf_active=-1;
                sound_click();
                prev_btn_left=btn_left; prev_btn_right=btn_right;
                return;
            }
        }

        /* Boton inicio */
        if(mx>=2&&mx<54&&my>=sh-26&&my<sh-2){
            startmenu_open=!startmenu_open;
            sound_click();
            prev_btn_left=btn_left; prev_btn_right=btn_right;
            return;
        }

        /* Click en menu inicio */
        if(startmenu_open){
            startmenu_click(mx,my);
            startmenu_open=0;
            prev_btn_left=btn_left; prev_btn_right=btn_right;
            return;
        }

        /* Click en pestanas de taskbar */
        {
            int cw = g_clock_12h ? 100 : 84;
            int vx = sw-cw-110;
            int reserved_right = 110 + 70 + 46 + 26 + 10;
            int tab_area_end = sw - cw - reserved_right;
            int available = tab_area_end - 62;
            int visible_tabs = available > 0 ? available / (TAB_W + TAB_GAP) : 0;
            int tx = 62;
            int shown = 0;
            int visible_count = 0;
            for(int i=0;i<win_count && visible_count < visible_tabs;i++){
                if(!windows[i].active) continue;
                if(shown < taskbar_start_index){ shown++; continue; }
                if(mx>=tx&&mx<tx+TAB_W&&my>=sh-23&&my<sh-5){
                    if(windows[i].minimized){
                        windows[i].minimized=0;
                        for(int j=0;j<win_count;j++)windows[j].focused=0;
                        windows[i].focused=1;
                    } else if(windows[i].focused){
                        windows[i].minimized=1;
                        windows[i].focused=0;
                    } else {
                        for(int j=0;j<win_count;j++)windows[j].focused=0;
                        windows[i].focused=1;
                    }
                    sound_click();
                    prev_btn_left=btn_left; prev_btn_right=btn_right;
                    return;
                }
                tx+=TAB_W+TAB_GAP;
                visible_count++;
            }
        }

        /* Click en ventanas */
        int hit=-1;
        for(int i=win_count-1;i>=0;i--)
            if(in_win(&windows[i],mx,my)){hit=i;break;}

        for(int i=0;i<win_count;i++)windows[i].focused=0;

        if(hit>=0){
            windows[hit].focused=1;
            if(in_close(&windows[hit],mx,my)){
                windows[hit].active=0; wallpaper_invalidate();
                windows[hit].minimized=0;
                windows[hit].maximized=0;
                sound_close_win();
            } else if(in_maximize(&windows[hit],mx,my)){
                toggle_maximize(&windows[hit]);
                sound_click();
            } else if(in_minimize(&windows[hit],mx,my)){
                windows[hit].minimized=1;
                windows[hit].focused=0;
                sound_click();
            } else if(in_resize(&windows[hit],mx,my)){
                windows[hit].resizing=1;
                sound_click();
            } else if(in_titlebar(&windows[hit],mx,my)){
                if(!windows[hit].maximized){
                    windows[hit].dragging=1;
                    windows[hit].drag_ox=mx-windows[hit].x;
                    windows[hit].drag_oy=my-windows[hit].y;
                }
                sound_click();
            } else if(windows[hit].type==WIN_CALC){
                if(calc_click(windows[hit].x,windows[hit].y,windows[hit].w,mx,my))
                    sound_click();
            } else if(windows[hit].type==WIN_NOTEPAD){
                if(notepad_click(windows[hit].x,windows[hit].y,windows[hit].w,windows[hit].h,mx,my))
                    sound_click();
            } else if(windows[hit].type==WIN_SETTINGS){
                if(settings_click(windows[hit].x,windows[hit].y,windows[hit].w,windows[hit].h,mx,my))
                    sound_click();
            } else if(windows[hit].type==WIN_SNAKE){
                snake_restart();
                sound_click();
            } else if(windows[hit].type==WIN_TETRIS){
                tetris_restart();
                sound_click();
            } else if(windows[hit].type==WIN_TRASH){
                if(trash_click(windows[hit].x,windows[hit].y,windows[hit].w,windows[hit].h,mx,my))
                    sound_click();
            } else if(windows[hit].type==WIN_GALLERY){
                if(gallery_click(windows[hit].x,windows[hit].y,windows[hit].w,windows[hit].h,mx,my))
                    sound_click();
            } else if(windows[hit].type==WIN_IMAGEVIEWER){
                if(imageviewer_click(windows[hit].x,windows[hit].y,windows[hit].w,windows[hit].h,mx,my))
                    sound_click();
            } else if(windows[hit].type==WIN_MUSICPLAYER){
                if(musicplayer_click(windows[hit].x,windows[hit].y,windows[hit].w,windows[hit].h,mx,my))
                    sound_click();
            } else if(windows[hit].type==WIN_GUIDE){
                if(guide_click(windows[hit].x,windows[hit].y,windows[hit].w,windows[hit].h,mx,my))
                    sound_click();
            } else if(windows[hit].type==WIN_PACMAN){
                pacman_restart();
                sound_click();
            } else if(windows[hit].type==WIN_FILEMANAGER){
                int r = filemanager_click(windows[hit].x,windows[hit].y,windows[hit].w,windows[hit].h,mx,my);
                if(r==1){
                    sound_click();
                    open_window_by_title("Notas");
                } else if(r==2){
                    sound_close_win();
                }
            } else if(windows[hit].type==WIN_WALLPAPER){
                for(int k=0;k<5;k++){
                    int bx=windows[hit].x+BORDER+10;
                    int by=windows[hit].y+TITLEBAR_H+10+k*34;
                    if(mx>=bx&&mx<bx+150&&my>=by&&my<by+26){
                        wallpaper_set((wallpaper_t)k);
                        sound_click();
                    }
                }
            } else if(windows[hit].type==WIN_BROWSER){
                browser_mouse(windows[hit].x+BORDER,
                              windows[hit].y+TITLEBAR_H,
                              windows[hit].w-BORDER*2,
                              windows[hit].h-TITLEBAR_H-BORDER,
                              mx, my, 1);
            } else if(windows[hit].type==WIN_USBEXPLORER){
                usbexplorer_mouse(windows[hit].x+BORDER,
                                  windows[hit].y+TITLEBAR_H,
                                  windows[hit].w-BORDER*2,
                                  windows[hit].h-TITLEBAR_H-BORDER,
                                  mx, my, 1);
                /* Iniciar drag si hay un archivo seleccionado */
                int file_idx = usbexplorer_selected_file();
                if(file_idx>=0) desktop_drag_start_usb(file_idx,mx,my);
            } else if(windows[hit].type==WIN_CODEEDITOR){
                code_mouse(windows[hit].x,windows[hit].y,
                           windows[hit].w,windows[hit].h,mx,my,1);
            } else if(windows[hit].type==WIN_PAINT){
                paint_mouse(windows[hit].x,windows[hit].y,
                            windows[hit].w,windows[hit].h,mx,my,1);
                paint_click(windows[hit].x,windows[hit].y,
                            windows[hit].w,windows[hit].h,mx,my);
            } else if(windows[hit].type==WIN_SPREADSHEET){
                spreadsheet_click(windows[hit].x,windows[hit].y,
                                  windows[hit].w,windows[hit].h,mx,my);
            } else if(windows[hit].type==WIN_CALENDAR){
                calendar_click(windows[hit].x,windows[hit].y,
                               windows[hit].w,windows[hit].h,mx,my);
            } else if(windows[hit].type==WIN_ALARM){
                alarm_click(windows[hit].x,windows[hit].y,
                            windows[hit].w,windows[hit].h,mx,my);
            } else if(windows[hit].type==WIN_SYSLOG){
                syslog_click(windows[hit].x,windows[hit].y,
                             windows[hit].w,windows[hit].h,mx,my);
            } else if(windows[hit].type==WIN_MINESWEEPER){
                minesweeper_click(windows[hit].x,windows[hit].y,
                                  windows[hit].w,windows[hit].h,mx,my,0);
            } else if(windows[hit].type==WIN_2048){
                game2048_click(windows[hit].x,windows[hit].y,
                               windows[hit].w,windows[hit].h,mx,my);
            } else if(windows[hit].type==WIN_CHESS){
                chess_click(windows[hit].x,windows[hit].y,
                            windows[hit].w,windows[hit].h,mx,my);
            } else if(windows[hit].type==WIN_BREAKOUT){
                breakout_click(windows[hit].x,windows[hit].y,
                               windows[hit].w,windows[hit].h,mx,my);
            } else if(windows[hit].type==WIN_CONWAY){
                conway_click(windows[hit].x,windows[hit].y,
                             windows[hit].w,windows[hit].h,mx,my);
            }
        } else {
            startmenu_open=0;
            if(click)  desktop_mouse_down(mx,my);
            if(release) desktop_mouse_up(mx,my);
        }
    }
    prev_btn_left=btn_left;
    prev_btn_right=btn_right;
}

/* handle mouse wheel for startmenu scrolling */
static void handle_mouse_wheel(void){
    int wh = mouse_get_wheel();
    if(wh==0) return;
    if(startmenu_open){
        /* each wheel delta moves one item (sign depends on device) */
        smenu_start_index -= wh; /* wheel positive -> scroll up */
        int sh = fb_height();
        int menu_h = SMENU_H;
        if(menu_h > sh - 28) menu_h = sh - 28;
        if(menu_h < 28 + 24) menu_h = 28 + 24;
        int visible = (menu_h-28)/24; if(visible<1) visible=1;
        int max_start = SMENU_ITEMS - visible;
        if(max_start < 0) max_start = 0;
        if(smenu_start_index < 0) smenu_start_index = 0;
        if(smenu_start_index > max_start) smenu_start_index = max_start;
        return;
    }
    int mx_, my_;
    mouse_get_pos(&mx_, &my_);
    int sw = fb_width(), sh = fb_height();
    int cw = g_clock_12h ? 100 : 84;
    int vx = sw-cw-110;
    int reserved_right = 110 + 70 + 46 + 26 + 10;
    int tab_area_end = sw - cw - reserved_right;
    if(my_ >= sh-28 && my_ < sh && mx_ >= 62 && mx_ < tab_area_end){
        int active_count=0;
        for(int i=0;i<win_count;i++) if(windows[i].active) active_count++;
        int available = tab_area_end - 62;
        int visible_tabs = 0;
        if(available >= TAB_W) visible_tabs = 1 + (available - TAB_W) / (TAB_W + TAB_GAP);
        taskbar_start_index -= wh;
        if(taskbar_start_index < 0) taskbar_start_index = 0;
        if(taskbar_start_index > active_count - visible_tabs) taskbar_start_index = active_count - visible_tabs;
        if(taskbar_start_index < 0) taskbar_start_index = 0;
    }
}

/* ---- teclado: enrutar a ventana enfocada ---- */
static int alt_held = 0;

static void handle_keyboard(void){
    int ch = keyboard_poll();
    if(ch<0) return;

    screensaver_reset();

    /* Lockscreen captura todo */
    if(lockscreen_active()){ lockscreen_key((char)ch); return; }

    /* Esc: cerrar menus */
    if(ch==27){ startmenu_open=0; cmenu_open=0; wifi_menu_open=0; wf_active=-1; return; }

    /* Menu WiFi: capturar teclas para editar campos */
    if(wifi_menu_open && wf_active>=0){
        wifi_menu_key(ch);
        return;
    }

    /* Alt+Tab simulado con Tab solo (Alt no llega facilmente) */
    if(ch=='\t'){
        int focused=-1;
        for(int i=0;i<win_count;i++) if(windows[i].focused&&windows[i].active&&!windows[i].minimized) focused=i;
        int next=(focused+1)%win_count;
        int tries=0;
        while(tries<win_count && (!windows[next].active||windows[next].minimized)){ next=(next+1)%win_count; tries++; }
        for(int i=0;i<win_count;i++) windows[i].focused=0;
        windows[next].focused=1;
        sound_click();
        (void)alt_held;
        return;
    }
    for(int i=0;i<win_count;i++){
        if(windows[i].active&&!windows[i].minimized&&windows[i].focused){
            if(windows[i].type==WIN_TERMINAL) terminal_putchar(ch);
            else if(windows[i].type==WIN_NOTEPAD) notepad_putchar(ch);
            else if(windows[i].type==WIN_SNAKE){
                if(ch==KEY_UP) snake_key(0);
                else if(ch==KEY_DOWN) snake_key(1);
                else if(ch==KEY_LEFT) snake_key(2);
                else if(ch==KEY_RIGHT) snake_key(3);
            }
            else if(windows[i].type==WIN_PACMAN){
                if(ch==KEY_UP) pacman_key(0);
                else if(ch==KEY_DOWN) pacman_key(1);
                else if(ch==KEY_LEFT) pacman_key(2);
                else if(ch==KEY_RIGHT) pacman_key(3);
            }
            else if(windows[i].type==WIN_TETRIS){
                if(ch==KEY_LEFT) tetris_key(0);
                else if(ch==KEY_RIGHT) tetris_key(1);
                else if(ch==KEY_UP) tetris_key(2);
                else if(ch==KEY_DOWN) tetris_key(3);
                else if(ch==' ') tetris_key(4);
            }
            else if(windows[i].type==WIN_PONG){
                if(ch=='w'||ch=='W') pong_key(0);
                else if(ch=='s'||ch=='S') pong_key(1);
                else if(ch==KEY_UP) pong_key(2);
                else if(ch==KEY_DOWN) pong_key(3);
                else if(ch=='\n'||ch==' ') pong_key(4);
            }
            else if(windows[i].type==WIN_BROWSER){
                browser_key(ch);
            }
            else if(windows[i].type==WIN_USBEXPLORER){
                usbexplorer_key(ch);
            }
            else if(windows[i].type==WIN_CODEEDITOR){
                code_key(ch);
            }
            else if(windows[i].type==WIN_SPREADSHEET){
                spreadsheet_key(ch);
            }
            else if(windows[i].type==WIN_CALENDAR){
                calendar_key(ch);
            }
            else if(windows[i].type==WIN_2048){
                if(ch==KEY_UP||ch=='w'||ch=='W') game2048_key(0);
                else if(ch==KEY_DOWN||ch=='s'||ch=='S') game2048_key(1);
                else if(ch==KEY_LEFT||ch=='a'||ch=='A') game2048_key(2);
                else if(ch==KEY_RIGHT||ch=='d'||ch=='D') game2048_key(3);
            }
            else if(windows[i].type==WIN_CONWAY){
                conway_key((char)ch);
            }
            break;
        }
    }
}

/* ---- API ---- */
int gui_new_window(int x,int y,int w,int h,const char *title,const char *content){
    if(win_count>=MAX_WINDOWS)return -1;
    window_t *win=&windows[win_count];
    win->x=x;win->y=y;win->w=w;win->h=h;
    win->active=1;win->focused=0;win->dragging=0;win->minimized=0;
    win->maximized=0;win->resizing=0;
    win->type=WIN_NORMAL;
    scpy(win->title,title,32);
    scpy(win->content,content,512);
    return win_count++;
}

int gui_new_window_typed(int x,int y,int w,int h,const char *title,win_type_t type){
    int id=gui_new_window(x,y,w,h,title,"");
    if(id>=0)windows[id].type=type;
    return id;
}

void gui_init(void){
    apply_theme();
    for(int i=0;i<MAX_WINDOWS;i++){
        windows[i].active=0;
        windows[i].minimized=0;
        windows[i].maximized=0;
        lazy_inited[i]=0;
    }
    win_count=0;

    /* Visibles al iniciar */
    gui_new_window(60,50,300,190,"Bienvenido",
        "CoreM v1.0\n\nVBE 1280x720 32bpp\nClick derecho: menu\nUsa el menu Core ->");
    gui_new_window_typed(380,50,440,260,"Terminal",WIN_TERMINAL);
    gui_new_window_typed(860,50,230,240,"Reloj",WIN_CLOCK);

    /* Disponibles desde el menu Core */
    gui_new_window(60,300,280,200,"Info",
        "CPU: x86 32-bit\nModo Protegido\nVBE 32bpp\nPS/2 + USB tablet\nPC Speaker audio");
    gui_new_window_typed(370,300,200,220,"Fondo",WIN_WALLPAPER);
    gui_new_window_typed(600,300,280,220,"Acerca de",WIN_ABOUT);
    gui_new_window_typed(60,300,300,360,"Calculadora",WIN_CALC);
    gui_new_window_typed(420,300,340,300,"Archivos",WIN_FILEMANAGER);
    gui_new_window_typed(420,300,400,300,"Notas",WIN_NOTEPAD);
    gui_new_window_typed(60,300,320,300,"Monitor",WIN_SYSMONITOR);
    gui_new_window_typed(420,300,320,280,"Configuracion",WIN_SETTINGS);
    gui_new_window_typed(60,300,420,360,"Snake",WIN_SNAKE);
    gui_new_window_typed(60,300,460,400,"Tetris",WIN_TETRIS);
    gui_new_window_typed(60,300,360,300,"Papelera",WIN_TRASH);
    gui_new_window_typed(60,300,380,320,"Galeria",WIN_GALLERY);
    gui_new_window_typed(60,300,360,330,"Imagenes",WIN_IMAGEVIEWER);
    gui_new_window_typed(60,300,380,300,"Musica",WIN_MUSICPLAYER);
    gui_new_window_typed(60,300,340,280,"Guia",WIN_GUIDE);
    /* 19*14=266 wide + 8 border, 21*14=294 high + titlebar + hud */
    gui_new_window_typed(60,50,278,342,"Pacman",WIN_PACMAN);
    gui_new_window_typed(60,60,500,320,"Pong",WIN_PONG);
    gui_new_window_typed(60,300,560,380,"Dispositivos",WIN_PCIVIEWER);

    /* Apps del menu: registrar ventanas sin inicializar aun */
    gui_new_window_typed(80,40,640,420,"Navegador",WIN_BROWSER);
    gui_new_window_typed(100,60,580,400,"USB",WIN_USBEXPLORER);
    gui_new_window_typed(60,40,700,480,"Code",WIN_CODEEDITOR);
    gui_new_window_typed(60,40,560,380,"Paint",WIN_PAINT);
    gui_new_window_typed(60,40,700,440,"Hoja de Calculo",WIN_SPREADSHEET);
    gui_new_window_typed(60,40,560,480,"Calendario",WIN_CALENDAR);
    gui_new_window_typed(60,40,340,360,"Alarma",WIN_ALARM);
    gui_new_window_typed(60,40,580,380,"Logs del Sistema",WIN_SYSLOG);
    gui_new_window_typed(60,40,380,430,"Buscaminas",WIN_MINESWEEPER);
    gui_new_window_typed(60,40,380,460,"2048",WIN_2048);
    gui_new_window_typed(60,40,440,440,"Ajedrez",WIN_CHESS);
    gui_new_window_typed(60,40,420,380,"Breakout",WIN_BREAKOUT);
    gui_new_window_typed(60,40,700,500,"Conway",WIN_CONWAY);

    /* Ocultar las que no son del set inicial */
    for(int i=3;i<win_count;i++) windows[i].active=0;

    /* Bienvenida personalizada con el usuario logueado */
    char welcome[256];
    const char *u = users_get_current();
    int p=0;
    const char *l1="CoreM v1.0\n\nHola, ";
    for(int q=0;l1[q];q++) welcome[p++]=l1[q];
    for(int q=0;u[q];q++) welcome[p++]=u[q];
    const char *l2="!\n\nClick derecho: menu\nUsa el menu Core ->";
    for(int q=0;l2[q];q++) welcome[p++]=l2[q];
    welcome[p]='\0';
    scpy(windows[0].content, welcome, 512);

    /* Solo inits esenciales — el resto es lazy en open_window_by_title */
    terminal_init();
    notepad_init();
    calc_init();
    /* Marcar solo los que ya fueron inicializados */
    for(int i=0;i<win_count;i++){
        if(windows[i].type==WIN_TERMINAL || windows[i].type==WIN_NOTEPAD || windows[i].type==WIN_CALC)
            lazy_inited[i]=1;
    }
    toast_init();
    lockscreen_init();
    screensaver_init();
    desktop_init();

    windows[1].focused=1; /* Terminal con foco */
}

void gui_run(void){
    sound_startup();
    toast_show("Bienvenido a CoreM OS!");
    uint32_t last_tick = 0;
    while(1){
        handle_mouse();
        handle_mouse_wheel();
        handle_keyboard();
        /* Throttle: solo redibujar cuando el timer PIT avanza (100Hz max) */
        uint32_t now = timer_ticks();
        if(now == last_tick) {
            __asm__ volatile ("hlt");
            continue;
        }
        last_tick = now;
        /* Si el lockscreen esta activo, dibujar y throttle con ticks */
        if(lockscreen_active()){
            lockscreen_draw();
            fb_flush();
            continue;
        }
        net_poll();
        snake_tick();
        tetris_tick();
        pacman_tick();
        pong_tick();
        musicplayer_tick();
        alarm_tick();
        screensaver_tick();
        toast_tick();
        /* Breakout tick en ventana activa */
        for(int i=0;i<win_count;i++){
            if(windows[i].active&&!windows[i].minimized&&windows[i].type==WIN_BREAKOUT){
                breakout_tick(windows[i].x,windows[i].y,windows[i].w,windows[i].h);
            }
        }
        /* Conway tick en ventana activa */
        for(int i=0;i<win_count;i++){
            if(windows[i].active&&!windows[i].minimized&&windows[i].type==WIN_CONWAY){
                conway_tick(windows[i].x,windows[i].y,windows[i].w,windows[i].h);
            }
        }
        if(screensaver_active()){
            screensaver_draw();
            fb_flush();
            continue;
        }
        redraw_all();
    }
}

int gui_window_at(int px, int py){
    for(int i=win_count-1;i>=0;i--){
        if(windows[i].active && !windows[i].minimized && in_win(&windows[i],px,py))
            return i;
    }
    return -1;
}

window_t* gui_get_window(int idx){
    if(idx<0||idx>=win_count) return 0;
    return &windows[idx];
}
