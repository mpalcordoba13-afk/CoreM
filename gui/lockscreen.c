/*
 * lockscreen.c – Pantalla de bloqueo
 * Contraseña por defecto: "1234"
 * Diseño liviano: sin gradiente de fondo completo en cada frame.
 */
#include "lockscreen.h"
#include "framebuffer.h"
#include "rtc.h"
#include <stdint.h>

#define PASS_MAX 16
static char password[PASS_MAX+1] = "1234";
static char input[PASS_MAX+1]    = "";
static int  input_len            = 0;
static int  locked               = 0;
static int  shake                = 0;
static int  wrong_count          = 0;
static int  need_full_redraw     = 1; /* flag: redibujar fondo completo */

void lockscreen_init(void){ locked=0; input_len=0; input[0]='\0'; shake=0; wrong_count=0; }
int  lockscreen_active(void){ return locked; }
void lockscreen_lock(void){
    locked=1; input_len=0; input[0]='\0'; shake=0; need_full_redraw=1;
}

static int seq(const char *a,const char *b){
    while(*a&&*b){ if(*a!=*b)return 0; a++;b++; }
    return *a==*b;
}
static int slen(const char *s){ int n=0; while(s[n])n++; return n; }

/* Dibuja solo la caja de contraseña (rápido) */
static void draw_box(void){
    int sw=fb_width(), sh=fb_height();
    int bw=300, bh=110;
    int bx=sw/2-bw/2, by=sh/2-20;

    /* Offset de shake */
    int off=0;
    if(shake>0){ off=(shake%6<3)?6:-6; shake--; }

    uint32_t box_bg  = fb_color(0x10,0x18,0x30);
    uint32_t border  = wrong_count>0 ? fb_color(0xff,0x44,0x44) : fb_color(0x44,0x88,0xcc);
    uint32_t white   = fb_color(0xff,0xff,0xff);
    uint32_t grey    = fb_color(0x88,0xaa,0xdd);
    uint32_t dot_col = fb_color(0x44,0xcc,0xff);

    /* Limpiar área de la caja con color sólido (sin gradiente) */
    fb_fill_rect(bx+off-4, by-4, bw+8, bh+8, fb_color(0x08,0x10,0x28));
    fb_fill_rect(bx+off, by, bw, bh, box_bg);
    fb_draw_rect(bx+off, by, bw, bh, border);

    fb_draw_str(bx+off+12, by+12, "Ingresa tu contrasena:", grey, box_bg);

    /* Puntos */
    int dot_start = bx+off + bw/2 - (input_len*14)/2;
    for(int i=0;i<input_len;i++)
        fb_fill_circle(dot_start+i*14+5, by+50, 5, dot_col);

    /* Línea bajo los puntos */
    fb_fill_rect(bx+off+12, by+62, bw-24, 2, border);

    /* Mensaje de error */
    if(wrong_count>0){
        char emsg[32]; int ep=0;
        const char *pre="Contrasena incorrecta";
        for(;pre[ep];ep++) emsg[ep]=pre[ep];
        emsg[ep]='\0';
        fb_draw_str(bx+off+12, by+72, emsg, fb_color(0xff,0x66,0x66), box_bg);
    }

    /* Hint enter */
    fb_draw_str(bx+off+12, by+88, "ENTER para desbloquear", fb_color(0x44,0x66,0x88), box_bg);
    (void)white; (void)slen;
}

/* Dibuja hora (solo texto, fondo ya puesto) */
static void draw_clock(void){
    int sw=fb_width(), sh=fb_height();
    rtc_time_t rt; rtc_read(&rt);

    /* Limpiar área de la hora */
    uint32_t bg = fb_color(0x08,0x10,0x28);
    fb_fill_rect(sw/2-120, sh/2-160, 240, 50, bg);

    char tbuf[6];
    tbuf[0]='0'+rt.hour/10; tbuf[1]='0'+rt.hour%10;
    tbuf[2]=':';
    tbuf[3]='0'+rt.min/10;  tbuf[4]='0'+rt.min%10;
    tbuf[5]='\0';
    fb_draw_str_scaled(sw/2-75, sh/2-155, tbuf, fb_color(0xff,0xff,0xff), bg, 3);
}

void lockscreen_draw(void){
    if(!locked) return;
    int sw=fb_width(), sh=fb_height();

    if(need_full_redraw){
        /* Solo la primera vez: fondo sólido (rápido, sin gradiente) */
        fb_fill_rect(0, 0, sw, sh, fb_color(0x08,0x10,0x28));

        /* Hint inferior */
        fb_draw_str(sw/2-130, sh-40,
            "Mueve el mouse o escribe para continuar",
            fb_color(0x33,0x55,0x77), fb_color(0x08,0x10,0x28));

        need_full_redraw=0;
    }

    draw_clock();
    draw_box();
}

void lockscreen_key(char c){
    if(!locked) return;
    if(c=='\n'||c=='\r'){
        if(seq(input,password)){
            locked=0; wrong_count=0; need_full_redraw=1;
        } else {
            wrong_count++;
            shake=18;
            input_len=0; input[0]='\0';
        }
    } else if(c=='\b'){
        if(input_len>0){ input_len--; input[input_len]='\0'; }
    } else if(c>=' '&&c<127&&input_len<PASS_MAX){
        input[input_len++]=c; input[input_len]='\0';
    }
}
