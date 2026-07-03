#ifndef USB_PRINTER_H
#define USB_PRINTER_H

/*
 * usb_printer.h - Driver de clase USB Printer (clase 0x07).
 *
 * Busca, entre los dispositivos ya enumerados por usb_uhci.c, una
 * interfaz de impresora (bInterfaceClass = 7) con un endpoint bulk OUT,
 * y permite enviarle texto plano crudo.
 *
 * No implementa IPP, PCL ni PostScript: solo manda bytes tal cual.
 * Funciona con impresoras que aceptan texto plano directamente
 * (impresoras de tickets/recibos ESC/POS, muchas matriciales, y la
 * mayoria de "virtual printers" usadas para pruebas).
 */

/* Escanea los dispositivos USB ya detectados por usb_init() y, si
 * encuentra una impresora, deja la interfaz lista (SET_CONFIGURATION).
 * Devuelve 1 si se encontro una impresora utilizable, 0 si no. */
int usb_printer_init(void);

/* 1 si usb_printer_init() encontro una impresora valida. */
int usb_printer_present(void);

/* Envia 'len' bytes crudos al endpoint bulk OUT de la impresora.
 * Devuelve 0 si OK, -1 si fallo el envio. */
int usb_printer_send(const char *data, int len);

/* Conveniencia: envia un string terminado en '\0' y agrega salto de
 * linea + form feed al final (corta la pagina en impresoras de ticket). */
int usb_printer_print_text(const char *text);

#endif
