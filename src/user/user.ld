ENTRY(_start)

SECTIONS {
    . = 0x400000;

    .text : {
        KEEP(*(.text.startup))   /* _start 必须在 0x400000 (loader 固定入口) */
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    .data : { *(.data) }

    .bss : {
        *(COMMON)
        *(.bss)
    }
}
