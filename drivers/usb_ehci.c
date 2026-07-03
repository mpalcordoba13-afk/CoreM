/*
 * usb_ehci.c  –  Driver EHCI (USB 2.0 High-Speed) mínimo para MyOS
 *
 * EHCI = Enhanced Host Controller Interface (prog-if 0x20, clase 0x0C sub 0x03)
 * Soportado por QEMU con: -device usb-ehci,id=ehci
 *
 * Implementa:
 *   - Detección del controlador EHCI vía PCI
 *   - Inicialización y configuración de MMIO
 *   - Async Schedule (Queue Heads + Transfer Descriptors) para control y bulk
 *   - Enumeración de dispositivos (SET_ADDRESS + GET_DESCRIPTOR)
 *   - Compatible con usb_control_transfer() y usb_bulk_transfer() de usb_uhci
 *
 * Restricciones:
 *   - Sin malloc: estructuras estáticas alineadas
 *   - Sin IRQ: polling puro
 *   - Solo porta Root Hub directa (no Transaction Translators para FS/LS)
 */

#include "usb_ehci.h"
#include "usb.h"
#include "pci.h"
#include "timer.h"
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Registros EHCI (MMIO, base = BAR0)                                  */
/* ------------------------------------------------------------------ */

/* Capability Registers (offset desde MMIO base) */
#define EHCI_CAPLENGTH   0x00   /* byte: longitud de capability regs   */
#define EHCI_HCIVERSION  0x02   /* word: versión HCI                   */
#define EHCI_HCSPARAMS   0x04   /* dword: structural params            */
#define EHCI_HCCPARAMS   0x08   /* dword: capability params            */

/* Operational Registers (offset desde MMIO base + CAPLENGTH) */
#define EHCI_USBCMD      0x00
#define EHCI_USBSTS      0x04
#define EHCI_USBINTR     0x08
#define EHCI_FRINDEX     0x0C
#define EHCI_CTRLDSSEG   0x10
#define EHCI_PERIODICLISTBASE 0x14
#define EHCI_ASYNCLISTADDR   0x18
#define EHCI_CONFIGFLAG  0x40
#define EHCI_PORTSC(n)   (0x44 + (n)*4)

/* USBCMD bits */
#define CMD_RS      (1u<<0)   /* Run/Stop          */
#define CMD_HCRESET (1u<<1)   /* Host Controller Reset */
#define CMD_FLS_1K  (0u<<2)   /* Frame List Size: 1024 */
#define CMD_PSE     (1u<<4)   /* Periodic Schedule Enable */
#define CMD_ASE     (1u<<5)   /* Async Schedule Enable */
#define CMD_IOAAD   (1u<<6)   /* Interrupt on Async Advance Doorbell */
#define CMD_ITC(n)  ((n)<<16) /* Interrupt Threshold Control */

/* USBSTS bits */
#define STS_USBINT  (1u<<0)
#define STS_USBERR  (1u<<1)
#define STS_PCD     (1u<<2)   /* Port Change Detect */
#define STS_FLR     (1u<<3)
#define STS_HSE     (1u<<4)   /* Host System Error */
#define STS_IAA     (1u<<5)   /* Interrupt on Async Advance */
#define STS_HCHALTED (1u<<12)
#define STS_RECLAMATION (1u<<13)
#define STS_PSS     (1u<<14)  /* Periodic Schedule Status */
#define STS_ASS     (1u<<15)  /* Async Schedule Status */

/* PORTSC bits */
#define PORT_CCS    (1u<<0)   /* Current Connect Status */
#define PORT_CSC    (1u<<1)   /* Connect Status Change  */
#define PORT_PE     (1u<<2)   /* Port Enable            */
#define PORT_PEC    (1u<<3)   /* Port Enable Change     */
#define PORT_OCA    (1u<<4)
#define PORT_OCC    (1u<<5)
#define PORT_PR     (1u<<8)   /* Port Reset             */
#define PORT_SUSP   (1u<<7)
#define PORT_LSDA   (1u<<9)   /* Line Status bit D-      */
#define PORT_PP     (1u<<12)  /* Port Power             */
#define PORT_OWNER  (1u<<13)  /* Port Owner (0=EHCI)    */
#define PORT_SPEED_MASK (3u<<26)  /* en xHCI, en EHCI no aplica */

/* ------------------------------------------------------------------ */
/* QH (Queue Head) y qTD (queue Transfer Descriptor) — 32 bytes c/u   */
/* ------------------------------------------------------------------ */

typedef struct __attribute__((packed, aligned(32))) {
    volatile uint32_t next_qtd;       /* Physical pointer to next qTD */
    volatile uint32_t alt_next_qtd;   /* Alt next (used for short packets) */
    volatile uint32_t token;          /* Status, PID, toggle, length   */
    volatile uint32_t buf[5];         /* Buffer page pointers          */
} ehci_qtd_t;

/* token bits */
#define QTD_PING        (1u<<0)
#define QTD_SPLIT_XSTATE (1u<<1)
#define QTD_MISS_FRAME  (1u<<2)
#define QTD_XACT_ERR   (1u<<3)
#define QTD_BABBLE     (1u<<4)
#define QTD_DATA_BUF_ERR (1u<<5)
#define QTD_HALTED     (1u<<6)
#define QTD_ACTIVE     (1u<<7)
#define QTD_PID_OUT    (0u<<8)
#define QTD_PID_IN     (1u<<8)
#define QTD_PID_SETUP  (2u<<8)
#define QTD_CERR(n)    ((n)<<10)
#define QTD_C_PAGE(n)  ((n)<<12)
#define QTD_IOC        (1u<<15)  /* Interrupt on Complete */
#define QTD_BYTES(n)   ((n)<<16)
#define QTD_TOGGLE     (1u<<31)
#define QTD_TERMINATE  0x1u

#define QTD_ERR_MASK   (QTD_HALTED|QTD_BABBLE|QTD_XACT_ERR|QTD_DATA_BUF_ERR)

typedef struct __attribute__((packed, aligned(32))) {
    volatile uint32_t horiz_link;     /* Horizontal link to next QH    */
    volatile uint32_t ep_chars;       /* Endpoint characteristics      */
    volatile uint32_t ep_caps;        /* Endpoint capabilities         */
    volatile uint32_t cur_qtd;        /* Current qTD pointer           */
    /* Transfer overlay (mirrors qTD) */
    volatile uint32_t next_qtd;
    volatile uint32_t alt_next_qtd;
    volatile uint32_t token;
    volatile uint32_t buf[5];
} ehci_qh_t;

/* horiz_link bits */
#define QH_TYPE_ITD   0u
#define QH_TYPE_QH    2u
#define QH_TERMINATE  1u

/* ep_chars bits */
#define QH_DEVADDR(a)  ((a)&0x7F)
#define QH_INACT       (1u<<7)
#define QH_ENDPT(e)    (((e)&0xF)<<8)
#define QH_EPS_HS      (2u<<12)   /* High Speed */
#define QH_EPS_FS      (0u<<12)   /* Full Speed */
#define QH_EPS_LS      (1u<<12)   /* Low Speed  */
#define QH_DTC         (1u<<14)   /* Data Toggle Control (from overlay) */
#define QH_H           (1u<<15)   /* Head of Reclamation List */
#define QH_MAXPKT(n)   (((n)&0x7FF)<<16)
#define QH_C           (1u<<27)   /* Control endpoint flag (FS/LS non-isoc) */
#define QH_RL(n)       (((n)&0xF)<<28)

/* ep_caps bits */
#define QH_SMASK(n)    ((n)&0xFF)
#define QH_CMASK(n)    (((n)&0xFF)<<8)
#define QH_HUBADDR(a)  (((a)&0x7F)<<16)
#define QH_HUBPORT(p)  (((p)&0x7F)<<23)
#define QH_MULT(n)     (((n)&0x3)<<30)

/* ------------------------------------------------------------------ */
/* Estructuras estáticas (sin malloc)                                   */
/* ------------------------------------------------------------------ */
#define QTD_POOL_SIZE  32
#define QH_POOL_SIZE   4
#define PERIODIC_SIZE  1024

static uint32_t   periodic_list[PERIODIC_SIZE] __attribute__((aligned(4096)));
static ehci_qh_t  async_head  __attribute__((aligned(32)));   /* head QH */
static ehci_qh_t  qh_pool[QH_POOL_SIZE] __attribute__((aligned(32)));
static ehci_qtd_t qtd_pool[QTD_POOL_SIZE] __attribute__((aligned(32)));

/* ------------------------------------------------------------------ */
/* Estado global                                                        */
/* ------------------------------------------------------------------ */
static volatile uint8_t *ehci_cap  = 0;   /* capability registers */
static volatile uint8_t *ehci_op   = 0;   /* operational registers */
static int    ehci_nports           = 0;
static int    ehci_ok               = 0;

/* Dispositivos USB encontrados (compartidos con UHCI via usb.h) */
extern usb_device_t usb_devices[];   /* definido en usb_uhci.c */
extern int          usb_dev_count;

/* ------------------------------------------------------------------ */
/* Acceso MMIO                                                          */
/* ------------------------------------------------------------------ */
static inline uint32_t op_read(uint32_t off){
    return *(volatile uint32_t*)(ehci_op + off);
}
static inline void op_write(uint32_t off, uint32_t val){
    *(volatile uint32_t*)(ehci_op + off) = val;
}

/* ------------------------------------------------------------------ */
/* Delay helpers                                                        */
/* ------------------------------------------------------------------ */
static void ehci_delay_ms(uint32_t ms){
    uint32_t t = timer_ticks() + (ms/10) + 1;
    while(timer_ticks() < t) __asm__ volatile("nop");
}

/* ------------------------------------------------------------------ */
/* Pool de qTDs                                                         */
/* ------------------------------------------------------------------ */
static int qtd_next_free = 0;

static ehci_qtd_t* qtd_alloc(int n){
    /* bump allocator, se reinicia circularmente */
    if (qtd_next_free + n > QTD_POOL_SIZE) qtd_next_free = 0;
    ehci_qtd_t *q = &qtd_pool[qtd_next_free];
    qtd_next_free += n;
    return q;
}

/* ------------------------------------------------------------------ */
/* Ejecutar transferencia sobre un QH                                   */
/* ------------------------------------------------------------------ */
static int ehci_run_qh(ehci_qh_t *qh, ehci_qtd_t *first_qtd,
                        ehci_qtd_t *last_qtd){
    /* Enganchamos el QH a la cabeza del async schedule */
    qh->horiz_link   = ((uint32_t)(uintptr_t)&async_head) | QH_TYPE_QH;
    async_head.horiz_link = ((uint32_t)(uintptr_t)qh) | QH_TYPE_QH;

    /* Apuntar el overlay al primer qTD */
    qh->next_qtd     = (uint32_t)(uintptr_t)first_qtd;
    qh->alt_next_qtd = QTD_TERMINATE;
    qh->token        = 0;   /* limpiar status anterior */

    /* Esperar a que el último qTD se complete o timeout (1s) */
    uint32_t deadline = timer_ticks() + 100;
    while(timer_ticks() < deadline){
        uint32_t tok = last_qtd->token;
        if(!(tok & QTD_ACTIVE)) break;
    }

    /* Desenganchar: async_head vuelve a apuntarse a sí mismo */
    async_head.horiz_link = ((uint32_t)(uintptr_t)&async_head) | QH_TYPE_QH;

    /* Revisar errores */
    for(ehci_qtd_t *q = first_qtd; ; ){
        uint32_t tok = q->token;
        if(tok & QTD_ACTIVE)  return -1;   /* timeout */
        if(tok & QTD_ERR_MASK) return -1;
        if((uint32_t)(uintptr_t)q == (uint32_t)(uintptr_t)last_qtd) break;
        uint32_t nxt = q->next_qtd & ~0x1Fu;
        if(!nxt || (q->next_qtd & QTD_TERMINATE)) break;
        q = (ehci_qtd_t*)(uintptr_t)nxt;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Control transfer (SETUP + DATA* + STATUS)                           */
/* ------------------------------------------------------------------ */
int ehci_control_transfer(usb_device_t *dev,
                           uint8_t bmRequestType, uint8_t bRequest,
                           uint16_t wValue, uint16_t wIndex, uint16_t wLength,
                           void *buf, int dir_in){
    if(!ehci_ok || !dev) return -1;

    static uint8_t setup_pkt[8];
    setup_pkt[0]=bmRequestType; setup_pkt[1]=bRequest;
    setup_pkt[2]=(uint8_t)(wValue&0xFF); setup_pkt[3]=(uint8_t)(wValue>>8);
    setup_pkt[4]=(uint8_t)(wIndex&0xFF); setup_pkt[5]=(uint8_t)(wIndex>>8);
    setup_pkt[6]=(uint8_t)(wLength&0xFF);setup_pkt[7]=(uint8_t)(wLength>>8);

    int mp = dev->max_packet0 ? dev->max_packet0 : 64;
    int n_data = wLength > 0 ? ((wLength + mp - 1) / mp) : 0;
    int n_qtd  = 1 + n_data + 1;   /* SETUP + DATA* + STATUS */
    if(n_qtd > QTD_POOL_SIZE) return -1;

    ehci_qtd_t *tds = qtd_alloc(n_qtd);
    int ti = 0;

    /* SETUP stage (toggle=0) */
    tds[ti].next_qtd     = QTD_TERMINATE;
    tds[ti].alt_next_qtd = QTD_TERMINATE;
    tds[ti].token = QTD_ACTIVE | QTD_PID_SETUP | QTD_CERR(3) | QTD_BYTES(8);
    tds[ti].buf[0] = (uint32_t)(uintptr_t)setup_pkt;
    for(int k=1;k<5;k++) tds[ti].buf[k]=0;
    ti++;

    /* DATA stage */
    uint8_t *p = (uint8_t*)buf;
    int rem = (int)wLength;
    uint32_t tog = QTD_TOGGLE;   /* DATA1 para el primer paquete de datos */
    while(rem>0 && ti<n_qtd-1){
        int chunk = rem>mp?mp:rem;
        tds[ti].next_qtd     = QTD_TERMINATE;
        tds[ti].alt_next_qtd = QTD_TERMINATE;
        tds[ti].token = QTD_ACTIVE | (dir_in?QTD_PID_IN:QTD_PID_OUT)
                       | QTD_CERR(3) | QTD_BYTES(chunk) | tog;
        tds[ti].buf[0] = (uint32_t)(uintptr_t)p;
        for(int k=1;k<5;k++) tds[ti].buf[k]=0;
        p+=chunk; rem-=chunk;
        tog ^= QTD_TOGGLE;
        ti++;
    }

    /* STATUS stage (toggle=1, dirección opuesta) */
    uint32_t stat_pid = (wLength>0 && dir_in) ? QTD_PID_OUT : QTD_PID_IN;
    tds[ti].next_qtd     = QTD_TERMINATE;
    tds[ti].alt_next_qtd = QTD_TERMINATE;
    tds[ti].token = QTD_ACTIVE | stat_pid | QTD_CERR(3) | QTD_TOGGLE | QTD_IOC;
    tds[ti].buf[0]=0; for(int k=1;k<5;k++) tds[ti].buf[k]=0;

    /* Encadenar qTDs */
    for(int k=0;k<ti;k++)
        tds[k].next_qtd = (uint32_t)(uintptr_t)&tds[k+1];

    /* Configurar QH */
    ehci_qh_t *qh = &qh_pool[0];
    uint32_t eps = dev->low_speed ? QH_EPS_LS : QH_EPS_HS;
    qh->ep_chars = QH_DEVADDR(dev->address) | QH_ENDPT(0)
                 | eps | QH_DTC | QH_MAXPKT(mp) | QH_RL(8);
    qh->ep_caps  = QH_MULT(1);
    qh->cur_qtd  = 0;

    return ehci_run_qh(qh, &tds[0], &tds[ti]);
}

/* ------------------------------------------------------------------ */
/* Bulk transfer                                                        */
/* ------------------------------------------------------------------ */
int ehci_bulk_transfer(usb_device_t *dev, uint8_t ep,
                        void *buf, int len, int dir_in){
    if(!ehci_ok || !dev || len<=0) return -1;

    int mp = 512;  /* high-speed bulk max packet = 512 bytes */
    int n_qtd = (len + mp - 1) / mp + 1;
    if(n_qtd > QTD_POOL_SIZE) n_qtd = QTD_POOL_SIZE;

    ehci_qtd_t *tds = qtd_alloc(n_qtd);

    static uint8_t bulk_toggle_in  = 0;
    static uint8_t bulk_toggle_out = 0;
    uint8_t *tog_ref = dir_in ? &bulk_toggle_in : &bulk_toggle_out;

    uint8_t *p = (uint8_t*)buf;
    int rem = len, ti = 0;
    while(rem>0 && ti<n_qtd){
        int chunk = rem>mp?mp:rem;
        tds[ti].next_qtd     = QTD_TERMINATE;
        tds[ti].alt_next_qtd = QTD_TERMINATE;
        tds[ti].token = QTD_ACTIVE | (dir_in?QTD_PID_IN:QTD_PID_OUT)
                       | QTD_CERR(3) | QTD_BYTES(chunk)
                       | (*tog_ref ? QTD_TOGGLE : 0);
        tds[ti].buf[0] = (uint32_t)(uintptr_t)p;
        for(int k=1;k<5;k++) tds[ti].buf[k]=0;
        *tog_ref ^= 1;
        p+=chunk; rem-=chunk; ti++;
    }
    if(ti>0) tds[ti-1].token |= QTD_IOC;
    for(int k=0;k<ti-1;k++)
        tds[k].next_qtd = (uint32_t)(uintptr_t)&tds[k+1];

    ehci_qh_t *qh = &qh_pool[1];
    qh->ep_chars = QH_DEVADDR(dev->address) | QH_ENDPT(ep)
                 | QH_EPS_HS | QH_MAXPKT(mp) | QH_RL(8);
    qh->ep_caps  = QH_MULT(1);
    qh->cur_qtd  = 0;

    int r = ehci_run_qh(qh, &tds[0], &tds[ti-1]);

    /* Calcular bytes transferidos */
    if(r!=0) return -1;
    int total = 0;
    for(int k=0;k<ti;k++){
        int remaining = (tds[k].token >> 16) & 0x7FFF;
        int chunk = (k==0) ? (len<mp?len:mp) : mp;
        total += chunk - remaining;
    }
    return total;
}

/* ------------------------------------------------------------------ */
/* Enumeración de puerto                                                */
/* ------------------------------------------------------------------ */
static void enumerate_ehci_port(int port){
    if(usb_dev_count >= USB_MAX_DEVICES) return;

    uint32_t sc = op_read(EHCI_PORTSC(port));
    if(!(sc & PORT_CCS)) return;

    /* Verificar que no está en manos de companion controller */
    if(sc & PORT_OWNER) {
        /* Tomar posesión del puerto */
        op_write(EHCI_PORTSC(port), sc & ~PORT_OWNER);
        ehci_delay_ms(50);
        sc = op_read(EHCI_PORTSC(port));
        if(!(sc & PORT_CCS)) return;
    }

    /* Reset del puerto */
    op_write(EHCI_PORTSC(port), (sc & ~PORT_PE) | PORT_PR);
    ehci_delay_ms(50);
    sc = op_read(EHCI_PORTSC(port));
    op_write(EHCI_PORTSC(port), sc & ~PORT_PR);
    ehci_delay_ms(10);

    sc = op_read(EHCI_PORTSC(port));
    if(!(sc & PORT_PE)){
        /* Puerto no habilitado: es FS/LS, cederlo al companion OHCI/UHCI */
        op_write(EHCI_PORTSC(port), sc | PORT_OWNER);
        return;
    }

    /* Limpiar bits de cambio */
    op_write(EHCI_PORTSC(port), sc | PORT_CSC | PORT_PEC);

    /* El dispositivo está en high-speed */
    usb_device_t tmp = {0};
    tmp.valid=1; tmp.address=0; tmp.port=(uint8_t)port;
    tmp.low_speed=0; tmp.max_packet0=64;

    /* GET_DESCRIPTOR Device (8 bytes) en dirección 0 */
    uint8_t desc[18]={0};
    if(ehci_control_transfer(&tmp, 0x80, 0x06, (1<<8)|0, 0, 8, desc, 1)!=0) return;
    if(desc[7]) tmp.max_packet0=desc[7];

    /* SET_ADDRESS */
    uint8_t new_addr=(uint8_t)(usb_dev_count+1);
    if(ehci_control_transfer(&tmp, 0x00, 0x05, new_addr, 0, 0, 0, 0)!=0) return;
    ehci_delay_ms(10);
    tmp.address=new_addr;

    /* GET_DESCRIPTOR Device completo (18 bytes) */
    for(int i=0;i<18;i++) desc[i]=0;
    if(ehci_control_transfer(&tmp, 0x80, 0x06, (1<<8)|0, 0, 18, desc, 1)!=0) return;

    tmp.dev_class   =desc[4];
    tmp.dev_subclass=desc[5];
    tmp.dev_protocol=desc[6];
    tmp.vendor_id   =(uint16_t)(desc[8]|(desc[9]<<8));
    tmp.product_id  =(uint16_t)(desc[10]|(desc[11]<<8));

    /* Si clase==0 (como en Mass Storage), leer Configuration+Interface Descriptor */
    if(tmp.dev_class == 0){
        static uint8_t cfg[64];
        for(int i=0;i<64;i++) cfg[i]=0;
        /* GET_DESCRIPTOR Configuration (type=2) */
        if(ehci_control_transfer(&tmp, 0x80, 0x06, (2<<8)|0, 0, 64, cfg, 1)==0){
            /* Buscar Interface Descriptor (type=0x04) dentro del bloque */
            int total = (int)(cfg[2]|(cfg[3]<<8));
            if(total>64) total=64;
            int i=0;
            while(i<total){
                uint8_t len  = cfg[i];
                uint8_t type = cfg[i+1];
                if(len<2) break;
                if(type==0x04){ /* Interface Descriptor */
                    tmp.dev_class    = cfg[i+5];
                    tmp.dev_subclass = cfg[i+6];
                    tmp.dev_protocol = cfg[i+7];
                    break;
                }
                i+=len;
            }
        }
    }

    usb_devices[usb_dev_count++]=tmp;
}

/* ------------------------------------------------------------------ */
/* Init                                                                 */
/* ------------------------------------------------------------------ */
int ehci_controller_present(void){ return ehci_ok; }

void usb_ehci_init(void){
    ehci_ok=0;

    /* Buscar EHCI en PCI: clase 0x0C, subclase 0x03, prog-if 0x20 */
    pci_device_t pci_dev;
    int found=0;
    for(int i=0;i<pci_count();i++){
        const pci_device_t *d=pci_get(i);
        if(d->class_code==0x0C && d->subclass==0x03 && d->prog_if==0x20){
            pci_dev=*d; found=1; break;
        }
    }
    if(!found) return;

    pci_enable_device(&pci_dev);

    /* BAR0 = MMIO base */
    uint32_t bar0=pci_get_bar(&pci_dev,0);
    if(bar0 & 1) return;   /* debe ser Memory BAR */
    uint32_t mmio_base=bar0 & ~0xFu;
    if(!mmio_base) return;

    ehci_cap=(volatile uint8_t*)(uintptr_t)mmio_base;
    uint8_t cap_len=ehci_cap[EHCI_CAPLENGTH];
    ehci_op=(volatile uint8_t*)(uintptr_t)(mmio_base+cap_len);

    /* Número de puertos del root hub */
    uint32_t hcsparams=*(volatile uint32_t*)(ehci_cap+EHCI_HCSPARAMS);
    ehci_nports=(int)(hcsparams & 0xF);

    /* Reset del controlador */
    op_write(EHCI_USBCMD, CMD_HCRESET);
    for(int i=0;i<100;i++){
        ehci_delay_ms(1);
        if(!(op_read(EHCI_USBCMD)&CMD_HCRESET)) break;
    }

    /* Periodic list (no la usamos pero hay que inicializarla) */
    for(int i=0;i<PERIODIC_SIZE;i++) periodic_list[i]=1u; /* TERMINATE */
    op_write(EHCI_PERIODICLISTBASE,(uint32_t)(uintptr_t)periodic_list);

    /* Async schedule: cabeza circular que apunta a sí misma */
    async_head.horiz_link = ((uint32_t)(uintptr_t)&async_head) | QH_TYPE_QH;
    async_head.ep_chars   = QH_H | QH_EPS_HS | QH_MAXPKT(64);
    async_head.ep_caps    = QH_MULT(1);
    async_head.next_qtd   = QTD_TERMINATE;
    async_head.alt_next_qtd=QTD_TERMINATE;
    async_head.token      = QTD_HALTED;  /* idle */
    op_write(EHCI_ASYNCLISTADDR,(uint32_t)(uintptr_t)&async_head);

    /* Arrancar: Async Schedule Enable + Run */
    op_write(EHCI_USBINTR, 0);   /* sin IRQs */
    op_write(EHCI_USBCMD, CMD_RS | CMD_ASE | CMD_ITC(1));
    ehci_delay_ms(10);

    /* Tomar control de todos los puertos (ConfigFlag = 1) */
    op_write(EHCI_CONFIGFLAG, 1);
    ehci_delay_ms(50);

    /* Encender puertos */
    for(int p=0;p<ehci_nports;p++){
        uint32_t sc=op_read(EHCI_PORTSC(p));
        if(!(sc & PORT_PP)){
            op_write(EHCI_PORTSC(p), sc|PORT_PP);
            ehci_delay_ms(20);
        }
    }
    ehci_delay_ms(100);

    ehci_ok=1;

    /* Enumerar puertos */
    for(int p=0;p<ehci_nports;p++){
        enumerate_ehci_port(p);
    }
}
