#ifndef RTC_H
#define RTC_H
#include <stdint.h>

typedef struct { uint8_t sec, min, hour, day, month; uint16_t year; } rtc_time_t;

void rtc_read(rtc_time_t *t);
void rtc_write(rtc_time_t *t);
void rtc_set_time(uint8_t hour, uint8_t min, uint8_t sec);
void rtc_set_date(uint8_t day, uint8_t month, uint16_t year);
void rtc_get_str(char *buf);                  /* "HH:MM:SS" 24h */
void rtc_get_str_fmt(char *buf, int use12h);  /* 24h o 12h AM/PM */

#endif
