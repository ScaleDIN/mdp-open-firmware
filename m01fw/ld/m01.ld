/* MDP-M01 (GD32F303VE / STM32F103VE) application linker script.
 * 0x08000000-0x08007FFF  stock Miniware bootloader (never touch)
 * 0x08008000-0x0803BFFF  this application (208 KB)
 * 0x0803C000-0x0803FFFF  stock settings + boot logo (left intact)
 * 0x08040000-0x0807FFFF  free (256 KB), not used yet
 */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08008000, LENGTH = 208K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}

/* The stock bootloader only jumps if (SP & 0x2FFF0000) == 0x20000000, so the
 * initial SP must stay below 0x20010000 (top of RAM fails the check). */
_estack = ORIGIN(RAM) + LENGTH(RAM) - 16;

SECTIONS
{
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH

  .text :
  {
    *(.text*)
    *(.rodata*)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
  } > FLASH

  .ARM.exidx : { *(.ARM.exidx*) } > FLASH

  _sidata = LOADADDR(.data);
  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > RAM AT > FLASH

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > RAM

  PROVIDE(end = _ebss);   /* newlib heap start (snprintf may malloc) */

  /* keep at least 4 KB for the stack */
  ASSERT(_ebss + 0x1000 <= _estack, "RAM overflow")
}
