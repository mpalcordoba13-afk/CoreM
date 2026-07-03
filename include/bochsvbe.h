#ifndef BOCHSVBE_H
#define BOCHSVBE_H
#include <stdint.h>

/* Configura el framebuffer usando los registros VBE de Bochs/QEMU */
/* Retorna la dirección del framebuffer, o 0 si falla */
uint32_t* bvbe_init(uint16_t width, uint16_t height);

#endif
