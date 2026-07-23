<!-- SPDX-License-Identifier: GFDL-1.3-no-invariants-or-later -->
<!-- SPDX-FileCopyrightText: 2020 Free Software Foundation, Inc. -->

[SIS manual](README.md)

# Building SIS

SIS uses the waf build system, and can simply be built using `./waf configure`
followed by `./waf`. The simulator is built out of tree and placed in
`build/sis`; `./waf install` installs it. The bundled `waf` script needs no
separate installation, only Python 3.

The manual is not part of the build. It is Markdown under `doc/` and needs no
rendering step.

The following custom configure options are recognized:

**`--enable-l1cache`**  
Enable the emulation of a L1 cache in multi-processor systems. Each core in an
MP LEON3/RISC-V system will have a 4Kbyte instruction cache and a 4 Kbyte data
cache. The cache only affects instruction timing, and has no effect on
instruction behaviour.

**`--enable-optimization=level`**  
Set the optimization level the compiler is invoked with. The default is 2.

The simulator is C++17. On POSIX hosts it is built with GCC or Clang. Windows
builds are native and target MSVC; no MSYS or Cygwin runtime is involved.
