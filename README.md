# MyOS — Sistema operativo x86 desde cero

## Estructura del proyecto

```
myos/
├── boot/
│   └── boot.asm          ← Bootloader Multiboot (GRUB-compatible)
├── kernel/
│   ├── kernel.c          ← Punto de entrada del kernel
│   ├── gdt.c             ← Global Descriptor Table
│   ├── gdt_flush.asm     ← Carga la GDT en hardware
│   ├── idt.c             ← Interrupt Descriptor Table + PIC
│   ├── idt_asm.asm       ← Stubs de ISR/IRQ en assembly
│   └── shell.c           ← Shell interactiva
├── drivers/
│   ├── vga.c             ← Driver de pantalla VGA 80x25
│   └── keyboard.c        ← Driver de teclado PS/2
├── include/
│   ├── vga.h
│   ├── gdt.h
│   ├── idt.h
│   ├── keyboard.h
│   └── shell.h
├── iso/boot/grub/
│   └── grub.cfg          ← Config de GRUB para la ISO
├── linker.ld             ← Script del linker
└── Makefile
```

---

## Requisitos — Instalar en MSYS2

Abrí la terminal **MSYS2 MinGW** y ejecutá:

```bash
# Actualizar paquetes base
pacman -Syu

# Herramientas de compilación cruzada para x86 (i686-elf)
pacman -S mingw-w64-x86_64-cross-i686-elf-gcc
pacman -S nasm
pacman -S make

# Para crear ISOs (opcional pero recomendado)
pacman -S grub xorriso
```

> **Nota:** Si `i686-elf-gcc` no aparece con ese nombre, buscá con:
> `pacman -Ss cross | grep i686`

---

## Compilar y ejecutar

```bash
cd myos

# Compilar el kernel
make

# Ejecutar directamente en QEMU (más rápido para desarrollo)
make run

# Crear ISO booteable y ejecutar desde ella
make iso
make run-iso

# Modo debug (muestra interrupciones en consola)
make debug

# Limpiar objetos
make clean
```

### Probar con un USB virtual (para el driver UHCI)

`make run` ya incluye `-usb -device usb-tablet`, así que QEMU expone un
controlador UHCI aunque no conectes nada más. Para simular además un
pendrive con un sistema de archivos FAT32 real:

```bash
# 1. Crear una imagen de 64MB y formatearla FAT32 (una sola vez)
qemu-img create usbdisk.img 64M
mkfs.vfat -F 32 usbdisk.img
# (opcional) copiarle algo: mcopy -i usbdisk.img foto.bmp ::foto.bmp

# 2. Arrancar el kernel con el pendrive conectado
qemu-system-i386 -kernel myos.kernel -m 64M -vga std \
    -usb -device usb-tablet \
    -drive if=none,id=usbstick,file=usbdisk.img,format=raw \
    -device usb-storage,drive=usbstick
```

Con esto, al abrir la ventana **Dispositivos** deberías ver el
controlador UHCI y, debajo, el dispositivo USB de almacenamiento
detectado (clase `Almacenamiento`, con su Vendor ID / Product ID).

---

## Lo que hace este OS

Al arrancar verás el proceso de inicialización:

```
[OK] VGA inicializado
[OK] GDT cargada
[OK] IDT cargada + PIC remapeado
[OK] Teclado PS/2 activo
[OK] Interrupciones habilitadas

 __  __         ___  ____
|  \/  |_   _  / _ \/ ___|
| |\/| | | | || | | \___ \
| |  | | |_| || |_| |___) |
|_|  |_|\__, | \___/|____/
        |___/  v0.1

myos> _
```

### Comandos disponibles en la shell

| Comando         | Descripción                        |
|-----------------|------------------------------------|
| `help`          | Lista de comandos                  |
| `clear`         | Limpia la pantalla                 |
| `echo hola`     | Imprime texto                      |
| `info`          | Información del sistema            |
| `color cyan`    | Cambia color del texto             |
| `calc 15+7`     | Calculadora (+ - * /)              |
| `history`       | Historial de comandos              |
| `reboot`        | Reinicia el sistema                |
| `halt`          | Apaga la CPU                       |

---

## Arquitectura interna

### Bootloader (boot.asm)
Usa el protocolo **Multiboot**, lo que permite que GRUB lo cargue
directamente. El bootloader configura el stack y llama a `kernel_main()`.

### GDT (Global Descriptor Table)
Define 5 segmentos: null, código kernel (ring 0), datos kernel (ring 0),
código usuario (ring 3), datos usuario (ring 3). Necesaria para que la CPU
funcione en modo protegido correctamente.

### IDT (Interrupt Descriptor Table)
32 manejadores de excepciones (división por cero, page fault, etc.)
y 16 IRQs de hardware (timer, teclado, etc.). El PIC 8259 se remapea
para que las IRQs no colisionen con las excepciones.

### Driver VGA
Escribe directamente en `0xB8000` (memoria de video VGA). Soporta
colores, scroll automático, cursor de hardware, y backspace.

### Driver de teclado
Lee scancodes del puerto `0x60` via IRQ1. Convierte a ASCII con
soporte para Shift y Caps Lock. Buffer circular de 256 bytes.

---

## Próximos pasos sugeridos

1. **Gestión de memoria física** — bitmap allocator para páginas de 4KB
2. **Paging** — activar modo paginado con page directory y page tables
3. **Heap del kernel** — `kmalloc` / `kfree`
4. **Timer PIT** — IRQ0 para scheduling y sleep
5. **Procesos** — estructuras PCB y context switching
6. **Filesystem** — leer una imagen de disco (FAT12 o ext2 simple)
