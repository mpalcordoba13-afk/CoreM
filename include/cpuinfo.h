#ifndef CPUINFO_H
#define CPUINFO_H
#include <stdint.h>

typedef struct {
    char vendor[13];      /* "GenuineIntel" / "AuthenticAMD" */
    char brand[49];       /* "Intel(R) Core(TM) i5..." */
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    uint32_t features;    /* flags EDX de CPUID 1 */
    int has_fpu;
    int has_mmx;
    int has_sse;
    int has_sse2;
    int has_apic;
} cpuinfo_t;

void cpuinfo_read(cpuinfo_t *info);

#endif
