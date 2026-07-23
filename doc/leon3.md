<!-- SPDX-License-Identifier: GFDL-1.3-no-invariants-or-later -->
<!-- SPDX-FileCopyrightText: 2020 Free Software Foundation, Inc. -->

# LEON3 emulation

In LEON3 mode, SIS emulates a LEON3 system as defined in the GRLIB IP manual.
The emulated system includes the standard peripherals such as APBUART, GPTIMER,
IRQMP and SRCTRL. The emulated system includes 32 Mbyte ROM and 32 Mbyte RAM.
The SPARC emulation supports an FPU but not the LEON3 MMU.

To start sis in LEON3 mode, use the -leon3 switch.

## LEON3 peripherals

The following IP cores from GRLIB are emulated in LEON3 mode:

| IP Core | Address    | Interrupt |
|---------|------------|-----------|
| APBMAST | 0x80000000 | \-        |
| APBUART | 0x80000100 | 3         |
| IRQMP   | 0x80000200 | \-        |
| GPTIMER | 0x80000300 | 8, 9      |
| GRETH   | 0x80000B00 | 6         |

## Memory interface

The following memory areas are valid for LEON3:

| Address                 | Type           |
|-------------------------|----------------|
| 0x00000000 - 0x01000000 | ROM (16 Mbyte) |
| 0x40000000 - 0x41000000 | RAM (64 Mbyte) |
| 0x80000000 - 0x81000000 | APB bus        |
| 0xFFFFF000 - 0xFFFFFFFF | AHB plug&play  |

Access to non-existing memory will result in a memory exception trap.

## Power-down mode

The LEON3 power-down register (%asr19) is supported. When power-down is
entered, time is skipped forward until the next event in the event queue. A
Ctrl-C in the simulator window will exit the power-down mode.
