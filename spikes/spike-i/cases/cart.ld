OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS {
    . = 0x00010000;

    .text           : { *(.text._start) *(.text .text.*) }
    .text.mylib     : { KEEP(*(.text.mylib)) }
    .rodata         : { *(.rodata .rodata.*) }
    .sdata : {
        PROVIDE(__global_pointer$ = . + 0x800);
        *(.sdata .sdata.*)
    }
    .data           : { *(.data .data.*) }
    .bss            : { *(.bss .bss.*) *(COMMON) }
    .cart.info      : { KEEP(*(.cart.info)) }
    .cart.config    : { KEEP(*(.cart.config)) }
    .cart.resources : { KEEP(*(.cart.resources)) }
    .dynamic        : { *(.dynamic) }
    .got            : { *(.got) *(.got.plt) }
    .plt            : { *(.plt) }

    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame) }
}
