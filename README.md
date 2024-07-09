SIS - Simple Instruction Simulator
==================================

SIS uses the GNU autoconf system, and can simply be build using:

  ```shell
    ./configure 
  ```

followed by 

  ```shell
  make
  ```

To build a PDF version of the manual, do

  ```shell
  make sis.pdf.
  ```

To enable emulation of an L1 cache, run configure with --enable-l1cache. This option
only improves timing accuracy, it does not affect simulation behaviour.
