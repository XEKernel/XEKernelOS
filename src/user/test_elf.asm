; min_test.asm — absolute minimal: just breakpoint (int3)

[bits 32]
[global _start]

_start:
    int3        ; breakpoint → #BP (vec=3) in ISR
    jmp $
