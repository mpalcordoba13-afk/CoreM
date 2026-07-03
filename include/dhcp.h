#ifndef DHCP_H
#define DHCP_H
#include <stdint.h>

/*
 * dhcp.h - Cliente DHCP minimo (DISCOVER -> OFFER -> REQUEST -> ACK).
 *
 * Configura automaticamente IP, mascara, gateway y DNS usando
 * net_set_ip() internamente. Solo hay que llamar dhcp_request() una
 * vez al arrancar (bloqueante, con polling, hasta timeout_ms).
 *
 * Sin renovacion de lease (el lease es de 24h en la mayoria de routers;
 * para un OS de sesion corta no hace falta mas).
 */

/* Intenta obtener configuracion de red por DHCP.
 * Devuelve 1 si consiguio IP, 0 si no hubo respuesta en timeout_ms. */
int dhcp_request(uint32_t timeout_ms);

/* Devuelve la IP obtenida por DHCP como string "a.b.c.d",
 * o "0.0.0.0" si aun no se configuro. */
const char* dhcp_get_ip(void);

#endif
