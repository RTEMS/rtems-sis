<!-- SPDX-License-Identifier: GFDL-1.3-no-invariants-or-later -->
<!-- SPDX-FileCopyrightText: 2020 Free Software Foundation, Inc. -->

# RISC-V emulation

In RISC-V mode, SIS emulates a RV32IMACFD processor as defined in the RISC-V
specification 1.9. Two different SOCs can be emulated, GRISCV and a CLINT base
system.

The GRISCV SOC uses the same peripherals and memory maps as a SPARC LEON3
processor. A CLINT based system uses a CLINT core for timers and UARTs with the
following address map:

| Address                 | Type           |
|-------------------------|----------------|
| 0x02000000 - 0x02100000 | CLINT          |
| 0x0C000000 - 0x0C200000 | PLIC           |
| 0x10000000 - 0x10000100 | NS16550 UART   |
| 0x20000000 - 0x21000000 | ROM (16 Mbyte) |
| 0x80000000 - 0x84000000 | RAM (64 Mbyte) |

The DTB (device-tree table) is located at the end of ROM (0x20FF0000).

## Power-down mode

The RISC-V power-down feature (WFI) is supported. When power-down is entered,
time is skipped forward until the next event in the event queue. Ctrl-C in the
simulator window will exit the power-down mode.

## Code coverage

Code coverage is currently only supported for 32-bit instructions, i.e. the
C-extension can not be used when code coverage is measured.

## RISC-V 64-bit timer

The standard RISC-V 64-bit timer is provided and can be read through the time
and timeh CSR.
