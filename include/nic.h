#ifndef NIC_H
#define NIC_H
#include <stdint.h>

/*
 * nic.h - Capa de abstraccion de NIC.
 *
 * Prueba en orden: RTL8139 (PCs muy viejas, QEMU default),
 * luego RTL8169/8168/8111 (PCs 2004-2020, el chip Realtek mas comun).
 *
 * El resto del codigo (net.c, dhcp.c, kernel.c) usa solo estas
 * funciones y no sabe que chip hay debajo.
 */

/* Detecta y configura la primera NIC compatible que encuentre.
 * Devuelve 1 si encontro alguna, 0 si no. */
int  nic_init(void);

/* 1 si nic_init encontro una tarjeta. */
int  nic_present(void);

/* Nombre del chip detectado ("RTL8139", "RTL8169", etc.) */
const char* nic_name(void);

/* Copia la MAC (6 bytes) en out_mac. */
void nic_get_mac(uint8_t out_mac[6]);

/* Envia un frame Ethernet crudo (header incluido). len <= 1514.
 * Devuelve 0 si OK, -1 si fallo. */
int  nic_send(const uint8_t *frame, int len);

/* Recibe el proximo frame disponible (polling, no bloqueante).
 * Devuelve longitud del frame, 0 si no habia nada. */
int  nic_poll_recv(uint8_t *buf, int maxlen);

#endif
