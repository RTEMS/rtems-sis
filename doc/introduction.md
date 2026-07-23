<!-- SPDX-License-Identifier: GFDL-1.3-no-invariants-or-later -->
<!-- SPDX-FileCopyrightText: 2020 Free Software Foundation, Inc. -->

[SIS manual](README.md)

# Introduction

SIS is a SPARC V7/V8 and RISC-V RV32IMACFD architecture simulator. It consist
of three main parts: an event-based simulator core, a cpu (SPARC/RISCV)
emulation module and system-specific memory and peripheral modules.

SIS can emulate six specific systems:

**`ERC32`**  
ERC32 SPARC V7 processor

**`LEON2`**  
LEON2 SPARC V8 processor

**`LEON3`**  
LEON3 SPARC V8 processor

**`GR740`**  
LEON4 SPARC V8 processor

**`GRISCV`**  
RISC-V (RV32IMACFD) processor with GRLIB peripherals

**`RV32`**  
RISC-V (RV32IMACFD) processor with CLINT and ns16550 UART

The LEON3/4 and RISC-V emulation supports SMP with up to four processor cores.
