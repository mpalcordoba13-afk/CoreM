#include "settings.h"
#include "gui.h"
#include "framebuffer.h"
#include "rtc.h"
#include "sound.h"
#include <stdint.h>

int g_dark_theme = 0;
int g_clock_12h  = 0;
int g_sound_on   = 1;

static rtc_time_t edit;

#define COL(r,g,b) fb_color(r,g,b)

void settings_init(void) {
    rtc_read(&edit);
}

static void btn(int x,int y,int w,int h,const char *label,uint32_t bg,uint32_t fg){
    fb_fill_rect(x,y,w,h,bg);
    fb_draw_rect(x,y,w,h,fb_color(0x55,0x55,0x88));
    int tlen=0; while(label[tlen])tlen++;
    fb_draw_str(x+(w-tlen*9)/2,y+(h-8)/2,label,fg,bg);
}

void settings_draw(int wx,int wy,int ww,int wh){
    (void)wh;
    uint32_t bg = g_dark_theme ? COL(0x22,0x22,0x30) : COL(0xf2,0xf2,0xf2);
    uint32_t fg = g_dark_theme ? COL(0xee,0xee,0xff) : COL(0x11,0x11,0x22);
    int ox=wx+BORDER+10, oy=wy+TITLEBAR_H+12;
    int bw = ww-BORDER*2-20;

    fb_draw_str(ox,oy,"Configuracion",COL(0x0f,0x88,0xcc),bg); oy+=26;

    /* Tema */
    fb_draw_str(ox,oy+6,"Tema:",fg,bg);
    btn(ox+100,oy,bw-100,26, g_dark_theme?"Oscuro":"Claro",
        COL(0x20,0x55,0x95),COL(0xff,0xff,0xff));
    oy+=36;

    /* Formato de reloj */
    fb_draw_str(ox,oy+6,"Reloj:",fg,bg);
    btn(ox+100,oy,bw-100,26, g_clock_12h?"12 horas (AM/PM)":"24 horas",
        COL(0x20,0x55,0x95),COL(0xff,0xff,0xff));
    oy+=36;

    /* Sonido */
    fb_draw_str(ox,oy+6,"Sonido:",fg,bg);
    btn(ox+100,oy,bw-100,26, g_sound_on?"Activado":"Silenciado",
        g_sound_on?COL(0x20,0x80,0x40):COL(0x80,0x30,0x30),COL(0xff,0xff,0xff));
    oy+=46;

    /* Hora */
    fb_draw_str(ox,oy,"Ajustar hora del sistema:",fg,bg);
    oy+=24;

    btn(ox,oy,30,28,"-",COL(0x33,0x55,0x77),COL(0xff,0xff,0xff));
    char hb[3]; hb[0]='0'+(edit.hour/10); hb[1]='0'+(edit.hour%10); hb[2]='\0';
    fb_fill_rect(ox+34,oy,46,28,bg);
    fb_draw_rect(ox+34,oy,46,28,COL(0x55,0x55,0x88));
    fb_draw_str(ox+50,oy+10,hb,fg,bg);
    btn(ox+84,oy,30,28,"+",COL(0x33,0x55,0x77),COL(0xff,0xff,0xff));

    fb_draw_str(ox+122,oy+10,":",fg,bg);

    btn(ox+138,oy,30,28,"-",COL(0x33,0x55,0x77),COL(0xff,0xff,0xff));
    char mb[3]; mb[0]='0'+(edit.min/10); mb[1]='0'+(edit.min%10); mb[2]='\0';
    fb_fill_rect(ox+172,oy,46,28,bg);
    fb_draw_rect(ox+172,oy,46,28,COL(0x55,0x55,0x88));
    fb_draw_str(ox+188,oy+10,mb,fg,bg);
    btn(ox+222,oy,30,28,"+",COL(0x33,0x55,0x77),COL(0xff,0xff,0xff));
    oy+=40;

    btn(ox,oy,bw,30,"Aplicar hora",COL(0x20,0x80,0x40),COL(0xff,0xff,0xff));
}

int settings_click(int wx,int wy,int ww,int wh,int mx,int my){
    (void)wh;
    int ox=wx+BORDER+10, oy=wy+TITLEBAR_H+12;
    int bw = ww-BORDER*2-20;
    oy+=26;

    /* Tema */
    if(mx>=ox+100&&mx<ox+100+(bw-100)&&my>=oy&&my<oy+26){ g_dark_theme=!g_dark_theme; return 1; }
    oy+=36;
    /* Reloj */
    if(mx>=ox+100&&mx<ox+100+(bw-100)&&my>=oy&&my<oy+26){ g_clock_12h=!g_clock_12h; return 1; }
    oy+=36;
    /* Sonido */
    if(mx>=ox+100&&mx<ox+100+(bw-100)&&my>=oy&&my<oy+26){ g_sound_on=!g_sound_on; return 1; }
    oy+=46+24;

    /* Hora -/+ */
    if(mx>=ox&&mx<ox+30&&my>=oy&&my<oy+28){ edit.hour=(edit.hour+23)%24; return 1; }
    if(mx>=ox+84&&mx<ox+114&&my>=oy&&my<oy+28){ edit.hour=(edit.hour+1)%24; return 1; }
    /* Minuto -/+ */
    if(mx>=ox+138&&mx<ox+168&&my>=oy&&my<oy+28){ edit.min=(edit.min+59)%60; return 1; }
    if(mx>=ox+222&&mx<ox+252&&my>=oy&&my<oy+28){ edit.min=(edit.min+1)%60; return 1; }
    oy+=40;

    /* Aplicar */
    if(mx>=ox&&mx<ox+bw&&my>=oy&&my<oy+30){
        edit.sec=0;
        rtc_write(&edit);
        return 1;
    }
    return 0;
}
