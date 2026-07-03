#include "breakout.h"
#include "framebuffer.h"
#include "gui.h"
#include "sound.h"
#include <stdint.h>

#define BR_COLS 10
#define BR_ROWS 6
#define BRICK_W 36
#define BRICK_H 14
#define PADDLE_W 70
#define PADDLE_H 10
#define BALL_R   6

typedef struct { int x,y,alive,hits; } brick_t;
static brick_t bricks[BR_ROWS][BR_COLS];
static int ball_x,ball_y,ball_vx,ball_vy;
static int paddle_x;
static int score=0,lives=3,level=1;
static int br_state=0; /* 0=idle,1=playing,2=won,3=lost */
static int bricks_left=0;

static uint32_t brick_colors[BR_ROWS];

static void br_reset(void){
    brick_colors[0]=fb_color(0xff,0x44,0x44);
    brick_colors[1]=fb_color(0xff,0x88,0x00);
    brick_colors[2]=fb_color(0xff,0xff,0x00);
    brick_colors[3]=fb_color(0x44,0xff,0x44);
    brick_colors[4]=fb_color(0x44,0xcc,0xff);
    brick_colors[5]=fb_color(0xcc,0x44,0xff);
    bricks_left=0;
    for(int r=0;r<BR_ROWS;r++) for(int c=0;c<BR_COLS;c++){
        bricks[r][c].alive=1; bricks[r][c].hits=(r<2)?2:1; bricks_left++;
    }
    score=0; lives=3; level=1; br_state=0;
}

static void place_ball(int wx,int wy,int ww,int wh){
    int ox=wx+BORDER+2, oy=wy+TITLEBAR_H+2;
    int pw=ww-BORDER*2-4, ph=wh-TITLEBAR_H-BORDER-4;
    ball_x=ox+pw/2; ball_y=oy+ph-60;
    ball_vx=3; ball_vy=-3-level;
    paddle_x=ox+pw/2-PADDLE_W/2;
    (void)ph;
}

void breakout_init(void){ br_reset(); }

void breakout_mouse(int wx,int wy,int ww,int wh,int mx){
    int ox=wx+BORDER+2;
    int pw=ww-BORDER*2-4;
    paddle_x=mx-PADDLE_W/2;
    if(paddle_x<ox) paddle_x=ox;
    if(paddle_x+PADDLE_W>ox+pw) paddle_x=ox+pw-PADDLE_W;
    (void)wy;(void)wh;
}

void breakout_tick(int wx,int wy,int ww,int wh){
    if(br_state!=1) return;
    int ox=wx+BORDER+2, oy=wy+TITLEBAR_H+2;
    int pw=ww-BORDER*2-4, ph=wh-TITLEBAR_H-BORDER-4;
    int brick_ox=ox+2, brick_oy=oy+20;
    int paddle_y=oy+ph-PADDLE_H-6;

    ball_x+=ball_vx; ball_y+=ball_vy;
    /* Paredes */
    if(ball_x-BALL_R<ox){ ball_x=ox+BALL_R; ball_vx=-ball_vx; }
    if(ball_x+BALL_R>ox+pw){ ball_x=ox+pw-BALL_R; ball_vx=-ball_vx; }
    if(ball_y-BALL_R<oy){ ball_y=oy+BALL_R; ball_vy=-ball_vy; }
    /* Paddle */
    if(ball_y+BALL_R>=paddle_y&&ball_y+BALL_R<=paddle_y+PADDLE_H&&ball_x>=paddle_x&&ball_x<=paddle_x+PADDLE_W){
        ball_vy=-ball_vy<0?ball_vy:-(-ball_vy); /* asegurar va arriba */
        if(ball_vy>0) ball_vy=-ball_vy;
        int rel=ball_x-(paddle_x+PADDLE_W/2);
        ball_vx=rel/8; if(ball_vx==0) ball_vx=1;
        sound_click();
    }
    /* Caída */
    if(ball_y-BALL_R>oy+ph){ lives--; if(lives<=0){ br_state=3; } else { place_ball(wx,wy,ww,wh); br_state=0; } return; }
    /* Ladrillos */
    for(int r=0;r<BR_ROWS;r++) for(int c=0;c<BR_COLS;c++){
        if(!bricks[r][c].alive) continue;
        int bx=brick_ox+c*(BRICK_W+2), by=brick_oy+r*(BRICK_H+2);
        if(ball_x+BALL_R>bx&&ball_x-BALL_R<bx+BRICK_W&&ball_y+BALL_R>by&&ball_y-BALL_R<by+BRICK_H){
            bricks[r][c].hits--;
            if(bricks[r][c].hits<=0){ bricks[r][c].alive=0; score+=10*level; bricks_left--; }
            /* rebote simple */
            int from_top=(ball_y-BALL_R<by+BRICK_H/2);
            if(from_top) ball_vy=-ball_vy; else ball_vx=-ball_vx;
            sound_click();
            if(bricks_left==0){ br_state=2; level++; }
            goto done;
        }
    }
    done:;
    (void)ph;
}

static void itoa10(int n,char*b){if(n==0){b[0]='0';b[1]='\0';return;}char t[10];int i=0;while(n>0){t[i++]='0'+n%10;n/=10;}int p=0;while(i>0)b[p++]=t[--i];b[p]='\0';}

void breakout_draw(int wx,int wy,int ww,int wh){
    int ox=wx+BORDER+2, oy=wy+TITLEBAR_H+2;
    int pw=ww-BORDER*2-4, ph=wh-TITLEBAR_H-BORDER-4;
    uint32_t bg=fb_color(0x08,0x08,0x18);
    fb_fill_rect(ox,oy,pw,ph,bg);
    /* HUD */
    fb_fill_rect(ox,oy,pw,18,fb_color(0x11,0x11,0x22));
    char sb[16]; itoa10(score,sb); fb_draw_str(ox+4,oy+5,sb,fb_color(0xff,0xff,0x44),fb_color(0x11,0x11,0x22));
    fb_draw_str(ox+80,oy+5,"pts",fb_color(0x88,0x88,0xaa),fb_color(0x11,0x11,0x22));
    /* Vidas */
    for(int i=0;i<lives;i++) fb_fill_circle(ox+pw-20-i*18,oy+9,6,fb_color(0xff,0x44,0x44));
    char lb[8]; lb[0]='N'; lb[1]='i'; lb[2]='v'; lb[3]=':'; lb[4]=' '; lb[5]='0'+level; lb[6]='\0';
    fb_draw_str(ox+pw/2-20,oy+5,lb,fb_color(0x88,0xee,0x88),fb_color(0x11,0x11,0x22));

    int brick_ox=ox+2, brick_oy=oy+22;
    /* Ladrillos */
    for(int r=0;r<BR_ROWS;r++) for(int c=0;c<BR_COLS;c++){
        if(!bricks[r][c].alive) continue;
        int bx=brick_ox+c*(BRICK_W+2), by=brick_oy+r*(BRICK_H+2);
        uint32_t bc=brick_colors[r];
        if(bricks[r][c].hits>1) bc=fb_color(0xff,0xff,0xff); /* blanco=duro */
        fb_fill_rect(bx,by,BRICK_W,BRICK_H,bc);
        fb_fill_rect(bx,by,BRICK_W,2,fb_color(0xff,0xff,0xff));
        fb_fill_rect(bx,by,2,BRICK_H,fb_color(0xff,0xff,0xff));
        fb_fill_rect(bx+BRICK_W-2,by,2,BRICK_H,fb_color(0x44,0x44,0x44));
        fb_fill_rect(bx,by+BRICK_H-2,BRICK_W,2,fb_color(0x44,0x44,0x44));
    }
    /* Paddle */
    int paddle_y=oy+ph-PADDLE_H-6;
    fb_fill_rect(paddle_x,paddle_y,PADDLE_W,PADDLE_H,fb_color(0x88,0xcc,0xff));
    fb_fill_rect(paddle_x,paddle_y,PADDLE_W,3,fb_color(0xcc,0xee,0xff));
    /* Ball */
    if(br_state==1||br_state==0)
        fb_fill_circle(ball_x,ball_y,BALL_R,fb_color(0xff,0xff,0xff));

    /* Overlay */
    if(br_state==0){
        fb_draw_str(ox+pw/2-70,oy+ph/2,"Click para lanzar",fb_color(0xff,0xff,0xff),bg);
    } else if(br_state==2){
        fb_draw_str(ox+pw/2-50,oy+ph/2,"NIVEL SUPERADO!",fb_color(0x44,0xff,0x44),bg);
        fb_draw_str(ox+pw/2-50,oy+ph/2+16,"Click para seguir",fb_color(0xcc,0xcc,0xcc),bg);
    } else if(br_state==3){
        fb_draw_str(ox+pw/2-40,oy+ph/2,"GAME OVER",fb_color(0xff,0x44,0x44),bg);
        fb_draw_str(ox+pw/2-50,oy+ph/2+16,"Click para reiniciar",fb_color(0xcc,0xcc,0xcc),bg);
    }
}

int breakout_click(int wx,int wy,int ww,int wh,int mx,int my){
    if(br_state==0){ place_ball(wx,wy,ww,wh); br_state=1; }
    else if(br_state==2){ br_reset(); place_ball(wx,wy,ww,wh); bricks_left=BR_ROWS*BR_COLS; br_state=1; }
    else if(br_state==3){ br_reset(); place_ball(wx,wy,ww,wh); br_state=1; }
    (void)mx;(void)my;(void)ww;(void)wh;
    return 0;
}
