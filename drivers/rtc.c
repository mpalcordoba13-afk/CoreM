#include "rtc.h"
#include <stdint.h>

static inline void outb(uint16_t p, uint8_t v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(p));
}
static inline uint8_t rtc_reg(uint8_t reg) {
    outb(0x70, reg);
    uint8_t val;
    __asm__ volatile ("inb $0x71, %0" : "=a"(val));
    return val;
}
static void rtc_write_reg(uint8_t reg, uint8_t val) {
    outb(0x70, reg);
    outb(0x71, val);
}

static uint8_t bcd2bin(uint8_t v) { return (v>>4)*10 + (v&0xF); }
static uint8_t bin2bcd(uint8_t v) { return ((v/10)<<4) | (v%10); }

static void u2s(char *s, uint8_t v) {
    s[0] = '0' + v/10;
    s[1] = '0' + v%10;
}

void rtc_read(rtc_time_t *t) {
    while (rtc_reg(0x0A) & 0x80);
    t->sec   = bcd2bin(rtc_reg(0x00));
    t->min   = bcd2bin(rtc_reg(0x02));
    t->hour  = bcd2bin(rtc_reg(0x04));
    t->day   = bcd2bin(rtc_reg(0x07));
    t->month = bcd2bin(rtc_reg(0x08));
    t->year  = bcd2bin(rtc_reg(0x09)) + 2000;
}

void rtc_write(rtc_time_t *t) {
    uint8_t statusB = rtc_reg(0x0B);
    rtc_write_reg(0x0B, statusB | 0x80); /* halt updates */
    rtc_write_reg(0x00, bin2bcd(t->sec));
    rtc_write_reg(0x02, bin2bcd(t->min));
    rtc_write_reg(0x04, bin2bcd(t->hour));
    rtc_write_reg(0x07, bin2bcd(t->day));
    rtc_write_reg(0x08, bin2bcd(t->month));
    rtc_write_reg(0x09, bin2bcd((uint8_t)(t->year - 2000)));
    rtc_write_reg(0x0B, statusB & (uint8_t)~0x80);
}

void rtc_set_time(uint8_t hour, uint8_t min, uint8_t sec) {
    rtc_time_t t;
    rtc_read(&t);
    t.hour = hour % 24;
    t.min  = min % 60;
    t.sec  = sec % 60;
    rtc_write(&t);
}

void rtc_set_date(uint8_t day, uint8_t month, uint16_t year) {
    rtc_time_t t;
    rtc_read(&t);
    t.day   = day;
    t.month = month;
    t.year  = year;
    rtc_write(&t);
}

void rtc_get_str(char *buf) {
    rtc_time_t t; rtc_read(&t);
    u2s(buf+0, t.hour); buf[2]=':';
    u2s(buf+3, t.min);  buf[5]=':';
    u2s(buf+6, t.sec);  buf[8]='\0';
}

void rtc_get_str_fmt(char *buf, int use12h) {
    rtc_time_t t; rtc_read(&t);
    uint8_t h = t.hour;
    const char *suf = "AM";
    if (use12h) {
        suf = (h>=12) ? "PM" : "AM";
        h = h % 12;
        if (h==0) h=12;
    }
    u2s(buf+0, h);     buf[2]=':';
    u2s(buf+3, t.min); buf[5]=':';
    u2s(buf+6, t.sec);
    if (use12h) {
        buf[8]=' '; buf[9]=suf[0]; buf[10]=suf[1]; buf[11]='\0';
    } else {
        buf[8]='\0';
    }
}
