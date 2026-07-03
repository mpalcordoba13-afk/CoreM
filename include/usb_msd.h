#ifndef USB_MSD_H
#define USB_MSD_H
#include <stdint.h>

/* Inicializa el driver Mass Storage buscando entre los dispositivos USB
 * ya enumerados por usb_init().  Llama DESPUÉS de usb_init(). */
void     usb_msd_init(void);

/* 1 si hay un USB Mass Storage funcional conectado */
int      usb_msd_present(void);

/* Número total de sectores de 512 bytes del dispositivo */
uint32_t usb_msd_sector_count(void);

/* Lee 'count' sectores de 512 bytes desde LBA 'lba' hacia 'buf'.
 * buf debe tener al menos count*512 bytes.
 * Retorna 0 en éxito, -1 en error. */
int      usb_msd_read_sectors(uint32_t lba, uint32_t count, void *buf);

#endif
