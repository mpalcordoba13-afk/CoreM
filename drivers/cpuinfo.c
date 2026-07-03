#include "cpuinfo.h"
#include <stdint.h>

static void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                  uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile (
        "cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf)
    );
}

static void copy4(char *dst, uint32_t val) {
    dst[0] = val & 0xFF;
    dst[1] = (val >> 8) & 0xFF;
    dst[2] = (val >> 16) & 0xFF;
    dst[3] = (val >> 24) & 0xFF;
}

void cpuinfo_read(cpuinfo_t *info) {
    uint32_t eax, ebx, ecx, edx;

    /* Vendor string */
    cpuid(0, &eax, &ebx, &ecx, &edx);
    copy4(info->vendor + 0, ebx);
    copy4(info->vendor + 4, edx);
    copy4(info->vendor + 8, ecx);
    info->vendor[12] = '\0';

    /* Family/model/stepping + features */
    cpuid(1, &eax, &ebx, &ecx, &edx);
    info->stepping = eax & 0xF;
    info->model    = (eax >> 4) & 0xF;
    info->family   = (eax >> 8) & 0xF;
    info->features = edx;
    info->has_fpu  = (edx >> 0) & 1;
    info->has_apic = (edx >> 9) & 1;
    info->has_mmx  = (edx >> 23) & 1;
    info->has_sse  = (edx >> 25) & 1;
    info->has_sse2 = (edx >> 26) & 1;

    /* Brand string (hojas 0x80000002-4) */
    uint32_t max_ext;
    cpuid(0x80000000, &max_ext, &ebx, &ecx, &edx);

    if (max_ext >= 0x80000004) {
        uint32_t *p = (uint32_t*)info->brand;
        cpuid(0x80000002, &p[0], &p[1], &p[2],  &p[3]);
        cpuid(0x80000003, &p[4], &p[5], &p[6],  &p[7]);
        cpuid(0x80000004, &p[8], &p[9], &p[10], &p[11]);
        info->brand[48] = '\0';
        /* Recortar espacios al inicio */
        char *b = info->brand;
        int start = 0;
        while (b[start] == ' ') start++;
        if (start > 0) {
            int i = 0;
            while (b[start + i]) { b[i] = b[start + i]; i++; }
            b[i] = '\0';
        }
    } else {
        info->brand[0] = '?'; info->brand[1] = '\0';
    }
}
