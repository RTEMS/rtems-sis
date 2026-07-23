<!-- SPDX-License-Identifier: BSD-2-Clause -->
<!-- SPDX-FileCopyrightText: 2024 Amar Takhar -->

SIS - Simple Instruction Simulator
==================================

SIS uses the waf build system, and can simply be built using:

  ```shell
  ./waf configure
  ```

followed by

  ```shell
  ./waf
  ```

The simulator is placed in `build/sis`. Install it with

  ```shell
  ./waf install --prefix=/usr/local
  ```

Building requires Python 3 for waf itself. The bundled `waf` script needs no
separate installation.

To enable emulation of an L1 cache, run configure with --enable-l1cache. This
option only improves timing accuracy, it does not affect simulation behaviour.

The optimization level defaults to -O2 and can be changed with
--enable-optimization=LEVEL.

The manual is in [doc/](doc/README.md). It is Markdown and needs no rendering
step.
