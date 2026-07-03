#ifndef PCI_H
#define PCI_H
#include <stdint.h>

#define PCI_MAX_DEVICES 64   /* aumentado para cubrir más dispositivos */

typedef struct {
    uint8_t  bus, slot, func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    char     vendor_name[20];
    char     device_desc[28];
} pci_device_t;

void pci_scan(void);
int  pci_count(void);
const pci_device_t* pci_get(int i);

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void     pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);
uint32_t pci_get_bar(const pci_device_t *d, int barnum);
void     pci_enable_device(const pci_device_t *d);

#endif
