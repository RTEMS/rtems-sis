<!-- SPDX-License-Identifier: GFDL-1.3-no-invariants-or-later -->
<!-- SPDX-FileCopyrightText: 2020 Free Software Foundation, Inc. -->

# Code coverage

Code coverage data will be produced if sis is started with the -cov switch. The
coverage data will be stored in a file name same as the file used with the load
command, appended with .cov. For instance, if sis is run with hello.exe, the
coverage data will be stored in hello.exe.cov. The coverage file is created
when the simulator is exited.

The coverage file data consists of a starting address, and a number of coverage
points indicating incremental 32-bit word addresses:

    0x40000000  0 0 0 19 9 1 1 1 1 0 .....

The coverage points are in hexadecimal format. Bit 0 (lsb) indicates an
executed instruction. Bit 3 indicates taken branch and bit 4 indicates an
untaken branch. Bits 2 and 3 are currently not used.

For RISC-V, code coverage is only supported for 32-bit instructions, i.e. the
C-extension can not be used when code coverage is measured.
