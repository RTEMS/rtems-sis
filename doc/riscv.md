<!-- SPDX-License-Identifier: GFDL-1.3-no-invariants-or-later -->
<!-- SPDX-FileCopyrightText: 2020 Free Software Foundation, Inc. -->

[SIS manual](README.md) / [Emulated Systems](emulated-systems.md)

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

### CLINT

The CLINT follows the register layout of the SiFive FU540-C000 manual, which
is what the RISC-V specification leaves to the platform: `msip` at offset 0,
`mtimecmp` at 0x4000 and `mtime` at 0xbff8, each per hart.

The least significant bit of an `msip` register is reflected in the MSIP bit
of that hart's `mip` CSR and the other bits read as zero. `mtime` is
readable and writable; writing it offsets the emulated clock rather than
disturbing it, and a reset returns it to the clock. A timer interrupt is
pending whenever `mtime` has reached `mtimecmp`, and both a write to `mtime`
and a write to `mtimecmp` re-evaluate that.

### PLIC

An interrupt source is delivered only if its priority register is non-zero;
priority zero is reserved and disables the source. Of the sources that are
pending and enabled, a claim returns the one with the highest priority, and
ties are broken by the lowest interrupt ID.

### NS16550

The UART is a 16550 at a four byte register stride, as specified by the
PC16550D datasheet. Offset 0x08 is the write-only FIFO control register and
the read-only interrupt identification register, not a scratch register, and
the line control register is readable as well as writable.

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
