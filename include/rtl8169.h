#ifndef RTL8169_H
#define RTL8169_H
#include <stdint.h>
#include "ndis.h"

int  rtl8169_init(void);
int  rtl8169_present(void);
void rtl8169_get_mac(uint8_t out_mac[6]);
int  rtl8169_send(const uint8_t *frame, int len);
int  rtl8169_poll_recv(uint8_t *buf, int maxlen);

extern const ndis_miniport_t rtl8169_miniport;
#endif
