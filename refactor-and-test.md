# Refactor-and-test: driving a board file to 100% coverage

A handoff for the ongoing work of testing the simulator's board files to 100%
line and branch coverage by moving each subsystem behind a dependency-injected
policy template as it is tested. `erc32.cc` is the file in progress and
`erc32_mec.h` is the finished model. The same recipe applies to the other board
files (`leon2.cc`, `leon3.cc`, `gr740.cc`, `rv32.cc`) afterwards.

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

Two kinds of testing are in play and neither replaces the other:

- Host unit tests (doctest cases under `tests/`, linked against `libsis.a`)
  drive individual subsystems and are what coverage measures.
- End-to-end runs of real target binaries through the simulator
  (`./waf test-run`) are the only proof the board still works. Every production
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

Because the template body lives in a header, gcovr is told to measure the header
and it is graduated like a source file. gcovr merges the two instantiations
(board and test) across translation units, but the requirement is that the test
environment alone reaches 100%: graduation must never depend on exercising the
board integration path.

## What is done and what remains

Done, in `erc32_mec.h`, graduated in `tests/covered.txt`:

- The MEC interrupt controller. `template <MecEnv Env> class Mec` holds the
  interrupt registers and the level logic. Its environment provides `Verbose`,
  `Irl`, `ReportError` and `Log`. The `MecEnv` concept states that contract.
  `erc32.cc` instantiates `Mec<RealEnv>`; `tests/erc32.cc` drives `Mec<TestEnv>`
  with no globals.

Not yet converted. These MEC subsystems are still tested through the
global-poking `mec_fixture` in `tests/erc32.cc`, which sets up machine state and
reads and writes registers through `ms->memory_read`/`memory_write`:

- Timers (RTC and GPT counters, scalers, reloads, control register).
- Configuration and memory control (memory config, write protection, block
  protection, ROM width, software reset, fault status).
- Memory access dispatch (region decode, access-size stores, byte pointers).
- The UART (transmit buffering, receive, status, control), which additionally
  reaches host stdin through the `stdin_feed` helper.

`erc32.cc` itself is partially covered and is not yet graduated. Only
`erc32_mec.h` is. Convert one subsystem at a time; graduate `erc32.cc` only once
the last of its logic has moved into the header and both the header and the
board file read 100%.

To see the current numbers at any time:

```shell
./waf configure --enable-coverage && ./waf
gcovr --txt-metric branch --filter 'erc32.*'
```

## The recipe, one subsystem per commit

Do exactly one subsystem per change. The board is production code, so keep it
working at every step.

1. **Add the subsystem to the header behind the existing tests.** Move its
   state and logic into `erc32_mec.h` (a new template, or new members and
   methods on `Mec` if it shares the interrupt environment). Extend the `MecEnv`
   concept, or add a second concept, for any new dependency the subsystem needs
   from its environment. In `erc32.cc`, add the forwarding methods to `RealEnv`
   and replace the old code with calls into the template. Do not touch the tests
   yet. The pre-existing `mec_fixture` cases must still pass, proving the board
   is unchanged.

2. **Name the injected code Google style.** CamelCase types and methods,
   private members with a trailing underscore, cheap accessors named like the
   member they return, all inside the `erc32` namespace. GNU `.clang-format`
   layout is unchanged, so only the identifiers differ. Run
   `uv run clang-format -i erc32_mec.h erc32.cc`.

3. **Run the end-to-end tests.** `./waf test-run --fast`. This is the proof the
   board still runs real binaries. Do it after every production change, not just
   at the end. Drop `--fast` for the full set (adds crypt01) before the commit
   that finishes the file.

4. **Rewrite the subsystem's tests onto the test environment.** Replace its
   `mec_fixture` cases with cases that construct the template on a `TestEnv` and
   assert against exact values. Extend `TestEnv` with whatever the new
   environment methods return. Assert the safe default state explicitly at the
   start (reset values), the way the interrupt cases and the `mec_fixture`
   constructor do with `REQUIRE (rd (R_IMR) == 0x7ffe)`. Keep one small
   integration case that goes through `erc32.cc`'s register dispatch on the real
   environment, so the board's dispatch arcs are covered; the bulk of the logic
   coverage comes from the isolated cases.

5. **Test from the specification.** Register fields, reset values, reserved-bit
   behavior and interrupt levels come from the reference manuals under `ref/`
   (start at `ref/INDEX.md`; ERC32 and the SPARC IU manual for the MEC), not
   from reading the implementation back to itself. A test that encodes what the
   code does cannot catch what the code gets wrong.

6. **Fix bugs in their own commit, before the test that would catch them.** If
   testing to spec uncovers a defect, the fix is a separate commit that precedes
   the coverage test. Confirm the fix against `./waf test-run` and, when it is a
   behavioral change, A/B it against the unmodified tree first. Do not let a
   test bake in wrong behavior.

7. **Handle dead branches by restructuring, not by marking.** A branch no test
   can reach because it is genuinely unreachable is removed or the code is
   restructured so the arc no longer exists. No coverage-suppression markers.
   Confirm with `./waf test-run` that the restructure is behavior-neutral.

8. **Confirm coverage and, when the file is finished, graduate it.** Build with
   `--enable-coverage` and check that the subsystem's arcs are all covered by the
   test environment alone. When the last subsystem has moved and both
   `erc32_mec.h` and `erc32.cc` are at 100% line and branch, add `erc32.cc` to
   `tests/covered.txt` (keep it sorted) in the finishing commit.

## Build, gate and test commands

```shell
# Coverage build and the per-file gate
./waf configure --enable-coverage && ./waf
tests/check-coverage.py                 # runs gcovr via gcovr.cfg, fails on any regression

# A readable branch report for one file
gcovr --txt-metric branch --filter 'erc32.*'

# End-to-end runs (the only proof the board still works)
./waf test-run --fast                   # skips crypt01
./waf test-run                          # full set, before the finishing commit

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

Hard-won on this file, so the next person does not rediscover them.

- **Spec-driven testing earns its keep.** The one real bug found while covering
  `erc32.cc` (the GPT scaler read was gated on the wrong enable bit) surfaced
  only because the test came from the manual, not from reading the code back to
  itself. Report the hardware's real ID, version and register values from the
  reference manuals; never invent a constant to make a test pass.

- **The performance question is settled. Do not re-benchmark.** A micro-
  benchmark and an end-to-end crypt01 run compared three dispatch styles for the
  hot memory path: today's function-pointer vtable (~108.8s), a template with an
  out-of-line global implementation (~109.1s, a tie), and a template with the
  body inlined into the loop (~119.4s, about 10% slower from register and
  instruction-cache pressure). A monomorphic function-pointer call is already
  branch-predicted and costs nothing. So keep the `memsys`/`cpu_arch` vtables,
  and where you convert a hot path for testability, keep the body in a global
  function reached by a static template call, never inlined and never indirect.

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
  DI cases failed from cross-contamination until each got a fresh fixture and the
  buffer was primed first. Isolate UART cases and prime, do not assume a clean
  buffer.

- **SIS fakes a regression two ways, so A/B first.** A build piped into
  `head` dies on SIGPIPE and leaves a stale `build/sis` that looks built; an
  inherited stdin that never closes makes a run hang in the emulated UART and
  looks like a hang in target code. Before believing a regression or an
  improvement from a behavioral change, run it against an unmodified tree.

- **A coverage number describes one build configuration.** The `--enable-coverage`
  build is `-O0`; the shipping build is `-O2`. Do not compare timings across the
  two, and see `doc/building-sis.md` for why a single percentage is
  configuration-specific.

## Moving to another machine

The work is going to a more powerful host. A fresh `git clone` brings the
tracked tree but not everything this workflow needs:

- **`ref/` is untracked and local-only.** The spec-driven step has no specs
  without it, and `ref/INDEX.md` and `ref/SOURCES.md` live inside it, so they go
  too. Copy the whole `ref/` directory across by hand (for example
  `rsync`/`scp`); git will not carry it. Bring any other untracked working files
  that matter, such as `BUGS.md` and task notes.

- **Toolchain on the new box:** Python 3 (runs the vendored `waf`), a
  GCC-compatible C++20 compiler (GCC 10+ for concepts; `--enable-coverage` is
  GCC-only), `gcovr` (the coverage gate), `uv` (pulls the pinned `clang-format`),
  and `dtc` plus `xxd` only if you regenerate the RISC-V DTB.

- **First thing after the move:** `./waf configure && ./waf`, then `./waf
  test-run`, and confirm the baseline is green before changing anything. Then
  `tests/check-coverage.py` to confirm the gate passes on the graduated files.

## Commit conventions

Follow `CLAUDE.md`: 50/72 rule, `<scope>: Sentence-case` subject, a
`Signed-off-by:` trailer that only the human author adds, and an `Assisted-by:`
trailer for assisted commits. One subsystem per commit; a bug fix is its own
commit ahead of the test that covers it. Markdown changes are owned by
`@approvers/docs` per `CODEOWNERS`.
