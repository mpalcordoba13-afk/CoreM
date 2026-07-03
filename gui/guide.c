#include "guide.h"
#include "gui.h"
#include "framebuffer.h"
#include <stdint.h>

#define GUIDE_PAGES 4
static int page = 0;

static const char *pages[GUIDE_PAGES][9] = {
{
 "Bienvenido a CoreM","",
 "- Click derecho en el escritorio:",
 "  menu rapido (fondo, nota, config)",
 "- Arrastra ventanas a los bordes",
 "  de la pantalla para anclarlas",
 "- Iconos arriba a la derecha son",
 "  tus archivos guardados",
 ""
},
{
 "Ventanas y atajos","",
 "- Boton _ minimiza (sigue en barra)",
 "- Boton [] maximiza/restaura",
 "- Boton x cierra del todo",
 "- Esquina inf. derecha: redimensionar",
 "- Alt+Tab cambia ventana activa",
 "- Esc cierra menus abiertos",
 ""
},
{
 "Apps nuevas","",
 "- Imagenes: galeria de /assets",
 "- Musica: reproductor de /sounds",
 "- Snake y Tetris en el menu Core",
 "- Papelera: restaura o vacia",
 "- Configuracion: tema, reloj, hora",
 "- Terminal: 'calc 5+3' rapido",
 ""
},
{
 "Terminal","",
 "- help, ls, cat, echo, date",
 "- settime HH MM SS",
 "- setdate DD MM AAAA",
 "- passwd <clave nueva>",
 "- Flecha arriba: repetir comando",
 "- calc <expresion>",
 ""
}
};

void guide_draw(int wx,int wy,int ww,int wh){
    (void)wh;
    int ox=wx+BORDER+10, oy=wy+TITLEBAR_H+10;
    uint32_t bg=fb_color(0xf2,0xf2,0xf2), fg=fb_color(0x11,0x11,0x22);

    fb_draw_str(ox,oy,pages[page][0],fb_color(0x0f,0x34,0x60),bg);
    oy+=26;
    for(int i=2;i<9;i++){
        if(pages[page][i][0]=='\0') continue;
        fb_draw_str(ox,oy,pages[page][i],fg,bg);
        oy+=16;
    }

    int bw=ww-BORDER*2-20;
    int by=wy+wh-BORDER-38;
    fb_fill_rect(ox,by,60,28,fb_color(0x33,0x55,0x77));
    fb_draw_str(ox+18,by+10,"<",fb_color(0xff,0xff,0xff),fb_color(0x33,0x55,0x77));

    char pg[8]; pg[0]='0'+page+1; pg[1]='/'; pg[2]='0'+GUIDE_PAGES; pg[3]='\0';
    fb_draw_str(ox+bw/2-12,by+10,pg,fg,bg);

    fb_fill_rect(ox+bw-60,by,60,28,fb_color(0x33,0x55,0x77));
    fb_draw_str(ox+bw-42,by+10,">",fb_color(0xff,0xff,0xff),fb_color(0x33,0x55,0x77));
}

int guide_click(int wx,int wy,int ww,int wh,int mx,int my){
    int ox=wx+BORDER+10;
    int bw=ww-BORDER*2-20;
    int by=wy+wh-BORDER-38;
    if(mx>=ox&&mx<ox+60&&my>=by&&my<by+28){ page=(page+GUIDE_PAGES-1)%GUIDE_PAGES; return 1; }
    if(mx>=ox+bw-60&&mx<ox+bw&&my>=by&&my<by+28){ page=(page+1)%GUIDE_PAGES; return 1; }
    return 0;
}
