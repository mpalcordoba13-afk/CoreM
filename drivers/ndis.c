/*
 * ndis.c - NDIS Core para MyOS.
 *
 * Mantiene el registro de miniports y despacha las llamadas al
 * adaptador activo. Sin malloc: tabla estatica de hasta NDIS_MAX_DRIVERS
 * miniports registrados.
 *
 * Flujo tipico:
 *   kernel.c llama ndis_register(&rtl8139_miniport)
 *   kernel.c llama ndis_register(&rtl8169_miniport)
 *   kernel.c llama ndis_init()      <- prueba en orden
 *   ... sistema arranca ...
 *   net.c    llama ndis_send()
 *   net.c    llama ndis_poll_recv() (en el loop principal via net_poll)
 */
#include "ndis.h"
#include <stdint.h>

#define NDIS_MAX_DRIVERS 8

static const ndis_miniport_t *registry[NDIS_MAX_DRIVERS];
static int registry_count = 0;
static const ndis_miniport_t *active = 0;

static ndis_stats_t stats;

void ndis_register(const ndis_miniport_t *mp){
    if(!mp || registry_count >= NDIS_MAX_DRIVERS) return;
    registry[registry_count++] = mp;
}

int ndis_init(void){
    active = 0;
    for(int i = 0; i < registry_count; i++){
        const ndis_miniport_t *mp = registry[i];
        if(!mp || !mp->init || !mp->send || !mp->poll_recv) continue;
        if(mp->init()){
            active = mp;
            return 1;
        }
    }
    return 0;
}

int ndis_present(void){
    return active != 0 && (!active->present || active->present());
}

const char* ndis_adapter_name(void){
    return active ? active->name : "Sin NIC";
}

void ndis_get_mac(uint8_t out_mac[6]){
    if(active && active->get_mac){
        active->get_mac(out_mac);
    } else {
        for(int i=0;i<6;i++) out_mac[i]=0;
    }
}

int ndis_send(const uint8_t *frame, int len){
    if(!active || !active->send) return -1;
    int r = active->send(frame, len);
    if(r == 0){
        stats.tx_frames++;
        stats.tx_bytes += (uint32_t)len;
    } else {
        stats.tx_errors++;
    }
    return r;
}

int ndis_poll_recv(uint8_t *buf, int maxlen){
    if(!active || !active->poll_recv) return 0;
    int n = active->poll_recv(buf, maxlen);
    if(n > 0){
        stats.rx_frames++;
        stats.rx_bytes += (uint32_t)n;
    }
    return n;
}

const ndis_stats_t* ndis_get_stats(void){ return &stats; }
void ndis_reset_stats(void){
    stats.tx_frames=0; stats.rx_frames=0;
    stats.tx_errors=0; stats.rx_errors=0;
    stats.tx_bytes=0;  stats.rx_bytes=0;
}
