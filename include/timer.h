#ifndef TIMER_H
#define TIMER_H
#include <stdint.h>

void timer_init(void);
uint32_t timer_ticks(void);
uint32_t timer_seconds(void);
void timer_sleep(uint32_t ms);

#endif
