/* ============================================================
 * XEKernelOS Kernel Linker Script
 *
 * 将 .text, .rodata, .data, .bss 放在 0x20000 起始
 * ============================================================ */

ENTRY(kernel_main)

SECTIONS {
    . = 0x20000;

    .text : {
        *(.text.init)
        *(.text)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    .data : {
        *(.data)
    }

    .bss : {
        _bss_start = .;
        *(.bss)
        *(COMMON)
        _bss_end = .;
    }
}

_heap_start = 0x01000000;
_heap_end = 0x01F00000;
