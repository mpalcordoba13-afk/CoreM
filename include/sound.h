#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>

void sound_beep(uint32_t freq, uint32_t ms);
void sound_stop(void);
void sound_startup(void);
void sound_click(void);
void sound_close_win(void);

#endif
