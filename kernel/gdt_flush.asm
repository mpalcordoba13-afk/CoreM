; gdt_flush.asm - Carga la GDT y recarga los registros de segmento
global gdt_flush

gdt_flush:
    mov eax, [esp+4]    ; Parámetro: dirección del gdt_ptr
    lgdt [eax]          ; Cargar GDT

    ; Recargar segmentos de datos (índice 2 en GDT = 0x10)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Far jump para recargar CS (índice 1 en GDT = 0x08)
    jmp 0x08:.flush
.flush:
    ret
