/*
 * SPDX-License-Identifier: MIT
 */

    .section .text.vectors, "ax"
    .balign 64
    .globl interrupt_vectors
interrupt_vectors:
    .option push
    .option norvc
    .rept 20
    j unexpected_interrupt
    .endr
    j usb3_stub
    j usb3_link_stub
    .rept 6
    j unexpected_interrupt
    .endr
    j hspi_stub
    .rept 8
    j unexpected_interrupt
    .endr
    .option pop

    .balign 4
usb3_stub:
    jal usb3_interrupt
    mret

usb3_link_stub:
    jal usb3_link_interrupt
    mret

hspi_stub:
    jal data_link_interrupt
    mret

unexpected_interrupt:
    j unexpected_interrupt
