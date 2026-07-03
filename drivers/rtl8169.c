/*
 * rtl8169.c - Driver polling para Realtek RTL8169 / RTL8168 / RTL8111.
 *
 * Cubre los chips mas comunes en PCs de escritorio y laptops 2004-2020:
 *   0x8169  RTL8169 (original Gigabit)
 *   0x8110  RTL8110S/SB/SC
 *   0x8168  RTL8168B/C/D/E/F (PCIe, muy comun en laptops)
 *   0x8111  RTL8111B-G (idem, mas reciente)
 *   0x8161  RTL8161 (variante)
 *   0x8136  RTL8101E/8102E (Fast Ethernet, chips de netbooks)
 *
 * Diferencias clave con RTL8139:
 *   - Usa anillos de descriptores DMA en vez de buffer circular.
 *   - Cada descriptor apunta a un buffer separado.
 *   - Soporta Gigabit (los 8168/8111) aunque no nos importa la
 *     velocidad en un OS hobby.
 *
 * Implementacion: polling (sin IRQ), 16 descriptores RX, 4 TX.
 * Todos los buffers son estaticos e identity-mapped (phys == virt).
 */
#include "rtl8169.h"
#include "pci.h"
#include "timer.h"
#include <stdint.h>

/* ---- IDs de dispositivo soportados -------------------------------- */
#define RTL_VENDOR 0x10EC
static const uint16_t RTL8169_IDS[] = {
    0x8169, 0x8110, 0x8168, 0x8111, 0x8161, 0x8136, 0
};

/* ---- Registros (offset desde IO base, BAR0 = IO espacio) ---------- */
#define R_IDR0      0x00   /* MAC address (6 bytes) */
#define R_MAR0      0x08   /* Multicast filter */
#define R_DTCCR     0x10   /* Dump Tally Counter */
#define R_TNPDS_LO  0x20   /* TX Normal Priority Descriptor Start, lo32 */
#define R_TNPDS_HI  0x24   /* TX Normal Priority Descriptor Start, hi32 */
#define R_RDSAR_LO  0xE4   /* RX Descriptor Start Address, lo32 */
#define R_RDSAR_HI  0xE8   /* RX Descriptor Start Address, hi32 */
#define R_CR        0x37   /* Command Register */
#define R_TPPOLL    0x38   /* TX Priority Polling */
#define R_IMR       0x3C   /* Interrupt Mask (16-bit) */
#define R_ISR       0x3E   /* Interrupt Status (16-bit) */
#define R_TCR       0x40   /* TX Config (32-bit) */
#define R_RCR       0x44   /* RX Config (32-bit) */
#define R_CR9346    0x50   /* EEPROM control (config mode unlock) */
#define R_CONFIG1   0x52
#define R_CONFIG2   0x53
#define R_PHYAR     0x60   /* PHY access */
#define R_RMS       0xDA   /* RX Max Packet Size (16-bit, 8169 only) */
#define R_MTPS      0xEC   /* Max TX Packet Size (8168 only) */

/* Bits del Command Register */
#define CR_RST  0x10
#define CR_RE   0x08
#define CR_TE   0x04

/* Bits de descriptor */
#define DESC_OWN  (1u<<31)  /* NIC posee el descriptor */
#define DESC_EOR  (1u<<30)  /* End of Ring */
#define DESC_FS   (1u<<29)  /* First Segment (TX) */
#define DESC_LS   (1u<<28)  /* Last Segment (TX) */

/* ---- Descriptores DMA (16 bytes, alineados a 256 bytes el array) -- */
#define RX_DESC_COUNT 16
#define TX_DESC_COUNT 4
#define RX_BUF_SIZE   1536   /* multiplo de 8, mayor que MTU */
#define TX_BUF_SIZE   1536

typedef struct {
    uint32_t status;  /* command/status */
    uint32_t vlan;    /* VLAN tag (ignorado) */
    uint32_t buf_lo;  /* direccion fisica del buffer, 32 bits bajos */
    uint32_t buf_hi;  /* 32 bits altos (siempre 0 en 32-bit) */
} __attribute__((packed)) rtl8169_desc_t;

static rtl8169_desc_t rx_ring[RX_DESC_COUNT] __attribute__((aligned(256)));
static rtl8169_desc_t tx_ring[TX_DESC_COUNT] __attribute__((aligned(256)));
static uint8_t rx_bufs[RX_DESC_COUNT][RX_BUF_SIZE] __attribute__((aligned(8)));
static uint8_t tx_bufs[TX_DESC_COUNT][TX_BUF_SIZE] __attribute__((aligned(8)));

static uint16_t io_base = 0;
static int      found   = 0;
static uint8_t  mac[6];
static int      cur_rx  = 0;
static int      cur_tx  = 0;

/* ---- IO helpers --------------------------------------------------- */
static inline void outb(uint16_t p, uint8_t v)  { __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline void outl_(uint16_t p, uint32_t v) { __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p)); }
static inline uint8_t  inb(uint16_t p)  { uint8_t  v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline uint16_t inw_(uint16_t p) { uint16_t v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline uint32_t inl_(uint16_t p) { uint32_t v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void outw_(uint16_t p, uint16_t v){ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }

static void delay_ms(uint32_t ms){
    uint32_t t = timer_ticks() + ms/10 + 1;
    while(timer_ticks() < t) __asm__ volatile("nop");
}

/* ---- Inicializar un descriptor RX --------------------------------- */
static void init_rx_desc(int i){
    rx_ring[i].buf_lo = (uint32_t)(uintptr_t)rx_bufs[i];
    rx_ring[i].buf_hi = 0;
    rx_ring[i].vlan   = 0;
    /* OWN=1 (NIC posee), EOR en el ultimo, tamanio del buffer */
    rx_ring[i].status = DESC_OWN | (i==RX_DESC_COUNT-1 ? DESC_EOR : 0) | RX_BUF_SIZE;
}

/* ---- Buscar el chip en el bus PCI --------------------------------- */
static int find_chip(pci_device_t *out){
    int n = pci_count();
    for(int i=0;i<n;i++){
        const pci_device_t *d = pci_get(i);
        if(d->vendor_id != RTL_VENDOR) continue;
        for(int j=0; RTL8169_IDS[j]; j++){
            if(d->device_id == RTL8169_IDS[j]){ *out = *d; return 1; }
        }
    }
    return 0;
}

/* ---- API publica -------------------------------------------------- */
int rtl8169_init(void){
    found = 0; io_base = 0; cur_rx = 0; cur_tx = 0;

    pci_device_t dev;
    if(!find_chip(&dev)) return 0;

    pci_enable_device(&dev);

    /* BAR0 = IO space (bit0=1) */
    uint32_t bar0 = pci_get_bar(&dev, 0);
    if(!(bar0 & 1)){ /* si BAR0 es MMIO, intentar BAR2 */
        bar0 = pci_get_bar(&dev, 2);
        if(!(bar0 & 1)) return 0;
    }
    io_base = (uint16_t)(bar0 & 0xFFFC);

    /* Desbloquear registros de configuracion */
    outb(io_base+R_CR9346, 0xC0);

    /* Software reset */
    outb(io_base+R_CR, CR_RST);
    uint32_t deadline = timer_ticks() + 100;
    while((inb(io_base+R_CR) & CR_RST) && timer_ticks() < deadline);

    /* Leer MAC */
    for(int i=0;i<6;i++) mac[i] = inb(io_base+R_IDR0+i);

    /* Inicializar descriptores RX */
    for(int i=0;i<RX_DESC_COUNT;i++) init_rx_desc(i);

    /* Inicializar descriptores TX (driver los posee al inicio) */
    for(int i=0;i<TX_DESC_COUNT;i++){
        tx_ring[i].status = (i==TX_DESC_COUNT-1) ? DESC_EOR : 0;
        tx_ring[i].buf_lo = (uint32_t)(uintptr_t)tx_bufs[i];
        tx_ring[i].buf_hi = 0;
        tx_ring[i].vlan   = 0;
    }

    /* Configurar anillos DMA */
    outl_(io_base+R_TNPDS_LO, (uint32_t)(uintptr_t)tx_ring);
    outl_(io_base+R_TNPDS_HI, 0);
    outl_(io_base+R_RDSAR_LO, (uint32_t)(uintptr_t)rx_ring);
    outl_(io_base+R_RDSAR_HI, 0);

    /* Max RX packet size: registros difieren segun chip */
    if(dev.device_id == 0x8136 || dev.device_id == 0x8168 || dev.device_id == 0x8111){
        outb(io_base+R_MTPS, 0x3B); /* max TX packet size para 8168 */
        outw_(io_base+R_RMS, 0x05F3);
    } else {
        outw_(io_base+R_RMS, 0x05F3); /* 1524 bytes max RX */
    }

    /* Sin interrupciones (polling) */
    outw_(io_base+R_IMR, 0x0000);
    /* Limpiar ISR */
    outw_(io_base+R_ISR, 0xFFFF);

    /* TCR: DMA burst maximo (1024 bytes), IFG normal */
    outl_(io_base+R_TCR, 0x03000700);

    /* RCR: aceptar broadcast + multicast + unicast propia,
     *      sin promiscuo, DMA burst 1024, sin FIFO threshold */
    outl_(io_base+R_RCR, 0x0000E70F);

    /* Regresar chip a modo normal */
    outb(io_base+R_CR9346, 0x00);

    /* Habilitar RX y TX */
    outb(io_base+R_CR, CR_RE | CR_TE);

    delay_ms(10); /* esperar que el PHY negocie */

    found = 1;
    return 1;
}

int rtl8169_present(void){ return found; }

void rtl8169_get_mac(uint8_t out_mac[6]){
    for(int i=0;i<6;i++) out_mac[i] = mac[i];
}

int rtl8169_send(const uint8_t *frame, int len){
    if(!found || !frame || len<=0 || len>TX_BUF_SIZE) return -1;

    int idx = cur_tx;
    cur_tx = (cur_tx+1) % TX_DESC_COUNT;

    /* Esperar a que el descriptor este libre (driver lo posee) */
    uint32_t deadline = timer_ticks() + 50;
    while((tx_ring[idx].status & DESC_OWN) && timer_ticks() < deadline)
        __asm__ volatile("nop");
    if(tx_ring[idx].status & DESC_OWN) return -1; /* timeout */

    /* Copiar frame al buffer TX, rellenar a 60 bytes minimo */
    int padded = len < 60 ? 60 : len;
    for(int i=0;i<len;i++) tx_bufs[idx][i] = frame[i];
    for(int i=len;i<padded;i++) tx_bufs[idx][i] = 0;

    /* Entregar descriptor al NIC */
    uint32_t eor = (idx == TX_DESC_COUNT-1) ? DESC_EOR : 0;
    tx_ring[idx].status = DESC_OWN | DESC_FS | DESC_LS | eor | (uint32_t)padded;

    /* Notificar al NIC (NPQ bit en TPPOLL) */
    outb(io_base+R_TPPOLL, 0x40);

    /* Esperar TOK */
    deadline = timer_ticks() + 50;
    while((tx_ring[idx].status & DESC_OWN) && timer_ticks() < deadline)
        __asm__ volatile("nop");

    return (tx_ring[idx].status & DESC_OWN) ? -1 : 0;
}

int rtl8169_poll_recv(uint8_t *buf, int maxlen){
    if(!found) return 0;

    rtl8169_desc_t *desc = &rx_ring[cur_rx];
    /* Si NIC sigue siendo duenio, no hay nada nuevo */
    if(desc->status & DESC_OWN) return 0;

    uint32_t status = desc->status;
    /* Chequear errores basicos (bit 21 = RES = error de recepcion) */
    if(status & (1u<<21)){
        /* Frame con error: devolver descriptor al NIC */
        init_rx_desc(cur_rx);
        cur_rx = (cur_rx+1) % RX_DESC_COUNT;
        return 0;
    }

    /* Longitud del frame (bits 13:0), incluye el CRC de 4 bytes */
    int frame_len = (int)(status & 0x3FFF) - 4;
    if(frame_len <= 0 || frame_len > RX_BUF_SIZE){
        init_rx_desc(cur_rx);
        cur_rx = (cur_rx+1) % RX_DESC_COUNT;
        return 0;
    }

    int copy = frame_len < maxlen ? frame_len : maxlen;
    for(int i=0;i<copy;i++) buf[i] = rx_bufs[cur_rx][i];

    /* Devolver descriptor al NIC */
    init_rx_desc(cur_rx);
    cur_rx = (cur_rx+1) % RX_DESC_COUNT;

    return copy;
}

/* ---- NDIS Miniport export ---- */
#include "ndis.h"
static void rtl8169_halt(void){ if(io_base) outb(io_base+R_CR, 0x00); }
const ndis_miniport_t rtl8169_miniport = {
    "RTL8169/8168",
    rtl8169_init,
    rtl8169_present,
    rtl8169_get_mac,
    rtl8169_send,
    rtl8169_poll_recv,
    rtl8169_halt
};
