/*
 * usb_uhci.c - Driver minimo de controlador USB UHCI (Universal Host
 * Controller Interface), usado por los chipsets Intel PIIX3/PIIX4
 * (el mismo que expone QEMU con la flag -usb).
 *
 * Implementa:
 *   - Deteccion del controlador via PCI (clase 0x0C, subclase 0x03, prog-if 0x00)
 *   - Reset global + reset de puertos raiz
 *   - Motor de transferencias por TD/QH (sin IRQ, por polling)
 *   - Transferencias de control (para enumerar dispositivos)
 *   - Transferencias bulk (usadas por el driver de Mass Storage, etapa 2)
 *   - Enumeracion basica: SET_ADDRESS + GET_DESCRIPTOR(Device)
 *
 * No usa malloc (no existe en este kernel): todas las estructuras de
 * hardware (TDs, QH, frame list) son arreglos estaticos alineados.
 */
#include "usb.h"
#include "pci.h"
#include "timer.h"
#include <stdint.h>

/* ---------------- Acceso a puertos de E/S ---------------- */
static inline void outb(uint16_t p, uint8_t v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline void outw(uint16_t p, uint16_t v){ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }
static inline void outl_(uint16_t p, uint32_t v){ __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p)); }
static inline uint16_t inw(uint16_t p){ uint16_t v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p)); return v; }

/* ---------------- Registros UHCI (offset desde IO base) ---------------- */
#define REG_USBCMD    0x00
#define REG_USBSTS    0x02
#define REG_USBINTR   0x04
#define REG_FRNUM     0x06
#define REG_FRBASEADD 0x08
#define REG_SOFMOD    0x0C
#define REG_PORTSC1   0x10
#define REG_PORTSC2   0x12

#define USBCMD_RS      0x0001
#define USBCMD_GRESET  0x0004
#define USBCMD_CF      0x0040
#define USBCMD_MAXP    0x0080

#define PORTSC_CCS  0x0001
#define PORTSC_CSC  0x0002
#define PORTSC_PE   0x0004
#define PORTSC_PEC  0x0008
#define PORTSC_LSDA 0x0100
#define PORTSC_PR   0x0200

/* ---------------- TD / QH de hardware ---------------- */
typedef struct {
    volatile uint32_t link;
    volatile uint32_t cs;
    volatile uint32_t token;
    volatile uint32_t buffer;
    uint32_t sw0, sw1, sw2, sw3; /* relleno hasta 32 bytes, uso libre del software */
} __attribute__((packed, aligned(16))) uhci_td_t;

typedef struct {
    volatile uint32_t head_link;
    volatile uint32_t element_link;
    uint32_t sw0, sw1;
} __attribute__((packed, aligned(16))) uhci_qh_t;

/* Bits del campo link (head_link / element_link / td.link) */
#define LP_TERMINATE 0x1
#define LP_QH        0x2
#define LP_VF        0x4

/* Bits del campo Control/Status del TD */
#define TD_CS_ACTLEN_MASK 0x7FF
#define TD_CS_BITSTUFF  (1u<<19)
#define TD_CS_CRCTO     (1u<<20)
#define TD_CS_NAK       (1u<<21)
#define TD_CS_BABBLE    (1u<<22)
#define TD_CS_DBUFERR   (1u<<23)
#define TD_CS_STALLED   (1u<<24)
#define TD_CS_ACTIVE    (1u<<25)
#define TD_CS_LS        (1u<<28)
#define TD_CS_CERR_SHIFT 29
#define TD_CS_SPD       (1u<<31)
#define TD_CS_ERROR_MASK (TD_CS_BITSTUFF|TD_CS_CRCTO|TD_CS_NAK|TD_CS_BABBLE|TD_CS_DBUFERR|TD_CS_STALLED)

#define USB_PID_SETUP 0x2D
#define USB_PID_IN    0x69
#define USB_PID_OUT   0xE1

/* ---------------- Estructuras estaticas (sin malloc) ---------------- */
static uint32_t frame_list[1024] __attribute__((aligned(4096)));
static uhci_qh_t main_qh __attribute__((aligned(16)));

#define TD_POOL_SIZE 40
static uhci_td_t td_pool[TD_POOL_SIZE] __attribute__((aligned(16)));

static uint16_t uhci_io_base = 0;
static int controller_ok = 0;

usb_device_t usb_devices[USB_MAX_DEVICES];
int          usb_dev_count = 0;

/* ---------------- Utilidades ---------------- */
static void usb_delay_ms(uint32_t ms){
    uint32_t target = timer_ticks() + (ms/10) + 1; /* PIT a 100Hz -> 10ms/tick */
    while (timer_ticks() < target) { __asm__ volatile("nop"); }
}

static uhci_td_t* td_alloc(int n){
    /* Reserva un bloque contiguo de n TDs del pool estatico. Este driver
     * solo ejecuta una transferencia sincronica a la vez (enumeracion o
     * una llamada de control/bulk), asi que un puntero "bump" que se
     * reinicia cuando se llena es suficiente. */
    static int next = 0;
    if (next + n > TD_POOL_SIZE) next = 0;
    uhci_td_t *t = &td_pool[next];
    next += n;
    return t;
}

/* ---------------- Motor de transferencias ---------------- */

/* Arma una cadena de TDs y la ejecuta via main_qh, esperando a que termine.
 * Retorna 0 si OK, -1 si hubo error/timeout. */
static int run_transfer(uint8_t address, uint8_t endpoint, int low_speed,
                         const uint8_t *pids, const uint8_t *toggles,
                         const int *lens, void **bufs, int count,
                         int *actual_len_out){
    if (count <= 0 || count > TD_POOL_SIZE) return -1;
    uhci_td_t *tds = td_alloc(count);

    for (int i=0;i<count;i++){
        uhci_td_t *t = &tds[i];
        uint32_t cs = TD_CS_ACTIVE | (3u<<TD_CS_CERR_SHIFT);
        if (low_speed) cs |= TD_CS_LS;
        if (pids[i]==USB_PID_IN) cs |= TD_CS_SPD; /* aceptar paquetes cortos en IN */
        t->cs = cs;

        uint32_t maxlen_field = (lens[i]==0) ? 0x7FF : ((uint32_t)(lens[i]-1) & 0x7FF);
        uint32_t token = (maxlen_field<<21) | ((uint32_t)toggles[i]<<19)
                        | ((uint32_t)(endpoint&0xF)<<15) | ((uint32_t)(address&0x7F)<<8)
                        | pids[i];
        t->token = token;
        t->buffer = (uint32_t)(uintptr_t)bufs[i];

        if (i==count-1){
            t->link = LP_TERMINATE;
        } else {
            t->link = ((uint32_t)(uintptr_t)&tds[i+1]) | LP_VF; /* depth-first, sigue siendo TD */
        }
    }

    /* Engancha la cadena al QH principal para que el HC la procese */
    main_qh.element_link = (uint32_t)(uintptr_t)&tds[0];

    /* Espera a que el ultimo TD deje de estar activo (o ~500ms de timeout) */
    uint32_t deadline = timer_ticks() + 50;
    while (timer_ticks() < deadline){
        if (!(tds[count-1].cs & TD_CS_ACTIVE)) break;
    }

    int ok = 1, total = 0;
    for (int i=0;i<count;i++){
        uint32_t cs = tds[i].cs;
        if (cs & TD_CS_ACTIVE) { ok = 0; break; } /* nunca termino: timeout */
        if (cs & TD_CS_ERROR_MASK) { ok = 0; }
        if (pids[i]==USB_PID_IN){
            int al = cs & TD_CS_ACTLEN_MASK;
            if (al != TD_CS_ACTLEN_MASK) total += al; /* 0x7FF = paquete de 0 bytes */
        }
    }

    /* Desengancha para que el HC no reprocese esta cadena en el siguiente frame */
    main_qh.element_link = LP_TERMINATE;

    if (actual_len_out) *actual_len_out = total;
    return ok ? 0 : -1;
}

int usb_control_transfer(usb_device_t *dev,
                          uint8_t bmRequestType, uint8_t bRequest,
                          uint16_t wValue, uint16_t wIndex, uint16_t wLength,
                          void *buf, int dir_in){
    /* Dispositivos high-speed (max_packet0==64) van a EHCI, no a UHCI */
    if (dev && dev->max_packet0 == 64 && !dev->low_speed){
        extern int ehci_controller_present(void);
        extern int ehci_control_transfer(usb_device_t*, uint8_t, uint8_t,
                                          uint16_t, uint16_t, uint16_t, void*, int);
        if (ehci_controller_present())
            return ehci_control_transfer(dev, bmRequestType, bRequest,
                                          wValue, wIndex, wLength, buf, dir_in);
    }
    if (!controller_ok || !dev) return -1;

    uint8_t setup[8];
    setup[0]=bmRequestType; setup[1]=bRequest;
    setup[2]=(uint8_t)(wValue&0xFF); setup[3]=(uint8_t)(wValue>>8);
    setup[4]=(uint8_t)(wIndex&0xFF); setup[5]=(uint8_t)(wIndex>>8);
    setup[6]=(uint8_t)(wLength&0xFF); setup[7]=(uint8_t)(wLength>>8);

    uint8_t mp = dev->max_packet0 ? dev->max_packet0 : 8;

    uint8_t  pids[TD_POOL_SIZE];
    uint8_t  toggles[TD_POOL_SIZE];
    int      lens[TD_POOL_SIZE];
    void    *bufs[TD_POOL_SIZE];
    int n=0;

    pids[n]=USB_PID_SETUP; toggles[n]=0; lens[n]=8; bufs[n]=setup; n++;

    uint8_t toggle = 1;
    int remaining = wLength;
    uint8_t *p = (uint8_t*)buf;
    while (remaining > 0 && n < TD_POOL_SIZE-2){
        int chunk = remaining > mp ? mp : remaining;
        pids[n] = dir_in ? USB_PID_IN : USB_PID_OUT;
        toggles[n] = toggle; toggle ^= 1;
        lens[n] = chunk;
        bufs[n] = p;
        p += chunk; remaining -= chunk;
        n++;
    }

    /* Etapa de estado: direccion opuesta a los datos (o IN si no hubo datos),
     * siempre con data toggle = 1 (DATA1) segun la especificacion USB. */
    pids[n] = (wLength>0 && dir_in) ? USB_PID_OUT : USB_PID_IN;
    toggles[n] = 1; lens[n] = 0; bufs[n] = 0; n++;

    int actual=0;
    return run_transfer(dev->address, 0, dev->low_speed,
                         pids, toggles, lens, bufs, n, &actual);
}

int usb_bulk_transfer(usb_device_t *dev, uint8_t ep, void *buf, int len, int dir_in){
    /* Dispositivos high-speed van a EHCI */
    if (dev && dev->max_packet0 == 64 && !dev->low_speed){
        extern int ehci_controller_present(void);
        extern int ehci_bulk_transfer(usb_device_t*, uint8_t, void*, int, int);
        if (ehci_controller_present())
            return ehci_bulk_transfer(dev, ep, buf, len, dir_in);
    }
    if (!controller_ok || !dev || len<=0) return -1;
    const int mp = 64; /* tamano tipico de paquete bulk full-speed */

    uint8_t  pids[TD_POOL_SIZE];
    uint8_t  toggles[TD_POOL_SIZE];
    int      lens[TD_POOL_SIZE];
    void    *bufs[TD_POOL_SIZE];
    int n=0;

    /* El data toggle de bulk se mantiene por endpoint+sentido entre llamadas,
     * como exige el protocolo USB. El driver de clase Mass Storage debe
     * resincronizarlo (CLEAR_FEATURE ENDPOINT_HALT) si detecta un STALL. */
    static uint8_t toggle_in = 0, toggle_out = 0;
    uint8_t *toggle_ref = dir_in ? &toggle_in : &toggle_out;

    uint8_t *p = (uint8_t*)buf;
    int remaining = len;
    while (remaining>0 && n<TD_POOL_SIZE){
        int chunk = remaining > mp ? mp : remaining;
        pids[n] = dir_in ? USB_PID_IN : USB_PID_OUT;
        toggles[n] = *toggle_ref; *toggle_ref ^= 1;
        lens[n]=chunk; bufs[n]=p;
        p+=chunk; remaining-=chunk; n++;
    }
    int actual=0;
    int r = run_transfer(dev->address, ep, dev->low_speed,
                          pids, toggles, lens, bufs, n, &actual);
    return r==0 ? actual : -1;
}

/* ---------------- Enumeracion ---------------- */
static int enumerate_port(int port_index, int low_speed){
    if (usb_dev_count >= USB_MAX_DEVICES) return -1;

    usb_device_t tmp;
    tmp.valid=1; tmp.address=0; tmp.port=(uint8_t)port_index;
    tmp.low_speed=(uint8_t)low_speed; tmp.max_packet0 = low_speed?8:64;
    tmp.dev_class=0; tmp.dev_subclass=0; tmp.dev_protocol=0;
    tmp.vendor_id=0; tmp.product_id=0;
    tmp.is_mass_storage=0; tmp.msd_iface=0; tmp.msd_in_ep=0; tmp.msd_out_ep=0; tmp.msd_max_lun=0;

    /* 1) Primeros 8 bytes del Device Descriptor en direccion 0, para
     *    conocer bMaxPacketSize0 antes de fijar la direccion real. */
    uint8_t desc[18];
    for (int i=0;i<18;i++) desc[i]=0;
    if (usb_control_transfer(&tmp, 0x80, 0x06, (1<<8)|0, 0, 8, desc, 1) != 0)
        return -1;
    if (desc[7]) tmp.max_packet0 = desc[7];

    /* 2) SET_ADDRESS: cada dispositivo nuevo recibe address = indice+1 */
    uint8_t new_addr = (uint8_t)(usb_dev_count + 1);
    if (usb_control_transfer(&tmp, 0x00, 0x05, new_addr, 0, 0, 0, 0) != 0)
        return -1;
    usb_delay_ms(10);
    tmp.address = new_addr;

    /* 3) Device Descriptor completo (18 bytes), ya en la nueva direccion */
    for (int i=0;i<18;i++) desc[i]=0;
    if (usb_control_transfer(&tmp, 0x80, 0x06, (1<<8)|0, 0, 18, desc, 1) != 0)
        return -1;

    tmp.dev_class    = desc[4];
    tmp.dev_subclass = desc[5];
    tmp.dev_protocol = desc[6];
    tmp.vendor_id    = (uint16_t)(desc[8]  | (desc[9]<<8));
    tmp.product_id   = (uint16_t)(desc[10] | (desc[11]<<8));

    usb_devices[usb_dev_count++] = tmp;
    return 0;
}

static void reset_and_enumerate_port(int idx, uint16_t portsc_reg){
    uint16_t sc = inw(uhci_io_base+portsc_reg);
    if (!(sc & PORTSC_CCS)) return; /* nada conectado en este puerto */

    /* Reset de puerto: 50ms con PR=1 (USB 2.0 spec, root hub reset) */
    outw(uhci_io_base+portsc_reg, sc | PORTSC_PR);
    usb_delay_ms(50);
    sc = inw(uhci_io_base+portsc_reg);
    outw(uhci_io_base+portsc_reg, sc & ~PORTSC_PR);
    usb_delay_ms(10);

    sc = inw(uhci_io_base+portsc_reg);
    if (!(sc & PORTSC_CCS)) return; /* se desconecto durante el reset */

    int low_speed = (sc & PORTSC_LSDA) ? 1 : 0;

    /* Limpia los bits "write-1-to-clear" (CSC/PEC) y habilita el puerto */
    outw(uhci_io_base+portsc_reg, (sc & ~(PORTSC_CSC|PORTSC_PEC)) | PORTSC_PE);
    usb_delay_ms(10);

    enumerate_port(idx, low_speed);
}

/* ---------------- Init de controlador ---------------- */
static int find_uhci_pci(pci_device_t *out){
    int n = pci_count();
    for (int i=0;i<n;i++){
        const pci_device_t *d = pci_get(i);
        if (d->class_code==0x0C && d->subclass==0x03 && d->prog_if==0x00){
            *out = *d;
            return 1;
        }
    }
    return 0;
}

void usb_init(void){
    usb_dev_count = 0;
    controller_ok = 0;
    uhci_io_base = 0;

    pci_device_t uhci;
    if (!find_uhci_pci(&uhci)) return; /* no hay controlador UHCI en este sistema */

    pci_enable_device(&uhci);

    uint32_t bar4 = pci_get_bar(&uhci, 4); /* UHCI usa BAR4 para su espacio de E/S */
    if (!(bar4 & 0x1)) return;             /* debe ser un BAR de E/S */
    uhci_io_base = (uint16_t)(bar4 & 0xFFFC);

    /* Reset global */
    outw(uhci_io_base+REG_USBCMD, USBCMD_GRESET);
    usb_delay_ms(50);
    outw(uhci_io_base+REG_USBCMD, 0);
    usb_delay_ms(10);

    /* "Schedule": todas las entradas de la frame list apuntan a la misma QH
     * principal (suficiente para transferencias sincronicas, una a la vez). */
    main_qh.head_link = LP_TERMINATE;
    main_qh.element_link = LP_TERMINATE;
    uint32_t qh_ptr = ((uint32_t)(uintptr_t)&main_qh) | LP_QH;
    for (int i=0;i<1024;i++) frame_list[i] = qh_ptr;

    outw(uhci_io_base+REG_FRNUM, 0);
    outl_(uhci_io_base+REG_FRBASEADD, (uint32_t)(uintptr_t)frame_list);
    outb(uhci_io_base+REG_SOFMOD, 0x40);
    outw(uhci_io_base+REG_USBINTR, 0); /* sin IRQs: este driver funciona por polling */

    /* Arranca el controlador: Configure Flag + paquetes de 64 bytes + Run */
    outw(uhci_io_base+REG_USBCMD, USBCMD_CF | USBCMD_MAXP | USBCMD_RS);
    usb_delay_ms(20);

    controller_ok = 1;

    /* Enumera los dos puertos raiz que tiene un UHCI estandar */
    reset_and_enumerate_port(0, REG_PORTSC1);
    reset_and_enumerate_port(1, REG_PORTSC2);
}

int usb_controller_present(void){ return controller_ok; }
int usb_device_count(void){ return usb_dev_count; }
usb_device_t* usb_get_device(int i){
    if (i<0 || i>=usb_dev_count) return 0;
    return &usb_devices[i];
}
