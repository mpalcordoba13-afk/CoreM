#include "vga.h"
#include <stdint.h>
#include <stddef.h>

#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_MEMORY  ((uint16_t*)0xB8000)

/* Puertos VGA para mover cursor */
#define VGA_PORT_CMD  0x3D4
#define VGA_PORT_DATA 0x3D5

static int cursor_row = 0;
static int cursor_col = 0;
static uint8_t current_color = 0;

/* Escribir en puerto I/O */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static uint8_t make_color(vga_color_t fg, vga_color_t bg) {
    return fg | (bg << 4);
}

static uint16_t make_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

static void update_hw_cursor(void) {
    uint16_t pos = cursor_row * VGA_WIDTH + cursor_col;
    outb(VGA_PORT_CMD, 14);
    outb(VGA_PORT_DATA, (pos >> 8) & 0xFF);
    outb(VGA_PORT_CMD, 15);
    outb(VGA_PORT_DATA, pos & 0xFF);
}

static void scroll(void) {
    /* Mover todo una línea hacia arriba */
    for (int row = 1; row < VGA_HEIGHT; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            VGA_MEMORY[(row - 1) * VGA_WIDTH + col] =
                VGA_MEMORY[row * VGA_WIDTH + col];
        }
    }
    /* Limpiar última fila */
    for (int col = 0; col < VGA_WIDTH; col++) {
        VGA_MEMORY[(VGA_HEIGHT - 1) * VGA_WIDTH + col] =
            make_entry(' ', current_color);
    }
    cursor_row = VGA_HEIGHT - 1;
}

void vga_init(void) {
    current_color = make_color(VGA_WHITE, VGA_BLACK);
    vga_clear();
}

void vga_clear(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        VGA_MEMORY[i] = make_entry(' ', current_color);
    }
    cursor_row = 0;
    cursor_col = 0;
    update_hw_cursor();
}

void vga_set_color(vga_color_t fg, vga_color_t bg) {
    current_color = make_color(fg, bg);
}

void vga_set_cursor(int row, int col) {
    cursor_row = row;
    cursor_col = col;
    update_hw_cursor();
}

void vga_get_cursor(int *row, int *col) {
    *row = cursor_row;
    *col = cursor_col;
}

void vga_putchar(char c) {
    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else if (c == '\r') {
        cursor_col = 0;
    } else if (c == '\t') {
        cursor_col = (cursor_col + 4) & ~3;
    } else if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
            VGA_MEMORY[cursor_row * VGA_WIDTH + cursor_col] =
                make_entry(' ', current_color);
        }
    } else {
        VGA_MEMORY[cursor_row * VGA_WIDTH + cursor_col] =
            make_entry(c, current_color);
        cursor_col++;
        if (cursor_col >= VGA_WIDTH) {
            cursor_col = 0;
            cursor_row++;
        }
    }

    if (cursor_row >= VGA_HEIGHT) {
        scroll();
    }

    update_hw_cursor();
}

void vga_puts(const char *str) {
    while (*str) {
        vga_putchar(*str++);
    }
}

void vga_puthex(uint32_t n) {
    const char *digits = "0123456789ABCDEF";
    vga_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        vga_putchar(digits[(n >> i) & 0xF]);
    }
}

void vga_putdec(uint32_t n) {
    if (n == 0) {
        vga_putchar('0');
        return;
    }
    char buf[12];
    int i = 0;
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    for (int j = i - 1; j >= 0; j--) {
        vga_putchar(buf[j]);
    }
}
