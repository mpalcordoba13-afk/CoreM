/*
 * net.c - Stack de red minimo construido sobre rtl8139.c.
 *
 * Cubre lo justo para un 'ping' funcional de extremo a extremo:
 *   - Ethernet: armado/parseo de frames, broadcast.
 *   - ARP: pedir y responder direcciones MAC, con una cache chica.
 *   - IPv4: armado/parseo de header + checksum, sin fragmentacion.
 *   - ICMP: Echo Request/Reply (responde pings ajenos y manda los propios).
 *
 * Todavia no hay: DHCP (IP fija a mano), UDP, TCP, ni DNS. Eso es la
 * base sobre la que se construirian esas capas mas adelante.
 */
#include "net.h"
#include "ndis.h"
#include "timer.h"
#include <stdint.h>

#define ETH_TYPE_ARP 0x0806
#define ETH_TYPE_IP  0x0800
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2
#define IP_PROTO_ICMP  1
#define IP_PROTO_UDP   17
#define IP_PROTO_TCP   6
#define ARP_CACHE_SIZE 8
#define DNS_PORT       53
#define DNS_SRC_PORT   53000

static const uint8_t BCAST_MAC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
static const uint8_t ZERO_MAC[6]  = {0,0,0,0,0,0};

static uint8_t our_mac[6];
static uint8_t our_ip[4], our_mask[4], our_gw[4], our_dns[4];
static int configured = 0;
static char ip_str[16];

typedef struct { uint8_t ip[4]; uint8_t mac[6]; int valid; } arp_entry_t;
static arp_entry_t arp_cache[ARP_CACHE_SIZE];

static uint16_t ping_id = 0;
static uint16_t ping_seq = 0;
static int ping_pending = 0;
static int ping_got_reply = 0;

static uint16_t dns_query_id = 0;
static int dns_pending = 0;
static int dns_got_reply = 0;
static uint8_t dns_result_ip[4];

/* ---------------- Utilidades ---------------- */
static int ip_eq(const uint8_t a[4], const uint8_t b[4]){
    return a[0]==b[0] && a[1]==b[1] && a[2]==b[2] && a[3]==b[3];
}
static void put16be(uint8_t *p, uint16_t v){ p[0]=(uint8_t)(v>>8); p[1]=(uint8_t)v; }
static uint16_t get16be(const uint8_t *p){ return (uint16_t)((p[0]<<8)|p[1]); }

static uint16_t ip_checksum(const uint8_t *data, int len){
    uint32_t sum = 0;
    int i = 0;
    for (; i+1 < len; i += 2) sum += (uint32_t)((data[i]<<8)|data[i+1]);
    if (i < len) sum += (uint32_t)(data[i]<<8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static void itoa_dec(int n, char *buf){
    int i=0;
    if (n==0){ buf[i++]='0'; }
    else { char tmp[5]; int j=0; while(n>0){tmp[j++]='0'+(n%10); n/=10;} while(j>0) buf[i++]=tmp[--j]; }
    buf[i]='\0';
}

static void rebuild_ip_str(void){
    int p=0; char nb[5];
    for (int i=0;i<4;i++){
        itoa_dec(our_ip[i], nb);
        for (int q=0; nb[q]; q++) ip_str[p++]=nb[q];
        if (i<3) ip_str[p++]='.';
    }
    ip_str[p]='\0';
}

/* ---------------- Ethernet ---------------- */
static void eth_send(const uint8_t dst_mac[6], uint16_t ethertype, const uint8_t *payload, int len){
    static uint8_t frame[1514];
    if (len > (int)sizeof(frame)-14) len = sizeof(frame)-14;
    for (int i=0;i<6;i++) frame[i]   = dst_mac[i];
    for (int i=0;i<6;i++) frame[6+i] = our_mac[i];
    put16be(frame+12, ethertype);
    for (int i=0;i<len;i++) frame[14+i] = payload[i];
    ndis_send(frame, 14+len);
}

/* ---------------- ARP ---------------- */
static int arp_cache_lookup(const uint8_t ip[4], uint8_t mac_out[6]){
    for (int i=0;i<ARP_CACHE_SIZE;i++){
        if (arp_cache[i].valid && ip_eq(arp_cache[i].ip, ip)){
            for (int j=0;j<6;j++) mac_out[j]=arp_cache[i].mac[j];
            return 1;
        }
    }
    return 0;
}
static void arp_cache_insert(const uint8_t ip[4], const uint8_t mac[6]){
    for (int i=0;i<ARP_CACHE_SIZE;i++){
        if (arp_cache[i].valid && ip_eq(arp_cache[i].ip, ip)){
            for (int j=0;j<6;j++) arp_cache[i].mac[j]=mac[j];
            return;
        }
    }
    for (int i=0;i<ARP_CACHE_SIZE;i++){
        if (!arp_cache[i].valid){
            arp_cache[i].valid=1;
            for (int j=0;j<4;j++) arp_cache[i].ip[j]=ip[j];
            for (int j=0;j<6;j++) arp_cache[i].mac[j]=mac[j];
            return;
        }
    }
    /* cache llena: pisa la primera entrada (politica simple) */
    arp_cache[0].valid=1;
    for (int j=0;j<4;j++) arp_cache[0].ip[j]=ip[j];
    for (int j=0;j<6;j++) arp_cache[0].mac[j]=mac[j];
}

static void arp_build_and_send(uint16_t op, const uint8_t target_mac[6], const uint8_t target_ip[4]){
    uint8_t pkt[28];
    put16be(pkt+0, 1);      /* htype = Ethernet */
    put16be(pkt+2, 0x0800); /* ptype = IPv4 */
    pkt[4]=6; pkt[5]=4;     /* hlen, plen */
    put16be(pkt+6, op);
    for (int i=0;i<6;i++) pkt[8+i]  = our_mac[i];
    for (int i=0;i<4;i++) pkt[14+i] = our_ip[i];
    for (int i=0;i<6;i++) pkt[18+i] = target_mac[i];
    for (int i=0;i<4;i++) pkt[24+i] = target_ip[i];

    const uint8_t *eth_dst = (op==ARP_OP_REQUEST) ? BCAST_MAC : target_mac;
    eth_send(eth_dst, ETH_TYPE_ARP, pkt, 28);
}

static void handle_arp(const uint8_t *payload, int len){
    if (len < 28) return;
    uint16_t op = get16be(payload+6);
    const uint8_t *sender_mac = payload+8;
    const uint8_t *sender_ip  = payload+14;
    const uint8_t *target_ip  = payload+24;

    arp_cache_insert(sender_ip, sender_mac);

    if (op==ARP_OP_REQUEST && configured && ip_eq(target_ip, our_ip)){
        arp_build_and_send(ARP_OP_REPLY, sender_mac, sender_ip);
    }
}

static int arp_resolve(const uint8_t ip[4], uint8_t mac_out[6], uint32_t timeout_ms){
    if (arp_cache_lookup(ip, mac_out)) return 1;
    arp_build_and_send(ARP_OP_REQUEST, ZERO_MAC, ip);

    uint32_t deadline = timer_ticks() + (timeout_ms/10) + 1;
    while (timer_ticks() < deadline){
        net_poll();
        if (arp_cache_lookup(ip, mac_out)) return 1;
    }
    return 0;
}

/* Decide si hay que pedirle la MAC directamente al destino (misma
 * subred) o a nuestro gateway (destino fuera de la subred), y la
 * resuelve por ARP. Usado por ping, UDP y DNS por igual. */
/* Expuesta con alias para tcp.c */
int _net_arp_resolve_route(const uint8_t dst_ip[4], uint8_t mac_out[6], uint32_t timeout_ms){
    uint8_t arp_target[4];
    int same_subnet = 1;
    for (int i=0;i<4;i++) if ((dst_ip[i]&our_mask[i]) != (our_ip[i]&our_mask[i])) same_subnet=0;
    for (int i=0;i<4;i++) arp_target[i] = same_subnet ? dst_ip[i] : our_gw[i];
    return arp_resolve(arp_target, mac_out, timeout_ms);
}
static int resolve_route_mac(const uint8_t dst_ip[4], uint8_t mac_out[6], uint32_t timeout_ms){
    return _net_arp_resolve_route(dst_ip, mac_out, timeout_ms);
}

/* ---------------- IPv4 / ICMP ---------------- */
static void build_ip_header(uint8_t *hdr, const uint8_t dst_ip[4], uint8_t proto, int payload_len){
    hdr[0] = 0x45; /* version 4, IHL 5 (20 bytes, sin opciones) */
    hdr[1] = 0x00; /* DSCP/ECN */
    put16be(hdr+2, (uint16_t)(20+payload_len));
    put16be(hdr+4, 0x1234); /* identification, fijo: no fragmentamos */
    put16be(hdr+6, 0x0000); /* flags/fragment offset */
    hdr[8] = 64;     /* TTL */
    hdr[9] = proto;
    put16be(hdr+10, 0); /* checksum: se calcula despues */
    for (int i=0;i<4;i++) hdr[12+i] = our_ip[i];
    for (int i=0;i<4;i++) hdr[16+i] = dst_ip[i];
    put16be(hdr+10, ip_checksum(hdr, 20));
}

/* Expuesta con alias para tcp.c */
void _net_send_ip_raw(const uint8_t dst_mac[6], const uint8_t dst_ip[4],
                       uint8_t proto, const uint8_t *payload, int len){
    static uint8_t pkt[20+4096];
    if (len > (int)sizeof(pkt)-20) len = sizeof(pkt)-20;
    build_ip_header(pkt, dst_ip, proto, len);
    for (int i=0;i<len;i++) pkt[20+i] = payload[i];
    eth_send(dst_mac, ETH_TYPE_IP, pkt, 20+len);
}
static void send_ip_packet_direct(const uint8_t dst_mac[6], const uint8_t dst_ip[4],
                                   uint8_t proto, const uint8_t *payload, int len){
    _net_send_ip_raw(dst_mac, dst_ip, proto, payload, len);
}

static void handle_icmp(const uint8_t src_mac[6], const uint8_t src_ip[4],
                         const uint8_t *data, int len){
    if (len < 8) return;
    uint8_t type = data[0];
    uint16_t id  = get16be(data+4);
    uint16_t seq = get16be(data+6);

    if (type == 8 /* Echo Request */){
        static uint8_t reply[8+1472];
        if (len > (int)sizeof(reply)) len = sizeof(reply);
        for (int i=0;i<len;i++) reply[i]=data[i];
        reply[0]=0; /* Echo Reply */
        reply[1]=0;
        put16be(reply+2, 0);
        put16be(reply+2, ip_checksum(reply, len));
        send_ip_packet_direct(src_mac, src_ip, IP_PROTO_ICMP, reply, len);
    } else if (type == 0 /* Echo Reply */){
        if (ping_pending && id==ping_id && seq==ping_seq) ping_got_reply = 1;
    }
}

static void send_udp_packet(const uint8_t dst_mac[6], const uint8_t dst_ip[4],
                             uint16_t src_port, uint16_t dst_port,
                             const uint8_t *payload, int len){
    static uint8_t buf[8+512];
    if (len > (int)sizeof(buf)-8) len = sizeof(buf)-8;
    put16be(buf+0, src_port);
    put16be(buf+2, dst_port);
    put16be(buf+4, (uint16_t)(8+len));
    put16be(buf+6, 0); /* checksum UDP/IPv4 opcional: 0 = no calculado */
    for (int i=0;i<len;i++) buf[8+i] = payload[i];
    send_ip_packet_direct(dst_mac, dst_ip, IP_PROTO_UDP, buf, 8+len);
}

/* Avanza 'pos' mas alla de un nombre DNS (con o sin compresion por
 * puntero 0xC0). No necesita decodificarlo, solo saber donde termina. */
static int dns_skip_name(const uint8_t *msg, int len, int pos){
    while (pos < len){
        uint8_t l = msg[pos];
        if ((l & 0xC0) == 0xC0) return pos+2; /* puntero de compresion: 2 bytes */
        if (l == 0) return pos+1;             /* fin de nombre */
        pos += 1 + l;
    }
    return pos;
}

static void handle_dns_response(const uint8_t *payload, int len){
    if (!dns_pending || len < 12) return;
    uint16_t resp_id = get16be(payload+0);
    if (resp_id != dns_query_id) return;

    uint16_t qdcount = get16be(payload+4);
    uint16_t ancount = get16be(payload+6);
    int pos = 12;

    for (int i=0; i<qdcount; i++){
        pos = dns_skip_name(payload, len, pos);
        pos += 4; /* QTYPE + QCLASS */
        if (pos > len) return;
    }
    for (int i=0; i<ancount; i++){
        pos = dns_skip_name(payload, len, pos);
        if (pos+10 > len) return;
        uint16_t rtype  = get16be(payload+pos);
        uint16_t rclass = get16be(payload+pos+2);
        uint16_t rdlen  = get16be(payload+pos+8);
        pos += 10;
        if (pos+rdlen > len) return;
        if (rtype==1 && rclass==1 && rdlen==4){ /* registro A */
            for (int k=0;k<4;k++) dns_result_ip[k] = payload[pos+k];
            dns_got_reply = 1;
            return;
        }
        pos += rdlen;
    }
}

/* Forwards para modulos que reciben UDP */
void dhcp_handle_udp(const uint8_t *payload, int len);

static void handle_udp(const uint8_t *data, int len){
    if (len < 8) return;
    uint16_t dst_port = get16be(data+2);
    const uint8_t *payload = data+8;
    int paylen = len-8;
    if      (dst_port == DNS_SRC_PORT) handle_dns_response(payload, paylen);
    else if (dst_port == 68)           dhcp_handle_udp(payload, paylen);
}

/* Forward para tcp.c */
void tcp_handle_segment(const uint8_t *src_mac, const uint8_t *src_ip,
                         const uint8_t *seg, int len);

static void handle_ip(const uint8_t src_mac[6], const uint8_t *payload, int len){
    if (len < 20) return;
    int ihl = (payload[0] & 0x0F) * 4;
    if (ihl < 20 || len < ihl) return;
    uint8_t proto = payload[9];
    const uint8_t *src_ip = payload+12;
    if      (proto == IP_PROTO_ICMP) handle_icmp(src_mac, src_ip, payload+ihl, len-ihl);
    else if (proto == IP_PROTO_UDP)  handle_udp(payload+ihl, len-ihl);
    else if (proto == IP_PROTO_TCP)  tcp_handle_segment(src_mac, src_ip, payload+ihl, len-ihl);
}

/* ---------------- API publica ---------------- */
void net_set_ip(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3,
                 uint8_t mask0, uint8_t mask1, uint8_t mask2, uint8_t mask3,
                 uint8_t gw0, uint8_t gw1, uint8_t gw2, uint8_t gw3,
                 uint8_t dns0, uint8_t dns1, uint8_t dns2, uint8_t dns3){
    our_ip[0]=ip0; our_ip[1]=ip1; our_ip[2]=ip2; our_ip[3]=ip3;
    our_mask[0]=mask0; our_mask[1]=mask1; our_mask[2]=mask2; our_mask[3]=mask3;
    our_gw[0]=gw0; our_gw[1]=gw1; our_gw[2]=gw2; our_gw[3]=gw3;
    our_dns[0]=dns0; our_dns[1]=dns1; our_dns[2]=dns2; our_dns[3]=dns3;
    ndis_get_mac(our_mac);
    for (int i=0;i<ARP_CACHE_SIZE;i++) arp_cache[i].valid=0;
    configured = 1;
    rebuild_ip_str();
}

int net_is_configured(void){ return configured; }

const char* net_get_ip_str(void){
    if (!configured) return "0.0.0.0";
    return ip_str;
}

void net_poll(void){
    if (!ndis_present()) return;
    static uint8_t frame[1600];
    int len;
    int guard = 0;
    while (guard++ < 16 && (len = ndis_poll_recv(frame, sizeof(frame))) > 0){
        if (len < 14) continue;
        const uint8_t *src_mac = frame+6;
        uint16_t ethertype = get16be(frame+12);
        const uint8_t *payload = frame+14;
        int paylen = len-14;
        if (ethertype == ETH_TYPE_ARP) handle_arp(payload, paylen);
        else if (ethertype == ETH_TYPE_IP) handle_ip(src_mac, payload, paylen);
    }
}

int net_ping(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3, uint32_t timeout_ms){
    if (!configured || !ndis_present()) return -1;

    uint8_t dst_ip[4] = {ip0,ip1,ip2,ip3};
    uint8_t dst_mac[6];
    if (!resolve_route_mac(dst_ip, dst_mac, 1000)) return -1;

    ping_id = 0xBEEF;
    ping_seq++;
    ping_pending = 1;
    ping_got_reply = 0;

    uint8_t icmp[16];
    icmp[0]=8; icmp[1]=0; put16be(icmp+2,0);
    put16be(icmp+4, ping_id);
    put16be(icmp+6, ping_seq);
    for (int i=8;i<16;i++) icmp[i]=(uint8_t)i;
    put16be(icmp+2, ip_checksum(icmp,16));

    uint32_t start = timer_ticks();
    send_ip_packet_direct(dst_mac, dst_ip, IP_PROTO_ICMP, icmp, 16);

    uint32_t deadline = start + (timeout_ms/10) + 1;
    int result = -1;
    while (timer_ticks() < deadline){
        net_poll();
        if (ping_got_reply){
            result = (int)((timer_ticks()-start) * 10); /* ms aprox, PIT a 100Hz */
            break;
        }
    }
    ping_pending = 0;
    return result;
}

int net_udp_send(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3,
                  uint16_t src_port, uint16_t dst_port,
                  const uint8_t *payload, int len){
    if (!configured || !ndis_present() || len<=0) return -1;
    uint8_t dst_ip[4] = {ip0,ip1,ip2,ip3};
    uint8_t dst_mac[6];
    if (!resolve_route_mac(dst_ip, dst_mac, 1000)) return -1;
    send_udp_packet(dst_mac, dst_ip, src_port, dst_port, payload, len);
    return 0;
}

/* Convierte "example.com" al formato QNAME de DNS: secuencia de
 * etiquetas con longitud-prefijo, terminada en byte 0.
 * Devuelve la cantidad de bytes escritos, o -1 si una etiqueta es
 * invalida (vacia o > 63 bytes). */
static int encode_qname(const char *host, uint8_t *out){
    int n = 0, label_start = 0, i = 0;
    while (1){
        if (host[i]=='.' || host[i]=='\0'){
            int label_len = i - label_start;
            if (label_len<=0 || label_len>63) return -1;
            out[n++] = (uint8_t)label_len;
            for (int k=label_start;k<i;k++) out[n++] = (uint8_t)host[k];
            label_start = i+1;
            if (host[i]=='\0') break;
        }
        i++;
        if (i > 250) return -1; /* nombre demasiado largo */
    }
    out[n++] = 0;
    return n;
}

int net_dns_resolve(const char *hostname, uint8_t out_ip[4], uint32_t timeout_ms){
    if (!configured || !ndis_present() || !hostname) return -1;
    if (our_dns[0]==0 && our_dns[1]==0 && our_dns[2]==0 && our_dns[3]==0) return -1;

    uint8_t dst_mac[6];
    if (!resolve_route_mac(our_dns, dst_mac, 1000)) return -1;

    static uint8_t qbuf[300];
    int qn = encode_qname(hostname, qbuf+12);
    if (qn < 0) return -1;

    dns_query_id = (uint16_t)(dns_query_id + 1);
    put16be(qbuf+0, dns_query_id);
    put16be(qbuf+2, 0x0100); /* consulta estandar, recursion deseada */
    put16be(qbuf+4, 1);      /* QDCOUNT */
    put16be(qbuf+6, 0); put16be(qbuf+8, 0); put16be(qbuf+10, 0);

    int qpos = 12+qn;
    put16be(qbuf+qpos, 1);   /* QTYPE = A */
    put16be(qbuf+qpos+2, 1); /* QCLASS = IN */
    int total = qpos+4;

    dns_pending = 1;
    dns_got_reply = 0;
    send_udp_packet(dst_mac, our_dns, DNS_SRC_PORT, DNS_PORT, qbuf, total);

    uint32_t deadline = timer_ticks() + (timeout_ms/10) + 1;
    int result = -1;
    while (timer_ticks() < deadline){
        net_poll();
        if (dns_got_reply){
            for (int i=0;i<4;i++) out_ip[i] = dns_result_ip[i];
            result = 0;
            break;
        }
    }
    dns_pending = 0;
    return result;
}
