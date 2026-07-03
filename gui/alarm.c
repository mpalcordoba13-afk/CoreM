#include "alarm.h"
#include "framebuffer.h"
#include "gui.h"
#include "rtc.h"
#include "sound.h"
#include "timer.h"
#include <stdint.h>

#define MAX_ALARMS 8
typedef struct { int hour,min; int active; int fired; char label[24]; } alarm_t;
static alarm_t alarms[MAX_ALARMS];
static int al_count=0;
static int sel_h=7,sel_m=0;
static int ringing=0;
static uint32_t ring_start=0;

/* Timer mode */
static int timer_mode=0; /* 0=alarmas, 1=temporizador */
static int timer_secs=60; static int timer_running=0;
static uint32_t timer_start_tick=0;

static void itoa2(int n,char*b){ b[0]='0'+n/10; b[1]='0'+n%10; b[2]='\0'; }
static void itoa10(int n,char*b){if(n==0){b[0]='0';b[1]='\0';return;}char t[8];int i=0;while(n>0){t[i++]='0'+n%10;n/=10;}int p=0;while(i>0)b[p++]=t[--i];b[p]='\0';}
static void scpy(char*d,const char*s,int n){int i=0;while(s[i]&&i<n-1){d[i]=s[i];i++;}d[i]='\0';}

void alarm_init(void){ al_count=0; ringing=0; timer_running=0; }

void alarm_tick(void){
    if(ringing){
        uint32_t age=timer_ticks()-ring_start;
        if(age%20<10) sound_beep(880,50);
        if(age>300){ ringing=0; sound_stop(); }
    }
    if(timer_running){
        uint32_t elapsed=(timer_ticks()-timer_start_tick)/60;
        if(elapsed>=(uint32_t)timer_secs){ timer_running=0; ringing=1; ring_start=timer_ticks(); }
    }
    /* Check alarmas */
    rtc_time_t rt; rtc_read(&rt);
    for(int i=0;i<al_count;i++){
        if(alarms[i].active&&!alarms[i].fired&&alarms[i].hour==rt.hour&&alarms[i].min==rt.min&&rt.sec<2){
            ringing=1; ring_start=timer_ticks(); alarms[i].fired=1;
        }
        if(alarms[i].fired&&(alarms[i].hour!=rt.hour||alarms[i].min!=rt.min)) alarms[i].fired=0;
    }
}

void alarm_draw(int wx,int wy,int ww,int wh){
    uint32_t bg  =fb_color(0x12,0x18,0x28);
    uint32_t tbg =fb_color(0x1a,0x22,0x38);
    uint32_t blue=fb_color(0x22,0x88,0xff);
    uint32_t grn =fb_color(0x22,0xcc,0x66);
    uint32_t red =fb_color(0xff,0x44,0x44);
    uint32_t white=fb_color(0xee,0xee,0xff);
    uint32_t grey=fb_color(0x66,0x77,0x99);

    int ox=wx+BORDER+8, oy=wy+TITLEBAR_H+8;
    int pw=ww-BORDER*2-16;
    fb_fill_rect(wx+BORDER,wy+TITLEBAR_H,ww-BORDER*2,wh-TITLEBAR_H-BORDER,bg);

    /* Tabs */
    uint32_t t1bg=timer_mode==0?blue:tbg, t2bg=timer_mode==1?blue:tbg;
    fb_fill_rect(ox,oy,pw/2-2,22,t1bg);
    fb_draw_str(ox+pw/4-20,oy+7,"Alarmas",white,t1bg);
    fb_fill_rect(ox+pw/2+2,oy,pw/2-2,22,t2bg);
    fb_draw_str(ox+pw*3/4-30,oy+7,"Temporizador",white,t2bg);
    oy+=26;

    if(ringing){
        fb_fill_rect(ox,oy,pw,40,fb_color(0x44,0x00,0x00));
        fb_draw_str(ox+pw/2-60,oy+14,"** ALARMA SONANDO **",red,fb_color(0x44,0x00,0x00));
        fb_fill_rect(ox+pw/2-30,oy+44,60,20,tbg);
        fb_draw_str(ox+pw/2-22,oy+50,"Detener",white,tbg);
        oy+=70;
    }

    if(timer_mode==0){
        /* Nueva alarma */
        fb_fill_rect(ox,oy,pw,50,tbg);
        fb_draw_str(ox+6,oy+5,"Nueva alarma:",grey,tbg);

        /* HH */
        fb_fill_rect(ox+6,oy+18,20,20,fb_color(0x22,0x44,0x88));
        fb_draw_str(ox+8,oy+22,"^",white,fb_color(0x22,0x44,0x88));
        char hb[4]; itoa2(sel_h,hb);
        fb_draw_str_scaled(ox+30,oy+16,hb,white,tbg,2);
        fb_fill_rect(ox+6,oy+36,20,10,fb_color(0x22,0x44,0x88));
        fb_draw_str(ox+8,oy+37,"v",white,fb_color(0x22,0x44,0x88));

        fb_draw_str_scaled(ox+58,oy+16,":",white,tbg,2);

        /* MM */
        fb_fill_rect(ox+72,oy+18,20,20,fb_color(0x22,0x44,0x88));
        fb_draw_str(ox+74,oy+22,"^",white,fb_color(0x22,0x44,0x88));
        char mb[4]; itoa2(sel_m,mb);
        fb_draw_str_scaled(ox+96,oy+16,mb,white,tbg,2);
        fb_fill_rect(ox+72,oy+36,20,10,fb_color(0x22,0x44,0x88));
        fb_draw_str(ox+74,oy+37,"v",white,fb_color(0x22,0x44,0x88));

        fb_fill_rect(ox+pw-62,oy+18,56,20,grn);
        fb_draw_str(ox+pw-56,oy+24,"Agregar",white,grn);
        oy+=56;

        /* Lista de alarmas */
        for(int i=0;i<al_count;i++){
            uint32_t abg=alarms[i].active?tbg:fb_color(0x0e,0x14,0x20);
            fb_fill_rect(ox,oy+i*28,pw,26,abg);
            fb_draw_rect(ox,oy+i*28,pw,26,fb_color(0x33,0x44,0x66));
            char tb[8]; itoa2(alarms[i].hour,tb); tb[2]=':'; itoa2(alarms[i].min,tb+3);
            fb_draw_str_scaled(ox+6,oy+i*28+7,tb,alarms[i].active?white:grey,abg,2);
            /* Toggle */
            uint32_t tgl=alarms[i].active?grn:grey;
            fb_fill_rect(ox+pw-56,oy+i*28+6,24,14,tgl);
            fb_draw_str(ox+pw-52,oy+i*28+9,alarms[i].active?"ON":"OFF",white,tgl);
            /* Delete */
            fb_fill_rect(ox+pw-28,oy+i*28+6,22,14,red);
            fb_draw_str(ox+pw-24,oy+i*28+9,"X",white,red);
        }
    } else {
        /* Temporizador */
        uint32_t remain=timer_running?(uint32_t)timer_secs-(timer_ticks()-timer_start_tick)/60:(uint32_t)timer_secs;
        int rm=(int)remain/60, rs=(int)remain%60;
        char tb[8]; itoa2(rm,tb); tb[2]=':'; itoa2(rs,tb+3);
        fb_draw_str_scaled(ox+pw/2-48,oy+10,tb,timer_running?grn:white,bg,4);

        /* Botones +/- tiempo */
        fb_fill_rect(ox,oy+60,50,22,tbg);fb_draw_str(ox+6,oy+69,"-1min",grey,tbg);
        fb_fill_rect(ox+54,oy+60,50,22,tbg);fb_draw_str(ox+60,oy+69,"+1min",grey,tbg);
        fb_fill_rect(ox+pw/2-30,oy+90,60,24,timer_running?red:grn);
        fb_draw_str(ox+pw/2-22,oy+98,timer_running?"Parar":"Iniciar",white,timer_running?red:grn);
        /* Reset */
        fb_fill_rect(ox+pw-60,oy+90,56,24,tbg);
        fb_draw_str(ox+pw-54,oy+98,"Reset",grey,tbg);
        char sb[16]; const char*pref="Duracion: "; int p=0;
        for(;pref[p];p++) sb[p]=pref[p]; char nb[8]; itoa10(timer_secs,nb);
        for(int i=0;nb[i];i++) sb[p++]=nb[i]; sb[p++]='s'; sb[p]='\0';
        fb_draw_str(ox,oy+122,sb,grey,bg);
    }
}

int alarm_click(int wx,int wy,int ww,int wh,int mx,int my){
    int ox=wx+BORDER+8, oy=wy+TITLEBAR_H+8;
    int pw=ww-BORDER*2-16;
    /* Tabs */
    if(my>=oy&&my<oy+22){ timer_mode=(mx>=ox+pw/2+2)?1:0; return 0; }
    oy+=26;
    /* Detener alarma */
    if(ringing&&mx>=ox+pw/2-30&&mx<ox+pw/2+30&&my>=oy+44&&my<oy+64){ ringing=0; sound_stop(); return 0; }
    if(ringing) oy+=70;

    if(timer_mode==0){
        /* Controles hora */
        if(mx>=ox+6&&mx<ox+26){ if(my>=oy+18&&my<oy+38){sel_h=(sel_h+1)%24;} else if(my>=oy+36&&my<oy+46){sel_h=(sel_h+23)%24;} }
        if(mx>=ox+72&&mx<ox+92){ if(my>=oy+18&&my<oy+38){sel_m=(sel_m+1)%60;} else if(my>=oy+36&&my<oy+46){sel_m=(sel_m+59)%60;} }
        /* Agregar */
        if(mx>=ox+pw-62&&mx<ox+pw-6&&my>=oy+18&&my<oy+38&&al_count<MAX_ALARMS){
            alarms[al_count].hour=sel_h; alarms[al_count].min=sel_m;
            alarms[al_count].active=1; alarms[al_count].fired=0;
            scpy(alarms[al_count].label,"Alarma",24); al_count++;
        }
        oy+=56;
        for(int i=0;i<al_count;i++){
            int ry=oy+i*28;
            if(my>=ry&&my<ry+26){
                if(mx>=ox+pw-56&&mx<ox+pw-32) alarms[i].active=!alarms[i].active;
                if(mx>=ox+pw-28&&mx<ox+pw-6){ /* delete */
                    for(int j=i;j<al_count-1;j++) alarms[j]=alarms[j+1]; al_count--;
                }
            }
        }
    } else {
        oy+=60;
        if(mx>=ox&&mx<ox+50&&my>=oy&&my<oy+22){ if(timer_secs>60)timer_secs-=60; }
        if(mx>=ox+54&&mx<ox+104&&my>=oy&&my<oy+22){ timer_secs+=60; }
        oy+=30;
        if(mx>=ox+pw/2-30&&mx<ox+pw/2+30&&my>=oy&&my<oy+24){
            if(timer_running){ timer_running=0; } else { timer_running=1; timer_start_tick=timer_ticks(); }
        }
        if(mx>=ox+pw-60&&mx<ox+pw-4&&my>=oy&&my<oy+24){ timer_running=0; }
    }
    (void)ww;(void)wh;
    return 0;
}
