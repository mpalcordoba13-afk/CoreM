#ifndef RTL8139_H
#define RTL8139_H
#include <stdint.h>
#include "ndis.h"

int  rtl8139_init(void);
int  rtl8139_present(void);
void rtl8139_get_mac(uint8_t out_mac[6]);
int  rtl8139_send(const uint8_t *frame, int len);
int  rtl8139_poll_recv(uint8_t *buf, int maxlen);

extern const ndis_miniport_t rtl8139_miniport;
#endif
