#include "meminfo.h"
#include <stdint.h>

static uint32_t mem_lower = 0;
static uint32_t mem_upper = 0;

void meminfo_set(uint32_t lower_kb, uint32_t upper_kb) {
    mem_lower = lower_kb;
    mem_upper = upper_kb;
}

uint32_t meminfo_lower_kb(void) { return mem_lower; }
uint32_t meminfo_upper_kb(void) { return mem_upper; }
uint32_t meminfo_total_mb(void) { return (mem_lower + mem_upper) / 1024; }
