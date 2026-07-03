#include "keyboard.h"
#include "idt.h"
#include "vga.h"
#include <stdint.h>
#include <stddef.h>

#define KB_DATA_PORT    0x60
#define KB_BUFFER_SIZE  256

static char kb_buffer[KB_BUFFER_SIZE];
static int  kb_buf_read  = 0;
static int  kb_buf_write = 0;
static int  kb_buf_count = 0;
static volatile uint32_t kb_total = 0;

static int shift_pressed = 0;
static int caps_lock     = 0;
static int extended      = 0; /* prefijo 0xE0 (teclas de flecha) */

static const char scancode_map[] = {
    0,    27,  '1', '2', '3', '4', '5', '6', '7', '8',
    '9', '0', '-', '=',  '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',
    '[', ']', '\n', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
    '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
    0, '*', 0, ' ', 0
};

static const char scancode_map_shift[] = {
    0,    27,  '!', '@', '#', '$', '%', '^', '&', '*',
    '(', ')', '_', '+',  '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P',
    '{', '}', '\n', 0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
    '"', '~', 0, '|',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    0, '*', 0, ' ', 0
};

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void kb_buf_push(char c) {
    if (kb_buf_count < KB_BUFFER_SIZE) {
        kb_buffer[kb_buf_write] = c;
        kb_buf_write = (kb_buf_write + 1) % KB_BUFFER_SIZE;
        kb_buf_count++;
        kb_total++;
    }
}

static void keyboard_irq_handler(registers_t *regs) {
    (void)regs;
    uint8_t scancode = inb(KB_DATA_PORT);

    /* Prefijo de tecla extendida (flechas, etc) */
    if (scancode == 0xE0) { extended = 1; return; }

    if (extended) {
        extended = 0;
        if (scancode & 0x80) return; /* soltar tecla extendida, ignorar */
        switch (scancode) {
            case 0x48: kb_buf_push((char)KEY_UP);    return;
            case 0x50: kb_buf_push((char)KEY_DOWN);  return;
            case 0x4B: kb_buf_push((char)KEY_LEFT);  return;
            case 0x4D: kb_buf_push((char)KEY_RIGHT); return;
            default: return;
        }
    }

    if (scancode & 0x80) {
        uint8_t released = scancode & 0x7F;
        if (released == 0x2A || released == 0x36) shift_pressed = 0;
        return;
    }

    if (scancode == 0x2A || scancode == 0x36) { shift_pressed = 1; return; }
    if (scancode == 0x3A) { caps_lock = !caps_lock; return; }

    if (scancode < sizeof(scancode_map)) {
        char c = shift_pressed ? scancode_map_shift[scancode] : scancode_map[scancode];
        if (caps_lock && c >= 'a' && c <= 'z') c -= 32;
        else if (caps_lock && c >= 'A' && c <= 'Z') c += 32;
        if (c != 0) kb_buf_push(c);
    }
}

void keyboard_init(void) {
    idt_set_handler(33, keyboard_irq_handler);
}

int keyboard_has_key(void) { return kb_buf_count > 0; }
uint32_t keyboard_key_count(void) { return kb_total; }

char keyboard_getchar(void) {
    while (kb_buf_count == 0) { __asm__ volatile ("hlt"); }
    char c = kb_buffer[kb_buf_read];
    kb_buf_read = (kb_buf_read + 1) % KB_BUFFER_SIZE;
    kb_buf_count--;
    return c;
}

int keyboard_poll(void) {
    if (kb_buf_count == 0) return -1;
    char c = kb_buffer[kb_buf_read];
    kb_buf_read = (kb_buf_read + 1) % KB_BUFFER_SIZE;
    kb_buf_count--;
    return (unsigned char)c;
}

int keyboard_readline(char *buf, int maxlen) {
    int i = 0;
    while (i < maxlen - 1) {
        char c = keyboard_getchar();
        if (c == '\n' || c == '\r') {
            buf[i] = '\0';
            vga_putchar('\n');
            return i;
        } else if (c == '\b') {
            if (i > 0) { i--; vga_putchar('\b'); }
        } else {
            buf[i++] = c;
            vga_putchar(c);
        }
    }
    buf[i] = '\0';
    return i;
}

void keyboard_flush(void) {
    kb_buf_read = kb_buf_write = kb_buf_count = 0;
}
