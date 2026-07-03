#include "timer.h"
#include "idt.h"
#include <stdint.h>

static volatile uint32_t ticks = 0;

static inline void outb(uint16_t p, uint8_t v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(p));
}

static void timer_handler(registers_t *r) {
    (void)r;
    ticks++;
}

/* PIT a 100 Hz (IRQ0 = interrupcion 32) */
void timer_init(void) {
    uint32_t divisor = 1193182 / 100;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
    idt_set_handler(32, timer_handler);
}

uint32_t timer_ticks(void)   { return ticks; }
uint32_t timer_seconds(void) { return ticks / 100; }

void timer_sleep(uint32_t ms) {
    uint32_t end = timer_ticks() + (ms / 10);
    while (timer_ticks() < end) {
        /* busy wait until timer advances */
    }
}
