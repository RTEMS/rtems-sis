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

  printf ("usage: sis [-uart1 uart_device1] [-uart2 uart_device2]\n");
  printf ("[-m <n>] [-dumbio] [-gdb] [-port port]\n");
  printf ("[-nouartrx] [-extirq number]\n");
  printf ("[-cov] [-nfp] [-ift] [-wrp] [-rom8] [-uben]\n");
  printf ("[-freq frequency] [-c batch_file]\n");
  printf ("[-erc32] [-leon2] [-leon3] [-gr740] [-griscv] [-rv32]\n");
  printf ("[-d] [-v] [-rt] [-bridge name] [files]\n");
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
