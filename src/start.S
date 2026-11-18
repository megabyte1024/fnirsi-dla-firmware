/*
 * SPDX-License-Identifier: MIT
 */

    .section .vectors, "ax"
    .globl _start
_start:
    .option push
    .option norvc
    j reset_handler
    .option pop
    .word 0x43535442

    .section .text.reset, "ax"
reset_handler:
    csrci mstatus, 0x8
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    la sp, stack_top
    la t0, trap_handler
    csrw mtvec, t0

    la a0, data_load
    la a1, data_start
    la a2, data_end
copy_data:
    bgeu a1, a2, data_done
    lw t0, 0(a0)
    sw t0, 0(a1)
    addi a0, a0, 4
    addi a1, a1, 4
    j copy_data
data_done:

    la a1, bss_start
    la a2, bss_end
zero_bss:
    bgeu a1, a2, bss_done
    sw zero, 0(a1)
    addi a1, a1, 4
    j zero_bss
bss_done:

    call main
stopped:
    j stopped

    .balign 4
trap_handler:
    j trap_handler
