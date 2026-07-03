/*
 * rtl8139.c - Driver de la NIC Realtek RTL8139, por polling (sin IRQ,
 * mismo estilo que usb_uhci.c). Es la tarjeta que QEMU emula por
 * defecto, asi que sirve tanto para probar en QEMU como en hardware
 * real / otros emuladores que la soporten.
 *
 * Registros usados (offset desde el IO base, BAR0):
 *   0x00-0x05  IDR0-5     Direccion MAC (solo lectura)
 *   0x30       RBSTART    Direccion fisica del buffer de RX
 *   0x37       CR         Command Register
 *   0x38       CAPR       Current Address of Packet Read
 *   0x3C       IMR        Interrupt Mask Register
 *   0x3E       ISR        Interrupt Status Register
 *   0x40       TCR        Transmit Config Register
 *   0x44       RCR        Receive Config Register
 *   0x10/14/18/1C  TSD0-3 Transmit Status (uno por buffer de TX)
 *   0x20/24/28/2C  TSAD0-3 Transmit Start Address (uno por buffer de TX)
 */
#include "rtl8139.h"
#include "pci.h"
#include "timer.h"
#include <stdint.h>

#define RTL_VENDOR_ID 0x10EC
#define RTL_DEVICE_ID 0x8139

#define REG_IDR0    0x00
#define REG_TSD0    0x10
#define REG_TSAD0   0x20
#define REG_RBSTART 0x30
#define REG_CR      0x37
#define REG_CAPR    0x38
#define REG_IMR     0x3C
#define REG_ISR     0x3E
#define REG_TCR     0x40
#define REG_RCR     0x44
#define REG_CONFIG1 0x52

#define CR_BUFE 0x01
#define CR_TE   0x04
#define CR_RE   0x08
#define CR_RST  0x10

#define RX_BUF_LEN   8192
#define RX_BUF_PAD   (16 + 1500)
#define TX_BUF_COUNT 4
#define TX_BUF_SIZE  1792 /* >= MTU de 1514, alineado comodo */

static inline void outb(uint16_t p, uint8_t v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline void outw(uint16_t p, uint16_t v){ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }
static inline void outl_(uint16_t p, uint32_t v){ __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p)); }
static inline uint8_t  inb(uint16_t p){ uint8_t v;  __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline uint16_t inw_(uint16_t p){ uint16_t v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p)); return v; }

/* Buffers estaticos, alineados a 4 bytes (requisito del hardware). */
static uint8_t rx_buffer[RX_BUF_LEN + RX_BUF_PAD] __attribute__((aligned(4)));
static uint8_t tx_buffer[TX_BUF_COUNT][TX_BUF_SIZE] __attribute__((aligned(4)));

static uint16_t io_base = 0;
static int present = 0;
static uint32_t cur_rx = 0;
static int next_tx = 0;
static uint8_t mac[6];

static void delay_ms(uint32_t ms){
    uint32_t target = timer_ticks() + (ms/10) + 1;
    while (timer_ticks() < target) { __asm__ volatile("nop"); }
}

static int find_rtl_pci(pci_device_t *out){
    int n = pci_count();
    for (int i=0;i<n;i++){
        const pci_device_t *d = pci_get(i);
        if (d->vendor_id==RTL_VENDOR_ID && d->device_id==RTL_DEVICE_ID){
            *out = *d;
            return 1;
        }
    }
    return 0;
}

int rtl8139_init(void){
    present = 0;
    io_base = 0;
    cur_rx = 0;
    next_tx = 0;

    pci_device_t dev;
    if (!find_rtl_pci(&dev)) return 0; /* no hay tarjeta RTL8139 */

    pci_enable_device(&dev);

    uint32_t bar0 = pci_get_bar(&dev, 0);
    if (!(bar0 & 0x1)) return 0; /* esperamos un BAR de E/S */
    io_base = (uint16_t)(bar0 & 0xFFFC);

    /* Power on (despierta el chip si estaba en un estado de bajo consumo) */
    outb(io_base+REG_CONFIG1, 0x00);

    /* Software reset, esperamos a que el bit RST se limpie solo */
    outb(io_base+REG_CR, CR_RST);
    uint32_t deadline = timer_ticks() + 100;
    while ((inb(io_base+REG_CR) & CR_RST) && timer_ticks() < deadline) { /* espera */ }

    /* Buffer de recepcion: direccion fisica (identity-mapped, sin paging) */
    outl_(io_base+REG_RBSTART, (uint32_t)(uintptr_t)rx_buffer);

    /* Sin IRQs: este driver funciona por polling */
    outw(io_base+REG_IMR, 0x0000);

    /* RCR: aceptar broadcast + multicast + unicast a nuestra MAC,
     * y WRAP=1 (bit7) para que el hardware nunca parta un paquete al
     * llegar al final del buffer (por eso reservamos el padding extra). */
    outl_(io_base+REG_RCR, 0x0000008F);

    /* TCR: valores estandar (interframe gap normal) */
    outl_(io_base+REG_TCR, 0x03000000);

    /* Habilita RX y TX */
    outb(io_base+REG_CR, CR_RE | CR_TE);

    /* Lee la MAC grabada en la EEPROM/registros */
    for (int i=0;i<6;i++) mac[i] = inb(io_base+REG_IDR0+i);

    present = 1;
    delay_ms(5);
    return 1;
}

int rtl8139_present(void){ return present; }

void rtl8139_get_mac(uint8_t out_mac[6]){
    for (int i=0;i<6;i++) out_mac[i] = mac[i];
}

int rtl8139_send(const uint8_t *frame, int len){
    if (!present || !frame || len<=0 || len>TX_BUF_SIZE) return -1;

    int idx = next_tx;
    next_tx = (next_tx+1) % TX_BUF_COUNT;

    for (int i=0;i<len;i++) tx_buffer[idx][i] = frame[i];
    /* Ethernet exige un minimo de 60 bytes (sin contar el CRC, que pone
     * el hardware solo); si el frame es mas chico, rellenamos con ceros. */
    int padded_len = len < 60 ? 60 : len;
    for (int i=len;i<padded_len;i++) tx_buffer[idx][i] = 0;

    outl_(io_base+REG_TSAD0 + idx*4, (uint32_t)(uintptr_t)tx_buffer[idx]);
    outl_(io_base+REG_TSD0  + idx*4, (uint32_t)padded_len); /* dispara la transmision */

    /* Espera a que el bit TOK (0x8000) se prenda, o timeout */
    uint32_t deadline = timer_ticks() + 50;
    while (timer_ticks() < deadline){
        uint32_t tsd;
        __asm__ volatile("inl %1,%0":"=a"(tsd):"Nd"((uint16_t)(io_base+REG_TSD0+idx*4)));
        if (tsd & 0x8000) return 0; /* TOK */
        if (tsd & 0x40000000) return -1; /* TUN: underrun */
    }
    return -1; /* timeout */
}

int rtl8139_poll_recv(uint8_t *buf, int maxlen){
    if (!present) return 0;
    if (inb(io_base+REG_CR) & CR_BUFE) return 0; /* buffer vacio, nada nuevo */

    uint32_t offset = cur_rx % RX_BUF_LEN;
    uint8_t *p = rx_buffer + offset;

    uint16_t rx_status = (uint16_t)(p[0] | (p[1]<<8));
    uint16_t rx_size    = (uint16_t)(p[2] | (p[3]<<8)); /* incluye el CRC de 4 bytes */

    /* Defensa basica: si el tamano es absurdo (corrupcion / desync),
     * reseteamos el puntero de lectura en vez de quedar en loop. */
    if (!(rx_status & 0x0001) || rx_size < 4 || rx_size > 1600){
        cur_rx = 0;
        outw(io_base+REG_CAPR, (uint16_t)(cur_rx - 16));
        return 0;
    }

    int data_len = rx_size - 4; /* sin el CRC */
    int copy_len = data_len < maxlen ? data_len : maxlen;
    for (int i=0;i<copy_len;i++) buf[i] = p[4+i];

    cur_rx = (cur_rx + rx_size + 4 + 3) & ~3u;
    if (cur_rx > RX_BUF_LEN) cur_rx -= RX_BUF_LEN;
    outw(io_base+REG_CAPR, (uint16_t)(cur_rx - 16));

    return copy_len;
}

/* ---- NDIS Miniport export ---- */
#include "ndis.h"
static void rtl8139_halt(void){ /* polling, nada que hacer */ }
const ndis_miniport_t rtl8139_miniport = {
    "RTL8139",
    rtl8139_init,
    rtl8139_present,
    rtl8139_get_mac,
    rtl8139_send,
    rtl8139_poll_recv,
    rtl8139_halt
};
