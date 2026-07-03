/*
 * nic.c - Capa de abstraccion de NIC.
 *
 * Prueba chips en orden de probabilidad:
 *   1. RTL8139 (QEMU default, PCs muy viejas <2004)
 *   2. RTL8169/8168/8111 (PCs 2004-2020, el mas comun en hardware real)
 *
 * Expone una API uniforme que usa el resto del sistema.
 */
#include "nic.h"
#include "rtl8139.h"
#include "rtl8169.h"
#include <stdint.h>

#define NIC_NONE   0
#define NIC_8139   1
#define NIC_8169   2

static int active_nic = NIC_NONE;

int nic_init(void){
    active_nic = NIC_NONE;
    if(rtl8139_init()){ active_nic = NIC_8139; return 1; }
    if(rtl8169_init()){ active_nic = NIC_8169; return 1; }
    return 0;
}

int nic_present(void){ return active_nic != NIC_NONE; }

const char* nic_name(void){
    if(active_nic == NIC_8139) return "RTL8139";
    if(active_nic == NIC_8169) return "RTL8169/8168";
    return "Sin NIC";
}

void nic_get_mac(uint8_t out_mac[6]){
    if(active_nic == NIC_8139) rtl8139_get_mac(out_mac);
    else if(active_nic == NIC_8169) rtl8169_get_mac(out_mac);
    else for(int i=0;i<6;i++) out_mac[i]=0;
}

int nic_send(const uint8_t *frame, int len){
    if(active_nic == NIC_8139) return rtl8139_send(frame, len);
    if(active_nic == NIC_8169) return rtl8169_send(frame, len);
    return -1;
}

int nic_poll_recv(uint8_t *buf, int maxlen){
    if(active_nic == NIC_8139) return rtl8139_poll_recv(buf, maxlen);
    if(active_nic == NIC_8169) return rtl8169_poll_recv(buf, maxlen);
    return 0;
}
