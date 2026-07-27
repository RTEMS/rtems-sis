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

**`-bp num`**; **`delete num`**  
Delete breakpoint *num*. Use `bp` or `break` to see which number is assigned to
the breakpoints.

**`csr`**  
Show RISC-V CSR registers

**`cont [count]`**  
Continue execution at present position, optionally for *count* instructions.

**`disas [addr] [count]`**  
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

**`history [trace_length]`**  
Enable the instruction trace buffer. The *trace_length* last executed
instructions will be placed in the trace buffer. A `history` command without a
*trace_length* will display the trace buffer. Specifying a zero trace length
will disable the trace buffer.

**`load file_name`**  
Load an ELF file into simulator memory.

**`mem [addr] [count]`**  
Display memory at \[*addr*\] for \[*count*\] bytes. Same default values as for
the `disas` command.

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
Valid register names for SPARC are pc, npc, psr, tbr, wim, y, fsr, and the
window registers g1-g7, o0-o7, l0-l7 and i0-i7.

Valid register names for RISC-V are pc, the integer registers ra, sp, gp, tp,
t0-t6, s0-s11 and a0-a7 under their ABI names, the floating point registers
ft0-ft11, fs0-fs11 and fa0-fa7, and the control registers mstatus, mtvec,
mepc, mcause, mie, mip, mscratch, fcsr, fflags and frm. A control register is
written the way a `csrw` instruction writes it, so a field the core does not
implement is dropped rather than stored.

The register hardwired to zero, `g0` on SPARC and `zero` on RISC-V, cannot be
set.

**`reset`**  
Perform a power-on reset. The simulated time and the execution statistics are
cleared and every device is reset. Unlike `run`, this does not set the program
counter from the loaded file and does not execute anything.

**`run [count]`**  
Reset the simulator and start execution from the entry point of the loaded ELF
file. If an instruction count is given (*count*), the simulator will stop after
the specified number of instructions. The event queue is emptied but any set
breakpoints remain.

**`step`**  
Execute one instruction and print it to the simulator console. Equal to command
`trace 1`

**`trace [count]`**  
Resume the simulator at the present position and print each execute instruction
executes. If an instruction count is given (*count*), the simulator will stop
after the specified number of instructions.

**`cpu [num]`**  
Print the cpu the shell acts on, or select cpu *num*. Commands that show or
set registers apply to this cpu.

**`ncpu [num]`**  
Print the number of online cpus, or set it to *num*. See
[Multi-processing](multi-processing.md).

**`debug [level]`**  
Print the debug level, or set it to *level*. A higher level makes the
simulator narrate more of what it does.

**`shell command`**  
Run *command* through the host shell.

**`tlimit value [unit]`**  
Set a limit on simulated time. The *unit* is `us`, `ms` or `s`, microseconds
by default. The limit is counted from the present simulated time.

**`tcont value [unit]`**  
Continue execution at the present position for at most *value* of simulated
time. Equal to `tlimit` followed by `cont`.

**`tgo address [value [unit]]`**  
Set pc to *address* and resume, for at most *value* of simulated time. With no
*value* the entry point of the loaded file is used and no limit is set.

**`trun value [unit]`**  
Reset the simulator and start execution from the entry point of the loaded ELF
file, for at most *value* of simulated time. Equal to `run` with a time limit.

**`wmem addr data`**  
Write *data* to memory at *addr*. Data is written as a 32-bit word.

**`wp`**  
Print all watchpoints

**`+wpr address`**; **`rwatch address`**  
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
