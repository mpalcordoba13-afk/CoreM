#ifndef USB_H
#define USB_H
#include <stdint.h>

#define USB_MAX_DEVICES 4

typedef struct {
    uint8_t  valid;          /* 1 si la entrada esta ocupada */
    uint8_t  address;        /* direccion USB asignada (1..127) */
    uint8_t  port;           /* puerto raiz UHCI (0 o 1) */
    uint8_t  low_speed;      /* 1 = low speed, 0 = full speed */
    uint8_t  max_packet0;    /* tamano maximo de paquete del EP0 */
    uint8_t  dev_class;
    uint8_t  dev_subclass;
    uint8_t  dev_protocol;
    uint16_t vendor_id;
    uint16_t product_id;

    /* Rellenado por el driver de clase Mass Storage (etapa 2) */
    uint8_t  is_mass_storage;
    uint8_t  msd_iface;
    uint8_t  msd_in_ep;
    uint8_t  msd_out_ep;
    uint8_t  msd_max_lun;
} usb_device_t;

/* Inicializa el/los controlador(es) UHCI encontrados por PCI,
 * resetea los puertos raiz y enumera los dispositivos conectados. */
void usb_init(void);

int  usb_device_count(void);
usb_device_t* usb_get_device(int i);

/* 1 si se encontro y se inicializo al menos un controlador UHCI */
int  usb_controller_present(void);

/* ---- Transferencias de bajo nivel, usadas por drivers de clase ---- */

/* Transferencia de control de 8 bytes de setup + datos opcionales.
 * dir_in: 1 = IN (el dispositivo envia datos al host), 0 = OUT.
 * Retorna 0 en exito, -1 en error/timeout. */
int usb_control_transfer(usb_device_t *dev,
                          uint8_t bmRequestType, uint8_t bRequest,
                          uint16_t wValue, uint16_t wIndex, uint16_t wLength,
                          void *buf, int dir_in);

/* Transferencia bulk sobre un endpoint ya conocido (etapa 2: Mass Storage).
 * ep: numero de endpoint (0..15). dir_in: 1=IN, 0=OUT.
 * Retorna bytes transferidos o -1 en error. */
int usb_bulk_transfer(usb_device_t *dev, uint8_t ep, void *buf, int len, int dir_in);

#endif
