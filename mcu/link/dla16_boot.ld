/*
 * SPDX-License-Identifier: MIT
 */

OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
  BOOT_FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 0x00001000
  RAMS (rw)       : ORIGIN = 0x20000000, LENGTH = 16K
  RAMX (rw)       : ORIGIN = 0x20020000, LENGTH = 64K
}

stack_top = ORIGIN(RAMS) + LENGTH(RAMS);

SECTIONS
{
  .vectors ORIGIN(BOOT_FLASH) :
  {
    KEEP(*(.vectors))
  } > BOOT_FLASH

  .text :
  {
    *(.text.reset)
    *(.text .text.*)
    *(.rodata .rodata.*)
    *(.srodata .srodata.*)
    . = ALIGN(4);
  } > BOOT_FLASH

  .data :
  {
    data_start = .;
    *(.data .data.*)
    . = ALIGN(8);
    __global_pointer$ = . + 0x800;
    *(.sdata .sdata.*)
    . = ALIGN(4);
    data_end = .;
  } > RAMS AT > BOOT_FLASH
  data_load = LOADADDR(.data);

  .bss (NOLOAD) :
  {
    bss_start = .;
    *(.sbss .sbss.*)
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(4);
    bss_end = .;
  } > RAMS

  .dmadata (NOLOAD) :
  {
    *(.dmadata .dmadata.*)
  } > RAMX

  image_end = LOADADDR(.data) + SIZEOF(.data);

  /DISCARD/ : { *(.comment) *(.riscv.attributes) *(.note*) }
}

ASSERT(image_end <= 0x1000, "bootloader passes 0x1000, where the application starts")
