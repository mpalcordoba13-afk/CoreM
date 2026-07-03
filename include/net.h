#ifndef NET_H
#define NET_H
#include <stdint.h>

/*
 * net.h - Stack de red minimo: Ethernet + ARP + IPv4 + ICMP (ping).
 *
 * Sin DHCP todavia: la IP/gateway/mascara se configuran a mano con
 * net_set_ip(). Sin TCP/UDP/DNS todavia: eso es el siguiente escalon.
 *
 * Requiere haber llamado antes a rtl8139_init() con exito.
 */

/* Configura nuestra IP, mascara, gateway y servidor DNS (los 4 octetos
 * de cada uno, ej: net_set_ip(192,168,1,50, 255,255,255,0,
 * 192,168,1,1, 192,168,1,1)). */
void net_set_ip(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3,
                 uint8_t mask0, uint8_t mask1, uint8_t mask2, uint8_t mask3,
                 uint8_t gw0, uint8_t gw1, uint8_t gw2, uint8_t gw3,
                 uint8_t dns0, uint8_t dns1, uint8_t dns2, uint8_t dns3);

int net_is_configured(void);

/* Hay que llamarla seguido (idealmente en cada vuelta del loop principal
 * de la GUI) para recibir y procesar paquetes entrantes: responde ARP,
 * responde ping (ICMP echo request) y registra las respuestas de ping
 * que nosotros mandamos. */
void net_poll(void);

/* Manda un ICMP Echo Request a la IP dada y espera (bloqueando, con
 * polling interno) hasta timeout_ms por la respuesta.
 * Devuelve el tiempo de ida y vuelta en milisegundos si hubo respuesta,
 * o -1 si hubo timeout / no se pudo resolver la MAC por ARP. */
int net_ping(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3, uint32_t timeout_ms);

/* Devuelve un puntero a string estatico con la IP configurada,
 * formato "a.b.c.d" (o "0.0.0.0" si no se configuro todavia). */
const char* net_get_ip_str(void);

/* Manda un datagrama UDP crudo a ip:dst_port, desde src_port.
 * Resuelve la MAC por ARP si hace falta (bloqueante, hasta 1s).
 * Devuelve 0 si OK, -1 si fallo (sin ruta / sin respuesta ARP). */
int net_udp_send(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3,
                  uint16_t src_port, uint16_t dst_port,
                  const uint8_t *payload, int len);

/* Resuelve un nombre de dominio (ej "example.com") a IPv4 usando el
 * servidor DNS configurado con net_set_ip(). Bloqueante con polling
 * interno hasta timeout_ms. Devuelve 0 si encontro una direccion
 * (la deja en out_ip), o -1 si no hubo respuesta / no hay DNS configurado. */
int net_dns_resolve(const char *hostname, uint8_t out_ip[4], uint32_t timeout_ms);

/* --------------- TCP --------------- */

/* Maximo de conexiones TCP simultaneas (una por ahora para el uso tipico). */
#define TCP_MAX_SOCKETS 4

typedef int tcp_sock_t; /* indice de socket, -1 = error */

/* Abre una conexion TCP a ip:port. Bloqueante hasta completar el
 * three-way handshake o timeout. Devuelve el socket fd (>=0) o -1. */
tcp_sock_t tcp_connect(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3,
                        uint16_t port, uint32_t timeout_ms);

/* Envia datos por un socket TCP abierto.
 * Devuelve los bytes enviados (puede ser < len si el buffer esta lleno)
 * o -1 si la conexion se cerro / hubo error. */
int tcp_send(tcp_sock_t sock, const uint8_t *data, int len);

/* Recibe datos disponibles en el socket (no bloqueante).
 * Devuelve bytes copiados a buf (0 si no hay nada), o -1 si se cerro. */
int tcp_recv(tcp_sock_t sock, uint8_t *buf, int maxlen);

/* Cierra la conexion TCP (manda FIN). */
void tcp_close(tcp_sock_t sock);

/* Conveniencia: descarga la URL http://host/path y escribe el cuerpo
 * en out (hasta maxlen bytes). Solo HTTP, sin HTTPS. Bloqueante.
 * Devuelve bytes escritos, o -1. */
int net_http_get(const char *host, const char *path, char *out, int maxlen);

#endif
