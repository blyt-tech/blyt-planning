OUTPUT_ARCH(riscv)
ENTRY(_start)

/* HEAP_SIZE is supplied via --defsym HEAP_SIZE=N at link time.
 * Pass 0 for carts that do not use the heap allocator (e.g. atomics cart).
 * The heap section must be a multiple of 16 bytes (enforced by ALIGN(16) and
 * the Makefile's validation of heap_size values). */
PROVIDE(HEAP_SIZE = 0);

MEMORY {
    /* 128 MiB flat address space starting at the cart load address.
     * All sections (text, data, bss, heap) live in this single region. */
    RAM (rwx) : ORIGIN = 0x00010000, LENGTH = 128M
}

SECTIONS {
    . = 0x00010000;

    .text           : { *(.text._start) *(.text .text.*) } >RAM
    .text.mylib     : { KEEP(*(.text.mylib)) } >RAM
    .rodata         : { *(.rodata .rodata.*) } >RAM
    .sdata : {
        PROVIDE(__global_pointer$ = . + 0x800);
        *(.sdata .sdata.*)
    } >RAM
    .data           : { *(.data .data.*) } >RAM
    .bss            : { *(.bss .bss.*) *(COMMON) } >RAM
    .cart.info      : { KEEP(*(.cart.info)) } >RAM
    .cart.config    : { KEEP(*(.cart.config)) } >RAM
    .cart.resources : { KEEP(*(.cart.resources)) } >RAM
    .dynamic        : { *(.dynamic) } >RAM
    .got            : { *(.got) *(.got.plt) } >RAM
    .plt            : { *(.plt) } >RAM

    /* Heap region — NOLOAD so the ELF loader does not supply zero bytes.
     * crt0.S zeros this region explicitly before calling __blyt_heap_init.
     * ALIGN(16) ensures __blyt_heap_end is 16-byte aligned, which satisfies
     * the crt0.S zero-loop's 4-byte stride assumption (16 is a multiple of 4).
     * Placed in RAM so rv32emu maps it as PF_R|PF_W (writable). */
    .blyt_heap (NOLOAD) : ALIGN(16) {
        __blyt_heap_start = .;
        . += HEAP_SIZE;
        __blyt_heap_end   = .;
    } >RAM

    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame) }
}
