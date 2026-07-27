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
  behavior and interrupt levels come from the reference manuals under `ref/`.
  A test that encodes what the code does cannot catch what the code gets wrong.
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
1917 of 5891 arcs (32%). The totals move as headers join the filter and as
duplicated and dead code is removed, so compare per file rather than against
an older total.

| Area | Arcs | Taken |
|---|---|---|
| `riscv.cc` | 1125 | 1100 |
| `func.cc` | 603 | 30 |
| `grlib.cc`, `grspw.cc`, `gr1553.cc`, `greth.cc`, `memscrub.cc`, `tap.cc` | 1002 | 0 |
| `sis.cc`, `remote.cc`, `interf.cc` | 547 | 0 |
| `leon2.cc`, `leon3.cc`, `gr740.cc`, `rv32.cc` | 284 | 0 |
| `elf.cc` | 97 | 91 |
| graduated: `erc32_cfg.h`, `erc32_error.h`, `erc32_mec.h`, `erc32_timer.h`, `erc32_uart.h`, `erc32.cc`, `exec.cc`, `help.cc`, `sisio.cc`, `sparc.cc`, `uartport.cc` | | 100% |

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

   Then `riscv.cc`. **In progress**, at 1100 of 1125 arcs. Same shape as the
   SPARC core and it reuses the flat memory, which now swaps a sub-word
   address by `arch->bswap` rather than by the host so one window serves
   both.

   What is left is 25 arcs of two kinds, and no more of it is reachable from
   a test.

   Seventeen are the range check gcc emits on a switch whose selector is
   masked to the width of its case set, so no value can miss: the compressed
   quadrant and its three function fields, the two bit operation field of
   the compressed arithmetic group, the eight function codes of the M
   extension, the rounding mode field, and the classification a host
   `fpclassify` returns. Rule 6 closes each of them, and closing all of them
   measured **5% slower** on `grv32imac/crypt01` (65.9 s to 68.9 s over
   three runs each). The cost does not follow the change: reverting the
   whole executor half left the time at 69 s, and adding a comment and one
   never taken branch to the disassembler alone moved an otherwise
   unmodified tree from 65.9 s to 67.2 s. So `riscv.cc` is layout sensitive
   at the 2% level and the rest is unattributed. The restructuring is
   reverted and the arcs are left open, since performance not regressing
   outranks the gate. **Open decision:** whether to mark them with gcovr's
   `GCOVR_EXCL_BR_LINE`, which states the invariant in the source and costs
   no codegen. No file uses an exclusion marker yet, so adopting one is a
   project convention to settle first.

   The other eight are the branches of `riscv_get_regi` that the missing `p`
   packet makes dead, below.

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

   One gap found and left alone, because closing it is protocol visible:
   `remote.cc` handles `g` and `P` but not `p`, so a debugger can **write** a
   control register through `P` and never read one back. That also makes the
   control register branch of `riscv_get_regi` unreachable, which is the one
   thing standing between the register access path and full coverage. Decide
   whether the `g` packet should carry the control registers before finishing
   that file.
   Its decoder has been through the exhaustive switch restructure already,
   and it is worth knowing that **it bought no speed there**: the RISC-V
   fields are masked to their width at extraction, so the compiler already
   knew the range and emitted no bounds check to remove. The SPARC gain came
   from switches whose scrutinee range the compiler could not see. Expect
   the restructure to pay only where the range is not already obvious.

2. `grlib.cc` and the GRLIB cores. One file behind four boards, and the GRLIB
   manual in `ref/` is already scoped to the cores SIS models.
3. `leon2.cc`, `leon3.cc`, `gr740.cc`, `rv32.cc`. Thin once `grlib.cc` and the
   shared port module are done.
4. `func.cc`, `elf.cc`, `interf.cc`, `remote.cc`, `sis.cc`. Simulator
   infrastructure with no hardware spec; `doc/` and the GDB remote protocol are
   the references. `func.cc` may deserve to move ahead of step 2 if its harness
   unlocks the board work.
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
  `./waf test-run` wall clock (crypt01 dominates), best of three, A/B against
  the parent commit, and investigate anything worse than 2%.

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
