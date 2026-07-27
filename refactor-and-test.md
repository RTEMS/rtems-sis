# Refactor-and-test: driving SIS to 100% coverage

A handoff for the ongoing work of testing every simulator source to 100% line
and branch coverage, moving each subsystem behind a dependency-injected policy
template as it is tested. `erc32.cc` is the file in progress and `erc32_mec.h`
is the finished model. The scope is the whole simulator, not one board.

Read `CLAUDE.md`, sections "Refactoring for dependency injection" and the
coverage paragraph under "Build", first. This file is the operational
walkthrough of what those sections state as rules.

## The goal and why it is shaped this way

The target is 100% line and branch coverage of every simulator source. The gate
is per file, not a project total: `tests/covered.txt` lists the files that have
reached 100%, and `tests/check-coverage.py` fails the build if any listed file
drops below 100% line or branch, or falls out of the report. A file joins the
list in the commit that finishes it, and nothing is ever removed. This keeps a
finished file from silently regressing while the tree as a whole is still far
from complete.

Two properties are held alongside coverage:

- **Spec-driven correctness.** Register fields, reset values, reserved-bit
  behavior and interrupt levels come from the reference manuals. A test that
  encodes what the code does cannot catch what the code gets wrong.

  **Look in `/opt/eb-docs` before `ref/`.** It is an indexed device
  documentation corpus: per chapter chunks with page citations, a
  `REGISTERS.md` naming every register, errata, and a `whereis.py` search
  tool. Read `/opt/eb-docs/CLAUDE.md` for how to navigate it. It carries a
  newer GR740 manual than `ref/` does, and it is what finally specified
  `gr1553.cc` and `grspw.cc`.

  Those two files were twice written off in this plan as unspecified, on the
  strength of grepping `ref/gr740-users-manual.md` for the tables their
  comments cite and finding nothing. The tables were always in the manual:
  the markdown conversion drops 363 of its 587 tables as picture
  placeholders, and every table those two files cite is among the ones lost.
  `ref/gr740-table-index.md` now maps all 587 to a PDF page so the gap
  cannot read as an absence again. A missing table is a tooling failure
  until proven otherwise.
- **No performance regression.** Neither in the simulated timing model nor in
  the simulator's own throughput. See "Performance" below for how each is held.

Two kinds of testing are in play and neither replaces the other:

- Host unit tests (doctest cases under `tests/`, linked against `libsis.a`)
  drive individual subsystems and are what coverage measures.
- End-to-end runs of real target binaries through the simulator
  (`./waf test-run`) are the only proof a board still works. Every production
  change is checked against them.

## The mental model

A board file mixes hardware logic (worth testing exactly) with reaches into the
simulator globals (`ms`, `sregs`, `ebase`, `sis_verbose`, `ext_irl`, and so on).
Testing the logic through the globals means every case sets up global machine
state and the cases interfere through it. The refactor breaks that: a subsystem
becomes a class template on an environment policy. Its state and its
dependencies are injected.

- The board instantiates the template on a real environment whose methods
  forward to the exact globals the old code used, so the board behaves
  identically and the pre-existing tests pass unchanged.
- A unit test instantiates the same template on a test environment that holds
  its own state, so a case drives the subsystem in isolation with no globals to
  set up or restore.

Because the template body lives in a header, gcovr is told to measure headers
and each one is graduated like a source file. gcovr merges the two
instantiations (board and test) across translation units, but the requirement is
that the test environment alone reaches 100%: graduation must never depend on
exercising the board integration path.

The pattern is not universal. It applies where globals stand between the logic
and a test. It does not apply to the CPU cores, which already take a
`struct pstate *` and reach memory through the `memsys` vtable: those get a test
harness, not a refactor. It does not apply to host I/O either, which is tested
against real descriptors the way `tests/sisio.cc` does.

## Where the tree stands

Measured with `./waf configure --enable-coverage && ./waf`, branch metric,
4981 of 5143 arcs (97%). The totals move as headers join the filter and as
duplicated and dead code is removed, so compare per file rather than against
an older total. Read the per file rows of the gcovr report, not its `TOTAL`
line: that row counts the standard library headers the build pulls in, whose
arc counts move between runs for reasons that have nothing to do with this
tree.

| Area | Arcs | Taken |
|---|---|---|
| `sparc.cc` | 1133 | 1133 |
| `riscv.cc` | 1106 | 1106 |
| `grlib.cc` | 387 | 385 |
| `gr740.cc` | 42 | 41 |
| `leon3.cc` | 42 | 41 |
| `memscrub.cc` | 94 | 1 |
| `tap.cc` | 65 | 0 |
| graduated: `elf.cc`, `erc32.cc`, `erc32_cfg.h`, `erc32_error.h`, `erc32_mec.h`, `erc32_timer.h`, `erc32_uart.h`, `exec.cc`, `func.cc`, `getdelim.h`, `gr1553.cc`, `greth.cc`, `grspw.cc`, `help.cc`, `interf.cc`, `leon2.cc`, `remote.cc`, `remote_socket.h`, `riscv.cc`, `rv32.cc`, `sis.cc`, `sisio.cc`, `sparc.cc`, `uartport.cc` | | 100% |

Done, graduated in `tests/covered.txt`:

- **`erc32_mec.h`** the MEC interrupt controller. `template <MecEnv Env> class
  Mec` holds the interrupt registers and the level logic. Its environment
  provides `Verbose`, `Irl`, `ReportError` and `Log`.
- **`erc32_timer.h`** the RTC, the GPT and the watchdog. `Timer` is one
  template driven by a `TimerSpec`, so the two timers differ only by data;
  `Watchdog` is its own template with the trap-door state machine.
- **`erc32_error.h`** the error handler, the error and reset status register,
  the system fault status register and the failing address register. The five
  error sources collapse into one table, so the reset-or-halt decision is
  written once.
- **`erc32_cfg.h`** the MEC control, memory configuration, waitstate, I/O
  configuration and access protection segment registers, and the eleven
  globals their decode used to write. The memory access path reads what they
  decode to through accessors, and asks `WriteProtected` whether a RAM write
  is allowed.
- **`erc32_uart.h`** the two MEC UART channels, one template with a
  `UartSpec` each, holding the receive and transmit buffers and the status
  bits. Its environment is parameterised on the host port.
- **`uartport.cc`** the host side of an emulated UART, shared by `erc32.cc`,
  `leon2.cc` and `grlib.cc` rather than written once each. Not a template: it
  is plain code taking a `struct uart_port` and tested against real
  descriptors, the way `tests/sisio.cc` works.
- **`sparc.cc`** the SPARC V8 integer unit, its floating point unit, its trap
  and interrupt logic, and its disassembler. No refactor: the core already
  takes a `struct pstate *` and reaches memory through the vtable, so it got
  the flat memory of `tests/cpumem.cc` and nothing else.

`tests/erc32.cc` drives each template on its own small test environment with
no globals.

### The real environment, settled

A board has **one** `RealEnv`, holding every method any of its subsystems
asks for. Two rules make that work, and both are worth copying:

- **Declare its methods, define them after the subsystems.** Several of them
  reach back into a subsystem, which cannot be constructed until the class is
  complete. Declaring in the class and defining below the objects breaks the
  cycle, and removes the forwarding functions the alternative needs.
- **A subsystem the board has more than one of extends `RealEnv`, it does not
  stand beside it.** Two timers and two UART channels each need a binding per
  instance, which one environment object cannot carry:
  `RealTimerEnv<Thunk>` adds the event queue callback which ticks that timer,
  `RealUartEnv<Port>` adds the host port that channel moves bytes through, and
  both inherit the rest. The binding is a template argument, so it stays a
  compile time constant and the call is direct, and the subsystem template
  still names no global, which is what decision (vii) asked for.

Keep the environments, the specs which distinguish the instances, and the
instances themselves together, in that order, so the shape of a board reads in
one place.

## The order of work

### ERC32 first

**`erc32.cc` is done.** It is at 100% line and branch and is in
`tests/covered.txt`, along with the five headers its subsystems moved into and
the shared `uartport.cc`. The recipe has now been proven against every shape
the file had: host I/O setup, an error manager that halts and resets the
machine, a watchdog, a register-decoding configuration block and a UART.
Steps 6 and 7 below were absorbed into the steps before them, because
deduplication and dead-code removal shrank the file faster than the coverage
work grew.

One subsystem per step, each its own header and template with its own narrow
concept, all in `namespace erc32`. `erc32.cc` holds one `RealEnv` satisfying
all of the concepts; each test file holds a small `TestEnv` per subsystem.

1. ~~**`erc32_timer.h`** RTC, GPT and watchdog.~~ Done.
2. ~~**`erc32_error.h`** ERSR, SFSR, FFAR, `mecparerror`, `decode_ersr`.~~ Done.
3. ~~**`erc32_cfg.h`** MEC control, memory configuration, waitstates, I/O
   configuration, software reset, power down, the protection segments.~~ Done.
4. ~~**The shared host UART port module.**~~ Done, as `uartport.cc`. It
   removed 182 arcs from the tree for 44 covered ones, and took `erc32.cc`
   from 325 arcs to 227.
5. ~~**`erc32_uart.h`** the MEC UART register model.~~ Done.
6. ~~**`erc32_mem.h`** memory access dispatch.~~ Not needed as a separate
   step: the access path is covered where it stands, and the segment
   protection moved into `erc32_cfg.h` with it. Read the performance note
   below before touching it again.
7. ~~Leftovers and the commit that graduates `erc32.cc`.~~ Done.

### Then the rest of the simulator

1. ~~`sparc.cc`~~ Done. Then `riscv.cc`. The largest arc count, the
   best specs, and the only place where a defect means a wrongly emulated
   program rather than a wrong warning. No production refactor: the cores
   already take a `struct pstate *` and reach memory through the vtable.

   The harness is in place. `tests/cpumem.{h,cc}` is a flat-memory `memsys`
   with a 64 K window at address zero; an access outside it takes a memory
   exception, which is how a case reaches a core's fault paths. The
   `sparc_fixture` in `tests/sparc.cc` points `ms` and `arch` at it, clears
   the register file, and executes one instruction word through
   `arch->dispatch_instruction`.

   Two things to know before adding cases:

   - **`init_regs` does not clear the register file**, so a case inherits
     whatever the one before it left. The fixture clears `g[]` and `r[]`
     itself.
   - **`dispatch_instruction` executes the instruction at `sregs->pc`** and
     advances `pc` and `npc` itself. A call, a branch or a `jmpl` therefore
     computes its target and its saved address from `pc`, not from `npc`.
   - **`init_regs` also keeps the low five bits of the old `psr`**, so a
     window and a mode leak between cases. The fixture sets the whole
     register outright.
   - **The core swaps the index of a single floating point register on a
     little endian host**, so `f<n>` is not `fs[n]`. The fixture reaches them
     through `fs()`, `fsi()` and `fd()`, which do the same swap.
   - **The `opf` encodings are file-local to `sparc.cc`**, so the test
     transcribes them from appendix B rather than sharing them.

   `sparc.cc` is at **100% line and branch**, from 5 arcs when this started,
   and is graduated in `tests/covered.txt`.
   Covered: the integer, logical and shift operations with their condition
   codes, the loads and stores in every width and both address spaces, the
   atomic accesses, the register windows and their traps, `rett`, `ticc`, the
   state and ancillary registers, multiply and divide, tagged arithmetic, all
   sixteen integer and sixteen floating point branch conditions against every
   set of condition codes, the whole floating point unit including its holds
   and its deferred trap queue, trap entry and interrupt checking, the
   register access the shell and the GDB stub use, the disassembler, and the
   target coverage collection.

   Six bugs came out of it, each confirmed against the manual before the
   test was written: `LDSBA` and `LDSHA` not sign extending, `STDFQ` not
   privileged, a negative square root overwriting the trap type of the
   status register with the current exception field alone, the disassembler
   comparing the immediate flag against the character `'1'` so that
   `or %rs1, 0, %rd` never printed as a move, and, in `func.cc`, the
   end-of-time sentinel wrapping into the past.

   Reaching the last arcs also removed dead code rather than testing it:
   `disp_reg` and `creg3` were never called, `stparx` only ever saw a
   discarded result, and five switch defaults could not be entered. See
   "Exhaustive switches" below for the decoder restructure, which was worth
   far more than the arcs.

   Two things the flat memory had to grow for it:

   - **`flatmem_fail_write`** makes one word read but refuse a write. The
     atomic instructions have a fault path between their read and their
     write which no address of a plain window reaches, because a window
     fails a read wherever it fails a write.
   - **`covram` has no declaration in `sis.h`**, so the test declares the
     bitmap of `func.cc` itself to assert that a jump was recorded.

   One finding was left for the maintainer rather than fixed, because it
   would move the simulated timing fingerprints: `fpexec` records `frd`
   already swapped and `LDF` swaps before comparing, but `STF` compares
   before its swap, so a store charges its hold for the sibling register.
   It is timing only, not a wrong result.

   Then `riscv.cc`. **100% line and branch**, from 912 arcs when this
   started, and graduated in `tests/covered.txt`. Same shape as the SPARC
   core and it reuses the flat memory, which now swaps a sub-word address by
   `arch->bswap` rather than by the host so one window serves both.

   Nineteen arcs were the range check gcc emits on a switch whose case set
   already covers every value a masked selector can hold: the compressed
   quadrant and its three function fields, the two bit operation field of the
   compressed arithmetic group, the eight function codes of the M extension,
   the rounding mode field, the class a host `fpclassify` returns, and the
   format test each conversion case sat under after its group had already
   selected that format. Rule 6 closes them, with the commonest case chosen
   from the case counts six RTEMS binaries produce rather than guessed.

   Restructuring them first looked 5% slower on `crypt01` and was reverted on
   that reading. The measurement was the fault: sequential blocks rather than
   the interleaved A/B the method section requires. Alternating two prebuilt
   binaries showed no difference at all, 65.0 s against 64.8 s median, so the
   restructuring went back in.

   Eight bugs so far, each confirmed against `ref/` before the test:

   - **A divide by zero reached the host divide instruction**, so a guest
     program doing something the M extension explicitly defines killed the
     simulator with SIGFPE. The signed overflow was undefined in C for the
     same reason.
   - **`mulh` and `mulhsu` widened the unsigned register through `int64`**,
     which zero extends, so both returned the unsigned product.
   - **`fclass` reported every value which is not a number as signalling**,
     which the code marked as unfinished.
   - **A debugger write of a floating point register indexed the file flat**
     while the read side indexes the low half of the double register, so a
     write of `f1` landed in the boxing half of `f0`.
   - **The named register write knew `x1` to `x7` and nothing else**, under
     numeric names neither the disassembler nor the register display prints,
     and it carried `psr` and `g0` over from SPARC. `doc/commands.md` already
     documented the ABI names as valid, so the manual was right and the code
     was not.
   - **`c.jr` and `c.jalr` had no null pointer guard** while every other jump
     did, including the direct compressed ones. That pair is how a program
     built for the compressed set calls a function pointer, so `grv32imac`,
     the one configuration where the check matters, was the one without it.
   - **`MIE_MSIE` was missing from `riscv.h`** while the software interrupt
     it enables was already handled.
   - Two dead switch defaults in the arithmetic groups, one unreachable
     pseudo trap default, and an unreachable all zero word guard in the byte
     load which the compressed decoder already catches.

   One gap found and closed, protocol visible but additively: `remote.cc`
   handled `g` and `P` but not `p`, so a debugger could **write** a control
   register through `P` and never read one back. `p` now answers, through a
   new `gdb_get_regi` in `cpu_arch`, and a register the target does not have
   replies empty so a session which never sends `p` is unchanged. Widening
   the `g` packet was the alternative and was not taken, since its layout is
   what gdb already agrees with. Verified against a socket, not only through
   the vtable: `P` writing `mstatus` and `p` reading back the masked value.
   Its decoder has been through the exhaustive switch restructure already,
   and it is worth knowing that **it bought no speed there**: the RISC-V
   fields are masked to their width at extraction, so the compiler already
   knew the range and emitted no bounds check to remove. The SPARC gain came
   from switches whose scrutinee range the compiler could not see. Expect
   the restructure to pay only where the range is not already obvious.

2. `grlib.cc` and the GRLIB cores. **376 of 378, two arcs left.** One file
   behind four boards. `tests/grlibcore.h` is the harness: it drives one
   `struct grlib_ipcore` through its own `read`, `write`, `init` and `reset`
   with no bus, because `grlib.cc` cannot unregister a core and does not
   bound `grlib_ahbs_add`/`grlib_ahbm_add`. It clears the event queue and
   resets the core, which is what stops a case inheriting a running timer or
   a latched engine flag. `tests/irqmp.cc` is the worked example.

   **What `ref/` actually specifies decides the order, and it is narrower
   than the file list suggests.** `SOURCES.md` says the GRLIB manual was
   converted for eight cores only. Checked per core:

   - **IRQMP**: complete, sections 96.3.1 to 96.3.12. Done first for that
     reason, and because every other core raises through `grlib_set_irq`, so
     a test of one of them asserts on `ext_irl` and needs IRQMP working.
   - **GRETH**: chapter 53, enough to work from. Covered behind
     `tests/faketap.cc`, which redefines `sis_tap_init` and `sis_tap_write`
     so the linker never extracts `tap.o` and no real device is opened.
   - **GPTIMER**: only the scaler pair, tables 464 and 465. The counter,
     reload and control registers are **not** in `ref/`.
   - **APBUART, L2C, DSU3, APBCTRL**: partial. The APBCTRL chapter is a VHDL
     component declaration with no register table.
   - **GR1553B and GRSPW2**: **nothing.** Chapter 16 of the GR740 manual has
     the descriptor formats and the operation sections, but the register
     tables the code cites (292, 321, 326) are in the full GRIP manual,
     which was not converted for either core.

   That last one is 348 of the remaining arcs and is an **open decision**:
   either convert those GRIP chapters into `ref/`, or cover the two files as
   frozen behaviour with an explicit note that the cases assert what the
   code does rather than what the manual says. Do not let it happen by
   accident.

   A case which pins current behaviour rather than a specified requirement
   says so in its name and its comment. When a defect is then fixed, those
   are exactly the cases which have to change, and the label is what makes
   that obvious rather than alarming.

   Four defects found and fixed here so far: the scaler value register had
   no write case, the timer control register cleared the interrupt pending
   bit on every write so its write one to clear test was dead code, a
   control write which disabled a timer left the underflow queued so it
   still wrapped the counter, and the interrupt controller picked its mask
   with a test the -1 default of the extended interrupt line passed. Only
   the first and last are backed by a table in `ref/`; the two control
   register fixes are inferred from the code's own stated intent and should
   be re-checked if the GPTIMER chapter is ever converted.

   **Two documents were added to `ref/` on 2026-07-27 to close the RV32
   gap**, both indexed in `INDEX.md` and `SOURCES.md`. The SiFive FU540-C000
   manual specifies the CLINT and PLIC memory maps, which no RISC-V
   specification fixes and which `rv32.cc` follows exactly: its table 36
   matches `grlib.cc`'s `msip` at 0, `mtimecmp` at `0x4000` and `mtime` at
   `0xbff8`. The PC16550D datasheet specifies the `ns16550` core. That core
   is a 16550 at a 4-byte register stride, not the hybrid it looks like: the
   `0x80` mask on LCR is DLAB, the `0x60` read of LSR is the master reset
   value, and the interrupt condition is IER bit 1 gated by MCR bit 3. Only
   the identifier `uart_txctrl` is SiFive naming, on the register the
   datasheet calls IIR/FCR.

   **Six defects found here and left unfixed**, each pinned by a case named
   as a suspected defect so a silent change is loud:
   - `clint_read` shifts `mip` right by 4 for `msip`, which is bit 3, so a
     read of it is always zero.
   - `clint_write`'s mtimecmp range test is inclusive of `CLINT_TIMEBASE`,
     so a write to `mtime`'s low word lands in hart 3's `mtimecmp`.
   - `plic_check_irq` scans without a break and so claims the **highest**
     pending id; section 10.3 gives the lowest id the highest priority.
   - `plic_check_irq` never reads `plic_prio`, so a source left at the
     reserved priority zero still interrupts.
   - `ns16550` offset `0x08` is a scratch register, where 8.5 and 8.6 make
     it a write-only FCR and a computed IIR.
   - `ns16550` has no read case for LCR, which 8.1 makes readable.

   `func.cc`'s `remove_event` is a seventh, found while chasing an
   unreachable GPTIMER guard: after unlinking a node it advances
   `ev1 = ev1->nxt` unconditionally, skipping the node it just spliced in,
   so two consecutive matching entries leave the second queued. **Fixed**,
   with `tests/event.cc` written first and shown to fail without it. A first
   attempt was reverted after it hung, and the cause was misrecorded as an
   ordering fault in `advance_time`; see the lesson on that below. The real
   cause was `tests/rv32board.cc`, removed in the same revert, and the repair
   went back in unchanged once that file was gone. The end to end runs report
   the same 234 passes and an unchanged fingerprint, so the stale entries were
   latent in those binaries rather than firing.

   **Two arcs are left in `grlib.cc` and both are honest gaps, not
   oversights.** `grlib_apb_bus`'s test for a bridge that no board has
   configured yet is only reachable before any case in the whole binary has
   added one, and `tests/grlibcores.cc` adds both. `apbuart_flush`'s test
   for a port that closed with bytes still queued needs `porta` closed,
   which is process global and whose `fout` other files still write through;
   covering it would trade one arc for exactly the cross-file fragility this
   round spent its time removing. Leave them.

   **Three unreachable branches were removed rather than documented.** An
   agent reported the first as "dead by construction, not a defect", which
   is true and is also a permanent hole in a 100% target, so the branch went
   instead: `grlib_read` tested a result variable every assigning path had
   already returned on. The other two became asserts the way the exhaustive
   switches did, the PLIC read ladder's last arm and the timer callback's
   test of its own enable bit. Note the second only became unreachable when
   `remove_event` was fixed: before that, `gpt_reset` removed every other
   queued callback, so one could survive a reset and arrive at a timer whose
   enable bit was clear. The asserts are live in the shipping build, which
   carries no `NDEBUG`, and stay quiet across all 234 end to end runs.
3. `leon2.cc`, `leon3.cc`, `gr740.cc`, `rv32.cc`. Thin once `grlib.cc` and the
   shared port module are done.

   **`leon2.cc` is done, 140 of 140 arcs, graduated.** Its last nine arcs
   were closed by removing dead code rather than by new cases, which is
   rule 6, and the pre-existing cases in `tests/leon.cc` cover what is
   left:

   - `chk_irq` searched bits 15 down to 1 for the highest pending
     interrupt and could fall out of the loop. It cannot: the value it
     searches is masked to bits 15 to 1, so a nonzero value always has
     one. The search is now the loop condition, which both arms reach.
   - `grlib_read_uart` and `grlib_write_uart` each decoded a three-way
     switch with a verbose complaint for an unimplemented register.
     `apb_read`, `apb_write` and `uart_intr` are the only callers and they
     pass the data or the status register and nothing else, so the
     complaint was unreachable. Both are a single test now.
   - `store_bytes` switched on the two-bit store size with all four cases
     named, so the no-case-matched arc was dead. It follows `erc32.cc`'s
     shape now, an if chain whose last arm needs no test.
   - `apb_read` and `apb_write` always returned `MOK`, and `memory_read`
     tested the result for a memory exception that could not happen. Both
     return void.

   Removed with them: seven unused locals in `memory_write`,
   `irqctrl_intack` and `leon2_reset`, and the unused `tmp` of
   `grlib_read_uart`.

   **`leon3.cc` stays at 38 of 42 and does not graduate.** Both remaining
   arcs are in `init_sim`, which cannot be called from the shared test
   binary; see the lesson below, and the header comment of
   `tests/leon.cc`, which records the probe that confirmed it. Nothing
   short of a teardown hook in `grlib.cc` closes them.
4. `func.cc`, `elf.cc`, `interf.cc`, `remote.cc`, `sis.cc`. Simulator
   infrastructure with no hardware spec; `doc/` and the GDB remote protocol are
   the references. `func.cc` may deserve to move ahead of step 2 if its harness
   unlocks the board work.

   **`elf.cc` is done, 95 of 95 arcs, graduated.** Its reference is the
   System V ABI, chapters 4 and 5. Four of the last six arcs were the
   unswapped side of the loader's byte swap: `tests/elf.cc` only ever
   built big-endian files, so a file matching the little-endian host
   never reached the body. `build_elf` now writes every field in the
   encoding its `EI_DATA` names, and the new case loads a little-endian
   file whose segment carries a load address separate from its virtual
   address, so the program header fields are load bearing rather than
   merely read.

   The remaining two are allocation failures, and they are reachable
   without contorting anything. `tests/elf.cc`'s `address_space_cap` caps
   `RLIMIT_AS` just above what the process already maps, reading the
   current size out of `/proc/self/statm`, so a section header naming
   four gigabytes fails its allocation while every ordinary allocation in
   the case still succeeds. The limit is restored on the way out. Use the
   same seam for any other allocation failure a file has to cover.

   Writing that case is what showed the section name table allocation was
   not checked at all, while the section contents allocation beside it
   was: a malformed size handed a null pointer straight to `fread`.
   **Fixed**, in its own commit ahead of the case that covers it.

   One dead arc went rather than being covered: `read_elf_body` tested its
   file pointer for null after having already read through it, and
   `elf_load` only calls it when `fopen` succeeded.

   **The mutation audit found three unasserted lines**, two of them in
   `tests/leon.cc` and one order dependence in `tests/elf.cc`, all fixed
   here. `reset_all` leaves emulated memory alone, so the cases asserting
   the loader wrote a word were passing on what an earlier case had left
   at the same address; the fixture zeroes it now. Nothing checked that a
   LEON2 APB access reports no memory exception and no waitstate, nor
   what waitstate count `store_bytes` reports, so faulting every APB
   access and charging seven waitstates on every store both changed
   nothing any case could see. Do this pass before claiming a file.

   One survivor is left and is honest. Breaking the test that reads the
   section name table header survives, because a garbage header reaches
   the allocation and the read below, which reject the file for their own
   reasons and produce the same `File read error`. The guard is covered
   and the case does assert the rejection; the failure simply cannot be
   told apart from the one downstream.

   **The stack overflow reported here is fixed.** `read_elf_body` read
   `e_phnum` program headers into `Elf32_Phdr ph[16]` with no bound, and
   `e_phnum` is a 16-bit field of the file, so a file naming more than
   sixteen overran the array. `e_phentsize`, also from the file, set the
   size of each `fread`, so a large value overran it even within sixteen
   entries. The array is allocated from the count now, and a read takes
   only what one header holds, leaving `e_phentsize` as the file's stride.

   It was reported as unpinnable, on the grounds that a case triggering it
   would corrupt the test binary's stack. That is true of a malformed file
   and not of a well formed one: `build_elf` takes a program header count,
   and sixty-four headers all naming the same segment load exactly as one
   does. Restoring the fixed array makes that case abort under glibc's
   fortified `fread` before it can corrupt anything. **A case that cannot
   be written safely against a malformed input is often writable against a
   valid one**, and the allocation failure beside it went the same way:
   `e_phnum` at 65535 asks for two megabytes, which `address_space_cap`
   refuses, so that arc is covered rather than left as a gap.

   **`func.cc` is done**, 100% line and branch, graduated in
   `tests/covered.txt` along with the new `getdelim.h`. Four things are
   worth carrying forward:

   - **The local `getline()` moved into `getdelim.h` behind an allocator
     policy.** Its two allocation failure returns are real error handling
     that must not be deleted and that no test can reach through
     `malloc()`, so the function became `sis::GetDelim<Alloc>` with
     `sis::HostAlloc` for the simulator and a fail-on-demand allocator in
     the test. The move also fixed a leak: the old code assigned the
     result of `realloc` straight back over the pointer it passed in, so
     a failure lost the buffer.
   - **Two `ebase.simtime == 0` guards were dead.** The `run` and `trun`
     commands both set `ebase.simtime = 0` a few lines above the guard
     that tests it, and nothing between the two can move the clock. Both
     were removed; `trun` keeps its separate `sregs->pc != 0` test.
   - **`dis_mem`'s top of memory guard compared the wrong variable.**
     `if (i >= 0xfffffffc)` tested the loop counter, which is bounded by
     the requested instruction count, where the address is what the loop
     wraps. Fixed to test `addr`.
   - **`advance_time` dereferences a null queue head if the end of time
     is the last entry left.** It unlinks the entry it is about to run
     and reads the new head before calling the callback, and nothing can
     be queued later than `last_event` at `UINT64_MAX`. So the "end of
     time ... exiting" warning is only reachable with a second event at
     the same time, which is what its test queues. Latent, since it
     needs 2^64 simulated cycles, and left for the maintainer rather
     than guarded in the run loop.

   Two things a case in `tests/funcq.cc` has to know. The fixture now
   points every core's `intack` at a local no-op, because `sparc.cc`
   calls it through the `pstate` on the way into an interrupt trap and
   the field carries whatever board ran last. And a case that marks the
   target coverage bitmap must work above the 64 K window of
   `tests/cpumem.cc`: the core test files record coverage at the
   addresses they execute at, which are all inside it.

   The `gdb` command is driven without a debugger by holding its port in
   the case itself, so `create_socket` fails to bind and `gdb_remote`
   returns instead of blocking in `accept`. Do not drive it any other
   way without a plan for the hang.
5. `greth.cc`, `tap.cc`, `grspw.cc`, `gr1553.cc`. Last. They need host tap
   devices and privileges, they model only what the GRLIB example applications
   exercise, and they are where 100% is most likely to force a restructure
   rather than a test.

## The recipe, two commits per subsystem

The board is production code, so keep it working at every step.

1. **Move the subsystem into its header, behind the existing tests.** State and
   logic into `erc32_<sub>.h` as a template on a new concept. In `erc32.cc`,
   add the forwarding methods to `RealEnv` and replace the old code with calls
   into the template. Add the header to the `gcovr.cfg` filter so it is
   measured. **Do not touch the tests, and do not add the header to
   `tests/covered.txt`.** The pre-existing cases must still pass, which is the
   evidence that the board is unchanged.

2. **Name the injected code Google style.** CamelCase types and methods,
   private members with a trailing underscore, cheap accessors named like the
   member they return. GNU `.clang-format` layout is unchanged, so only the
   identifiers differ. Run `uv run clang-format -i erc32_<sub>.h erc32.cc`.

3. **Prove the board still works.** `./waf test-run --fast`, and
   `./waf test-run --perf` with the fingerprints byte-identical. Do this after
   every production change, not just at the end. Drop `--fast` before the commit
   that finishes the file.

4. **Rewrite the subsystem's tests onto the test environment.** Its
   `mec_fixture` cases become cases that construct the template on a `TestEnv`
   and assert exact values from the manual. Assert the reset state explicitly
   first, the way the interrupt cases do with `REQUIRE (rd (R_IMR) == 0x7ffe)`.
   Keep one small integration case going through `erc32.cc`'s register dispatch
   on the real environment, so the board's dispatch arcs are covered. The bulk
   of the logic coverage comes from the isolated cases. The header joins
   `tests/covered.txt` in this commit.

5. **A bug found on the way is its own commit, landing between 1 and 4.** The
   defect surfaces while writing the spec tests, so it cannot precede the move,
   but it must precede the test that covers it. Confirm against
   `./waf test-run`, and A/B a behavioral change against an unmodified tree.
   Never let a test bake in wrong behavior.

6. **Dead branches are restructured away, not marked.** A branch no test can
   reach because it is genuinely unreachable is removed, or the code is
   reshaped so the arc no longer exists. No coverage-suppression markers.
   Confirm with `./waf test-run` that the restructure is behavior-neutral.

### Environments

An environment exposes named, intent-revealing methods. It never takes a C
function pointer. A timer says

    env_.ScheduleRtcTick (scaler + 1);

and `RealEnv` turns that into `event (rtc_intr, 0, delta)`, where `rtc_intr` is
a two-line thunk in `erc32.cc` calling back into the template. A `TestEnv`
records the delta and the call count, and the case advances the subsystem by
calling its tick method directly. The template never names a C function, and a
test environment stays a handful of counters instead of a fake event queue.

Where a subsystem schedules more than one kind of event, it gets one named
method per kind rather than an enum or a callback.

## Exhaustive switches, and what they were costing

A `switch` whose case labels already cover every value of its selector still
has an implicit no-case-matched arc. Nothing can take it, and the gate counts
it. Rule 6 says restructure rather than mark, so the decoder's five exhaustive
switches each name a case as `default:`.

**Pick the commonest case, and measure which that is.** Build with
`--enable-coverage`, run the real target binaries rather than the unit tests,
and read the branch percentages out of `gcov -b`. The unit tests exercise
conditions uniformly by construction and will mislead you. For `sparc.cc` the
answer was the arithmetic group at 60% of instruction formats, branch on equal
at 39% of conditional branches, and trap always at 100% of conditional traps.
Two of the five switches are reached by no target binary at all, so there the
choice cannot matter and the comment says so rather than implying data.

**This is a performance change, not just a coverage one.** The decoder runs on
every instruction, so removing its bounds checks compounded: erc32 crypt01 went
from 82.3 to 73.9 seconds, 10% faster, measured with interleaved runs against a
worktree of the parent commit. Expect the same shape in `riscv.cc`.

**An assert costs an arc in the coverage build.** The invariant a `default:`
now swallows is worth asserting, but the coverage build is unoptimized, so the
assert survives as a branch whose failing side nothing can take. The build
therefore defines `NDEBUG` when coverage is on, which is documented in
`doc/building-sis.md`. Without that the five asserts cost exactly the five arcs
the restructure had removed.

## When the manual and the code disagree

The spec is the default authority. A deviation is legal only when it is
deliberate, documented and tested as a deviation.

- **The code is wrong and nothing depends on it.** Fix it, own commit, test
  after.
- **The code is wrong and `./waf test-run` goes red when it is fixed.** The
  RTEMS binaries depend on the behavior. Do not fix it silently and do not bake
  it into a test. Stop, and put the manual section and the failing run in front
  of the maintainer. Whether RTEMS may rely on it is not a call to guess.
- **The code deviates knowingly for simulation reasons.** `UART_TX_TIME` and
  `UART_RX_TIME` at 1000 clocks, `MEC_WS` at zero waitstates, the trap entry
  jitter, `SIM_LOAD` at `0x0F0` in an address range the MEC manual marks
  unimplemented. The hardware has no such numbers. Test the simulator's
  contract ("the UART schedules its transmit at the configured interval"), name
  the spec section the test is not asserting in the test file header, and give
  the deviation a line in `doc/`.

## Performance

Two different properties, each held where it has signal.

- **The simulated timing model must not move.** A refactor is behavior-neutral
  by construction, so `./waf test-run --perf` must return fingerprints
  identical to the parent commit's. Movement means the move changed behavior.
  That is a defect in the move, not a reason to re-bless
  `tests/exe/perf-baseline.txt`.
- **Host throughput is measured where it is at risk.** That is the memory
  access dispatch step and, later, the decoders. `-O2` build, full
  `./waf test-run` wall clock (crypt01 dominates), A/B against the parent
  commit, and investigate anything worse than 2%. **Build both binaries to
  fixed paths first, then alternate them run by run.** Timing one build in a
  block and the other in a block afterwards does not work here: the host
  drifts by more than 2% over the minutes a rebuild takes, and a sequential
  A/B hands that drift to whichever build was timed during the slow stretch.
  It survives repetition, so it looks like a solid reading rather than
  noise.

The dispatch-style question itself is settled and must not be re-benchmarked. A
micro-benchmark and an end-to-end crypt01 run compared three styles for the hot
memory path: today's function-pointer vtable (~108.8s), a template with an
out-of-line global implementation (~109.1s, a tie), and a template with the body
inlined into the loop (~119.4s, about 10% slower from register and instruction
cache pressure). A monomorphic function-pointer call is already branch-predicted
and costs nothing. So keep the `memsys`/`cpu_arch` vtables, and where a hot path
is converted for testability, keep the body in a global function reached by a
static template call.

## Build, gate and test commands

```shell
# Coverage build and the per-file gate
./waf configure --enable-coverage && ./waf
tests/check-coverage.py                 # runs gcovr via gcovr.cfg

# A readable branch report for one file
gcovr --txt-metric branch --filter 'erc32.*'

# End-to-end runs (the only proof a board still works)
./waf test-run --fast                   # skips crypt01
./waf test-run --perf                   # timing fingerprints, must not move
./waf test-run                          # full set, before a finishing commit

# Non-coverage build for normal development (coverage forces -O0)
./waf configure && ./waf
```

Gotchas that have cost time here:

- `--enable-coverage` forces `-O0`. Switch back to a plain `./waf configure`
  when you are done measuring, or an exec-test timing looks like a regression.
- waf caches test runs. Touch a source file to force the unit tests to re-run.
- Never pipe `./waf` into `head`/`grep -m`. The SIGPIPE kills the build partway
  and leaves a stale `build/sis` that looks like a success.
- Redirect stdin from `/dev/null` when running a binary unattended, and note the
  UART tests use `stdin_feed` for exactly this reason: SIS polls stdin for the
  emulated UART.
- Unit tests share one process. A case that touches machine state must start
  from a fixture that sets `ms`/`arch` and calls `reset_all`.

## Lessons learned

Hard-won here, so the next person does not rediscover them.

- **Check that the spec is actually in `ref/` before planning to test from it.**
  The ERC32 work ran for several commits believing it had the manual. `ref/`
  held the TSC691E Integer Unit manual, which documents the IU, its traps and
  its pipeline. Every register the remaining subsystems implement lives in the
  TSC693E Memory Controller manual, which was missing. It is there now, but the
  same trap is waiting for any file whose peripheral spec nobody has opened.

- **Collapsing duplicated logic into a table exposes the rows that are
  missing.** The error handler wrote the same reset-or-halt block three times,
  once per error source it knew about. As a loop over a table of sources, the
  two rows the manual lists and the code never had were plain to see: an IU
  hardware error and an FPU comparison error latched and were then ignored.
  Look for the same shape in `grlib.cc` and the UART.

- **A wrong decode can hide behind a compensating caller.** The RAM and PROM
  size fields decoded four and sixteen times too large. Nothing failed,
  because the simulator's own boot loader programmed sizes its comment
  described correctly and the wrong decode turned them into the memory the
  board actually has. Fixing the decode alone turned 27 runs red; fixing the
  boot loader with it left every run green and the fingerprints unmoved. When
  a spec fix breaks the world, check whether a caller was compensating before
  concluding that RTEMS depends on the bug.

- **Move a check to where its registers already are.** The write protection
  segments were in `erc32_cfg.h` but the comparison against them stayed in
  `memory_write`, reading the segments back one accessor at a time. Moving it
  in as `WriteProtected` is what surfaced that it masked the address to 23
  bits, repeating every segment through the 16 Mbyte RAM. Reading the check
  next to the register description is what made the wrong mask visible.
  Guard the call at the call site so the hot path keeps its single test.

- **Spec-driven testing earns its keep.** The one real bug found while covering
  `erc32.cc` (the GPT scaler read was gated on the wrong enable bit) surfaced
  only because the test came from the manual, not from reading the code back to
  itself. Report the hardware's real ID, version and register values from the
  reference manuals; never invent a constant to make a test pass.

- **Look for the same code in three files before covering it once.** The host
  UART port setup was triplicated across `erc32.cc`, `leon2.cc` and
  `grlib.cc`. Deduplicating it into `uartport.cc` removed 182 arcs from the
  tree and replaced them with 44 covered ones, which is a far better trade
  than covering the same logic three times. Two of the three copies had
  drifted, and reading them side by side is what showed it: ERC32 hung port
  B's terminal settings off port A's open flag, and only GRLIB switched the
  Windows console into raw mode. Look for the next such triple before
  starting on `leon2.cc` or `grlib.cc`.

- **`sis.h` is include-guarded now, keep it that way.** A subsystem header
  includes `sis.h`, so including it beside the board file's own `sis.h` used to
  redefine every struct. The guard is what makes the header-per-subsystem
  pattern legal.

- **gcovr merges the board and test instantiations, but the test environment
  alone must reach 100%.** A graduated header must not lean on the board
  integration path for an arc; if only the real environment reaches some branch,
  the header is not actually testable in isolation and is not ready to graduate.

- **UART cases share host and buffer state.** The emulated UART buffers and the
  real stdin are process-global across the shared test binary. Two interrupt-era
  DI cases failed from cross-contamination until each got a fresh fixture and
  the buffer was primed first. Isolate UART cases and prime, do not assume a
  clean buffer.

- **SIS fakes a regression two ways, so A/B first.** A build piped into `head`
  dies on SIGPIPE and leaves a stale `build/sis` that looks built; an inherited
  stdin that never closes makes a run hang in the emulated UART and looks like a
  hang in target code. Before believing a regression or an improvement from a
  behavioral change, run it against an unmodified tree.

- **Never pipe a build to `/dev/null`.** `CLAUDE.md` warns about `head` and
  `grep -m` killing a build with SIGPIPE; discarding the output is the same
  trap without the signal. A failing `./waf --notests > /dev/null` left the
  previous binary in place, and the next three diagnostic runs were of stale
  code and pointed the wrong way. Let the build print, and read the last
  lines.

- **A green `test-run` only counts if the run reaches the code.** Removing
  the status bits the ERC32 UART data register returns left `test-run` fully
  green, which looked like proof that nothing depended on them. It was not:
  probing the read showed the ERC32 binaries never read that register once.
  Before concluding a spec fix is safe because the runs pass, check that the
  runs execute the code at all.

- **Run the unit tests under the address sanitizer once per file.** Building
  with `CXXFLAGS='-fsanitize=address -g -O1' LINKFLAGS=-fsanitize=address`
  found a read one byte past the ERC32 receive buffer, on the path taken when
  the buffer has been drained completely. Ordinary runs never noticed, because
  the byte read belongs to the next buffer. It also reports 131 kB leaked in
  five allocations from `read_elf_body` in `elf.cc`, which is untouched and
  waiting for that file's step.

- **Code that is never compiled rots, and nothing tells you.** `FAST_UART`
  was defined unconditionally by the build and by the autotools build before
  it, so a whole second UART model sat under `#ifndef FAST_UART` in three
  board files, never compiled once in this repository's history. Its transmit
  handler had an infinite loop, present since the initial commit. Removing it
  took 360 lines and then made a further 37 lines of state dead. Preprocess a
  file before and after such a removal and diff the output: identical output
  is proof the change is behavior-neutral, which no test can give you for code
  that was never built.

- **The build has no warning flags, so dead code accumulates unseen.** The
  wscript passes only `-O<level> -g -std=c++20`. Building the tree with
  `CXXFLAGS=-Wall ./waf configure && ./waf` reports 132 warnings, and roughly a
  third are unused variables, each one a piece of dead code a coverage pass
  will otherwise have to reason about. Two dead receive buffers in `leon2.cc`
  and `grlib.cc` were found this way. Measure a file with `-Wall` before
  covering it, and consider proposing the flag to the maintainer once the
  count is low enough to act on.

- **A coverage number describes one build configuration.** The
  `--enable-coverage` build is `-O0`; the shipping build is `-O2`. Do not
  compare timings across the two, and see `doc/building-sis.md` for why a single
  percentage is configuration-specific.

- **A crash in the suite is nobody's job when the work is fanned out.**
  Five parallel agents each hit the random-order segfault, each correctly
  verified it against a clean baseline, and each correctly reported it as
  pre-existing and out of their scope. All five were right and it stayed
  unfixed. It was a real defect in shipped code: `uart_port_open` did not
  reset `open`, `fd` and `ifd`, so a failed reattachment kept the flags of
  the attachment before it while `fin` went back to null, and
  `uart_port_close` then handed that null to `fclose`. The boards read that
  same flag to decide whether a port can still carry a character. Build with
  `-fsanitize=address` and read the backtrace rather than reasoning about
  it; two guesses at the cause from the symptom were both wrong. When
  splitting work up, keep this class centrally rather than per agent.

- **Parallel agents collide through statics, not through files.** Two
  agents given disjoint files both drove the `ns16550` core, whose
  `plic_ip`, `uart_ie`, `uart_mcr` and `ns16550_irq` are process-global with
  no reset entry point. The result passed for each agent alone and failed
  once merged. Assume any core with no `reset` in its `struct grlib_ipcore`
  is shared state between test files, and say so in the brief.

- **A drain loop can be a no-op and look thorough.** The PLIC fixture
  claimed until the claim read returned zero, but `plic_claim[hart]` is only
  written by `plic_check_irq`, which a claim read does not run, and which
  `plic_read` zeroes on the way out. The first read therefore always
  returned a stale zero and the loop cleared nothing. Assert the state a
  cleanup routine is supposed to have reached, at the end of the routine.

- **A board's `init_sim` cannot be called from the test binary.** It mounts
  the real cores at the addresses the board uses, `grlib.cc` resolves by
  first match and can never unregister, so whichever test file registers
  first wins and the other one's fixtures break under `--order-by=rand`.
  This is what keeps the last four arcs of `leon3.cc` and two of `gr740.cc`
  uncovered, and no better test closes it: only a teardown hook in
  `grlib.cc` would. Board `init_sim` is proven by the end to end runs
  instead. Two test files also collided by both choosing `0x50000000` for a
  fake slave, which passed for each agent alone and failed on merge, so pick
  a fake address by grepping the tree for it first.

- **Audit the agents' mutation claims, do not take them.** Every agent this
  round reported its cases mutation-killed. An independent pass over the
  files they had just covered found the ROM write of `gr740.cc` and
  `rv32.cc` unprotected: restoring the unmasked address, a wild store three
  gigabytes past a sixteen megabyte array, changed nothing any case could
  see. Coverage had gone from 0 to 95% on those files and would not have
  caught the bug coming back. Pick a few production lines per merged file,
  break them, and check the suite notices.

- **A mechanism that explains the symptom is not the cause.** The hang the
  `remove_event` repair produced was recorded here as an ordering fault in
  `advance_time`, which frees the firing cell before running its callback.
  That reads well and is wrong. A correct removal walk cannot link a cell to
  itself unless the queue is already malformed, and the queue was being
  malformed by `tests/rv32board.cc`, the file removed in the same revert. With
  that file gone the repair passes the whole suite over eight random
  orderings, under AddressSanitizer, with the fingerprints unchanged, and
  `advance_time` was never touched. The lesson is not about the event queue:
  a diagnosis reached by reading code and reasoning backwards from a symptom
  needs an experiment that can falsify it before it is written down as fact,
  because a wrong one blocks the real fix and sends the next reader after the
  wrong file. `tests/event.cc` now checks the queue invariant directly, so
  the corruption is a failed assertion rather than a hang to be diagnosed.

- **Run the sanitizers, not only the coverage gate.** Building the suite with
  `-fsanitize=address,undefined` and leak detection on found two real bugs in
  a single pass, both in code the gate already called finished: `greth.cc`,
  graduated at 100%, read past the end of the received frame when swapping the
  tail of the last word, and `init_bpt` dropped the instruction history buffer
  without freeing it. Coverage proves a line ran and mutation proves an
  assertion holds; neither says the line was memory safe. The sweep is cheap
  in a throwaway `git worktree` and is worth repeating whenever a batch
  merges. Run it as
  `ASAN_OPTIONS=allocator_may_return_null=1 ./build/tests/sis-test`: the
  cases that force an allocation failure ask for four gigabytes, and the
  sanitizer's allocator aborts on a request that size instead of returning
  the null the case is there to check. A later sweep found a third: `-c` built its shell command by
  copying an unbounded command line path into a fixed 256 byte allocation.
  The sanitizer reported only the leak; the overflow came out of reading
  what the leaked buffer was for, so treat a leak report as a pointer at
  code worth looking at rather than as the whole finding.

  **A subprocess hides its own sanitizer report.** `tests/sismain.cc` runs
  each `sis_main` case in a forked child whose output goes to a captured
  file, so a child that dies under the sanitizer shows up in the parent only
  as a non-zero exit status, with nothing in the parent's log. That is how
  the `-c` overflow presented. When a case that forks fails on a sanitized
  build and the parent log is clean, run the `sis` binary directly with the
  same arguments.

- **An allocation failure is testable, so do not write it off.** Two
  arcs of `elf.cc` were allocation failure returns, the shape this file
  lists as a legitimate gap. `RLIMIT_AS`, set just above what the
  process already maps and restored on the way out, makes one huge
  allocation fail while every ordinary one still succeeds.
  `tests/elf.cc`'s `address_space_cap` is the helper; copy it rather
  than documenting the next such arc as unreachable.

- **An agent working from a stale base leaves a hole the gate catches.** Six
  of the seven agents this round started on a commit well behind the campaign
  tip; most noticed and fast forwarded, and the one that did not still
  produced a faithful patch. The cost showed up only at the merge:
  `remote.cc` gained a `p` packet on this branch after that base, so the GDB
  stub tests, complete against the tree they were written for, left its case
  arc untouched and the per file gate failed the moment the file was
  graduated. Tell an agent to fast forward before it starts, and measure
  coverage after the merge rather than trusting the number from the worktree.

- **A killed `./waf` leaves a binary built from source you no longer have.**
  waf decides what to rebuild by hashing content, and it writes its record of
  what it built only when the run finishes. A run killed part way through,
  by a timeout or by the SIGPIPE that `head` sends, has already compiled and
  linked but has recorded nothing. Restoring the source then leaves the tree
  in the state waf last recorded, so the next `./waf` reports nothing to do
  and the stale objects stay. This bit a mutation experiment: the mutation
  was reverted, `git status` was clean, and the suite still failed the case
  the mutation targeted, on a binary built from a header that no longer
  existed. `./waf clean` is the way out, and a mutation loop whose build can
  be interrupted should end with one rather than trusting the revert.

- **A test may not name a fixed path under `/tmp`.** Three cases in
  `tests/funcq.cc` wrote to one, and when several builds of the suite ran at
  once one of them removed the file another had just written and was about to
  open. The failure looks exactly like an order dependent bug and is not one.
  Every other test file already builds its scratch name from an `mkstemp`
  template; these now append the process id.

- **Do not register a real board into the test binary.** `tests/rv32board.cc`
  did it from a static constructor to reach `gr740.cc` and `rv32.cc`, which
  fills an APB bridge to its sixteen core cap and corrupts the event queue.
  Removing the file makes every ordering pass, and it was the whole cause of
  the hang blamed above on `advance_time`. This is the same wall `leon3.cc`'s
  `init_sim` already hit, and the reason those two board files are back at
  zero: they need tests which drive the `memsys` entry points without
  `init_sim`. The ROM mask fix those tests found is kept and is currently
  unguarded, so guard it when they are rebuilt.

- **Coverage measured on a build configured without `--enable-coverage`
  reads as zero, not as an error.** The `.gcno` files survive a reconfigure,
  so gcovr reports every arc as untaken and it looks exactly like a test
  suite that runs but touches nothing. Check for `.gcda` files before
  believing a sudden collapse.

## Moving to another machine

A fresh `git clone` brings the tracked tree but not everything this workflow
needs:

- **`ref/` is untracked and local-only.** The spec-driven step has no specs
  without it, and `ref/INDEX.md` and `ref/SOURCES.md` live inside it, so they go
  too. Copy the whole `ref/` directory across by hand (for example `rsync` or
  `scp`); git will not carry it. `ref/SOURCES.md` records where every document
  came from, so a lost `ref/` can be rebuilt from it. Bring any other untracked
  working files that matter, such as `BUGS.md` and task notes.

- **Toolchain on the new box:** Python 3 (runs the vendored `waf`), a
  GCC-compatible C++20 compiler (GCC 10+ for concepts; `--enable-coverage` is
  GCC-only), `gcovr` (the coverage gate), `uv` (pulls the pinned `clang-format`,
  and `pymupdf4llm` if a reference manual needs converting), and `dtc` plus
  `xxd` only if you regenerate the RISC-V DTB.

- **First thing after the move:** `./waf configure && ./waf`, then `./waf
  test-run`, and confirm the baseline is green before changing anything. Then
  `tests/check-coverage.py` to confirm the gate passes on the graduated files.

## Commit conventions

Follow `CLAUDE.md`: 50/72 rule, `<scope>: Sentence-case` subject, a
`Signed-off-by:` trailer that only the human author adds, and an `Assisted-by:`
trailer for assisted commits. Two commits per subsystem as above, and a bug fix
is its own commit ahead of the test that covers it. Markdown changes are owned
by `@approvers/docs` per `CODEOWNERS`.
