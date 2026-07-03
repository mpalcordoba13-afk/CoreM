#include "pciviewer.h"
#include "gui.h"
#include "framebuffer.h"
#include "pci.h"
#include "usb.h"
#include <stdint.h>

static void itohex(uint16_t n, char *buf) {
    const char *h="0123456789ABCDEF";
    buf[0]='0';buf[1]='x';
    buf[2]=h[(n>>12)&0xF]; buf[3]=h[(n>>8)&0xF];
    buf[4]=h[(n>>4)&0xF];  buf[5]=h[n&0xF];
    buf[6]='\0';
}

void pciviewer_draw(int wx,int wy,int ww,int wh) {
    int ox=wx+BORDER+6, oy=wy+TITLEBAR_H+8;
    uint32_t bg=fb_color(0xf2,0xf2,0xf2);
    uint32_t hdr=fb_color(0x0f,0x34,0x60);
    uint32_t fg=fb_color(0x11,0x11,0x22);
    uint32_t alt=fb_color(0xe8,0xe8,0xf4);

    fb_draw_str(ox,oy,"Dispositivos PCI detectados:",hdr,bg); oy+=18;

    int n=pci_count();
    if(n==0){
        fb_draw_str(ox,oy,"(ninguno detectado)",fg,bg);
        return;
    }

    int row_h=20;
    int max_rows=(wh-TITLEBAR_H-30)/row_h;

    for(int i=0;i<n&&i<max_rows;i++){
        const pci_device_t *d=pci_get(i);
        uint32_t rowbg=(i%2==0)?bg:alt;
        int by=oy+i*row_h;
        fb_fill_rect(ox,by,ww-BORDER*2-12,row_h-2,rowbg);

        char vid[8],did[8];
        itohex(d->vendor_id,vid);
        itohex(d->device_id,did);

        fb_draw_str(ox+2,   by+5,d->vendor_name,hdr,rowbg);
        fb_draw_str(ox+90,  by+5,d->device_desc,fg,rowbg);
        fb_draw_str(ox+ww-BORDER*2-100,by+5,vid,fb_color(0x44,0x44,0x88),rowbg);
        fb_draw_str(ox+ww-BORDER*2-52, by+5,did,fb_color(0x44,0x44,0x88),rowbg);
    }
    if(n>max_rows){
        fb_draw_str(ox,oy+max_rows*row_h,"... y mas",fg,bg);
    }

    /* ---- Seccion USB (controlador UHCI + dispositivos enumerados) ---- */
    int uy = oy + (n<max_rows?n:max_rows)*row_h + 14;
    fb_draw_str(ox,uy,"USB (UHCI):",hdr,bg); uy+=18;

    if(!usb_controller_present()){
        fb_draw_str(ox,uy,"(sin controlador UHCI)",fg,bg);
        return;
    }

    int ucount = usb_device_count();
    if(ucount==0){
        fb_draw_str(ox,uy,"Controlador OK, sin dispositivos",fg,bg);
        return;
    }

    for(int i=0;i<ucount;i++){
        const usb_device_t *u = usb_get_device(i);
        if(!u) continue;
        char vid[8],pid[8];
        itohex(u->vendor_id,vid);
        itohex(u->product_id,pid);

        uint32_t rowbg=(i%2==0)?bg:alt;
        int by=uy+i*row_h;
        fb_fill_rect(ox,by,ww-BORDER*2-12,row_h-2,rowbg);

        const char *cls = "Dispositivo";
        if(u->dev_class==0x08) cls="Almacenamiento";
        else if(u->dev_class==0x03) cls="HID";
        else if(u->dev_class==0x09) cls="Hub";
        else if(u->dev_class==0x00) cls="(por interfaz)";

        fb_draw_str(ox+2,  by+5,cls,hdr,rowbg);
        fb_draw_str(ox+ww-BORDER*2-100,by+5,vid,fb_color(0x44,0x44,0x88),rowbg);
        fb_draw_str(ox+ww-BORDER*2-52, by+5,pid,fb_color(0x44,0x44,0x88),rowbg);
    }
}
