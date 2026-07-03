/*
 * syslog.c – Visor de logs del sistema
 */
#include "syslog.h"
#include "framebuffer.h"
#include "gui.h"
#include <stdint.h>

#define LOG_MAX   64
#define LOG_LEN   80

static char  log_buf[LOG_MAX][LOG_LEN];
static int   log_head=0;   /* próxima posición a escribir (circular) */
static int   log_count=0;
static int   log_scroll=0; /* cuántas líneas desde el fondo */

static void scpy(char *d,const char *s,int n){int i=0;while(s[i]&&i<n-1){d[i]=s[i];i++;}d[i]='\0';}
static int  slen(const char *s){int n=0;while(s[n])n++;return n;}

void syslog_init(void){
    log_head=0; log_count=0; log_scroll=0;
    syslog_write("Sistema iniciado");
    syslog_write("GUI cargada");
}

void syslog_write(const char *msg){
    scpy(log_buf[log_head], msg, LOG_LEN);
    log_head=(log_head+1)%LOG_MAX;
    if(log_count<LOG_MAX) log_count++;
}

void syslog_draw(int wx,int wy,int ww,int wh){
    int ox=wx+BORDER+6, oy=wy+TITLEBAR_H+6;
    int pw=ww-BORDER*2-12, ph=wh-TITLEBAR_H-BORDER-12;
    uint32_t bg    = fb_color(0x0a,0x0c,0x12);
    uint32_t green = fb_color(0x22,0xdd,0x66);
    uint32_t grey  = fb_color(0x55,0x66,0x77);
    uint32_t white = fb_color(0xdd,0xee,0xff);

    fb_fill_rect(ox,oy,pw,ph,bg);
    fb_draw_rect(ox,oy,pw,ph,grey);

    /* Título de columna */
    fb_fill_rect(ox,oy,pw,14,fb_color(0x11,0x22,0x33));
    fb_draw_str(ox+4,oy+3,"[LOG DEL SISTEMA]",green,fb_color(0x11,0x22,0x33));

    int row_h=12, max_rows=(ph-20)/row_h;
    int start = log_count - max_rows - log_scroll;
    if(start<0) start=0;

    for(int r=0;r<max_rows&&(start+r)<log_count;r++){
        int idx = (log_head - log_count + start + r + LOG_MAX) % LOG_MAX;
        int ly  = oy+18+r*row_h;
        /* Número de línea */
        char nb[6]; int n=start+r+1,i=0;
        if(n==0){nb[i++]='0';}else{char t[5];int j=0;while(n>0){t[j++]='0'+n%10;n/=10;}while(j>0)nb[i++]=t[--j];}
        nb[i]='\0';
        fb_draw_str(ox+4,ly,nb,grey,bg);
        fb_draw_str(ox+36,ly,log_buf[idx],white,bg);
        /* Resaltar líneas que contienen palabras clave */
        const char *line=log_buf[idx];
        int has_err=0,has_ok=0;
        for(int k=0;line[k];k++){
            if(line[k]=='E'&&line[k+1]=='R'&&line[k+2]=='R') has_err=1;
            if(line[k]=='O'&&line[k+1]=='K') has_ok=1;
        }
        if(has_err) fb_draw_str(ox+36,ly,log_buf[idx],fb_color(0xff,0x55,0x55),bg);
        if(has_ok)  fb_draw_str(ox+36,ly,log_buf[idx],green,bg);
    }

    /* Scrollbar */
    if(log_count>max_rows){
        int sb_x=ox+pw-8, sb_h=ph-20;
        fb_fill_rect(sb_x,oy+18,6,sb_h,fb_color(0x22,0x22,0x33));
        int thumb_h=sb_h*max_rows/log_count;
        if(thumb_h<16)thumb_h=16;
        int thumb_y=sb_h*(log_count-max_rows-log_scroll)/log_count;
        fb_fill_rect(sb_x,oy+18+thumb_y,6,thumb_h,grey);
    }

    /* Barra inferior: conteo */
    fb_fill_rect(ox,oy+ph-14,pw,14,fb_color(0x11,0x22,0x33));
    char cnt[32]; int p=0;
    const char *pre="Entradas: ";
    for(int k=0;pre[k];k++) cnt[p++]=pre[k];
    char nb2[8]; int n2=log_count,i2=0;
    if(n2==0){nb2[i2++]='0';}else{char t[6];int j=0;while(n2>0){t[j++]='0'+n2%10;n2/=10;}while(j>0)nb2[i2++]=t[--j];}
    nb2[i2]='\0';
    for(int k=0;nb2[k];k++) cnt[p++]=nb2[k];
    cnt[p]='\0';
    fb_draw_str(ox+4,oy+ph-12,cnt,grey,fb_color(0x11,0x22,0x33));
    (void)slen;
}

int syslog_click(int wx,int wy,int ww,int wh,int mx,int my){
    int oy=wy+TITLEBAR_H+6, ph=wh-TITLEBAR_H-BORDER-12;
    int row_h=12, max_rows=(ph-20)/row_h;
    /* Scroll con click en zona del log */
    if(mx>=wx&&mx<wx+ww&&my>=oy&&my<oy+ph){
        if(my < oy+ph/2){ if(log_scroll+max_rows<log_count) log_scroll++; }
        else             { if(log_scroll>0) log_scroll--; }
    }
    (void)ww;(void)mx;
    return 0;
}
