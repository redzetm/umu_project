ENTRY(_start)

SECTIONS
{
  . = 0x00100000;

  .multiboot ALIGN(4) : { KEEP(*(.multiboot)) }
  .text : { *(.text) }
  .rodata : { *(.rodata) }
  .data : { *(.data) }
  .bss : { 
    __bss_start = .; 
    *(.bss); 
    __bss_end = .; 
  }
}