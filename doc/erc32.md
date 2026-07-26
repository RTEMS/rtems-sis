<!-- SPDX-License-Identifier: GFDL-1.3-no-invariants-or-later -->
<!-- SPDX-FileCopyrightText: 2020 Free Software Foundation, Inc. -->

[SIS manual](README.md) / [Emulated Systems](emulated-systems.md)

# ERC32 SPARC V7 processor

The radiation-hard ERC32 processor was developed by ESA in the mid-90's for
critical space application. It was used in the control computer for the
International Space Station (ISS) and also in the ATV re-supply ship for the
ISS. The sub-sequent single-chip ERC32SC was used in a multitude of satellites,
launchers and interplanetary probes, and is still being manufactured by Atmel.
See the ESA ERC32 page (<http://microelectronics.esa.int/erc32/index.html>) for
more technical documentation.

Sis emulates the original three-chip version of the ERC32 processor, consisting
of the integer unit (IU), floating-point unit (FPU) and the memory controller
(MEC). The IU is based on the Cypress CY601 SPARC V7 processor, while the FPU
is based on the Meiko FPU. The MEC implements various peripheral functions and
a memory controller. The single-chip version of ERC32 (ERC32SC/TSC695F) is
functionally identical to the original chip-set, but can operate at a higher
frequency (25 MHz)

The following functions of the ERC32 are emulated by sis:

- IU & FPU with accurate timing

- UART A & B

- Real-time clock

- General purpose timer

- Interrupt controller

- Breakpoint register

- Watchpoint register

- 16 Mbyte ROM

- 16 Mbyte RAM

## IU and FPU instruction timing.

The simulator provides cycle true simulation for ERC32. The following table
shows the emulated instruction timing for IU & FPU:

| Instruction       | Cycles |
|-------------------|--------|
| jmpl, rett        | 2      |
| load              | 2      |
| store             | 3      |
| load double       | 3      |
| store double      | 4      |
| other integer ops | 1      |
| fabs              | 2      |
| fadds             | 4      |
| faddd             | 4      |
| fcmps             | 4      |
| fcmpd             | 4      |
| fdivs             | 20     |
| fdivd             | 35     |
| fmovs             | 2      |
| fmuls             | 5      |
| fmuld             | 9      |
| fnegs             | 2      |
| fsqrts            | 37     |
| fsqrtd            | 65     |
| fsubs             | 4      |
| fsubd             | 4      |
| fdtoi             | 7      |
| fdots             | 3      |
| fitos             | 6      |
| fitod             | 6      |
| fstoi             | 6      |
| fstod             | 2      |

The parallel operation between the IU and FPU is modelled. This means that a
FPU instruction will execute in parallel with other instructions as long as no
data or resource dependency is detected. See the 90C602E data sheet for the
various types of dependencies. Tracing using the 'trace' command will display
the current simulator time in the left column. This time indicates when the
instruction is fetched. If a dependency is detected, the following fetch will
be delayed until the conflict is resolved.

The load dependency in the IU is also modelled - if the destination register of
a load instruction is used by the following instruction, an idle cycle is
inserted.

## UART A and B

UART A is by default connected to the console, while UART B is disabled. Both
UARTs can be connected to any file/device using the -uart1 and -uart2 options
at start-up. The following registers are implemented:

| Register                  | Address    |
|---------------------------|------------|
| UART A RX and TX register | 0x01f800e0 |
| UART B RX and TX register | 0x01f800e4 |
| UART status register      | 0x01f800e8 |

The UARTs generate interrupt 4 and 5 after each received or transmitted
character. The error interrupt is generated if overflow occurs - other errors
cannot occur.

## Real-time clock and general purpose timer A

Both timers are a scaler driving a 32-bit down counter, as in the MEC
specification: the real-time clock has an 8-bit scaler and interrupts at level
13, the general purpose timer has a 16-bit scaler and interrupts at level 12.

The scaler is modelled as an elapsed-time delta rather than as a register
counting down on every clock: a counter tick is scheduled scaler + 1 clocks
ahead, and a read of a running scaler returns its programmed value less the
time since the last tick. The timing a program observes is the same, but there
is no scaler register to reload, so the two scaler load bits of the timer
control register (GSL, bit 3, and RTCSL, bit 11) are accepted and have no
effect. Every other bit of that register behaves as specified, including RTCCR
resetting to one.

The following registers are implemented:

| Register                                    | Address    |
|---------------------------------------------|------------|
| Real-time clock timer                       | 0x01f80080 |
| Real-time clock counter program register    | 0x01f80080 |
| Real-time clock scaler program register     | 0x01f80084 |
| General purpose timer                       | 0x01f80088 |
| General purpose timer counter register      | 0x01f80088 |
| General purpose timer scaler program reg.   | 0x01f8008c |
| Timer control register                      | 0x01f80098 |

## Interrupt controller

The interrupt controller is implemented as in the MEC specification with the
exception of the interrupt shape register. Since external interrupts are not
possible, the interrupt shape register is not implemented. The only internal
interrupts that are generated are the real-time clock, the general purpose
timer and UARTs. However, all 15 interrupts can be tested via the interrupt
force register.

The following registers are implemented:

| Register                   | Address    |
|----------------------------|------------|
| Interrupt pending register | 0x01f80048 |
| Interrupt mask register    | 0x01f8004c |
| Interrupt clear register   | 0x01f80050 |
| Interrupt force register   | 0x01f80054 |

## Error handler

Errors are latched in the error and reset status register. Every error source
the MEC acts on has a mask bit and a reset-or-halt bit in the MEC control
register: an unmasked error halts the processor, or resets it when the
reset-or-halt bit is set, and a masked error raises interrupt level 1 instead.
The register survives a reset holding its cause, so a program can read after
reset whether it was a system, software, error or watchdog reset. An FPU
hardware error is latched and ignored, as in the specification.

The simulator has no error inputs, so only two errors arise on their own: the
IU entering error mode, and a MEC hardware error, which stands in for the
register parity errors of the hardware and is reported for a write of a
reserved bit of a MEC register. Setting bit 15 of the MEC control register,
which the specification leaves unused, is also taken as a MEC hardware error.
The IU hardware error and the FPU comparison error are reachable only through
fault injection: with the error write enable bit of the test control register
set, a write of bits 5-0 injects the error and the handler acts on it as if
the hardware had signalled it.

The system fault status register and the failing address register record the
cause and the address of a memory exception. Only a data access latches them,
so an instruction fetch leaves them alone.

| Register                        | Address    |
|---------------------------------|------------|
| System fault status register    | 0x01f800a0 |
| Failing address register        | 0x01f800a4 |
| Error and reset status register | 0x01f800b0 |

## Memory interface

The following memory areas are valid for the ERC32 simulator:

| Register                | Address        |
|-------------------------|----------------|
| 0x00000000 - 0x01000000 | ROM (16 Mbyte) |
| 0x02000000 - 0x03000000 | RAM (16 Mbyte) |
| 0x01f80000 - 0x01f800ff | MEC registers  |

Access to unimplemented MEC registers or non-existing memory will result in a
memory exception trap.

The memory configuration register is used to define available memory in the
system. The fields RSIZ and PSIZ are used to set RAM and ROM size, the
remaining fields are not used. Both are encoded as in the specification: RSIZ
selects 256 Kbyte at zero and doubles from there, PSIZ selects 128 Kbyte, so
the two largest RAM encodings and the largest ROM encoding ask for more memory
than the emulated board has. NOTE: after reset, the MEC is set to decode 128
Kbyte of ROM and 256 Kbyte of RAM. The memory configuration register has to be
updated to reflect the available memory. The simulator's own boot loader
programs the full 16 Mbyte of each.

The PROM width bit is read-only, as in the specification, and follows the
`-rom8` option rather than the written data.

The waitstate configuration register is used to generate waitstates. This
register must also be updated with the correct configuration after reset. It
is write-only, so reading it is a memory exception. The two PROM fields encode
no waitstates both at zero and at one, so their count is one below the field
from two upwards.

The memory protection scheme is implemented - it is enabled through bit 3 in
the MEC control register.

The following registers are implemented:

| Register                         | Address    |
|----------------------------------|------------|
| MEC control register             | 0x01f80000 |
| Memory control register          | 0x01f80010 |
| Waitstate configuration register | 0x01f80018 |
| Memory access register 0         | 0x01f80020 |
| Memory access register 1         | 0x01f80024 |

## Watchdog

The watchdog is implemented as in the specification, including the trap door
window, which is open from reset until either the program register is written
or the watchdog elapses. The input clock is always the system clock regardless
of WDCS bit in MEC configuration register, so the scaler is not divided by 16
and the timeout formula of the specification reduces to (WDS + 1) * (WDC + 1)
system clocks.

The following registers are implemented:

| Register                              | Address    |
|---------------------------------------|------------|
| Watchdog program/acknowledge register | 0x01f80060 |
| Watchdog trap door set register       | 0x01f80064 |

## Software reset register

Implemented as in the specification (0x01f800004, write-only).

## Power-down mode

The power-down register (0x01f800008) is implemented as in the specification.
During power-down, the simulator skips time until next event in the event
queue. Ctrl-C in the simulator window will exit the power-down mode.

## MEC control register

The following bits are implemented in the MEC control register:

| Bit | Name | Function                 |
|-----|------|--------------------------|
| 0   | PRD  | Power-down mode enable   |
| 1   | SWR  | Soft reset enable        |
| 2   | APR  | Access protection enable |
