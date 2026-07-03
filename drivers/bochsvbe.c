#include "bochsvbe.h"
#include <stdint.h>

#define VBE_DISPI_IOPORT_INDEX  0x01CE
#define VBE_DISPI_IOPORT_DATA   0x01CF

#define VBE_DISPI_INDEX_XRES        1
#define VBE_DISPI_INDEX_YRES        2
#define VBE_DISPI_INDEX_BPP         3
#define VBE_DISPI_INDEX_ENABLE      4
#define VBE_DISPI_INDEX_VIRT_WIDTH  6
#define VBE_DISPI_INDEX_VIRT_HEIGHT 7
#define VBE_DISPI_INDEX_X_OFFSET    8
#define VBE_DISPI_INDEX_Y_OFFSET    9

#define VBE_DISPI_DISABLED    0x00
#define VBE_DISPI_ENABLED     0x01
#define VBE_DISPI_LFB_ENABLED 0x40

#define VBE_LFB_ADDR 0xFD000000

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t val;
    __asm__ volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static void vbe_write(uint16_t index, uint16_t value) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    outw(VBE_DISPI_IOPORT_DATA,  value);
}

static uint16_t vbe_read(uint16_t index) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    return inw(VBE_DISPI_IOPORT_DATA);
}

uint32_t* bvbe_init(uint16_t width, uint16_t height) {
    (void)vbe_read; /* suprimir warning de no usado */

    vbe_write(VBE_DISPI_INDEX_ENABLE,      VBE_DISPI_DISABLED);
    vbe_write(VBE_DISPI_INDEX_XRES,        width);
    vbe_write(VBE_DISPI_INDEX_YRES,        height);
    vbe_write(VBE_DISPI_INDEX_BPP,         32);
    vbe_write(VBE_DISPI_INDEX_VIRT_WIDTH,  width);
    vbe_write(VBE_DISPI_INDEX_VIRT_HEIGHT, height);
    vbe_write(VBE_DISPI_INDEX_X_OFFSET,    0);
    vbe_write(VBE_DISPI_INDEX_Y_OFFSET,    0);
    vbe_write(VBE_DISPI_INDEX_ENABLE,      VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

    return (uint32_t*)VBE_LFB_ADDR;
}
