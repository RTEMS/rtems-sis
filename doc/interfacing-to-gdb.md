<!-- SPDX-License-Identifier: GFDL-1.3-no-invariants-or-later -->
<!-- SPDX-FileCopyrightText: 2020 Free Software Foundation, Inc. -->

[SIS manual](README.md)

# Interfacing to GDB

SIS can be connected to gdb through a network socket using the gdb remote
interface. Either start SIS with -gdb, or issue the 'gdb' command inside SIS,
and connect gdb with 'target extended-remote localhost:1234'. The port can be
changed using the -port option.

## Registers

The `g` packet carries the registers gdb numbers for the target: on SPARC the
globals, the current window, the floating point file and the control
registers, and on RISC-V the integer file, the program counter and the
floating point file.

The `p` packet reads one register and reaches further. On RISC-V a control
register is numbered by its address plus 65, so `mstatus` at 0x300 is register
0x341, and a floating point register answers with all eight bytes rather than
the four the `g` packet has room for. A register the target does not have
answers with an empty packet, which is how gdb is told to fall back on `g`.

Registers are written one at a time with `P`, under the same numbering.
