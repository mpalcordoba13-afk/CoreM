#ifndef USB_EHCI_H
#define USB_EHCI_H
#include "usb.h"
#include <stdint.h>

/* Inicializa el controlador EHCI (USB 2.0).
 * Llama DESPUÉS de pci_scan() y ANTES de usb_msd_init(). */
void usb_ehci_init(void);

/* 1 si se encontró y configuró un controlador EHCI */
int ehci_controller_present(void);

/* Transferencias (usadas internamente por usb_msd a través de usb.h) */
int ehci_control_transfer(usb_device_t *dev,
                           uint8_t bmRequestType, uint8_t bRequest,
                           uint16_t wValue, uint16_t wIndex, uint16_t wLength,
                           void *buf, int dir_in);

int ehci_bulk_transfer(usb_device_t *dev, uint8_t ep,
                        void *buf, int len, int dir_in);

#endif
