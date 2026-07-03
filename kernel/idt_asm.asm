; idt_asm.asm - Stubs de interrupciones en assembly
; Cada ISR/IRQ guarda el estado y llama al handler en C

global idt_flush
global isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7
global isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15
global isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
global isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
global irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7
global irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15

extern isr_handler
extern irq_handler

; Cargar IDT
idt_flush:
    mov eax, [esp+4]
    lidt [eax]
    ret

; Macro para ISR sin código de error
%macro ISR_NOERRCODE 1
isr%1:
    push dword 0        ; código de error ficticio
    push dword %1       ; número de interrupción
    jmp isr_common_stub
%endmacro

; Macro para ISR con código de error (el CPU ya lo empujó)
%macro ISR_ERRCODE 1
isr%1:
    push dword %1
    jmp isr_common_stub
%endmacro

; Macro para IRQs
%macro IRQ 2
irq%1:
    push dword 0
    push dword %2
    jmp irq_common_stub
%endmacro

; Excepciones 0-31
ISR_NOERRCODE  0
ISR_NOERRCODE  1
ISR_NOERRCODE  2
ISR_NOERRCODE  3
ISR_NOERRCODE  4
ISR_NOERRCODE  5
ISR_NOERRCODE  6
ISR_NOERRCODE  7
ISR_ERRCODE    8
ISR_NOERRCODE  9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE   17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_NOERRCODE 30
ISR_NOERRCODE 31

; IRQs 0-15 → interrupciones 32-47
IRQ  0, 32
IRQ  1, 33
IRQ  2, 34
IRQ  3, 35
IRQ  4, 36
IRQ  5, 37
IRQ  6, 38
IRQ  7, 39
IRQ  8, 40
IRQ  9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

; Stub común para ISRs (excepciones)
isr_common_stub:
    pusha                   ; guarda eax,ecx,edx,ebx,esp,ebp,esi,edi
    mov ax, ds
    push eax                ; guarda selector de datos
    mov ax, 0x10            ; segmento de datos del kernel
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp                ; puntero a la estructura registers_t
    call isr_handler
    add esp, 4
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    popa
    add esp, 8              ; limpia int_no y err_code
    iret

; Stub común para IRQs (hardware)
irq_common_stub:
    pusha
    mov ax, ds
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp
    call irq_handler
    add esp, 4
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    popa
    add esp, 8
    iret
