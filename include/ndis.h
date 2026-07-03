#ifndef NDIS_H
#define NDIS_H
#include <stdint.h>

/*
 * ndis.h - Network Driver Interface Specification (simplificado) para MyOS.
 *
 * Inspirado en NDIS de Windows y netdev_ops de Linux. Separa la logica
 * de red (net.c, dhcp.c, tcp.c) de los drivers de hardware (rtl8139,
 * rtl8169, e1000, etc.).
 *
 * Para agregar una nueva NIC:
 *   1. Implementar las funciones del miniport en un .c nuevo.
 *   2. Declarar un ndis_miniport_t con esas funciones.
 *   3. Llamar ndis_register(&mi_miniport) en kernel.c.
 *   4. Listo. net.c no se toca.
 *
 * Arquitectura:
 *
 *   net.c / dhcp.c / tcp.c          <- Protocol drivers
 *        |  ndis_send / ndis_recv
 *   [ NDIS Core ]                   <- ndis.c
 *        |  init / send / recv
 *   rtl8139  rtl8169  e1000 ...     <- Miniport drivers
 */

/* ---- Miniport driver interface ---- */
/*
 * Cada driver de NIC declara una estructura ndis_miniport_t con sus
 * implementaciones. Solo init() y las funciones de I/O son obligatorias;
 * halt() es opcional (puede ser NULL).
 */
typedef struct {
    /* Nombre del driver/chip, ej "RTL8139", "RTL8169/8168" */
    const char *name;

    /* init(): detecta el hardware por PCI, lo inicializa y lo deja listo.
     *   Devuelve 1 si encontro el chip, 0 si no.
     *   NDIS llama a init() de cada miniport registrado hasta que uno
     *   devuelva 1. El primer exito "gana" y queda como el adaptador activo. */
    int  (*init)(void);

    /* present(): devuelve 1 si init() tuvo exito y el chip esta activo. */
    int  (*present)(void);

    /* get_mac(): llena out_mac[6] con la MAC del adaptador. */
    void (*get_mac)(uint8_t out_mac[6]);

    /* send(): envia un frame Ethernet crudo (con header).
     *   len debe ser <= 1514 bytes.
     *   Devuelve 0 si OK, -1 si fallo. */
    int  (*send)(const uint8_t *frame, int len);

    /* poll_recv(): modo polling — copia el proximo frame entrante a buf.
     *   Devuelve la longitud del frame, o 0 si no habia nada nuevo.
     *   No bloqueante. */
    int  (*poll_recv)(uint8_t *buf, int maxlen);

    /* halt(): apaga el hardware limpiamente (puede ser NULL). */
    void (*halt)(void);
} ndis_miniport_t;

/* ---- NDIS Core API ---- */

/* Registra un miniport driver. Hay que llamar esta funcion antes de
 * ndis_init(). El orden de registro determina la prioridad de probe:
 * el primero que devuelva init()==1 es el que se usa. */
void ndis_register(const ndis_miniport_t *mp);

/* Prueba todos los miniports registrados en orden.
 * Activa el primero que encuentre hardware compatible.
 * Devuelve 1 si encontro alguna NIC, 0 si no. */
int  ndis_init(void);

/* 1 si ndis_init() encontro y configuro una NIC. */
int  ndis_present(void);

/* Nombre del miniport activo, ej "RTL8169/8168". */
const char* ndis_adapter_name(void);

/* MAC del adaptador activo (6 bytes). */
void ndis_get_mac(uint8_t out_mac[6]);

/* Envia un frame Ethernet via el adaptador activo.
 * Devuelve 0 si OK, -1 si no hay NIC o fallo. */
int  ndis_send(const uint8_t *frame, int len);

/* Polling de recepcion. Devuelve longitud del frame o 0 si no hay nada. */
int  ndis_poll_recv(uint8_t *buf, int maxlen);

/* Estadisticas basicas (incrementadas por el core) */
typedef struct {
    uint32_t tx_frames;
    uint32_t rx_frames;
    uint32_t tx_errors;
    uint32_t rx_errors;
    uint32_t tx_bytes;
    uint32_t rx_bytes;
} ndis_stats_t;

const ndis_stats_t* ndis_get_stats(void);
void                ndis_reset_stats(void);

#endif
