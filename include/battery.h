#ifndef BATTERY_H
#define BATTERY_H
#include <stdint.h>

typedef enum {
    BAT_NOT_PRESENT = 0,
    BAT_DISCHARGING,
    BAT_CHARGING,
    BAT_FULL,
} bat_status_t;

typedef struct {
    bat_status_t status;
    int percent;      /* 0-100, -1 si no hay batería */
    int present;
} bat_info_t;

void battery_read(bat_info_t *info);

#endif
