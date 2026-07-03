#include "sound.h"
#include "settings.h"
#include <stdint.h>

static inline void outb(uint16_t p, uint8_t v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static inline uint8_t inb(uint16_t p){uint8_t v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}

static void pit_set_freq(uint32_t freq) {
    uint32_t div = 1193180 / freq;
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(div & 0xFF));
    outb(0x42, (uint8_t)(div >> 8));
}
static void speaker_on(void)  { outb(0x61, inb(0x61) |  3); }
static void speaker_off(void) { outb(0x61, inb(0x61) & ~3); }

static void delay_ms(uint32_t ms) {
    for (volatile uint32_t i = 0; i < ms * 4000; i++);
}

void sound_stop(void) { speaker_off(); }

void sound_beep(uint32_t freq, uint32_t ms) {
    if (!g_sound_on) return;
    pit_set_freq(freq);
    speaker_on();
    delay_ms(ms);
    speaker_off();
}

void sound_startup(void) {
    sound_beep(523, 80);  /* C5 */
    delay_ms(20);
    sound_beep(659, 80);  /* E5 */
    delay_ms(20);
    sound_beep(784, 120); /* G5 */
}

void sound_click(void)     { sound_beep(1200, 15); }
void sound_close_win(void) { sound_beep(400, 60);  }
