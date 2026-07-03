#include "sysmonitor.h"
#include "gui.h"
#include "framebuffer.h"
#include "cpuinfo.h"
#include "meminfo.h"
#include "pci.h"
#include "battery.h"
#include "fs.h"
#include "timer.h"
#include "keyboard.h"
#include "mouse.h"
#include "users.h"
#include <stdint.h>

static cpuinfo_t cpu;
static int cpu_read = 0;

static void itoa10(int n, char *buf) {
    int i=0;
    if(n<0){buf[i++]='-';n=-n;}
    if(n==0){buf[i++]='0';}
    else{char t[12];int j=0;while(n>0){t[j++]='0'+n%10;n/=10;}while(j>0)buf[i++]=t[--j];}
    buf[i]='\0';
}

static void draw_bar(int x, int y, int w, int h, int pct, uint32_t color) {
    fb_fill_rect(x,y,w,h,fb_color(0xdd,0xdd,0xee));
    fb_draw_rect(x,y,w,h,fb_color(0x88,0x88,0xaa));
    if(pct>0&&pct<=100){
        int fw=(w-2)*pct/100;
        if(fw<1)fw=1;
        fb_fill_rect(x+1,y+1,fw,h-2,color);
    }
}

void sysmonitor_draw(int wx,int wy,int ww,int wh) {
    if(!cpu_read){ cpuinfo_read(&cpu); cpu_read=1; }

    int ox=wx+BORDER+10, oy=wy+TITLEBAR_H+8;
    int bw=ww-BORDER*2-20;
    uint32_t bg=fb_color(0xf2,0xf2,0xf2);
    uint32_t fg=fb_color(0x11,0x11,0x22);
    uint32_t hdr=fb_color(0x0f,0x34,0x60);

    /* ---- CPU ---- */
    fb_draw_str(ox,oy,"CPU",hdr,bg); oy+=16;
    fb_draw_str(ox,oy,cpu.brand,fg,bg); oy+=14;

    char tmp[32];
    fb_draw_str(ox,oy,"Vendor:",fg,bg);
    fb_draw_str(ox+72,oy,cpu.vendor,fg,bg); oy+=14;

    fb_draw_str(ox,oy,"Familia:",fg,bg);
    itoa10(cpu.family,tmp);
    fb_draw_str(ox+72,oy,tmp,fg,bg);
    fb_draw_str(ox+90,oy," Modelo:",fg,bg);
    itoa10(cpu.model,tmp);
    fb_draw_str(ox+162,oy,tmp,fg,bg); oy+=14;

    fb_draw_str(ox,oy,"FPU:",fg,bg);
    fb_draw_str(ox+45,oy,cpu.has_fpu?"Si":"No",fg,bg);
    fb_draw_str(ox+75,oy,"MMX:",fg,bg);
    fb_draw_str(ox+105,oy,cpu.has_mmx?"Si":"No",fg,bg);
    fb_draw_str(ox+135,oy,"SSE:",fg,bg);
    fb_draw_str(ox+162,oy,cpu.has_sse?"Si":"No",fg,bg); oy+=18;

    /* ---- RAM ---- */
    fb_draw_str(ox,oy,"Memoria RAM",hdr,bg); oy+=16;

    uint32_t total_mb = meminfo_total_mb();
    uint32_t upper_kb = meminfo_upper_kb();
    itoa10((int)total_mb,tmp);
    fb_draw_str(ox,oy,"Total:",fg,bg);
    fb_draw_str(ox+60,oy,tmp,fg,bg);
    fb_draw_str(ox+60+9*(int)(tmp[1]?2:1),oy," MB",fg,bg); oy+=14;

    /* Barra de RAM (kernel usa ~4MB de los 64MB dados a QEMU) */
    int used_mb = 4;
    int total_shown = total_mb > 0 ? (int)total_mb : 64;
    int pct_ram = used_mb*100/total_shown;
    fb_draw_str(ox,oy,"Uso aprox:",fg,bg); oy+=14;
    draw_bar(ox,oy,bw,14,pct_ram,fb_color(0x20,0x80,0xc0));
    fb_draw_str(ox+bw+4,oy,tmp,fg,bg); oy+=20;

    /* ---- Batería ---- */
    fb_draw_str(ox,oy,"Bateria",hdr,bg); oy+=16;
    bat_info_t bat; battery_read(&bat);
    if(!bat.present){
        fb_draw_str(ox,oy,"No detectada (escritorio/QEMU)",fb_color(0x88,0x88,0x88),bg);
        oy+=14;
    } else {
        const char *st = "?";
        if(bat.status==BAT_CHARGING)    st="Cargando";
        else if(bat.status==BAT_DISCHARGING) st="Descargando";
        else if(bat.status==BAT_FULL)   st="Completa";
        fb_draw_str(ox,oy,st,fg,bg);
        fb_draw_str(ox+100,oy,"Nivel:",fg,bg);
        itoa10(bat.percent,tmp);
        fb_draw_str(ox+145,oy,tmp,fg,bg);
        fb_draw_str(ox+145+18,oy,"%",fg,bg); oy+=14;
        draw_bar(ox,oy,bw,14,bat.percent,
            bat.status==BAT_CHARGING ? fb_color(0x44,0xcc,0x44) : fb_color(0x44,0xcc,0x66));
        oy+=20;
    }

    /* ---- Sistema ---- */
    fb_draw_str(ox,oy,"Sistema",hdr,bg); oy+=16;
    fb_draw_str(ox,oy,"Usuario:",fg,bg);
    fb_draw_str(ox+80,oy,users_get_current(),fg,bg); oy+=14;
    fb_draw_str(ox,oy,"Uptime:",fg,bg);
    itoa10((int)timer_seconds(),tmp);
    fb_draw_str(ox+80,oy,tmp,fg,bg);
    fb_draw_str(ox+80+9*(int)(tmp[2]?3:tmp[1]?2:1),oy,"s",fg,bg); oy+=14;
    fb_draw_str(ox,oy,"Teclas:",fg,bg);
    itoa10((int)keyboard_key_count(),tmp);
    fb_draw_str(ox+80,oy,tmp,fg,bg); oy+=14;
    fb_draw_str(ox,oy,"Mouse pkts:",fg,bg);
    itoa10((int)mouse_packet_count(),tmp);
    fb_draw_str(ox+100,oy,tmp,fg,bg); oy+=18;

    /* Actividad animada */
    int bar_y = oy;
    if(bar_y + 14 < wy+wh-BORDER) {
        fb_draw_str(ox,bar_y,"Actividad:",fg,bg); bar_y+=14;
        fb_fill_rect(ox,bar_y,bw,12,fb_color(0xdd,0xdd,0xee));
        fb_draw_rect(ox,bar_y,bw,12,fb_color(0x88,0x88,0xaa));
        uint32_t t=timer_ticks()%200;
        int wpos=(t<100)?(int)t:(int)(200-t);
        int barx=ox+(bw-36)*wpos/100;
        fb_fill_rect(barx,bar_y,36,12,fb_color(0x40,0xc0,0x60));
    }
}
