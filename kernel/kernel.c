#include "bochsvbe.h"
#include "framebuffer.h"
#include "gdt.h"
#include "idt.h"
#include "timer.h"
#include "keyboard.h"
#include "mouse.h"
#include "sound.h"
#include "fs.h"
#include "users.h"
#include "meminfo.h"
#include "pci.h"
#include "usb.h"
#include "usb_printer.h"
#include "usb_msd.h"
#include "usb_ehci.h"
#include "ndis.h"
#include "rtl8139.h"
#include "rtl8169.h"
#include "net.h"
#include "dhcp.h"
#include "bootscreen.h"
#include "login.h"
#include "gui.h"
#include <stdint.h>

#define SCREEN_W 1280
#define SCREEN_H 720

/* Estructura Multiboot simplificada */
typedef struct {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint8_t  pad[116];
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
} __attribute__((packed)) mb_info_t;

void kernel_main(uint32_t magic, uint32_t mb_addr) {
    (void)magic;

    gdt_init();
    idt_init();
    __asm__ volatile ("sti");

    timer_init();
    keyboard_init();
    mouse_init();

    /* Leer info de memoria de Multiboot */
    if (mb_addr) {
        mb_info_t *mb = (mb_info_t*)mb_addr;
        if (mb->flags & 0x01) {
            meminfo_set(mb->mem_lower, mb->mem_upper);
        }
    }

    uint32_t *fb = bvbe_init(SCREEN_W, SCREEN_H);
    fb_init(fb, SCREEN_W, SCREEN_H, SCREEN_W * 4);

    /* Escanear PCI antes del boot screen */
    pci_scan();
    usb_init();
    usb_printer_init();
    usb_ehci_init();   /* USB 2.0 high-speed (EHCI) */
    usb_msd_init();    /* Mass Storage (usa UHCI o EHCI según corresponda) */
    /* Registrar miniports NDIS en orden de prioridad */
    ndis_register(&rtl8139_miniport); /* QEMU default / PCs muy viejas */
    ndis_register(&rtl8169_miniport); /* PCs 2004-2020, el mas comun    */
    ndis_init();
    if (ndis_present()) dhcp_request(4000);

    fs_init();
    users_init();

    bootscreen_show();
    login_run();
    keyboard_flush();

    gui_init();
    gui_run();

    __asm__ volatile ("cli; hlt");
}
