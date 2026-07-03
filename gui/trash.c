#include "trash.h"
#include "gui.h"
#include "framebuffer.h"
#include "fs.h"
#include <stdint.h>

typedef struct {
    char name[FS_NAME_LEN];
    char data[FS_DATA_LEN];
    int  size;
    int  used;
} trash_item_t;

static trash_item_t items[TRASH_MAX];

static void scpy(char *d,const char *s,int max){int i=0;while(s[i]&&i<max-1){d[i]=s[i];i++;}d[i]='\0';}

void trash_init(void){
    for(int i=0;i<TRASH_MAX;i++) items[i].used=0;
}

int trash_add(const char *name, const char *data, int len){
    for(int i=0;i<TRASH_MAX;i++){
        if(!items[i].used){
            scpy(items[i].name,name,FS_NAME_LEN);
            int l = len>FS_DATA_LEN-1 ? FS_DATA_LEN-1 : len;
            for(int k=0;k<l;k++) items[i].data[k]=data[k];
            items[i].data[l]='\0';
            items[i].size=l;
            items[i].used=1;
            return i;
        }
    }
    return -1;
}

int trash_get_entry(int idx, char *name_out, int *size_out){
    int c=0;
    for(int i=0;i<TRASH_MAX;i++){
        if(items[i].used){
            if(c==idx){ scpy(name_out,items[i].name,FS_NAME_LEN); *size_out=items[i].size; return 1; }
            c++;
        }
    }
    return 0;
}

int trash_restore(int idx){
    int c=0;
    for(int i=0;i<TRASH_MAX;i++){
        if(items[i].used){
            if(c==idx){
                fs_write(items[i].name, items[i].data, items[i].size);
                items[i].used=0;
                return 1;
            }
            c++;
        }
    }
    return 0;
}

int trash_empty(void){
    int n=0;
    for(int i=0;i<TRASH_MAX;i++) if(items[i].used){ items[i].used=0; n++; }
    return n;
}

void trash_draw(int wx,int wy,int ww,int wh){
    (void)wh;
    int ox=wx+BORDER+10, oy=wy+TITLEBAR_H+10;
    uint32_t bg=fb_color(0xf2,0xf2,0xf2), fg=fb_color(0x11,0x11,0x22);
    fb_draw_str(ox,oy,"Papelera de reciclaje",fb_color(0x0f,0x34,0x60),bg); oy+=24;

    char name[FS_NAME_LEN]; int size; int k=0;
    for(;;k++){
        if(!trash_get_entry(k,name,&size)) break;
        int by=oy+k*26;
        fb_fill_rect(ox,by,ww-BORDER*2-20,22,fb_color(0xe4,0xe4,0xf2));
        fb_draw_str(ox+8,by+6,name,fg,fb_color(0xe4,0xe4,0xf2));
        fb_fill_rect(ox+ww-BORDER*2-100,by+2,40,18,fb_color(0x20,0x80,0x40));
        fb_draw_str(ox+ww-BORDER*2-95,by+6,"Rest",fb_color(0xff,0xff,0xff),fb_color(0x20,0x80,0x40));
    }
    if(k==0) fb_draw_str(ox,oy,"(papelera vacia)",fg,bg);

    int by=oy+k*26+12;
    fb_fill_rect(ox,by,140,26,fb_color(0xcc,0x33,0x33));
    fb_draw_str(ox+12,by+8,"Vaciar papelera",fb_color(0xff,0xff,0xff),fb_color(0xcc,0x33,0x33));
}

int trash_click(int wx,int wy,int ww,int wh,int mx,int my){
    (void)wh;
    int ox=wx+BORDER+10, oy=wy+TITLEBAR_H+10+24;
    char name[FS_NAME_LEN]; int size; int k=0;
    for(;;k++){
        if(!trash_get_entry(k,name,&size)) break;
        int by=oy+k*26;
        int bx=ox+ww-BORDER*2-100;
        if(mx>=bx&&mx<bx+40&&my>=by+2&&my<by+20){ trash_restore(k); return 1; }
    }
    int by=oy+k*26+12;
    if(mx>=ox&&mx<ox+140&&my>=by&&my<by+26){ trash_empty(); return 1; }
    return 0;
}
