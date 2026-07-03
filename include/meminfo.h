#ifndef MEMINFO_H
#define MEMINFO_H
#include <stdint.h>

/* Llenado desde multiboot al arrancar */
void meminfo_set(uint32_t lower_kb, uint32_t upper_kb);
uint32_t meminfo_total_mb(void);
uint32_t meminfo_lower_kb(void);
uint32_t meminfo_upper_kb(void);

#endif
