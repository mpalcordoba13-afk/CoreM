#include "login.h"
#include "framebuffer.h"
#include "keyboard.h"
#include "users.h"
#include "sound.h"
#include <stdint.h>

static int slen(const char *s){int n=0;while(s[n])n++;return n;}

/* Dibuja fondo solo una vez, luego solo actualiza la caja */
static void draw_background(void){
    int sw=fb_width(), sh=fb_height();
    /* Color sólido — NO gradiente, es 10x más rápido */
    fb_fill_rect(0,0,sw,sh,fb_color(0x12,0x10,0x30));

    int cx=sw/2;
    int scale=5;
    const char *logo="CoreM";
    int logo_w=slen(logo)*9*scale;
    fb_draw_str_scaled(cx-logo_w/2,70,logo,
        fb_color(0xff,0xff,0xff),fb_color(0x12,0x10,0x30),scale);
    fb_draw_str(cx-72,145,"Inicio de sesion",
        fb_color(0xcc,0xcc,0xff),fb_color(0x12,0x10,0x30));
}

static void draw_fields(const char *user,const char *pass,int msgtype){
    int sw=fb_width();
    int cx=sw/2;
    int bw=380,bh=170,bx=cx-bw/2,by=190;
    uint32_t box=fb_color(0x20,0x1c,0x40);
    uint32_t bg2=fb_color(0x10,0x0e,0x24);
    uint32_t brd=fb_color(0x77,0x66,0xcc);
    uint32_t brd2=fb_color(0x55,0x55,0x88);
    uint32_t white=fb_color(0xff,0xff,0xff);
    uint32_t ltxt=fb_color(0xaa,0xaa,0xdd);

    fb_fill_rect(bx,by,bw,bh,box);
    fb_draw_rect(bx,by,bw,bh,brd);

    fb_draw_str(bx+24,by+18,"Usuario:",ltxt,box);
    fb_fill_rect(bx+24,by+36,bw-48,24,bg2);
    fb_draw_rect(bx+24,by+36,bw-48,24,brd2);
    fb_draw_str(bx+32,by+43,user,white,bg2);

    fb_draw_str(bx+24,by+72,"Contrasena:",ltxt,box);
    fb_fill_rect(bx+24,by+90,bw-48,24,bg2);
    fb_draw_rect(bx+24,by+90,bw-48,24,brd2);
    char masked[32]; int l=slen(pass); if(l>28)l=28;
    for(int i=0;i<l;i++) masked[i]='*'; masked[l]='\0';
    fb_draw_str(bx+32,by+97,masked,white,bg2);

    if(msgtype==1)
        fb_draw_str(bx+24,by+128,"Acceso concedido!",fb_color(0x77,0xff,0x99),box);
    else if(msgtype==2)
        fb_draw_str(bx+24,by+128,"Usuario o clave incorrecta",fb_color(0xff,0x88,0x88),box);
    else
        fb_draw_str(bx+24,by+128,"Presiona Enter para continuar",fb_color(0x88,0x88,0xaa),box);

    fb_draw_str(cx-160,by+bh+18,
        "Usuarios: admin / user   -   clave: 1234",
        fb_color(0x77,0x77,0x99),fb_color(0x12,0x10,0x30));

    fb_flush();
}

void login_run(void){
    char user[24],pass[24];

    draw_background();
    fb_flush();

    while(1){
        int ulen=0,plen=0;
        user[0]='\0'; pass[0]='\0';
        draw_fields(user,pass,0);

        /* Leer usuario */
        while(1){
            char c=keyboard_getchar();
            if(c=='\n'||c=='\r') break;
            else if(c=='\b'){ if(ulen>0){ulen--;user[ulen]='\0';} }
            else if(ulen<23&&c>=32&&c<127){ user[ulen++]=c; user[ulen]='\0'; }
            draw_fields(user,pass,0);
        }

        /* Leer contraseña */
        draw_fields(user,pass,0);
        while(1){
            char c=keyboard_getchar();
            if(c=='\n'||c=='\r') break;
            else if(c=='\b'){ if(plen>0){plen--;pass[plen]='\0';} }
            else if(plen<23&&c>=32&&c<127){ pass[plen++]=c; pass[plen]='\0'; }
            draw_fields(user,pass,0);
        }

        if(users_check(user,pass)){
            users_set_current(user);
            draw_fields(user,pass,1);
            /* Espera corta sin busy-loop pesado */
            for(volatile int i=0;i<500000;i++);
            return;
        } else {
            draw_fields(user,pass,2);
            for(volatile int i=0;i<500000;i++);
        }
    }
}
