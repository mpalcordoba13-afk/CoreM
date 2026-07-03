/*
 * paint.c – Editor de dibujo tipo MS Paint básico
 * Canvas 320x240, herramientas: lápiz, borrador, línea, rectángulo, relleno, colores
 */
#include "paint.h"
#include "framebuffer.h"
#include "gui.h"
#include <stdint.h>

#define CANVAS_W  400
#define CANVAS_H  280
#define TOOL_H    36
#define PALETTE_Y 8
#define PALETTE_X 8

/* Canvas en memoria */
static uint32_t canvas[CANVAS_W * CANVAS_H];
static int canvas_dirty = 1;

/* Estado de herramienta */
typedef enum { TOOL_PENCIL=0, TOOL_ERASER, TOOL_LINE, TOOL_RECT, TOOL_FILL } tool_t;
static tool_t cur_tool = TOOL_PENCIL;
static uint32_t fg_color, bg_color;
static int brush_size = 2;
static int prev_mx=-1, prev_my=-1;
static int drag_start_x=-1, drag_start_y=-1;
static int is_drawing=0;

/* Preview para línea/rect */
static uint32_t preview[CANVAS_W * CANVAS_H];
static int has_preview=0;

/* Paleta de colores */
#define PAL_N 20
static uint32_t palette[PAL_N];

static void canvas_clear(void){
    for(int i=0;i<CANVAS_W*CANVAS_H;i++) canvas[i]=0xFFFFFF;
    canvas_dirty=1;
}

static void init_palette(void){
    palette[0]  = fb_color(0x00,0x00,0x00); /* negro */
    palette[1]  = fb_color(0xff,0xff,0xff); /* blanco */
    palette[2]  = fb_color(0xff,0x00,0x00); /* rojo */
    palette[3]  = fb_color(0x00,0xff,0x00); /* verde */
    palette[4]  = fb_color(0x00,0x00,0xff); /* azul */
    palette[5]  = fb_color(0xff,0xff,0x00); /* amarillo */
    palette[6]  = fb_color(0xff,0x88,0x00); /* naranja */
    palette[7]  = fb_color(0xff,0x00,0xff); /* magenta */
    palette[8]  = fb_color(0x00,0xff,0xff); /* cyan */
    palette[9]  = fb_color(0x88,0x44,0x00); /* marrón */
    palette[10] = fb_color(0x88,0x00,0x00); /* rojo oscuro */
    palette[11] = fb_color(0x00,0x88,0x00); /* verde oscuro */
    palette[12] = fb_color(0x00,0x00,0x88); /* azul oscuro */
    palette[13] = fb_color(0x88,0x88,0x00); /* oliva */
    palette[14] = fb_color(0x00,0x88,0x88); /* teal */
    palette[15] = fb_color(0x88,0x00,0x88); /* morado */
    palette[16] = fb_color(0xcc,0xcc,0xcc); /* gris claro */
    palette[17] = fb_color(0x88,0x88,0x88); /* gris medio */
    palette[18] = fb_color(0x44,0x44,0x44); /* gris oscuro */
    palette[19] = fb_color(0xff,0xaa,0xcc); /* rosa */
}

void paint_init(void){
    init_palette();
    fg_color = palette[0];
    bg_color = palette[1];
    canvas_clear();
}

static int abs2(int a){ return a<0?-a:a; }

/* Dibujar en el canvas (coordenadas relativas al canvas) */
static void canvas_put(int x,int y,uint32_t col,int size){
    int r=size/2;
    for(int dy=-r;dy<=r;dy++)
        for(int dx=-r;dx<=r;dx++){
            int cx=x+dx, cy=y+dy;
            if(cx>=0&&cx<CANVAS_W&&cy>=0&&cy<CANVAS_H)
                canvas[cy*CANVAS_W+cx]=col;
        }
    canvas_dirty=1;
}

static void canvas_line(int x0,int y0,int x1,int y1,uint32_t col,int size,uint32_t *buf){
    int dx=abs2(x1-x0),dy=abs2(y1-y0);
    int sx=x0<x1?1:-1,sy=y0<y1?1:-1,err=dx-dy;
    int r=size/2;
    while(1){
        if(x0>=0&&x0<CANVAS_W&&y0>=0&&y0<CANVAS_H)
            for(int ry=-r;ry<=r;ry++) for(int rx=-r;rx<=r;rx++){
                int nx=x0+rx,ny=y0+ry;
                if(nx>=0&&nx<CANVAS_W&&ny>=0&&ny<CANVAS_H) buf[ny*CANVAS_W+nx]=col;
            }
        if(x0==x1&&y0==y1) break;
        int e2=2*err;
        if(e2>-dy){err-=dy;x0+=sx;}
        if(e2< dx){err+=dx;y0+=sy;}
    }
}

static void canvas_rect_outline(int x0,int y0,int x1,int y1,uint32_t col,uint32_t *buf){
    if(x0>x1){int t=x0;x0=x1;x1=t;}
    if(y0>y1){int t=y0;y0=y1;y1=t;}
    for(int x=x0;x<=x1;x++){
        if(y0>=0&&y0<CANVAS_H&&x>=0&&x<CANVAS_W) buf[y0*CANVAS_W+x]=col;
        if(y1>=0&&y1<CANVAS_H&&x>=0&&x<CANVAS_W) buf[y1*CANVAS_W+x]=col;
    }
    for(int y=y0;y<=y1;y++){
        if(x0>=0&&x0<CANVAS_W&&y>=0&&y<CANVAS_H) buf[y*CANVAS_W+x0]=col;
        if(x1>=0&&x1<CANVAS_W&&y>=0&&y<CANVAS_H) buf[y*CANVAS_W+x1]=col;
    }
}

/* Flood fill */
static void canvas_fill(int x,int y,uint32_t newcol){
    if(x<0||x>=CANVAS_W||y<0||y>=CANVAS_H) return;
    uint32_t old=canvas[y*CANVAS_W+x];
    if(old==newcol) return;
    /* BFS simple con stack en arreglo */
    static int sx[CANVAS_W*CANVAS_H]; static int sy[CANVAS_W*CANVAS_H];
    int head=0,tail=0;
    sx[tail]=x; sy[tail]=y; tail++;
    while(head!=tail){
        int cx=sx[head],cy=sy[head]; head++;
        if(cx<0||cx>=CANVAS_W||cy<0||cy>=CANVAS_H) continue;
        if(canvas[cy*CANVAS_W+cx]!=old) continue;
        canvas[cy*CANVAS_W+cx]=newcol;
        if(tail<CANVAS_W*CANVAS_H-4){
            sx[tail]=cx+1;sy[tail]=cy;tail++;
            sx[tail]=cx-1;sy[tail]=cy;tail++;
            sx[tail]=cx;sy[tail]=cy+1;tail++;
            sx[tail]=cx;sy[tail]=cy-1;tail++;
        }
    }
    canvas_dirty=1;
}

/* Coordenadas canvas dentro de la ventana */
static void canvas_rect(int wx,int wy,int ww,int wh,int*cx,int*cy,int*cw,int*ch){
    *cx=wx+BORDER+2;
    *cy=wy+TITLEBAR_H+TOOL_H+2;
    *cw=ww-BORDER*2-4; if(*cw>CANVAS_W)*cw=CANVAS_W;
    *ch=wh-TITLEBAR_H-BORDER-TOOL_H-4; if(*ch>CANVAS_H)*ch=CANVAS_H;
}

void paint_draw(int wx,int wy,int ww,int wh){
    uint32_t toolbar_bg=fb_color(0xdd,0xdd,0xee);
    uint32_t sel_col   =fb_color(0x44,0x88,0xff);

    /* Toolbar */
    fb_fill_rect(wx+BORDER,wy+TITLEBAR_H,ww-BORDER*2,TOOL_H,toolbar_bg);

    /* Herramientas */
    const char *tools[]={"Lapiz","Borra","Linea","Rect","Relleno"};
    for(int i=0;i<5;i++){
        int tx=wx+BORDER+4+i*74;
        uint32_t tbg=(cur_tool==(tool_t)i)?sel_col:fb_color(0xbb,0xbb,0xcc);
        fb_fill_rect(tx,wy+TITLEBAR_H+4,70,28,tbg);
        fb_draw_rect(tx,wy+TITLEBAR_H+4,70,28,fb_color(0x88,0x88,0xaa));
        fb_draw_str(tx+4,wy+TITLEBAR_H+11,tools[i],
            (cur_tool==(tool_t)i)?fb_color(0xff,0xff,0xff):fb_color(0x22,0x22,0x44),tbg);
    }

    /* Color activo */
    int cx2=wx+BORDER+4+5*74;
    fb_fill_rect(cx2,wy+TITLEBAR_H+4,28,28,fg_color);
    fb_draw_rect(cx2,wy+TITLEBAR_H+4,28,28,fb_color(0,0,0));
    fb_fill_rect(cx2+14,wy+TITLEBAR_H+18,16,16,bg_color);
    fb_draw_rect(cx2+14,wy+TITLEBAR_H+18,16,16,fb_color(0,0,0));

    /* Tamaño pincel */
    int sz_x=cx2+38;
    fb_draw_str(sz_x,wy+TITLEBAR_H+5,"Tam:",fb_color(0x22,0x22,0x44),toolbar_bg);
    fb_fill_rect(sz_x,wy+TITLEBAR_H+16,14,14,fb_color(0xaa,0xaa,0xcc));
    fb_draw_str(sz_x+4,wy+TITLEBAR_H+19,"-",fb_color(0x22,0x22,0x44),fb_color(0xaa,0xaa,0xcc));
    fb_fill_circle(sz_x+28,wy+TITLEBAR_H+22,brush_size+1,fb_color(0x22,0x22,0x44));
    fb_fill_rect(sz_x+42,wy+TITLEBAR_H+16,14,14,fb_color(0xaa,0xaa,0xcc));
    fb_draw_str(sz_x+46,wy+TITLEBAR_H+19,"+",fb_color(0x22,0x22,0x44),fb_color(0xaa,0xaa,0xcc));

    /* Paleta */
    int pal_x=sz_x+62;
    for(int i=0;i<PAL_N;i++){
        int px2=pal_x+(i%10)*16;
        int py2=wy+TITLEBAR_H+4+(i/10)*14;
        fb_fill_rect(px2,py2,14,12,palette[i]);
        fb_draw_rect(px2,py2,14,12,fb_color(0x88,0x88,0x88));
    }

    /* Canvas */
    int canx,cany,canw,canh;
    canvas_rect(wx,wy,ww,wh,&canx,&cany,&canw,&canh);
    fb_fill_rect(canx,cany,canw,canh,fb_color(0xff,0xff,0xff));

    /* Dibujar canvas */
    uint32_t *src = (has_preview && (cur_tool==TOOL_LINE||cur_tool==TOOL_RECT)) ? preview : canvas;
    for(int r=0;r<canh;r++)
        for(int c=0;c<canw;c++)
            fb_put_pixel(canx+c,cany+r,src[r*CANVAS_W+c]);

    fb_draw_rect(canx-1,cany-1,canw+2,canh+2,fb_color(0x88,0x88,0xaa));
}

void paint_mouse(int wx,int wy,int ww,int wh,int mx,int my,int btn){
    int canx,cany,canw,canh;
    canvas_rect(wx,wy,ww,wh,&canx,&cany,&canw,&canh);

    int in_canvas=(mx>=canx&&mx<canx+canw&&my>=cany&&my<cany+canh);
    int px=mx-canx, py=my-cany;

    if(btn){
        if(in_canvas){
            if(!is_drawing){
                is_drawing=1;
                drag_start_x=px; drag_start_y=py;
            }
            switch(cur_tool){
            case TOOL_PENCIL:
                if(prev_mx>=0) canvas_line(prev_mx,prev_my,px,py,fg_color,brush_size,canvas);
                else canvas_put(px,py,fg_color,brush_size);
                prev_mx=px; prev_my=py;
                break;
            case TOOL_ERASER:
                if(prev_mx>=0) canvas_line(prev_mx,prev_my,px,py,bg_color,brush_size*3,canvas);
                else canvas_put(px,py,bg_color,brush_size*3);
                prev_mx=px; prev_my=py;
                break;
            case TOOL_LINE:
                /* preview */
                for(int i=0;i<CANVAS_W*CANVAS_H;i++) preview[i]=canvas[i];
                canvas_line(drag_start_x,drag_start_y,px,py,fg_color,brush_size,preview);
                has_preview=1;
                break;
            case TOOL_RECT:
                for(int i=0;i<CANVAS_W*CANVAS_H;i++) preview[i]=canvas[i];
                canvas_rect_outline(drag_start_x,drag_start_y,px,py,fg_color,preview);
                has_preview=1;
                break;
            case TOOL_FILL:
                canvas_fill(px,py,fg_color);
                break;
            }
        }
    } else {
        /* Botón soltado */
        if(is_drawing && has_preview){
            for(int i=0;i<CANVAS_W*CANVAS_H;i++) canvas[i]=preview[i];
            has_preview=0; canvas_dirty=1;
        }
        is_drawing=0; prev_mx=-1; prev_my=-1;
        has_preview=0;
    }
    (void)ww;(void)wh;
}

int paint_click(int wx,int wy,int ww,int wh,int mx,int my){
    /* Herramientas */
    for(int i=0;i<5;i++){
        int tx=wx+BORDER+4+i*74;
        if(mx>=tx&&mx<tx+70&&my>=wy+TITLEBAR_H+4&&my<wy+TITLEBAR_H+32){
            cur_tool=(tool_t)i; return 0;
        }
    }
    /* Tamaño */
    int cx2=wx+BORDER+4+5*74;
    int sz_x=cx2+38;
    if(mx>=sz_x&&mx<sz_x+14&&my>=wy+TITLEBAR_H+16&&my<wy+TITLEBAR_H+30){
        if(brush_size>1) brush_size--;
    }
    if(mx>=sz_x+42&&mx<sz_x+56&&my>=wy+TITLEBAR_H+16&&my<wy+TITLEBAR_H+30){
        if(brush_size<12) brush_size++;
    }
    /* Paleta */
    int pal_x=sz_x+62;
    for(int i=0;i<PAL_N;i++){
        int px=pal_x+(i%10)*16;
        int py=wy+TITLEBAR_H+4+(i/10)*14;
        if(mx>=px&&mx<px+14&&my>=py&&my<py+12){
            fg_color=palette[i]; return 0;
        }
    }
    (void)ww;(void)wh;
    return 0;
}
