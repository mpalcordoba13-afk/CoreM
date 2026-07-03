/*
 * tcp.c - Stack TCP minimo sobre net.c/rtl8139.c.
 *
 * Implementa lo justo para un HTTP GET de texto plano:
 *   - Three-way handshake (SYN -> SYN-ACK -> ACK).
 *   - Envio de datos (PSH+ACK), recepcion de datos y ACKs.
 *   - Cierre limpio (FIN+ACK en ambas direcciones).
 *
 * Sin: ventana deslizante real (ventana fija de 8KB), retransmisiones
 * automaticas, opciones TCP avanzadas (SACK, timestamps), ni HTTPS.
 * Es la base sobre la que se puede construir todo eso.
 *
 * Por polling igual que el resto de los drivers de MyOS.
 */
#include "net.h"
#include "rtl8139.h"
#include "timer.h"
#include <stdint.h>

#define IP_PROTO_TCP 6

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10

#define TCP_STATE_CLOSED      0
#define TCP_STATE_SYN_SENT    1
#define TCP_STATE_ESTABLISHED 2
#define TCP_STATE_FIN_WAIT1   3
#define TCP_STATE_FIN_WAIT2   4
#define TCP_STATE_CLOSE_WAIT  5
#define TCP_STATE_LAST_ACK    6
#define TCP_STATE_TIME_WAIT   7

#define RX_BUF_SIZE 8192
#define TX_BUF_SIZE 4096

typedef struct {
    int state;
    uint8_t remote_ip[4];
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t seq;   /* proximo numero de secuencia a enviar */
    uint32_t ack;   /* proximo byte esperado del otro lado  */
    uint8_t remote_mac[6];
    uint8_t rx_buf[RX_BUF_SIZE];
    int rx_head, rx_tail; /* ring buffer de lectura */
    int got_fin;    /* recibimos FIN del otro lado */
} tcp_socket_t;

static tcp_socket_t sockets[TCP_MAX_SOCKETS];
static uint16_t next_local_port = 49152;

/* ------------------------------------------------------------------ */
/* Helpers de checksum y construccion de paquetes                      */
/* ------------------------------------------------------------------ */

static void put16be(uint8_t *p, uint16_t v){ p[0]=(uint8_t)(v>>8); p[1]=(uint8_t)v; }
static void put32be(uint8_t *p, uint32_t v){
    p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v;
}
static uint16_t get16be(const uint8_t *p){ return (uint16_t)((p[0]<<8)|p[1]); }
static uint32_t get32be(const uint8_t *p){
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}

/* Pseudo-header TCP para el checksum */
static uint16_t tcp_checksum(const uint8_t *src_ip, const uint8_t *dst_ip,
                               const uint8_t *tcp_seg, int tcp_len){
    uint32_t sum = 0;
    /* pseudo-header: src IP, dst IP, 0x00, proto=6, tcp length */
    for (int i=0;i<4;i++) sum += src_ip[i] << (i&1 ? 0 : 8);
    for (int i=0;i<4;i++) sum += dst_ip[i] << (i&1 ? 0 : 8);

    /* Recalculo correcto: suma de palabras de 16 bits del pseudo-header */
    sum = 0;
    sum += (uint32_t)(src_ip[0]<<8)|src_ip[1];
    sum += (uint32_t)(src_ip[2]<<8)|src_ip[3];
    sum += (uint32_t)(dst_ip[0]<<8)|dst_ip[1];
    sum += (uint32_t)(dst_ip[2]<<8)|dst_ip[3];
    sum += 6; /* protocolo TCP */
    sum += (uint32_t)tcp_len;

    int i=0;
    for (; i+1<tcp_len; i+=2) sum += (uint32_t)((tcp_seg[i]<<8)|tcp_seg[i+1]);
    if (i<tcp_len) sum += (uint32_t)(tcp_seg[i]<<8);
    while (sum>>16) sum=(sum&0xFFFF)+(sum>>16);
    return (uint16_t)~sum;
}

/* Nuestros datos de red (necesitamos la IP para el checksum) */
extern const char* net_get_ip_str(void); /* declarada en net.h */
static uint8_t our_ip_cache[4];
static int our_ip_loaded = 0;

static void load_our_ip(void){
    if (our_ip_loaded) return;
    const char *s = net_get_ip_str();
    int part=0, val=0, i=0;
    while (s[i]){
        if (s[i]>='0'&&s[i]<='9') val=val*10+(s[i]-'0');
        else if (s[i]=='.' && part<3){ our_ip_cache[part++]=(uint8_t)val; val=0; }
        i++;
    }
    our_ip_cache[part]=(uint8_t)val;
    our_ip_loaded=1;
}

/* Construye y envia un segmento TCP (sin payload o con payload). */
static void tcp_send_seg(tcp_socket_t *s, uint8_t flags,
                          const uint8_t *payload, int plen){
    static uint8_t seg[20+4096];
    int tcp_len = 20 + (plen>0 ? plen : 0);
    if (tcp_len > (int)sizeof(seg)) tcp_len = sizeof(seg);

    put16be(seg+0, s->local_port);
    put16be(seg+2, s->remote_port);
    put32be(seg+4, s->seq);
    put32be(seg+8, s->ack);
    seg[12] = 0x50; /* data offset = 5 words (20 bytes), sin opciones */
    seg[13] = flags;
    put16be(seg+14, 8192); /* ventana de recepcion */
    put16be(seg+16, 0);    /* checksum: se calcula abajo */
    put16be(seg+18, 0);    /* urgent pointer */

    if (plen>0 && payload)
        for (int i=0;i<plen && 20+i<(int)sizeof(seg);i++) seg[20+i]=payload[i];

    load_our_ip();
    uint16_t cs = tcp_checksum(our_ip_cache, s->remote_ip, seg, tcp_len);
    put16be(seg+16, cs);

    extern void _net_send_ip_raw(const uint8_t *dst_mac, const uint8_t *dst_ip,
                                  uint8_t proto, const uint8_t *payload, int len);
    _net_send_ip_raw(s->remote_mac, s->remote_ip, IP_PROTO_TCP, seg, tcp_len);
}

/* ------------------------------------------------------------------ */
/* Recepcion: net_poll() llama a esta funcion para cada segmento TCP   */
/* ------------------------------------------------------------------ */

void tcp_handle_segment(const uint8_t *src_mac, const uint8_t *src_ip,
                         const uint8_t *seg, int len){
    if (len < 20) return;
    uint16_t dst_port = get16be(seg+2);
    uint16_t src_port = get16be(seg+0);
    uint32_t seq_n    = get32be(seg+4);
    uint32_t ack_n    = get32be(seg+8);
    int data_off      = ((seg[12]>>4)&0xF)*4;
    uint8_t flags     = seg[13];
    const uint8_t *data = seg + data_off;
    int data_len = len - data_off;
    if (data_off > len) data_len = 0;

    for (int i=0;i<TCP_MAX_SOCKETS;i++){
        tcp_socket_t *s = &sockets[i];
        if (s->state == TCP_STATE_CLOSED) continue;
        if (s->local_port != dst_port) continue;
        int ip_match = 1;
        for (int k=0;k<4;k++) if (s->remote_ip[k]!=src_ip[k]) ip_match=0;
        if (!ip_match) continue;
        /* Puede que sea el primer segmento: permite cualquier src_port
         * en estado SYN_SENT antes de conocer el puerto remoto real. */
        if (s->state != TCP_STATE_SYN_SENT && s->remote_port != src_port) continue;

        if (flags & TCP_FLAG_RST){ s->state = TCP_STATE_CLOSED; return; }

        if (s->state == TCP_STATE_SYN_SENT){
            if ((flags & (TCP_FLAG_SYN|TCP_FLAG_ACK)) == (TCP_FLAG_SYN|TCP_FLAG_ACK)){
                s->remote_port = src_port;
                for (int k=0;k<6;k++) s->remote_mac[k]=src_mac[k];
                s->ack = seq_n + 1;
                s->seq = ack_n;
                s->state = TCP_STATE_ESTABLISHED;
                tcp_send_seg(s, TCP_FLAG_ACK, 0, 0); /* ACK del SYN-ACK */
            }
            return;
        }

        if (s->state == TCP_STATE_ESTABLISHED ||
            s->state == TCP_STATE_FIN_WAIT1   ||
            s->state == TCP_STATE_FIN_WAIT2){

            /* ACK del otro lado: actualiza su seq esperado */
            if (flags & TCP_FLAG_ACK) (void)ack_n; /* no retransmitimos por ahora */

            /* Datos recibidos */
            if (data_len > 0){
                s->ack = seq_n + (uint32_t)data_len;
                /* Copiar al ring buffer */
                for (int j=0;j<data_len;j++){
                    int next = (s->rx_head+1) % RX_BUF_SIZE;
                    if (next != s->rx_tail){ /* si no esta lleno */
                        s->rx_buf[s->rx_head] = data[j];
                        s->rx_head = next;
                    }
                }
                tcp_send_seg(s, TCP_FLAG_ACK, 0, 0);
            }

            /* FIN del otro lado */
            if (flags & TCP_FLAG_FIN){
                s->ack = seq_n + 1;
                s->got_fin = 1;
                tcp_send_seg(s, TCP_FLAG_ACK, 0, 0);
                if (s->state == TCP_STATE_ESTABLISHED){
                    s->state = TCP_STATE_CLOSE_WAIT;
                } else if (s->state == TCP_STATE_FIN_WAIT2){
                    s->state = TCP_STATE_TIME_WAIT;
                }
            }

            /* ACK de nuestro FIN (FIN_WAIT1 -> FIN_WAIT2) */
            if (s->state == TCP_STATE_FIN_WAIT1 && (flags & TCP_FLAG_ACK)){
                s->state = TCP_STATE_FIN_WAIT2;
            }
        }

        if (s->state == TCP_STATE_LAST_ACK && (flags & TCP_FLAG_ACK)){
            s->state = TCP_STATE_CLOSED;
        }
        return;
    }
}

/* ------------------------------------------------------------------ */
/* API publica                                                          */
/* ------------------------------------------------------------------ */

tcp_sock_t tcp_connect(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3,
                        uint16_t port, uint32_t timeout_ms){
    if (!net_is_configured() || !rtl8139_present()) return -1;

    /* Busca slot libre */
    int idx = -1;
    for (int i=0;i<TCP_MAX_SOCKETS;i++) if (sockets[i].state==TCP_STATE_CLOSED){idx=i;break;}
    if (idx<0) return -1;

    tcp_socket_t *s = &sockets[idx];
    s->state = TCP_STATE_SYN_SENT;
    s->remote_ip[0]=ip0; s->remote_ip[1]=ip1; s->remote_ip[2]=ip2; s->remote_ip[3]=ip3;
    s->remote_port = port;
    s->local_port  = next_local_port++;
    if (next_local_port < 49152) next_local_port = 49152;
    s->seq = (uint32_t)(timer_ticks() * 1000 + idx); /* ISN pseudo-aleatorio */
    s->ack = 0;
    s->rx_head = s->rx_tail = 0;
    s->got_fin = 0;

    our_ip_loaded = 0; /* recargar por si cambio */
    load_our_ip();

    /* ARP del gateway/destino: necesitamos la MAC antes del SYN.
     * net_udp_send ya la resuelve internamente, pero para TCP
     * necesitamos la MAC ahora. Hack: mandamos un UDP de 0 bytes
     * solo para que ARP quede en cache, luego leemos la MAC del cache. */
    /* Usamos un campo transitorio: se llenara en tcp_handle_segment()
     * con el SYN-ACK. Mientras, ponermos la MAC de broadcast para el SYN. */
    for (int i=0;i<6;i++) s->remote_mac[i]=0xFF;

    /* Resolucion ARP real via net internals */
    extern int _net_arp_resolve_route(const uint8_t *dst_ip, uint8_t *mac_out, uint32_t tms);
    if (!_net_arp_resolve_route(s->remote_ip, s->remote_mac, 1000)){
        s->state = TCP_STATE_CLOSED;
        return -1;
    }

    /* Enviar SYN */
    s->seq++; /* El SYN consume un numero de secuencia */
    uint32_t syn_seq = s->seq - 1;
    /* Reconstruimos: ponemos seq al valor inicial y mandamos */
    s->seq = syn_seq;
    tcp_send_seg(s, TCP_FLAG_SYN, 0, 0);
    s->seq++; /* SYN consumido */

    /* Esperar SYN-ACK */
    uint32_t deadline = timer_ticks() + (timeout_ms/10) + 1;
    while (timer_ticks() < deadline){
        net_poll();
        if (s->state == TCP_STATE_ESTABLISHED) return (tcp_sock_t)idx;
        if (s->state == TCP_STATE_CLOSED) return -1;
    }
    s->state = TCP_STATE_CLOSED;
    return -1;
}

int tcp_send(tcp_sock_t sock, const uint8_t *data, int len){
    if (sock<0||sock>=TCP_MAX_SOCKETS) return -1;
    tcp_socket_t *s = &sockets[sock];
    if (s->state != TCP_STATE_ESTABLISHED) return -1;
    if (!data || len<=0) return 0;
    if (len > TX_BUF_SIZE) len = TX_BUF_SIZE;
    tcp_send_seg(s, TCP_FLAG_PSH|TCP_FLAG_ACK, data, len);
    s->seq += (uint32_t)len;
    return len;
}

int tcp_recv(tcp_sock_t sock, uint8_t *buf, int maxlen){
    if (sock<0||sock>=TCP_MAX_SOCKETS) return -1;
    tcp_socket_t *s = &sockets[sock];
    if (s->state == TCP_STATE_CLOSED) return -1;
    int n = 0;
    while (n < maxlen && s->rx_tail != s->rx_head){
        buf[n++] = s->rx_buf[s->rx_tail];
        s->rx_tail = (s->rx_tail+1) % RX_BUF_SIZE;
    }
    if (n==0 && s->got_fin && s->state != TCP_STATE_ESTABLISHED) return -1;
    return n;
}

void tcp_close(tcp_sock_t sock){
    if (sock<0||sock>=TCP_MAX_SOCKETS) return;
    tcp_socket_t *s = &sockets[sock];
    if (s->state == TCP_STATE_ESTABLISHED){
        tcp_send_seg(s, TCP_FLAG_FIN|TCP_FLAG_ACK, 0, 0);
        s->seq++;
        s->state = TCP_STATE_FIN_WAIT1;
        /* Espera breve a que el otro lado responda */
        uint32_t deadline = timer_ticks() + 200;
        while (timer_ticks() < deadline && s->state != TCP_STATE_TIME_WAIT &&
               s->state != TCP_STATE_CLOSED){ net_poll(); }
    }
    if (s->state == TCP_STATE_CLOSE_WAIT){
        tcp_send_seg(s, TCP_FLAG_FIN|TCP_FLAG_ACK, 0, 0);
        s->seq++;
        s->state = TCP_STATE_LAST_ACK;
        uint32_t deadline = timer_ticks() + 100;
        while (timer_ticks() < deadline && s->state != TCP_STATE_CLOSED){ net_poll(); }
    }
    s->state = TCP_STATE_CLOSED;
}

/* ------------------------------------------------------------------ */
/* HTTP GET simple                                                      */
/* ------------------------------------------------------------------ */

static int slen2(const char *s){ int n=0; while(s[n]) n++; return n; }
static void scpy2(char *d, const char *s, int max){
    int i=0; while(s[i]&&i<max-1){d[i]=s[i];i++;} d[i]='\0';
}

int net_http_get(const char *host, const char *path, char *out, int maxlen){
    if (!host||!path||!out||maxlen<1) return -1;

    uint8_t ip[4];
    if (net_dns_resolve(host, ip, 3000) != 0) return -1;

    tcp_sock_t sock = tcp_connect(ip[0],ip[1],ip[2],ip[3], 80, 5000);
    if (sock < 0) return -1;

    /* Peticion GET */
    static char req[512]; int p=0;
    const char *method = "GET ";
    for(int i=0;method[i];i++) req[p++]=method[i];
    for(int i=0;path[i]&&p<480;i++) req[p++]=path[i];
    const char *http10 = " HTTP/1.0\r\nHost: ";
    for(int i=0;http10[i];i++) req[p++]=http10[i];
    for(int i=0;host[i]&&p<500;i++) req[p++]=host[i];
    const char *end = "\r\nConnection: close\r\n\r\n";
    for(int i=0;end[i];i++) req[p++]=end[i];

    if (tcp_send(sock, (uint8_t*)req, p) < 0){ tcp_close(sock); return -1; }

    /* Recibe la respuesta con sliding-window para detectar \r\n\r\n
     * sin buffer de acumulacion (evita el bug del buffer de 256 bytes). */
    static uint8_t rbuf[512];
    int total = 0;
    uint32_t deadline = timer_ticks() + 2000; /* 20s inicial */
    int header_done = 0;
    uint8_t h4[4] = {0,0,0,0}; /* sliding window de 4 bytes */

    /* Guardamos la primera linea del response para detectar redireccion */
    static char status_line[64]; int sl = 0;
    int in_status = 1; /* todavia leyendo la primera linea */

    while (timer_ticks() < deadline && total < maxlen-1){
        net_poll();
        int n = tcp_recv(sock, rbuf, sizeof(rbuf));
        if (n < 0) break;
        if (n == 0){
            if (sockets[sock].got_fin) break;
            continue;
        }
        deadline = timer_ticks() + 500; /* reiniciar timeout (5s) */

        for (int i=0; i<n && total < maxlen-1; i++){
            uint8_t c = rbuf[i];

            /* Acumular primera linea del status */
            if (in_status){
                if (c == '\n') in_status = 0;
                else if (c != '\r' && sl < 62) status_line[sl++] = (char)c;
                status_line[sl] = '\0';
            }

            if (!header_done){
                /* Sliding window de 4 bytes para detectar \r\n\r\n */
                h4[0]=h4[1]; h4[1]=h4[2]; h4[2]=h4[3]; h4[3]=c;
                if (h4[0]=='\r'&&h4[1]=='\n'&&h4[2]=='\r'&&h4[3]=='\n'){
                    header_done=1;
                }
            } else {
                out[total++] = (char)c;
            }
        }
    }
    out[total] = '\0';
    tcp_close(sock);

    /* Si recibimos redireccion (301/302) y cuerpo vacio, informamos */
    if (total == 0 && sl > 0){
        /* Buscar "301" o "302" en la status line */
        int is_redir = 0;
        for(int i=0;status_line[i];i++){
            if(status_line[i]=='3'&&(status_line[i+1]=='0')&&
               (status_line[i+2]=='1'||status_line[i+2]=='2')){
                is_redir=1; break;
            }
        }
        if(is_redir){
            const char *msg1 = "Redireccion HTTP detectada.\n"
                               "Este sitio usa HTTPS.\n\n"
                               "Proba uno de estos sitios que si son HTTP puro:\n\n"
                               "  http://info.cern.ch/\n"
                               "  http://neverssl.com/\n"
                               "  http://httpforever.com/\n\n"
                               "Estado del servidor: ";
            int m=0; while(msg1[m]&&m<maxlen-80) out[m]=msg1[m],m++;
            for(int i=0;status_line[i]&&m<maxlen-2;i++) out[m++]=status_line[i];
            out[m++]='\n'; out[m]='\0';
            return m;
        }
    }
    return total;
}
