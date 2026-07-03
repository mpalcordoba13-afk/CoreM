/*
 * dhcp.c - Cliente DHCP RFC-2131 minimo para MyOS.
 *
 * Flujo: DHCPDISCOVER (broadcast) -> DHCPOFFER -> DHCPREQUEST -> DHCPACK.
 * Todo sobre UDP: cliente en puerto 68, servidor en puerto 67.
 *
 * Extrae del ACK: IP ofrecida, mascara de subred (opcion 1),
 * gateway (opcion 3) y DNS (opcion 6). Llama a net_set_ip() con
 * esos valores para dejar la pila lista.
 *
 * Sin renovacion de lease automatica.
 */
#include "dhcp.h"
#include "net.h"
#include "ndis.h"
#include "timer.h"
#include <stdint.h>

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
#define DHCP_MAGIC       0x63825363UL

#define DHCPDISCOVER 1
#define DHCPOFFER    2
#define DHCPREQUEST  3
#define DHCPACK      5

#define OPT_SUBNET   1
#define OPT_ROUTER   3
#define OPT_DNS      6
#define OPT_REQIP   50
#define OPT_MSGTYPE 53
#define OPT_SERVERID 54
#define OPT_END    255

static uint8_t offered_ip[4];
static uint8_t offered_mask[4] = {255,255,255,0};
static uint8_t offered_gw[4];
static uint8_t offered_dns[4];
static uint8_t server_id[4];
static int dhcp_got_offer = 0;
static int dhcp_got_ack   = 0;
static uint32_t dhcp_xid  = 0;
static uint8_t our_mac[6];

/* ---- udp rx hook: net.c entrega paquetes UDP a este callback ------- */
/* (Se conecta igual que DNS: por puerto destino en handle_udp de net.c) */

/* Declaramos el callback que net.c llamara cuando llegue un UDP al
 * puerto 68.  La firma debe coincidir con lo que ponemos en net.c. */
void dhcp_handle_udp(const uint8_t *payload, int len){
    if (len < 240) return;
    if (payload[0] != 2) return; /* op=BOOTREPLY */

    uint32_t xid = ((uint32_t)payload[4]<<24)|((uint32_t)payload[5]<<16)|
                   ((uint32_t)payload[6]<<8)|payload[7];
    if (xid != dhcp_xid) return;

    uint32_t magic = ((uint32_t)payload[236]<<24)|((uint32_t)payload[237]<<16)|
                     ((uint32_t)payload[238]<<8)|payload[239];
    if (magic != DHCP_MAGIC) return;

    /* IP ofrecida esta en yiaddr (bytes 16-19) */
    for (int i=0;i<4;i++) offered_ip[i] = payload[16+i];

    /* Parsear opciones */
    uint8_t msg_type = 0;
    int pos = 240;
    while (pos < len){
        uint8_t opt = payload[pos++];
        if (opt == OPT_END) break;
        if (opt == 0) continue; /* PAD */
        if (pos >= len) break;
        uint8_t olen = payload[pos++];
        if (pos+olen > len) break;

        if (opt==OPT_MSGTYPE && olen>=1) msg_type = payload[pos];
        else if (opt==OPT_SUBNET && olen>=4)
            for(int i=0;i<4;i++) offered_mask[i]=payload[pos+i];
        else if (opt==OPT_ROUTER && olen>=4)
            for(int i=0;i<4;i++) offered_gw[i]=payload[pos+i];
        else if (opt==OPT_DNS && olen>=4)
            for(int i=0;i<4;i++) offered_dns[i]=payload[pos+i];
        else if (opt==OPT_SERVERID && olen>=4)
            for(int i=0;i<4;i++) server_id[i]=payload[pos+i];

        pos += olen;
    }

    if (msg_type==DHCPOFFER && !dhcp_got_offer){
        dhcp_got_offer = 1;
    } else if (msg_type==DHCPACK){
        dhcp_got_ack = 1;
    }
}

/* ---- construccion de mensajes DHCP --------------------------------- */

static uint8_t dhcp_buf[548]; /* tamano fijo historico del paquete DHCP */

static void build_discover(void){
    for(int i=0;i<548;i++) dhcp_buf[i]=0;
    dhcp_buf[0]=1;  /* op = BOOTREQUEST */
    dhcp_buf[1]=1;  /* htype = Ethernet */
    dhcp_buf[2]=6;  /* hlen = 6 */
    dhcp_buf[3]=0;  /* hops */
    dhcp_buf[4]=(uint8_t)(dhcp_xid>>24);
    dhcp_buf[5]=(uint8_t)(dhcp_xid>>16);
    dhcp_buf[6]=(uint8_t)(dhcp_xid>>8);
    dhcp_buf[7]=(uint8_t)dhcp_xid;
    dhcp_buf[10]=0x80; /* flags: broadcast */
    for(int i=0;i<6;i++) dhcp_buf[28+i]=our_mac[i];
    /* magic cookie */
    dhcp_buf[236]=0x63; dhcp_buf[237]=0x82;
    dhcp_buf[238]=0x53; dhcp_buf[239]=0x63;
    /* opciones */
    int p=240;
    dhcp_buf[p++]=OPT_MSGTYPE; dhcp_buf[p++]=1; dhcp_buf[p++]=DHCPDISCOVER;
    dhcp_buf[p++]=OPT_END;
}

static void build_request(void){
    for(int i=0;i<548;i++) dhcp_buf[i]=0;
    dhcp_buf[0]=1; dhcp_buf[1]=1; dhcp_buf[2]=6; dhcp_buf[3]=0;
    dhcp_buf[4]=(uint8_t)(dhcp_xid>>24);
    dhcp_buf[5]=(uint8_t)(dhcp_xid>>16);
    dhcp_buf[6]=(uint8_t)(dhcp_xid>>8);
    dhcp_buf[7]=(uint8_t)dhcp_xid;
    dhcp_buf[10]=0x80;
    for(int i=0;i<6;i++) dhcp_buf[28+i]=our_mac[i];
    dhcp_buf[236]=0x63; dhcp_buf[237]=0x82;
    dhcp_buf[238]=0x53; dhcp_buf[239]=0x63;
    int p=240;
    dhcp_buf[p++]=OPT_MSGTYPE; dhcp_buf[p++]=1; dhcp_buf[p++]=DHCPREQUEST;
    dhcp_buf[p++]=OPT_REQIP; dhcp_buf[p++]=4;
    for(int i=0;i<4;i++) dhcp_buf[p++]=offered_ip[i];
    dhcp_buf[p++]=OPT_SERVERID; dhcp_buf[p++]=4;
    for(int i=0;i<4;i++) dhcp_buf[p++]=server_id[i];
    dhcp_buf[p++]=OPT_END;
}

/* ---- API publica --------------------------------------------------- */

int dhcp_request(uint32_t timeout_ms){
    if (!ndis_present()) return 0;

    ndis_get_mac(our_mac);
    dhcp_xid = (uint32_t)(timer_ticks() ^ 0xDEAD1234UL);
    dhcp_got_offer = 0;
    dhcp_got_ack   = 0;

    /* Necesitamos IP temporal 0.0.0.0 para mandar el broadcast.
     * net_set_ip con todo en cero habilita el envio desde src 0.0.0.0. */
    net_set_ip(0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0);

    /* DISCOVER */
    build_discover();
    net_udp_send(255,255,255,255, DHCP_CLIENT_PORT, DHCP_SERVER_PORT,
                 dhcp_buf, 548);

    /* Esperar OFFER */
    uint32_t deadline = timer_ticks() + (timeout_ms/10)/2 + 1;
    while (timer_ticks() < deadline && !dhcp_got_offer) net_poll();
    if (!dhcp_got_offer) return 0;

    /* REQUEST */
    build_request();
    net_udp_send(255,255,255,255, DHCP_CLIENT_PORT, DHCP_SERVER_PORT,
                 dhcp_buf, 548);

    /* Esperar ACK */
    deadline = timer_ticks() + (timeout_ms/10)/2 + 1;
    while (timer_ticks() < deadline && !dhcp_got_ack) net_poll();
    if (!dhcp_got_ack) return 0;

    /* Aplicar configuracion */
    net_set_ip(offered_ip[0], offered_ip[1], offered_ip[2], offered_ip[3],
               offered_mask[0], offered_mask[1], offered_mask[2], offered_mask[3],
               offered_gw[0],   offered_gw[1],   offered_gw[2],   offered_gw[3],
               offered_dns[0],  offered_dns[1],  offered_dns[2],  offered_dns[3]);
    return 1;
}

const char* dhcp_get_ip(void){
    return net_get_ip_str();
}
