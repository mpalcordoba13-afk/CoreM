#include "calendar.h"
#include "framebuffer.h"
#include "gui.h"
#include "rtc.h"
#include <stdint.h>

#define MAX_EVENTS 32
#define EV_LEN 48

typedef struct { int day,month,year; char text[EV_LEN]; int active; } cal_event_t;
static cal_event_t events[MAX_EVENTS];
static int ev_count=0;

static int view_month, view_year;
static int sel_day=-1;
static char new_ev[EV_LEN]; static int new_ev_len=0; static int adding=0;

static void scpy(char*d,const char*s,int n){int i=0;while(s[i]&&i<n-1){d[i]=s[i];i++;}d[i]='\0';}
static int slen(const char*s){int n=0;while(s[n])n++;return n;}
static void itoa10(int n,char*b){if(n==0){b[0]='0';b[1]='\0';return;}char t[8];int i=0;while(n>0){t[i++]='0'+n%10;n/=10;}int p=0;while(i>0)b[p++]=t[--i];b[p]='\0';}

/* Día de la semana del 1 del mes (0=Dom) – Tomohiko Sakamoto */
static int day_of_week(int y,int m,int d){
    static int t[]={0,3,2,5,0,3,5,1,4,6,2,4};
    if(m<3)y--;
    return(y+y/4-y/100+y/400+t[m-1]+d)%7;
}
static int days_in_month(int m,int y){
    static int dm[]={0,31,28,31,30,31,30,31,31,30,31,30,31};
    if(m==2&&(y%4==0&&(y%100!=0||y%400==0)))return 29;
    return dm[m];
}

void calendar_init(void){
    rtc_time_t rt; rtc_read(&rt);
    view_month=rt.month; view_year=rt.year;
    ev_count=0;
    /* Evento de ejemplo */
    events[0].day=rt.day; events[0].month=rt.month; events[0].year=rt.year;
    scpy(events[0].text,"Hoy!",EV_LEN); events[0].active=1; ev_count=1;
}

void calendar_draw(int wx,int wy,int ww,int wh){
    uint32_t bg   =fb_color(0xf2,0xf4,0xff);
    uint32_t hdr  =fb_color(0x22,0x44,0x88);
    uint32_t wknd =fb_color(0xff,0xee,0xee);
    uint32_t today_bg=fb_color(0x44,0x88,0xff);
    uint32_t sel_bg  =fb_color(0xcc,0xdd,0xff);
    uint32_t evt_col =fb_color(0xff,0x88,0x00);
    uint32_t white   =fb_color(0xff,0xff,0xff);
    uint32_t txt     =fb_color(0x11,0x11,0x33);
    uint32_t grid    =fb_color(0xcc,0xcc,0xdd);

    int ox=wx+BORDER+4, oy=wy+TITLEBAR_H+4;
    int pw=ww-BORDER*2-8;

    /* Header nav */
    fb_fill_rect(ox,oy,pw,28,hdr);
    fb_fill_rect(ox+2,oy+4,22,20,fb_color(0x33,0x66,0xaa));
    fb_draw_str(ox+6,oy+9,"<",white,fb_color(0x33,0x66,0xaa));
    fb_fill_rect(ox+pw-24,oy+4,22,20,fb_color(0x33,0x66,0xaa));
    fb_draw_str(ox+pw-20,oy+9,">",white,fb_color(0x33,0x66,0xaa));

    const char*months[]={"Enero","Febrero","Marzo","Abril","Mayo","Junio",
                          "Julio","Agosto","Septiembre","Octubre","Noviembre","Diciembre"};
    char title[24]; int p=0;
    const char*mn=months[view_month-1]; for(int i=0;mn[i];i++)title[p++]=mn[i];
    title[p++]=' '; char yb[6]; itoa10(view_year,yb); for(int i=0;yb[i];i++)title[p++]=yb[i]; title[p]='\0';
    int tlen=slen(title)*9;
    fb_draw_str(ox+pw/2-tlen/2,oy+9,title,white,hdr);

    /* Días de la semana */
    const char*dows[]={"Dom","Lun","Mar","Mie","Jue","Vie","Sab"};
    int cell_w=pw/7;
    for(int i=0;i<7;i++){
        uint32_t dbg=(i==0||i==6)?fb_color(0xff,0xdd,0xdd):fb_color(0xe8,0xec,0xff);
        fb_fill_rect(ox+i*cell_w,oy+30,cell_w,16,dbg);
        fb_draw_rect(ox+i*cell_w,oy+30,cell_w,16,grid);
        fb_draw_str(ox+i*cell_w+2,oy+33,dows[i],txt,dbg);
    }

    /* Celdas del mes */
    int start_dow=day_of_week(view_year,view_month,1);
    int dim=days_in_month(view_month,view_year);
    int cell_h=(wh-TITLEBAR_H-BORDER-50-30)/6; if(cell_h<20)cell_h=20;

    rtc_time_t rt; rtc_read(&rt);

    for(int d=1;d<=dim;d++){
        int slot=start_dow+(d-1);
        int col=slot%7, row=slot/7;
        int cx2=ox+col*cell_w, cy2=oy+48+row*cell_h;
        int is_today=(d==rt.day&&view_month==rt.month&&view_year==rt.year);
        int is_sel=(d==sel_day);
        int is_wknd=(col==0||col==6);
        uint32_t cbg=is_today?today_bg:(is_sel?sel_bg:(is_wknd?wknd:white));
        fb_fill_rect(cx2,cy2,cell_w,cell_h,cbg);
        fb_draw_rect(cx2,cy2,cell_w,cell_h,grid);
        char db[4]; itoa10(d,db);
        fb_draw_str(cx2+2,cy2+3,db,is_today?white:txt,cbg);
        /* Punto de evento */
        for(int e=0;e<ev_count;e++){
            if(events[e].active&&events[e].day==d&&events[e].month==view_month&&events[e].year==view_year){
                fb_fill_circle(cx2+cell_w-6,cy2+6,3,evt_col);
                break;
            }
        }
    }

    /* Panel de eventos del día seleccionado */
    if(sel_day>0){
        int ev_y=oy+48+6*cell_h+4;
        fb_fill_rect(ox,ev_y,pw,wh-TITLEBAR_H-BORDER-(ev_y-wy),bg);
        fb_draw_str(ox+2,ev_y+2,"Eventos:",fb_color(0x22,0x44,0x88),bg);
        int found=0;
        for(int e=0;e<ev_count;e++){
            if(events[e].active&&events[e].day==sel_day&&events[e].month==view_month&&events[e].year==view_year){
                fb_draw_str(ox+2,ev_y+14+found*12,events[e].text,txt,bg);
                found++;
            }
        }
        if(!found) fb_draw_str(ox+2,ev_y+14,"Sin eventos",fb_color(0x88,0x88,0xaa),bg);
        /* Input para nuevo evento */
        if(adding){
            fb_fill_rect(ox,ev_y+50,pw,18,white);
            fb_draw_rect(ox,ev_y+50,pw,18,fb_color(0x44,0x88,0xff));
            fb_draw_str(ox+3,ev_y+54,new_ev,txt,white);
            fb_fill_rect(ox+3+slen(new_ev)*9,ev_y+54,2,10,txt);
        } else {
            fb_fill_rect(ox,ev_y+50,80,16,fb_color(0x22,0x88,0x44));
            fb_draw_str(ox+4,ev_y+53,"+ Agregar",white,fb_color(0x22,0x88,0x44));
        }
    }
}

int calendar_click(int wx,int wy,int ww,int wh,int mx,int my){
    int ox=wx+BORDER+4, oy=wy+TITLEBAR_H+4;
    int pw=ww-BORDER*2-8;
    /* Nav */
    if(mx>=ox+2&&mx<ox+24&&my>=oy+4&&my<oy+24){
        view_month--; if(view_month<1){view_month=12;view_year--;} sel_day=-1; return 0;
    }
    if(mx>=ox+pw-24&&mx<ox+pw-2&&my>=oy+4&&my<oy+24){
        view_month++; if(view_month>12){view_month=1;view_year++;} sel_day=-1; return 0;
    }
    /* Celdas */
    int cell_w=pw/7;
    int cell_h=(wh-TITLEBAR_H-BORDER-50-30)/6; if(cell_h<20)cell_h=20;
    int start_dow=day_of_week(view_year,view_month,1);
    int dim=days_in_month(view_month,view_year);
    for(int d=1;d<=dim;d++){
        int slot=start_dow+(d-1);
        int col=slot%7,row=slot/7;
        int cx2=ox+col*cell_w,cy2=oy+48+row*cell_h;
        if(mx>=cx2&&mx<cx2+cell_w&&my>=cy2&&my<cy2+cell_h){ sel_day=d; adding=0; return 0; }
    }
    /* Botón agregar */
    if(sel_day>0){
        int ev_y=oy+48+6*cell_h+4;
        if(mx>=ox&&mx<ox+80&&my>=ev_y+50&&my<ev_y+66&&!adding){
            adding=1; new_ev[0]='\0'; new_ev_len=0;
        }
    }
    (void)ww;(void)wh;
    return 0;
}

void calendar_key(char c){
    if(!adding) return;
    if(c=='\n'||c=='\r'){
        if(new_ev_len>0&&ev_count<MAX_EVENTS){
            events[ev_count].day=sel_day; events[ev_count].month=view_month;
            events[ev_count].year=view_year; scpy(events[ev_count].text,new_ev,EV_LEN);
            events[ev_count].active=1; ev_count++;
        }
        adding=0; return;
    }
    if(c=='\b'){if(new_ev_len>0){new_ev_len--;new_ev[new_ev_len]='\0';}return;}
    if(c>=' '&&c<127&&new_ev_len<EV_LEN-1){new_ev[new_ev_len++]=c;new_ev[new_ev_len]='\0';}
}
