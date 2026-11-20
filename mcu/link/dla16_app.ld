/*
 * SPDX-License-Identifier: MIT
 */

OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
  APP_FLASH (rx) : ORIGIN = 0x00001000, LENGTH = 0x00064000
  RAMS (rw)      : ORIGIN = 0x20000000, LENGTH = 16K
  RAMX (rw)      : ORIGIN = 0x20020000, LENGTH = 64K
}

fast_code_end = 0x00010000;
restart_marks_start = 0x00065000;
stack_top = ORIGIN(RAMS) + LENGTH(RAMS);

SECTIONS
{
  .vectors ORIGIN(APP_FLASH) :
  {
    KEEP(*(.vectors))
  } > APP_FLASH

  .text :
  {
    *(.text.reset)
    *(.text .text.*)
    *(.rodata .rodata.*)
    *(.srodata .srodata.*)
    . = ALIGN(4);
  } > APP_FLASH

  .data :
  {
    data_start = .;
    *(.data .data.*)
    . = ALIGN(8);
    __global_pointer$ = . + 0x800;
    *(.sdata .sdata.*)
    . = ALIGN(4);
    data_end = .;
  } > RAMS AT > APP_FLASH
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

ASSERT(image_end <= fast_code_end, "image passes 64 KiB: code above it runs about 8 times slower (USER_MEM)")
ASSERT(image_end <= restart_marks_start, "image overlaps restart marks sector at 0x65000")
ASSERT(bss_end <= stack_top - 2048, "less than 2 KiB of stack left in RAMS")
