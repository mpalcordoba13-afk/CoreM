#include "pci.h"
#include <stdint.h>

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

static pci_device_t devices[PCI_MAX_DEVICES];
static int dev_count = 0;

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint32_t inl(uint16_t port) {
    uint32_t val;
    __asm__ volatile ("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static uint32_t pci_addr(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return (uint32_t)(1 << 31)
         | ((uint32_t)bus  << 16)
         | ((uint32_t)slot << 11)
         | ((uint32_t)func << 8)
         | (offset & 0xFC);
}

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    outl(PCI_CONFIG_ADDR, pci_addr(bus, slot, func, offset));
    return inl(PCI_CONFIG_DATA);
}

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    outl(PCI_CONFIG_ADDR, pci_addr(bus, slot, func, offset));
    outl(PCI_CONFIG_DATA, val);
}

uint32_t pci_get_bar(const pci_device_t *d, int barnum) {
    if (!d || barnum < 0 || barnum > 5) return 0;
    uint8_t off = 0x10 + (uint8_t)(barnum * 4);
    return pci_config_read32(d->bus, d->slot, d->func, off);
}

void pci_enable_device(const pci_device_t *d) {
    if (!d) return;
    uint32_t cmd = pci_config_read32(d->bus, d->slot, d->func, 0x04);
    cmd |= 0x07; /* IO Space + Memory Space + Bus Master */
    pci_config_write32(d->bus, d->slot, d->func, 0x04, cmd);
}

static void scpy(char *d, const char *s, int max) {
    int i = 0; while (s[i] && i < max-1) { d[i]=s[i]; i++; } d[i]='\0';
}

static const char* vendor_name(uint16_t id) {
    switch(id) {
        case 0x8086: return "Intel";
        case 0x1022: return "AMD";
        case 0x10DE: return "NVIDIA";
        case 0x1234: return "QEMU";
        case 0x1AF4: return "VirtIO";
        case 0x1B36: return "QEMU/Red Hat";
        case 0x10EC: return "Realtek";
        case 0x104C: return "Texas Inst.";
        case 0x1106: return "VIA";
        default:     return "Desconocido";
    }
}

static const char* class_desc(uint8_t cls, uint8_t sub) {
    switch(cls) {
        case 0x00: return "No clasificado";
        case 0x01:
            if(sub==0x01) return "IDE Controller";
            if(sub==0x06) return "SATA Controller";
            return "Almacenamiento";
        case 0x02: return "Red Ethernet";
        case 0x03: return "Controlador VGA";
        case 0x04: return "Controlador Audio";
        case 0x06:
            if(sub==0x00) return "CPU Host Bridge";
            if(sub==0x01) return "PCI-ISA Bridge";
            if(sub==0x04) return "PCI-PCI Bridge";
            return "Bridge";
        case 0x0C:
            if(sub==0x03) return "USB Controller";
            if(sub==0x05) return "SMBus";
            return "Bus Serial";
        case 0x80: return "Dispositivo misc";
        default:   return "Otro";
    }
}

static void scan_function(uint8_t bus, uint8_t slot, uint8_t func) {
    if (dev_count >= PCI_MAX_DEVICES) return;

    uint32_t val = pci_config_read32(bus, slot, func, 0);
    uint16_t vendor = val & 0xFFFF;
    if (vendor == 0xFFFF) return;

    uint16_t device   = (val >> 16) & 0xFFFF;
    uint32_t class_reg = pci_config_read32(bus, slot, func, 8);
    uint8_t cls = (class_reg >> 24) & 0xFF;
    uint8_t sub = (class_reg >> 16) & 0xFF;
    uint8_t pif = (class_reg >>  8) & 0xFF;

    pci_device_t *d = &devices[dev_count++];
    d->bus      = bus;
    d->slot     = slot;
    d->func     = func;
    d->vendor_id= vendor;
    d->device_id= device;
    d->class_code = cls;
    d->subclass   = sub;
    d->prog_if    = pif;
    scpy(d->vendor_name, vendor_name(vendor), 20);
    scpy(d->device_desc, class_desc(cls, sub), 28);
}

void pci_scan(void) {
    dev_count = 0;
    /* Escanear 256 buses, 32 slots, 8 funciones — estándar PCI */
    for (int bus = 0; bus < 256 && dev_count < PCI_MAX_DEVICES; bus++) {
        for (int slot = 0; slot < 32 && dev_count < PCI_MAX_DEVICES; slot++) {
            /* Primero comprobar si func 0 existe */
            uint32_t val = pci_config_read32((uint8_t)bus, (uint8_t)slot, 0, 0);
            if ((val & 0xFFFF) == 0xFFFF) continue;

            scan_function((uint8_t)bus, (uint8_t)slot, 0);

            /* Comprobar si es multi-función (bit 7 del header type) */
            uint32_t hdr = pci_config_read32((uint8_t)bus, (uint8_t)slot, 0, 0x0C);
            uint8_t header_type = (hdr >> 16) & 0xFF;
            if (header_type & 0x80) {
                /* Dispositivo multi-función: escanear funciones 1-7 */
                for (int func = 1; func < 8 && dev_count < PCI_MAX_DEVICES; func++) {
                    scan_function((uint8_t)bus, (uint8_t)slot, (uint8_t)func);
                }
            }
        }
    }
}

int pci_count(void) { return dev_count; }
const pci_device_t* pci_get(int i) {
    if (i < 0 || i >= dev_count) return 0;
    return &devices[i];
}
