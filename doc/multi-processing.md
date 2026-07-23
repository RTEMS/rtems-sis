<!-- SPDX-License-Identifier: GFDL-1.3-no-invariants-or-later -->
<!-- SPDX-FileCopyrightText: 2020 Free Software Foundation, Inc. -->

# Multi-processing

When emulating a LEON3 or RISC-V processor, SIS can emulate up to four cores in
the target system (SMP). The cores are simulated in a round-robin fashion with
a time-slice of 50 clocks. Shorter or longer time-slices can be selected using
-d *clocks*.

To start SIS with SMP, use the switch -m *n* when starting the simulator where
n can be 2 - 4.
