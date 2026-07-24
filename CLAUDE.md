<!-- SPDX-License-Identifier: BSD-2-Clause -->
<!-- SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG -->

# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

SIS is a SPARC V8 / RISC-V (RV32) instruction-level simulator, historically used to
run and debug RTEMS (and other) binaries without hardware. It emulates several boards:
ERC32, LEON2, LEON3, GR740/LEON4 (all SPARC), and RISC-V/GRLIB and generic RV32 targets.
It provides an interactive command shell, a GDB remote-serial-protocol server, code
coverage collection, and basic Ethernet (tap device) emulation.

Derived from the upstream project by Jiri Gaisler (see printed banner in `sis.cc`).
This repo is a permanent fork for RTEMS tooling purposes; the build system and the
Windows support diverge from upstream and are not merged back.

## Build

waf, vendored as the tracked `waf` script (2.1.9); only Python 3 is needed to run it.

```shell
./waf configure
./waf
```

The simulator is built out of tree and lands in `build/sis`. `./waf install
--prefix=<dir>` installs it to `<dir>/bin`, honouring `--destdir`. `./waf distclean`
removes the build directory entirely.

Everything except the entry point is built into `build/libsis.a`; `main.cc` only
calls `sis_main`. The library exists so the unit tests can link the simulator
without a second `main`. It is internal and is not installed.

Adding a source file means adding it to `SOURCES` in `wscript`; there is no generated
makefile to keep in sync. Test sources need no build edit, `tests/wscript_build`
globs them.

Windows builds target MSVC and are native: no MSYS or Cygwin runtime. That
configuration is untested; it has never been built or run by its author.

Never pipe `./waf` into `head` or `grep -m`. The early exit sends SIGPIPE, the build
dies part way through, and the stale `build/sis` left behind looks like a successful
build.

Useful configure options:
- `--enable-l1cache` — emulate an L1 cache for timing accuracy only (no behavioral change).
- `--enable-optimization=LEVEL` — optimization level passed to `-O`, default 2.
- `--enable-coverage` — instrument for gcov and force `-O0`; GCC compatible compilers only.

Unit tests are doctest cases under `tests/`, built into one `build/tests/sis-test`
linked against `libsis.a`. `./waf` builds and runs them; a failure fails the build.
`--notests` skips them. They share one process, so a case that touches machine state
must start from a fixture that sets `ms`/`arch` and calls `reset_all`.

The target is 100% line and branch coverage of every simulator source, gated per file:
`tests/covered.txt` lists the files that have reached it and `tests/check-coverage.py`
fails if one of them slips. Settings live in `gcovr.cfg`; see `doc/building-sis.md` for
the workflow and for why a percentage describes one build configuration only. This is
host coverage of SIS itself, not the `-cov` target coverage SIS collects for the
program it emulates.

Unit tests do not replace running real target binaries (e.g. RTEMS test executables)
through the simulator, which is still the only end-to-end verification.

Redirect stdin from `/dev/null` when running a binary unattended. SIS polls stdin for
the emulated UART, so an inherited stdin that never delivers and never closes makes a
run block part way through and look like a hang in the emulated code.

The manual is Markdown under `doc/`, one file per chapter, indexed by
`doc/README.md`. It is not part of the build and needs no rendering step.

The RISC-V device tree blob is regenerated from `rv32.dts` by the explicit `./waf dtb`
target producing `rv32dtb.h`; only re-run this if `rv32.dts` changes (requires `dtc`
and `xxd` on `PATH`).

## Running

```shell
./build/sis [options] [file]
```

Key flags (see `help.c:sis_usage` for the authoritative list, or `sis -help`):
- Board select: `-erc32` (default) `-leon2` `-leon3` `-gr740` `-griscv` (RISC-V on GRLIB) `-rv32` (generic RV32/CLINT)
- `-gdb [-port N]` — start the GDB remote stub (default port 1234)
- `-r [-tlim N unit]` — run immediately instead of dropping into the interactive shell
- `-m N` / `-d N` — number of CPUs (MP simulation) and time-slice delta in clocks
- `-c <batch_file>` — run a batch of simulator commands at startup
- `-cov` — code coverage, written to `<loaded_file>.cov`

At the interactive prompt, `help` lists shell commands (`go`, `run`, `cont`, `step`,
`bp`/`+bp`/`-bp`, `dis`, `reg`, `mem`, `tra`, `hist`, `perf`, `cpu`, `batch`, `gdb`, ...).
Binary name prefix (`riscv*` / `sparc*`) also implicitly selects the architecture — see
top of `main()` in `sis.cc`.

## Architecture

The simulator is single-translation-unit-per-subsystem C, tied together through two
small vtable-style dispatch structs defined in `sis.h`:

- **`struct cpu_arch`** (`sis.h`) — per-ISA dispatch: instruction decode/execute,
  trap handling, interrupt checking, disassembly, register get/set, GDB register
  packing. Two instances: `sparc32` (`sparc.cc`) and `riscv` (`riscv.cc`). Selected via
  the global `arch` pointer in `func.cc`.
- **`struct memsys`** (`sis.h`) — per-board dispatch: init/reset, error mode,
  halt/exit, memory read/write (both the fast simulation path and the
  byte-oriented `sis_memory_read/write` used by GDB/ELF loading), boot init, IRQ
  injection. Instances: `erc32sys` (`erc32.cc`), `leon2` (`leon2.cc`), `leon3`
  (`leon3.cc`), `gr740` (`gr740.cc`), `rv32` (`rv32.cc`). Selected via the global `ms`
  pointer in `func.cc`, chosen from `-<board>` / `-<arch>` CLI flags or from CPU/arch
  info embedded in a loaded ELF file (see the board-selection logic in `sis.cc`'s
  `main()`).

Board files that model GRLIB-based SoCs (`leon2.cc`, `leon3.cc`, `gr740.cc`, `rv32.cc`)
build their peripheral memory map by registering AHB/APB devices (UART, timers, IRQ
controller, Ethernet, SDRAM controller, L2 cache, ...) with the shared bus model in
`grlib.cc`/`grlib.h` (`grlib_ahbs_add`, `grlib_apb_add`, `grlib_ahbm_add`,
`grlib_read`/`grlib_write`). `erc32.cc` is self-contained and does not use `grlib.cc`
(ERC32 predates the GRLIB peripheral set).

Three properties of that bus model decide where a new peripheral can go:

- An AHB slave decodes at 1 M granularity: the mask is 12 bits shifted left by 20, so
  1 M is the smallest window. Two cores less than 1 M apart cannot both be AHB slaves.
- An APB core decodes at 256 byte granularity, but only inside the 1 M window of its
  bridge (`grlib_apb_add` masks to `0x0fffff`). A board has a single APB bridge, so
  all of its APB cores must lie within one 1 M window.
- `grlib_init`/`grlib_reset` walk only the AHB master and slave lists. APB cores get
  their `init`/`reset` from the bridge, so an APB core reached through no bridge is
  never initialised.

A peripheral that moves data on its own (`greth.cc`) reads and writes emulated memory
through `ms->memory_read`/`ms->memory_write` for words, and through `ms->get_mem_ptr`
with the index XORed by `arch->bswap` for byte buffers.
Register writes should only arm the transfer: schedule the data movement and the
following `grlib_set_irq` from an `event()` callback, so that a driver is never
re-entered from inside its own store.

Other core pieces:
- `func.cc` — global simulator state (`sregs[]` per-CPU `pstate`, `ebase` event/
  breakpoint state), the event queue (`event()`/`now()`), the interactive command
  shell (`exec_cmd`), breakpoint/watchpoint checking, run loop (`run_sim`).
- `exec.cc` — shared SPARC integer helpers (multiply/divide) used by `sparc.cc`.
- `sparc.cc` / `riscv.cc` — the actual instruction decoders/interpreters and per-ISA
  trap/interrupt logic; these are the biggest files and where most CPU-behavior bugs
  live.
- `elf.cc`/`elf.h` — ELF loading; also detects the target CPU/arch from the ELF header
  to drive board auto-selection when no `-<board>` flag is given.
- `interf.cc` — the `sim_*` glue layer (`sim_read`, `sim_write`, `sim_resume`,
  `sim_insert_swbreakpoint`, ...) consumed by `remote.cc`'s GDB stub.
- `remote.cc` — GDB remote serial protocol server (`gdb_remote`), socket-based.
- `greth.cc` / `tap.cc` — emulated GRETH Ethernet device backed by a host tap device
  (networking; requires root/sudo for bridging, see `-bridge`).
- `linenoise.hpp` — vendored cpp-linenoise, the only line editor; GNU readline is
  not used. Header only, works on POSIX and the native Windows console. Included
  before `sis.h` in `sis.cc` because `sis.h`'s `CTRL_C` macro would otherwise
  replace the enumerator of the same name.
- `sisio.cc`/`sisio.h` — host I/O the standard library cannot express: non-blocking
  UART and console reads, and socket read/close. POSIX and native Windows
  implementations behind one interface. `-uart <device>` is POSIX only; on Windows
  the emulated UART is the console.
- `help.cc` — CLI usage (`sis_usage`) and interactive `help` text (`gen_help`); keep
  these in sync when adding/removing command-line flags or shell commands.

`struct pstate` (`sis.h`) is the per-CPU register/state block (IU registers, FPU
registers, RISC-V CSRs, cache emulation state, per-CPU statistics counters); `sregs[]`
in `func.cc` holds one per emulated core (`NCPU` = 4 max). Multi-core simulation
round-robins CPUs by time slice (`-d`/delta clocks), coordinated through the shared
event queue in `func.cc`.

Documentation source of truth for user-facing behavior (board peripheral maps, GDB
usage, coverage, networking, invocation options) is `doc/`; update it alongside
behavioral changes, matching `help.cc` output.

## Reference documentation

`ref/` holds offline reference manuals for the ISAs and peripherals SIS
emulates (SPARC V8, ERC32, LEON2, GRLIB/LEON3/GR740, RISC-V), converted to
Markdown/AsciiDoc for easy reading alongside the source, each with a
`<name>.toc.md` table of contents. These are third party documents, so `ref/`
is untracked and present only in a local working tree; a fresh clone has
none of it. Start at `ref/INDEX.md` for the
document-to-board map and `ref/SOURCES.md` for provenance/versions. The
GRLIB and RISC-V-current documents are deliberately scoped down to the
subset SIS actually implements rather than the full upstream manuals; see
`SOURCES.md` for what was left out and why.

## Conventions

- GNU-style C formatting, enforced by `.clang-format`. Run `uv run clang-format -i
  <file>...` on every `.cc`/`.h` file you add or touch before committing (`uv` picks up
  the `clang-format` version pinned in `pyproject.toml`). Skip `linenoise.hpp`,
  `doctest.h`, `elf.h`, and `rv32dtb.h` — they're vendored/generated and listed in
  `.clang-format-ignore`.
- `uint32`/`int32`/`uint64`/`int64`/`float32`/`float64` typedefs (`sis.h`) are used
  throughout instead of raw C types — match this in new code.
- GPLv3-or-later license header at the top of each simulator `.cc`/`.h` file; carry it
  over to new files. Everything under `tests/`, plus the build and coverage helpers
  (`gcovr.cfg`, `tests/check-coverage.py`, `tests/covered.txt`), is BSD-2-Clause
  instead, and new files of either license carry SPDX tags only, without the license
  paragraphs the older simulator sources repeat.
- `CODEOWNERS` requires markdown changes go through `@approvers/docs`; general code
  changes need `@approvers/general/maintainer`.

## Commit conventions

- Follow the [50/72 rule](https://deviq.com/practices/50-72-rule/): the subject
  line is at most **50** characters, then a blank line, then the body wrapped at
  **72** characters. `git log` indents by four spaces, so a 72-column body still
  fits an 80-column terminal.
- Subject line: `<scope>: Sentence-case`, at least 5 characters. The scope names
  the subsystem the change is confined to. A tree-wide change has no scope and
  starts with the sentence.
- Every commit needs a `Signed-off-by:` trailer. Only a human can certify the DCO, so
  never add one for an assistant.
- For assisted commits add an `Assisted-by:` trailer right before the `Signed-off-by:`,
  in the form `AGENT_NAME:MODEL_VERSION [TOOL1] [TOOL2]` defined by the kernel's
  `Documentation/process/coding-assistants.rst`. From Claude Code the agent name is
  `Claude` and the tool is `claude-code`. The model version is the ID of the model that
  did the work and differs between sessions, so read it off the session rather than
  copying an example:

      Assisted-by: Claude:claude-opus-4-8 claude-code
      Assisted-by: Claude:claude-sonnet-5 claude-code

- List only coding assistants and specialised analysis tools. Basic development tools
  such as git, the compiler or an editor are not listed.
- Do not add `Co-Authored-By:` or `Claude-Session:`.

## Style & Writing Rules

- Write directly and cleanly.
- NEVER use em dashes (—) or double hyphens (--) in code comments or git messages.
- Code comments must be concise and declarative.
- Avoid any parenthetical interjections, throat-clearing, or hedging phrases.
