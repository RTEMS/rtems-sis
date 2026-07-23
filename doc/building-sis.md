<!-- SPDX-License-Identifier: GFDL-1.3-no-invariants-or-later -->
<!-- SPDX-FileCopyrightText: 2020 Free Software Foundation, Inc. -->

[SIS manual](README.md)

# Building SIS

SIS uses the waf build system, and can simply be built using `./waf configure`
followed by `./waf`. The simulator is built out of tree and placed in
`build/sis`; `./waf install` installs it. The bundled `waf` script needs no
separate installation, only Python 3.

Everything the simulator does is built into `build/libsis.a`, and `build/sis`
is only an entry point calling `sis_main`. The unit tests link against the
library.

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

**`--enable-coverage`**  
Instrument the build for gcov and force `-O0`. Needs a GCC compatible
compiler. See [Unit tests](#unit-tests) below.

The simulator is C++17. On POSIX hosts it is built with GCC or Clang. Windows
builds are native and target MSVC; no MSYS or Cygwin runtime is involved.

## Unit tests

The tests are under `tests/`, one file per subject, built into a single
`build/tests/sis-test` binary that links against `libsis.a`. The framework is
doctest, vendored as `doctest.h`.

`./waf` builds and runs them and prints a summary; a failing case fails the
build. `--notests` skips them, `--alltests` forces a full re-run. The binary
can also be run directly, and takes the usual doctest options, so
`-tc=<name>` runs one case and `-s` shows every assertion.

The simulator keeps its state in globals, so the cases share one process.
A case that touches machine state starts from a fixture that sets `ms` and
`arch` and calls `reset_all`, and one that only needs a few globals restores
those. A case that inherits state from whatever ran before it is a bug in the
case.

### Coverage

The target is 100% line and branch coverage of every simulator source. To
measure it:

```shell
./waf distclean
./waf configure --enable-coverage
./waf
gcovr
```

`gcovr.cfg` holds the settings, so `gcovr` needs no arguments when run from
the top of the tree. Note this is coverage of the simulator's own code on the
host, which is a different thing from the
[code coverage](code-coverage.md) SIS collects for the program it emulates.

Two properties of the measurement are worth knowing.

The report only counts what the preprocessor emitted. Code behind an inactive
`#ifdef`, such as the `_WIN32` arms or `ENABLE_L1CACHE`, is neither covered
nor uncovered; it is absent. A percentage therefore describes one build
configuration, and the gate is the default configuration on Linux.

gcov counts arcs rather than source conditions, and every C++ function with a
non-trivial local emits arcs to a landing pad that only an exception reaches.
`gcovr.cfg` filters those out with `exclude-throw-branches` and
`exclude-unreachable-branches`, so a branch percentage means the filtered arc
set.

Coverage is gated per file rather than on the project total, because one file
regressing by thirty branches moves the total by about a tenth of a percent.
`tests/covered.txt` lists the files that have reached 100%, and

```shell
tests/check-coverage.py
```

fails if any of them is below 100% line or branch, or has dropped out of the
report. A file is added to the list by the commit that finishes it, and
entries are never removed: a file that regresses is fixed, not delisted.

Some code needs host resources. `tap.cc` opens `/dev/net/tun` and issues
`TUNSETIFF` and bridge ioctls, so its success paths need `CAP_NET_ADMIN` and
`CAP_NET_RAW` while its failure paths need the opposite. gcov accumulates
counts into the same data files across runs, so a privileged and an
unprivileged run in one work tree together cover both. Across separate CI
jobs, merge the reports instead with `gcovr --json` and
`gcovr --json-add-tracefile`.
