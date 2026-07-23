<!-- SPDX-License-Identifier: GFDL-1.3-no-invariants-or-later -->
<!-- SPDX-FileCopyrightText: 2020 Free Software Foundation, Inc. -->

[SIS manual](README.md)

# Interfacing to GDB

SIS can be connected to gdb through a network socket using the gdb remote
interface. Either start SIS with -gdb, or issue the 'gdb' command inside SIS,
and connect gdb with 'target extended-remote localhost:1234'. The port can be
changed using the -port option.
