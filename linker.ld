/* linker.ld - Script del linker para el kernel */
ENTRY(_start)

SECTIONS {
    /* El kernel se carga en 1MB (dirección estándar para kernels x86) */
    . = 1M;

    /* Sección multiboot y código */
    .text BLOCK(4K) : ALIGN(4K) {
        *(.multiboot)   /* Header multiboot DEBE ser primero */
        *(.text)
    }

    /* Datos de solo lectura */
    .rodata BLOCK(4K) : ALIGN(4K) {
        *(.rodata)
    }

    /* Datos inicializados */
    .data BLOCK(4K) : ALIGN(4K) {
        *(.data)
    }

    /* Datos sin inicializar (BSS) */
    .bss BLOCK(4K) : ALIGN(4K) {
        *(COMMON)
        *(.bss)
    }
}
