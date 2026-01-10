/* This file is part of SIS (SPARC instruction simulator)

   Copyright (C) 1995-2017 Free Software Foundation, Inc.
   Contributed by Jiri Gaisler, European Space Agency

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include "config.h"
#include <stdio.h>
#include "sis.h"

void
sis_usage ()
{

  printf ("Usage: sis [options] [files]\n");
  printf ("\nOptions:\n");
  printf ("  [-help]              -> Display this help message\n");
  printf ("  [-<processor>]       -> Set the processor for emulation. -erc32 is set by default\n");
  printf ("                          Supported values: [ -erc32 | -leon2 | -leon3 | -gr740 | -griscv | -rv32 ]\n");
  printf ("  [-v]                 -> Enable verbose output.\n");
  printf ("  [-r]                 -> Start execution immediately without an interactive shell.\n");
  printf ("  [-tlim <val> <unit>] -> Can be used when -r is set. It sets the amount of time the simulator runs before exiting\n");
  printf ("                          <unit> can be s (seconds), ms (milliseconds), us (microseconds). E.g., -tlim 100 s\n");
  printf ("  [-c <batch_file>]    -> Execute a batch file of SIS commands at startup\n");
  printf ("  [-gdb]               -> Enable the GDB remote server.\n");
  printf ("  [-port <gdb_port>]   -> Set the port for GDB server. -port 1234 is set by default.\n");
  printf ("  [-cov]               -> Enable code coverage. Coverage data will be stored in file name same as loaded file.\n");
  printf ("                          appended with .cov extension. E.g., hello.exe will produce hello.exe.cov\n");
  printf ("  [-freq <frequency>]  -> Set the frequency of emulated cpu. <frequency> is integer indicating frequency in MHz.\n");
  printf ("  [-d <clocks>]        -> Set number of <clocks> in each time-slice when <cores> is greater than 1.\n");
  printf ("                          Default is 50, set lower for higher accuracy.\n");
  printf ("  [-rt]                -> Enable real-time mode. When enabled, SIS tries to sync the simulator time\n");
  printf ("                          to the host time.\n");
  printf ("  [-extirq <irq_code>] -> Inject a hardware interrupt as soon as the simulated program starts.\n");
  printf ("  [-ift]               -> Enables trace of every instruction fetch. Useful for debugging\n");
  printf ("  [-nfp]               -> Disable the simulated FPU. Each FPU instruction will generate FPU disabled trap.\n");
  printf ("  [-bridge <bridge>]   -> Connect the tap device used for networking to <bridge>. Requires running as sudo/root.\n");
  printf ("                          Typical values are br0 or lxcbr0.\n");
  printf ("  [-m <cores>]         -> Set number of <cores> in Leon3 or RISC-V. <cores> can range from 2 to 4\n");
  printf ("  [-uart1 <device>]    -> Connect UART1 (serial port) of the simulator to <device>. stdin/stdout is set as default.\n");
  printf ("  [-uart2 <device>]    -> Connect UART2 (serial port) of the simulator to <device>. Disabled by default.\n");
  printf ("  [-dumbio]            -> Use simpler I/O mode for restricted terminals.\n");
  printf ("  [-nouartrx]          -> Disables UART RX (Receive) by ignoring keyboard input ie sent to UART port.\n");
  printf ("  [-wrp]               -> Enables write protection in certain memory regions. (ERC32 only)\n");
  printf ("  [-rom8]              -> Simulate 8-bit ROM timing and data-bus behavior (ERC32 only).\n");
  printf ("  [-uben]              -> Enable handling of unaligned memory accesses (ERC32 only).\n");
}

void
gen_help ()
{

  printf ("\n batch <file>          execute a batch file of SIS commands\n");
  printf (" +bp <addr>            add a breakpoint at <addr>\n");
  printf (" -bp <num>             delete breakpoint <num>\n");
  printf (" bp                    print all breakpoints\n");
  printf
    (" cont [icnt]           continue execution for [icnt] instructions\n");
  printf (" cpu <core>            select cpu core for further commands\n");
  printf (" deb <level>           set debug level\n");
  printf
    (" dis [addr] [count]    disassemble [count] instructions at address [addr]\n");
  printf (" echo <string>         print <string> to the simulator window\n");
  printf (" float                 print the FPU registers\n");
  printf
    (" go <addr> [icnt]      start execution at <addr> for [icnt] instructions\n");
  printf (" hist [trace_length]   enable/show trace history\n");
  printf (" load  <file_name>     load a file into simulator memory\n");
  printf
    (" mem [addr] [count]    display memory at [addr] for [count] bytes\n");
  printf (" quit                  exit the simulator\n");
  printf (" perf [reset]          show/reset performance statistics\n");
  printf
    (" reg [w<0-7>]          show integer registers (or windows, eg 're w2')\n");
  printf
    (" run [inst_count]      reset and start execution for [icnt] instruction\n");
  printf (" step                  single step\n");
  printf (" tra [inst_count]      trace [inst_count] instructions\n");
  printf ("\n type Ctrl-C to interrupt execution\n\n");
}
