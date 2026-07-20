; ============================================================
; XEKernelOS - ISR Stubs (48+ interrupt handlers)
; ============================================================

%macro ISR_NOERR 1
global isr%1
isr%1:
    push 0
    push %1
    jmp common_isr
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push %1
    jmp common_isr
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

ISR_NOERR 128

; IRQ handlers
%assign i 0
%rep 16
global irq %+ i
irq %+ i:
    push 0
    push 0x20 + i
    jmp common_isr
%assign i i+1
%endrep

extern c_isr_handler

common_isr:
    pusha
    push esp
    call c_isr_handler
    add esp, 4
    popa
    add esp, 8
    iretd

section .rodata
global isr_addrs
isr_addrs:
%assign i 0
%rep 32
    dd isr %+ i
%assign i i+1
%endrep
%assign i 0
%rep 16
    dd irq %+ i
%assign i i+1
%endrep
