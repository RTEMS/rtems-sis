<!-- SPDX-License-Identifier: GFDL-1.3-no-invariants-or-later -->
<!-- SPDX-FileCopyrightText: 2020 Free Software Foundation, Inc. -->

# LEON2 emulation

In LEON2 mode, SIS emulates a LEON2 system as defined in the LEON2 IP manual.
The emulated system includes the LEON2 standard peripherals, 16 Mbyte ROM and
16 Mbyte RAM. The SPARC emulation supports an FPU but not the LEON2 MMU.

To start sis in LEON2 mode, use the -leon2 switch.

## LEON2 peripherals

SIS emulates one LEON2 UART, the interrupt controller and the timer unit. The
interrupt controller is implemented as described in the LEON2 IP manual, with
the exception of the interrupt level register. Secondary interrupts are not
supported. The timer unit is configured with two timers and separate interrupts
(8 and 9). The scaler is configured to 16 bits, while the counters are 32 bits.
The UART generates interrupt 3.

## Memory interface

The following memory areas are valid for LEON2:

| Address                 | Type           |
|-------------------------|----------------|
| 0x00000000 - 0x01000000 | ROM (16 Mbyte) |
| 0x40000000 - 0x41000000 | RAM (16 Mbyte) |
| 0x80000000 - 0x80000100 | APB bus        |

Access to non-existing memory will result in a memory exception trap.

## Power-down mode

The LEON2 power-down register (0x80000018) is supported. When power-down is
entered, time is skipped forward until the next event in the event queue. A
Ctrl-C in the simulator window will exit the power-down mode.
