#include "mouse.h"
#include "idt.h"
#include <stdint.h>

#define MOUSE_DATA 0x60
#define MOUSE_CMD  0x64
#define SCREEN_W   1280
#define SCREEN_H   720

static int mouse_x = 640;
static int mouse_y = 360;
static int mouse_btn_l = 0;
static int mouse_btn_r = 0;

static uint8_t packet[4];
static int     packet_idx = 0;
static volatile uint32_t pkt_count = 0;
static int     mouse_packet_size = 3;
static volatile int mouse_wheel = 0;

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static void mouse_wait_write(void) { int t=100000; while (t-- && (inb(MOUSE_CMD)&0x02)); }
static void mouse_wait_read(void)  { int t=100000; while (t-- && !(inb(MOUSE_CMD)&0x01)); }
static void mouse_write(uint8_t val) {
    mouse_wait_write(); outb(MOUSE_CMD, 0xD4);
    mouse_wait_write(); outb(MOUSE_DATA, val);
}
static uint8_t mouse_read(void) { mouse_wait_read(); return inb(MOUSE_DATA); }

static void mouse_irq_handler(registers_t *regs) {
    (void)regs;
    uint8_t data = inb(MOUSE_DATA);
    packet[packet_idx++] = data;
    if (packet_idx < mouse_packet_size) return;
    packet_idx = 0;

    uint8_t flags = packet[0];
    if (!(flags & 0x08)) return;

    int8_t dx = (int8_t)packet[1];
    int8_t dy = (int8_t)packet[2];
    int8_t wheel = 0;
    if (mouse_packet_size == 4) wheel = (int8_t)packet[3];

    mouse_btn_l = (flags & 0x01) ? 1 : 0;
    mouse_btn_r = (flags & 0x02) ? 1 : 0;

    mouse_x += dx;
    mouse_y -= dy;
    if (wheel) mouse_wheel += wheel;
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= SCREEN_W) mouse_x = SCREEN_W - 1;
    if (mouse_y >= SCREEN_H) mouse_y = SCREEN_H - 1;

    pkt_count++;
}

void mouse_init(void) {
    mouse_wait_write(); outb(MOUSE_CMD, 0xA8);

    mouse_wait_write(); outb(MOUSE_CMD, 0x20);
    mouse_wait_read();
    uint8_t cfg = inb(MOUSE_DATA);
    cfg |= 0x02;
    cfg &= ~0x20;
    mouse_wait_write(); outb(MOUSE_CMD, 0x60);
    mouse_wait_write(); outb(MOUSE_DATA, cfg);

    mouse_write(0xFF);
    mouse_read(); mouse_read(); mouse_read();

    mouse_write(0xF6); mouse_read();

    mouse_write(0xE8); mouse_read();
    mouse_write(0x03); mouse_read();

     /* Try to enable Intellimouse (wheel) by setting sample rates 200,100,80
         then asking for device ID. If device ID==3 we have a wheel (4-byte packets). */
     mouse_write(0xF3); mouse_read(); mouse_write(200); mouse_read();
     mouse_write(0xF3); mouse_read(); mouse_write(100); mouse_read();
     mouse_write(0xF3); mouse_read(); mouse_write(80); mouse_read();
     /* Request device id */
     mouse_write(0xF2); mouse_read();
     uint8_t devid = mouse_read();
     if (devid == 0x03) mouse_packet_size = 4; else mouse_packet_size = 3;

    mouse_write(0xF4); mouse_read();

    idt_set_handler(44, mouse_irq_handler);
}

void mouse_get_pos(int *x, int *y) { *x = mouse_x; *y = mouse_y; }
int mouse_left_pressed(void)  { return mouse_btn_l; }
int mouse_right_pressed(void) { return mouse_btn_r; }
uint32_t mouse_packet_count(void) { return pkt_count; }

int mouse_get_wheel(void){ int v = mouse_wheel; mouse_wheel = 0; return v; }
