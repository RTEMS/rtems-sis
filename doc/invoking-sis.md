<!-- SPDX-License-Identifier: GFDL-1.3-no-invariants-or-later -->
<!-- SPDX-FileCopyrightText: 2020 Free Software Foundation, Inc. -->

[SIS manual](README.md)

# Invoking sis

The simulator is started as follows:

    sis [options] [file] 

The following options are recognized:

**`-help`**  
Display a help message

**`-erc32`**  
Emulate the SPARC V7 ERC32 processor

**`-leon2`**  
Emulate the SPARC V8 LEON2 processor

**`-leon3`**  
Emulate the SPARC V8 LEON3 processor

**`-gr740`**  
Emulate a (limited) GR740 SOC device

**`-griscv`**  
Emulate a GRISCV (RISCV/GRLIB) SOC device

**`-rv32`**  
Emulate a RISC-V RV32IMACFD processor with CLINT module.

**`-v`**  
Increase the debug level with 1, to provide more diagnostic messages. Can be
added multiple times.

**`-r`**  
Start execution immediately without an interactive shell. This is useful for
automated testing.

**`-tlim value unit`**  
Used together with *-r* to limit the amount of simulated time that the
simulator runs for before exiting. The following units are recognized: *us*,
*ms* and *s*. To limit simulated time to 100 seconds, use: *-tlim 100 s*.

**`-c file`**  
Read sis commands from *file* at startup.

**`-gdb`**  
Start a gdb server, listening on port 1234. An alternative port can be
specified with *-port nn*.

**`-port gdb_port`**  
Use *gdb_port* for the gdb server. Default is port 1234.

**`-cov`**  
Enable code coverage and write a coverage file at exit.

**`-freq freq`**  
Set frequency of emulated cpu. This is used by the 'perf' command to calculate
the MIPS figure for a particular configuration. The frequency must be an
integer indicating the frequency in MHz.

**`-d clocks`**  
Set the the number of *clocks* in each time-slice for multi-processor
simulation. Default is 50, set lower for higher accuracy.

**`-rt`**  
Real-time mode. When enabled, the simulator tries to synchronize the simulator
time to the wall (host) time. Useful for interactive programs. Enabled by
default when networking is used.

**`-extirq irq_num`**  
Set the external IRQ that injects a hardware interrupt *irq_num* as soon as the
simulated program starts executing.

**`-ift`**  
Enable the trace of every instruction fetch. Useful for debugging.

**`-nfp`**  
Disable the simulated FPU, so each FPU instruction will generate an FPU
disabled trap.

**`-bridge bridge`**  
Connect the tap device used for networking to the host *bridge*. Typical values
are br0 or lxcbr0. Requires running SIS with sudo/root.

**`-m cores`**  
Enable the number of *cores* (2 - 4) in a leon3 or RISC-V multi-processor
system.

**`-uart1 device`**  
Connect UART1 (console) of the simulator to *device*. stdin/stdout is default.
Character devices are a POSIX feature; on Windows the console is the only UART.

**`-uart2 device`**  
Connect UART2 (console) of the simulator to *device*. Disabled by default.

**`-nouartrx`**  
When this option is set, it disables UART RX (receive). It disables polling of
keyboard input to be sent to the simulated UART port.

**`-dumbio`**  
Switch to simpler I/O modes. Useful for environments which do not support
complex terminal features.

**`-wrp`**  
Enable write protection in certain memory regions for the ERC32 processor.

**`-rom8`**  
When enabled, it treats the ROM as an 8-bit wide device for ERC32 processors.
Useful when you want to simulate the timing and data-bus behavior of an 8-bit
ROM.

**`-uben`**  
Connect the console to UART B instead of UART A for ERC32. UART A is then left
unattached unless `-uart1` gives it a device.

**`file`**  
The executable file to be loaded must be an SPARC or RISCV ELF file. On
start-up, the file is loaded into the simulated memory.
