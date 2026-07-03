; boot.asm - Bootloader Multiboot sin VBE (el kernel configura Bochs VBE)
[BITS 32]

MBOOT_MAGIC    equ 0x1BADB002
MBOOT_ALIGN    equ 1 << 0
MBOOT_MEMINFO  equ 1 << 1
MBOOT_FLAGS    equ MBOOT_ALIGN | MBOOT_MEMINFO
MBOOT_CHECKSUM equ -(MBOOT_MAGIC + MBOOT_FLAGS)

section .multiboot
align 4
    dd MBOOT_MAGIC
    dd MBOOT_FLAGS
    dd MBOOT_CHECKSUM

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

section .text
global _start
extern kernel_main

_start:
    mov esp, stack_top
    push 0
    popf
    push ebx
    push eax
    call kernel_main
.hang:
    cli
    hlt
    jmp .hang
