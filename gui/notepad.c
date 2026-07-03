#include "notepad.h"
#include "gui.h"
#include "framebuffer.h"
#include "fs.h"
#include <stdint.h>

#define NOTE_MAX 1023

static char buf[NOTE_MAX+1];
static int  len = 0;
static char filename[FS_NAME_LEN] = "notas.txt";
static int  saved_flash = 0;

static void scpy(char *d,const char *s,int max){int i=0;while(s[i]&&i<max-1){d[i]=s[i];i++;}d[i]='\0';}

void notepad_init(void) {
    int r = fs_read(filename,buf,sizeof(buf));
    len = (r<0) ? 0 : r;
    if (r<0) buf[0]='\0';
}

void notepad_load(const char *name) {
    scpy(filename,name,FS_NAME_LEN);
    int r = fs_read(filename,buf,sizeof(buf));
    len = (r<0) ? 0 : r;
    if (r<0) buf[0]='\0';
    saved_flash = 0;
}

void notepad_putchar(int c) {
    saved_flash = 0;
    if (c=='\b') { if (len>0) { len--; buf[len]='\0'; } }
    else if (c=='\n'||c=='\r') { if (len<NOTE_MAX) { buf[len++]='\n'; buf[len]='\0'; } }
    else if (c>=32 && c<127 && len<NOTE_MAX) { buf[len++]=c; buf[len]='\0'; }
}

void notepad_draw(int wx,int wy,int ww,int wh) {
    int ox=wx+BORDER+8, oy=wy+TITLEBAR_H+8;
    int aw=ww-BORDER*2-16, ah=wh-TITLEBAR_H-BORDER-44;

    fb_fill_rect(ox,oy,aw,ah,fb_color(0xff,0xff,0xff));
    fb_draw_rect(ox,oy,aw,ah,fb_color(0x88,0x88,0xaa));

    int max_cols = (aw-8)/9;
    int cx=ox+4, cy=oy+4;
    for (int i=0;i<len;i++) {
        char ch=buf[i];
        if (ch=='\n') { cx=ox+4; cy+=11; continue; }
        if (cy <= oy+ah-10)
            fb_draw_char(cx,cy,ch,fb_color(0x11,0x11,0x22),fb_color(0xff,0xff,0xff));
        cx+=9;
        if (cx > ox+4+max_cols*9) { cx=ox+4; cy+=11; }
    }
    if (cy <= oy+ah-10) fb_fill_rect(cx,cy,2,10,fb_color(0x33,0x99,0xff));

    fb_draw_str(ox,oy+ah+8,"Archivo:",fb_color(0x33,0x33,0x55),fb_color(0xf2,0xf2,0xf2));
    fb_draw_str(ox+68,oy+ah+8,filename,fb_color(0x11,0x11,0x22),fb_color(0xf2,0xf2,0xf2));

    uint32_t btncol = saved_flash ? fb_color(0x33,0xcc,0x66) : fb_color(0x20,0x80,0x40);
    fb_fill_rect(ox+aw-90,oy+ah+2,90,24,btncol);
    fb_draw_str(ox+aw-78,oy+ah+9, saved_flash?"Guardado":"Guardar",fb_color(0xff,0xff,0xff),btncol);
}

int notepad_click(int wx,int wy,int ww,int wh,int mx,int my) {
    int ox=wx+BORDER+8, oy=wy+TITLEBAR_H+8;
    int aw=ww-BORDER*2-16, ah=wh-TITLEBAR_H-BORDER-44;
    int bx=ox+aw-90, by=oy+ah+2;
    if (mx>=bx&&mx<bx+90&&my>=by&&my<by+24) {
        fs_write(filename,buf,len);
        saved_flash = 1;
        return 1;
    }
    return 0;
}

const char* notepad_filename(void) { return filename; }

void notepad_set_content(const char *text){
    int i=0;
    while(text[i]&&i<NOTE_MAX){ buf[i]=text[i]; i++; }
    buf[i]='\0';
    len=i;
    saved_flash=0;
}
