#include "battery.h"
#include <stdint.h>

static inline void outb(uint16_t p, uint8_t v) {
    __asm__ volatile ("outb %0,%1" : : "a"(v), "Nd"(p));
}
static inline uint8_t inb(uint16_t p) {
    uint8_t v; __asm__ volatile ("inb %1,%0" : "=a"(v) : "Nd"(p)); return v;
}

/* APM BIOS via puertos EC (Embedded Controller) */
#define EC_DATA   0x62
#define EC_CMD    0x66
#define EC_STATUS 0x66

/* Intentar leer via ACPI EC - simplificado */
static int ec_wait_ibf(void) {
    int timeout = 10000;
    while ((inb(EC_STATUS) & 0x02) && timeout-- > 0);
    return timeout > 0;
}
static int ec_wait_obf(void) {
    int timeout = 10000;
    while (!(inb(EC_STATUS) & 0x01) && timeout-- > 0);
    return timeout > 0;
}
static int ec_read(uint8_t addr, uint8_t *val) {
    if (!ec_wait_ibf()) return 0;
    outb(EC_CMD, 0x80); /* READ command */
    if (!ec_wait_ibf()) return 0;
    outb(EC_DATA, addr);
    if (!ec_wait_obf()) return 0;
    *val = inb(EC_DATA);
    return 1;
}

void battery_read(bat_info_t *info) {
    info->present = 0;
    info->percent = -1;
    info->status  = BAT_NOT_PRESENT;

    /* Intentar leer EC en addr comunes de batería */
    /* addr 0x92 = estado en muchos laptops (Lenovo, HP, etc) */
    uint8_t stat = 0, pct = 0;
    int ok_stat = ec_read(0x92, &stat);
    int ok_pct  = ec_read(0x93, &pct);

    if (ok_stat && ok_pct && pct > 0 && pct <= 100) {
        info->present = 1;
        info->percent = pct;
        if (stat & 0x02)      info->status = BAT_CHARGING;
        else if (stat & 0x01) info->status = BAT_DISCHARGING;
        else                  info->status = BAT_FULL;
    }
    /* Si falla EC, intentar via CMOS/APM flag simple */
    else {
        /* No hay batería o no se puede leer (QEMU, PC de escritorio) */
        info->present  = 0;
        info->percent  = -1;
        info->status   = BAT_NOT_PRESENT;
    }
}
