<!-- SPDX-License-Identifier: GFDL-1.3-no-invariants-or-later -->
<!-- SPDX-FileCopyrightText: 2020 Free Software Foundation, Inc. -->

[SIS manual](README.md)

# Commands

Below is the description of commands that are recognized by the simulator. The
command-line is parsed using cpp-linenoise. A command history of 256 commands is
maintained. Use the up/down arrows to recall previous commands. For more
details, see the cpp-linenoise documentation.

**`batch file`**  
Execute a batch file of SIS commands.

**`+bp address`**; **`break address`**  
Set a breakpoint at *address*.

**`bp`**  
Print all breakpoints.

**`delete num`**  
Delete breakpoint *num*. Use `bp` or `break` to see which number is assigned to
the breakpoints.

**`csr`**  
Show RISC-V CSR registers

**`cont [count]`**  
Continue execution at present position, optionally for *count* instructions.

**`dis [addr] [count]`**  
Disassemble \[*count*\] instructions at address \[*addr*\]. Default values for
*count* is 16 and *addr* is the present program counter.

**`echo string`**  
Print *string* to the simulator window.

**`float`**  
Print the FPU registers

**`gdb [port]`**  
Start the gdb server interface. Default port is 1234, but can be overriden
using the *port* argument. `gdb` should be started with
`target extended-remote localhost:1234`.

**`go address [count]`**  
Set pc to *address* and resume execution. If *count* is given, `sis` will stop
after *count* instructions have been executed.

**`help`**  
Print a small help menu for the SIS commands.

**`hist [trace_length]`**  
Enable the instruction trace buffer. The *trace_length* last executed
instructions will be placed in the trace buffer. A `hist` command without a
*trace_length* will display the trace buffer. Specifying a zero trace length
will disable the trace buffer.

**`load file_name`**  
Load an ELF file into simulator memory.

**`mem [addr] [count]`**  
Display memory at \[*addr*\] for \[*count*\] bytes. Same default values as for
the `dis` command.

**`quit`**  
Exits the simulator.

**`perf [reset]`**  
The `perf` command will display various execution statistics. A `perf reset`
command will reset the statistics. This can be used if statistics shall be
calculated only over a part of the program. The `run` and `reset` command also
resets the statistic information.

**`reg [reg_name] [value]`**  
Print or set the CPU registers. `reg` without parameters prints the CPU
registers. `reg` *reg_name value* sets the corresponding register to *value*.
Valid register names for SPARC are psr, tbr, wim, y, g1-g7, o0-o7 and l0-l7.
Valid register names for RISCV-V are mtvec, mstatus, pc, ra, sp, gp, tp, t0-t6,
s0-s11 and a0-a7.

**`reset`**  
Perform a power-on reset. This command is equal to `run 0`.

**`run [count]`**  
Reset the simulator and start execution from the entry point of the loaded ELF
file. If an instruction count is given (*count*), the simulator will stop after
the specified number of instructions. The event queue is emptied but any set
breakpoints remain.

**`step`**  
Execute one instruction and print it to the simulator console. Equal to command
`trace 1`

**`sym`**  
List symbols and corresponding addresses in the loaded program.

**`trace [count]`**  
Resume the simulator at the present position and print each execute instruction
executes. If an instruction count is given (*count*), the simulator will stop
after the specified number of instructions.

**`wmem addr data`**  
Write *data* to memory at *addr*. Data is written as a 32-bit word.

**`wp`**  
Print all watchpoints

**`+wpr address`**  
Adds an read watchpoint at address *address*.

**`-wpr num`**  
Delete read watchpoint *num*. Use *wp* to see which number is assigned to the
watchpoints.

**`+wpw address`**; **`watch address`**  
Adds an write watchpoint at *address*.

**`-wpw num`**  
Delete write watchpoint *num*. Use `wp` to see which number is assigned to the
watchpoints.

Typing a 'Ctrl-C' will interrupt a running simulator.

Short forms of the commands are allowed, e.g 'c' 'co' or 'con' are all
interpreted as 'cont'.
