#include "calc.h"
#include "gui.h"
#include "framebuffer.h"
#include "sound.h"
#include <stdint.h>

#define BTN_W    56
#define BTN_H    36
#define BTN_PAD   5
#define DISPLAY_H 44
#define ROWS 6
#define COLS 4

static const char *btn_labels[ROWS*COLS] = {
    "MC", "MR",  "M+", "M-",
    "C",  "+/-", "%",  "/",
    "7",  "8",   "9",  "*",
    "4",  "5",   "6",  "-",
    "1",  "2",   "3",  "+",
    "0",  ".",   "=",  "BS",
};

static double acc=0, cur=0, memval=0;
static char   op=0;
static int    fresh=1, has_dot=0, dot_pos=1;
static char   display[24]="0";

static double myabs(double x){return x<0?-x:x;}

static void dbl2str(double v, char *buf) {
    int neg=(v<0); if(neg)v=-v;
    int integer=(int)v;
    double frac=v-integer;
    char tmp[16]; int i=0;
    if(integer==0){tmp[i++]='0';}
    else{char rev[12];int j=0;int n=integer;
         while(n>0){rev[j++]='0'+n%10;n/=10;}
         while(j>0)tmp[i++]=rev[--j];}
    int out=0;
    if(neg)buf[out++]='-';
    for(int k=0;k<i;k++)buf[out++]=tmp[k];
    if(frac>0.00001){
        buf[out++]='.';
        for(int d=0;d<5&&frac>0.00001;d++){
            frac*=10; int digit=(int)frac;
            buf[out++]='0'+digit; frac-=digit;
        }
        while(out>0&&buf[out-1]=='0')out--;
        if(out>0&&buf[out-1]=='.')out--;
    }
    buf[out]='\0';
}

static void calc_reset(void){
    acc=0;cur=0;op=0;fresh=1;has_dot=0;dot_pos=1;
    display[0]='0';display[1]='\0';
}

static void calc_press(int idx){
    const char *lbl=btn_labels[idx];

    if(lbl[0]>='0'&&lbl[0]<='9'){
        int digit=lbl[0]-'0';
        if(fresh){cur=0;has_dot=0;dot_pos=1;fresh=0;}
        if(!has_dot) cur=cur*10+digit;
        else{dot_pos*=10;cur=cur+(double)digit/dot_pos;}
        dbl2str(cur,display); return;
    }
    if(lbl[0]=='.'){
        if(fresh){cur=0;fresh=0;}
        if(!has_dot){has_dot=1;dot_pos=1;}
        dbl2str(cur,display);
        int len=0;while(display[len])len++;
        display[len]='.';display[len+1]='\0'; return;
    }
    if(lbl[0]=='C'&&lbl[1]==0){calc_reset();return;}
    if(lbl[0]=='M'&&lbl[1]=='C'){memval=0;return;}
    if(lbl[0]=='M'&&lbl[1]=='R'){cur=memval;fresh=0;dbl2str(cur,display);return;}
    if(lbl[0]=='M'&&lbl[1]=='+'){memval+=cur;return;}
    if(lbl[0]=='M'&&lbl[1]=='-'){memval-=cur;return;}
    if(lbl[0]=='B'&&lbl[1]=='S'){
        int len=0;while(display[len])len++;
        if(len>1)display[len-1]='\0';
        else{display[0]='0';display[1]='\0';}
        double v=0;int dec=0,decpos=1,neg=0,start=0;
        if(display[0]=='-'){neg=1;start=1;}
        for(int i=start;display[i];i++){
            if(display[i]=='.'){dec=1;continue;}
            int d=display[i]-'0';
            if(!dec)v=v*10+d;
            else{decpos*=10;v+=(double)d/decpos;}
        }
        cur=neg?-v:v; return;
    }
    if(lbl[0]=='+'&&lbl[1]=='/'){
        cur=-cur;dbl2str(cur,display);return;
    }
    if(lbl[0]=='%'){cur=cur/100.0;dbl2str(cur,display);return;}
    if(lbl[0]=='='){
        if(op){
            double res=acc;
            if(op=='+')res=acc+cur;
            else if(op=='-')res=acc-cur;
            else if(op=='*')res=acc*cur;
            else if(op=='/'){ if(myabs(cur)<0.000001){
                display[0]='E';display[1]='r';display[2]='r';display[3]='\0';
                op=0;fresh=1;return;} res=acc/cur;}
            acc=res; dbl2str(res,display);
        }
        op=0;fresh=1;return;
    }
    /* Operador */
    char newop=lbl[0];
    if(op&&!fresh){
        double res=acc;
        if(op=='+')res=acc+cur;
        else if(op=='-')res=acc-cur;
        else if(op=='*')res=acc*cur;
        else if(op=='/'&&myabs(cur)>0.000001)res=acc/cur;
        acc=res;dbl2str(res,display);
    } else acc=cur;
    op=newop;fresh=1;
}

static uint32_t btn_color(int idx){
    const char *l=btn_labels[idx];
    if(l[0]=='M')                         return fb_color(0x55,0x44,0x99);
    if(l[0]=='C'&&l[1]==0)                return fb_color(0xcc,0x33,0x33);
    if(l[0]=='=')                         return fb_color(0xe9,0x45,0x60);
    if(l[0]=='/'||l[0]=='*'||(l[0]=='-'&&l[1]==0)||(l[0]=='+'&&l[1]==0))
                                          return fb_color(0x0f,0x55,0x99);
    if(l[0]=='+'&&l[1]=='/')             return fb_color(0x33,0x55,0x77);
    if(l[0]=='%'||(l[0]=='B'&&l[1]=='S'))return fb_color(0x33,0x55,0x77);
    return fb_color(0x22,0x22,0x44);
}

void calc_draw(int wx,int wy,int ww){
    int ox=wx+BORDER+8;
    int oy=wy+TITLEBAR_H+8;
    int dw=ww-BORDER*2-16;

    /* Indicador de memoria */
    if (myabs(memval) > 0.000001) {
        fb_draw_str(ox,oy,"M",fb_color(0x99,0x88,0xff),fb_color(0xf2,0xf2,0xf2));
        oy += 12;
    } else {
        oy += 12;
    }

    /* Display */
    fb_fill_rect(ox,oy,dw,DISPLAY_H,fb_color(0x08,0x08,0x18));
    fb_draw_rect(ox,oy,dw,DISPLAY_H,fb_color(0x44,0x66,0xaa));
    int tlen=0;while(display[tlen])tlen++;
    int tx=ox+dw-tlen*9-8;if(tx<ox+4)tx=ox+4;
    fb_draw_str(tx,oy+DISPLAY_H/2-4,display,fb_color(0xff,0xff,0xff),fb_color(0x08,0x08,0x18));
    if(op){char ops[2]={op,'\0'};
           fb_draw_str(ox+4,oy+6,ops,fb_color(0x88,0xcc,0xff),fb_color(0x08,0x08,0x18));}

    oy+=DISPLAY_H+6;
    for(int row=0;row<ROWS;row++){
        for(int col=0;col<COLS;col++){
            int bi=row*COLS+col;
            int bx=ox+col*(BTN_W+BTN_PAD);
            int by=oy+row*(BTN_H+BTN_PAD);
            uint32_t bg=btn_color(bi);
            fb_fill_rect(bx,by,BTN_W,BTN_H,bg);
            fb_draw_rect(bx,by,BTN_W,BTN_H,fb_color(0x55,0x55,0x99));
            int llen=0;while(btn_labels[bi][llen])llen++;
            fb_draw_str(bx+(BTN_W-llen*9)/2,by+(BTN_H-8)/2,
                        btn_labels[bi],fb_color(0xff,0xff,0xff),bg);
        }
    }
}

int calc_click(int wx,int wy,int ww,int mx,int my){
    int ox=wx+BORDER+8;
    int oy=wy+TITLEBAR_H+8+12+DISPLAY_H+6;
    (void)ww;
    for(int row=0;row<ROWS;row++){
        for(int col=0;col<COLS;col++){
            int bx=ox+col*(BTN_W+BTN_PAD);
            int by=oy+row*(BTN_H+BTN_PAD);
            if(mx>=bx&&mx<bx+BTN_W&&my>=by&&my<by+BTN_H){
                calc_press(row*COLS+col);
                return 1;
            }
        }
    }
    return 0;
}

void calc_init(void){calc_reset();memval=0;}
