/* QEMU mps2-an505 (Cortex-M33): code in SSRAM1 and data in SSRAM2+3, both through
 * their Secure aliases (bit 28 set) because the CPU resets into Secure state with
 * INITSVTOR = 0x10000000 and the memory protection controllers reject Non-secure
 * accesses by default.
 * Sized like the RP2350-Zero's flash (4 MB) so the linker reports a real overflow. */
MEMORY
{
  ROM (rx)  : ORIGIN = 0x10000000, LENGTH = 4M
  RAM (rwx) : ORIGIN = 0x38000000, LENGTH = 4M
}
ENTRY(Reset_Handler)
SECTIONS
{
  .isr_vector : { KEEP(*(.isr_vector)) } > ROM
  .text : { *(.text*) KEEP(*(.init)) KEEP(*(.fini)) } > ROM
  .rodata : { *(.rodata*) } > ROM
  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > ROM
  .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > ROM
  .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .); } > ROM
  .init_array : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .); } > ROM
  .fini_array : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .); } > ROM
  __etext = .;
  .data : AT(__etext) { __data_start__ = .; *(.data*) . = ALIGN(4); __data_end__ = .; } > RAM
  .bss : { . = ALIGN(4); __bss_start__ = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end__ = .; } > RAM
  .heap (NOLOAD) : { . = ALIGN(8); __end__ = .; end = .; PROVIDE(__heap_start__ = .); . = ORIGIN(RAM) + LENGTH(RAM) - 256K; __HeapLimit = .; } > RAM
  .stack (NOLOAD) : { . = ALIGN(8); __StackLimit = .; . = ORIGIN(RAM) + LENGTH(RAM); __StackTop = .; } > RAM
  PROVIDE(__stack = __StackTop);
  PROVIDE(__rom_start = ORIGIN(ROM));
}
