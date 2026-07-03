/*
 * usb_printer.c - Driver minimo de clase USB Printer (clase 0x07,
 * subclase 0x01 "Printer", cualquier protocolo: 1=unidireccional,
 * 2=bidireccional, 3=1284.4).
 *
 * Reutiliza el motor de transferencias de usb_uhci.c (control + bulk).
 * No usa malloc: todo en buffers estaticos.
 *
 * Flujo:
 *   1. Pide los primeros 9 bytes del Configuration Descriptor para
 *      saber wTotalLength.
 *   2. Pide el descriptor de configuracion completo (hasta CFG_BUF_SIZE).
 *   3. Recorre los sub-descriptores buscando una interfaz con
 *      bInterfaceClass==7 && bInterfaceSubClass==1, y dentro de ella
 *      un endpoint bulk (bmAttributes&0x3==2) de salida (bit7==0).
 *   4. SET_CONFIGURATION con el bConfigurationValue del descriptor.
 *   5. Listo para usb_bulk_transfer() hacia ese endpoint.
 */
#include "usb_printer.h"
#include "usb.h"
#include <stdint.h>

#define CFG_BUF_SIZE 256

static int printer_found = 0;
static usb_device_t *printer_dev = 0;
static uint8_t printer_out_ep = 0;

static uint8_t cfg_buf[CFG_BUF_SIZE];

static int try_device(usb_device_t *dev) {
    /* Paso 1: cabecera del descriptor de configuracion (9 bytes) */
    uint8_t head[9];
    for (int i = 0; i < 9; i++) head[i] = 0;
    if (usb_control_transfer(dev, 0x80, 0x06, (2 << 8) | 0, 0, 9, head, 1) != 0)
        return 0;

    int total_len = head[2] | (head[3] << 8);
    if (total_len <= 0) return 0;
    if (total_len > CFG_BUF_SIZE) total_len = CFG_BUF_SIZE;

    /* Paso 2: descriptor de configuracion completo */
    for (int i = 0; i < CFG_BUF_SIZE; i++) cfg_buf[i] = 0;
    if (usb_control_transfer(dev, 0x80, 0x06, (2 << 8) | 0, 0, (uint16_t)total_len, cfg_buf, 1) != 0)
        return 0;

    uint8_t config_value = cfg_buf[5]; /* bConfigurationValue */

    /* Paso 3: recorrer sub-descriptores */
    int in_printer_iface = 0;
    int out_ep = -1;
    int pos = 0;
    while (pos + 2 <= total_len) {
        uint8_t blen = cfg_buf[pos];
        uint8_t btype = cfg_buf[pos + 1];
        if (blen == 0) break; /* descriptor corrupto, evitar loop infinito */

        if (btype == 0x04 && pos + 9 <= total_len) {
            /* Interface descriptor */
            uint8_t iface_class = cfg_buf[pos + 5];
            uint8_t iface_sub   = cfg_buf[pos + 6];
            in_printer_iface = (iface_class == 0x07 && iface_sub == 0x01);
        } else if (btype == 0x05 && pos + 7 <= total_len && in_printer_iface) {
            /* Endpoint descriptor */
            uint8_t ep_addr  = cfg_buf[pos + 2];
            uint8_t ep_attrs = cfg_buf[pos + 3];
            int is_bulk = (ep_attrs & 0x03) == 0x02;
            int is_out  = (ep_addr & 0x80) == 0x00;
            if (is_bulk && is_out) {
                out_ep = ep_addr & 0x0F;
                break;
            }
        }

        pos += blen;
    }

    if (out_ep < 0) return 0;

    /* Paso 4: activar la configuracion encontrada */
    if (usb_control_transfer(dev, 0x00, 0x09, config_value, 0, 0, 0, 0) != 0)
        return 0;

    printer_dev = dev;
    printer_out_ep = (uint8_t)out_ep;
    return 1;
}

int usb_printer_init(void) {
    printer_found = 0;
    printer_dev = 0;
    printer_out_ep = 0;

    int n = usb_device_count();
    for (int i = 0; i < n; i++) {
        usb_device_t *dev = usb_get_device(i);
        if (!dev || !dev->valid) continue;
        if (try_device(dev)) {
            printer_found = 1;
            break;
        }
    }
    return printer_found;
}

int usb_printer_present(void) {
    return printer_found;
}

int usb_printer_send(const char *data, int len) {
    if (!printer_found || !printer_dev || len <= 0) return -1;
    /* usb_bulk_transfer ya trocea en paquetes de 64 bytes internamente */
    int r = usb_bulk_transfer(printer_dev, printer_out_ep, (void *)data, len, 0);
    return (r >= 0) ? 0 : -1;
}

int usb_printer_print_text(const char *text) {
    if (!text) return -1;
    int len = 0;
    while (text[len]) len++;

    /* Buffer local: texto + '\n' + form feed (0x0C) para cortar pagina
     * en impresoras de ticket/recibo. Si no hace falta el FF, igual
     * no molesta a una impresora que solo entiende texto plano. */
    static char out_buf[1024];
    int n = 0;
    for (int i = 0; i < len && n < (int)sizeof(out_buf) - 2; i++) out_buf[n++] = text[i];
    out_buf[n++] = '\n';
    out_buf[n++] = 0x0C;

    return usb_printer_send(out_buf, n);
}
