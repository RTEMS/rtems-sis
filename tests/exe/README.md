<!-- SPDX-License-Identifier: BSD-2-Clause -->
<!-- SPDX-FileCopyrightText: 2026 embedded brains GmbH -->

# Overview

This directory holds test executables from the
[RTEMS](https://gitlab.rtems.org/rtems/rtos/) test suites, built against the
[Newlib](https://sourceware.org/git/gitweb.cgi?p=newlib-cygwin.git) C library.
One subdirectory per board holds the same suite compiled for that board.

It is the known good reference for SIS improvement work. `expected.txt` records
the gated status of every run and `./waf test-run` checks the simulator against
it. When a change makes a passing run fail, or fixes a run that is expected to
fail, `test-run` reports it and returns non zero. See [Gating](#gating).

# Run the executables

`${EXE}` is a path into one of the board directories.

| Board | Command |
| --- | --- |
| `erc32` | `sis -erc32 -r ${EXE}` |
| `gr712rc` | `sis -leon3 -r ${EXE}` |
| `gr740` | `sis -gr740 -r ${EXE}` |
| `gr740_smp` | `sis -gr740 -m 4 -r ${EXE}` |
| `griscv` | `sis -griscv -r ${EXE}` |
| `griscv_smp` | `sis -griscv -m 4 -r ${EXE}` |
| `leon2` | `sis -leon2 -r ${EXE}` |

There is no `-gr712rc` flag. The GR712RC is a dual core LEON3FT, so it runs on
the LEON3 board model with `-leon3`. Redirect stdin from `/dev/null` for an
unattended run; SIS polls stdin for the emulated UART and an inherited stdin
that never closes stalls the run.

# Success criterion

A run succeeds when its output shows either of the two RTEMS report formats:

- the classic banner `*** END OF TEST ${NAME} ***`, or
- a T-test summary line `Z:${NAME}:C:...:F:0:...` with a zero failure count.

The newer T-test executables (`spintrcritical*`, `ttest02`, `ts-validation*`,
the SMP tests) never print the classic banner, so a `Z:` line with `F:0` is
their success marker. A `Z:` line with a non zero `F:` field ran to completion
but failed internal checks. The register dump and `cpu 0 in error mode` line
that follow either marker are the normal way RTEMS shuts down, not a failure.

# Status

Built from RTEMS a52d944336 with the RTEMS 7 tools (GCC 15.2.0), one
configuration per board, and stripped of debug information with `strip -g`.

| Board | PASS | XFAIL | Notes |
| --- | ---: | ---: | --- |
| `erc32` | 28 | 0 | clean |
| `gr712rc` | 28 | 0 | clean |
| `gr740` | 28 | 0 | clean |
| `gr740_smp` | 33 | 0 | clean |
| `griscv` | 28 | 0 | clean |
| `griscv_smp` | 33 | 0 | clean |
| `leon2` | 28 | 0 | clean |

The non SMP boards carry 28 tests, the SMP boards add the five `smp*` tests for
33.

## Interrupt tests and timing noise

`erc32 spintrcritical20` used to report `T_INTERRUPT_TEST_TIMEOUT`, and it
was the last run in this corpus which did not pass. Two RTEMS defects were
behind the interrupt critical section failures here, both now fixed.

`spintrcritical01` to `05` share `spintrcritical01impl.h`, whose interrupt
handler must classify the interrupt only while the action is in progress.
Without that check the handler classifies and releases the semaphore on
every tick, so the search is fed samples which say nothing about the race.
The check was on the RTEMS 6 branch as afb30a1056 and had never reached the
mainline.

`spintrcritical20` exposed a weakness in `T_interrupt_test()` itself. The
search bisects a busy wait and adjusts its bracket on an early and on a late
interrupt, but `T_INTERRUPT_TEST_CONTINUE`, which means the interrupt hit
the action without satisfying the test, fell through both cases and left the
bracket untouched. The search then reused one time point for every
remaining iteration. On erc32 that showed as 9997 of 10000 iterations
landing inside the action with none of them satisfying the test. The search
now counts those results and, once nothing else has come back for a while,
walks the time point through the bracket so the whole action is covered.

Both are worth knowing when reading a timing failure here: an interrupt
test which times out is not necessarily a simulator problem.

# Test catalogue

Notes and classification for each test. Status columns list only the boards
where the result differs from PASS.

## Compute and library

| Test | What it does | Not PASS on |
| --- | --- | --- |
| `crypt01` | `crypt()` family correctness over test vectors. The heaviest test at about 108 s, the long pole of a parallel run. | |
| `record04` | Event recording ring buffer (`<rtems/record.h>`). | |
| `ttest02` | Self-test of the RTEMS T test framework, exercises its interrupt test. | |

## Filesystem

| Test | What it does | Not PASS on |
| --- | --- | --- |
| `fsdosfsname01` | FAT short, long and multibyte name handling and MS FAT compatibility. | |

## Interrupt and critical section

The `spintrcritical` suite stresses the interrupt critical section, an ISR
racing a kernel operation on the same object. All pass everywhere except the
six erc32 runs covered above.

| Test | Race under test |
| --- | --- |
| `spintrcritical01`..`05` | Unblock a task from an ISR while it blocks on a thread queue. |
| `spintrcritical08` | Period expiry while blocking on the period. |
| `spintrcritical09`, `10` | Timeout while blocking on a thread queue. |
| `spintrcritical11` | Event (Any) sent to a task in the act of blocking. |
| `spintrcritical12` | Event (All) sent to a task in the act of blocking. |
| `spintrcritical13` | Fire a timer from an ISR during `rtems_timer_fire_after`. |
| `spintrcritical14` | Same through the timer server. |
| `spintrcritical15` | Thread queue timeout while another thread blocks on it. |
| `spintrcritical16` | Timeout on the executing thread whose request was also satisfied. |
| `spintrcritical18` | Thread dispatch with interrupts arriving in the meantime. |
| `spintrcritical20` | `_Thread_queue_Process_timeout` interrupted by a dequeue. |
| `spintrcritical21` | Event from an ISR while the receiver blocks, with and without timeout. |
| `spintrcritical22` | Dequeue of the executing thread as it is about to enqueue. |
| `spintrcritical23` | Priority update in interrupt context during a task level priority change. |
| `spintrcritical24` | `close()` interrupted by an `fcntl()` that alters the iop flags. |

## Timing and performance

| Test | What it does | Not PASS on |
| --- | --- | --- |
| `tmcontext01` | Context switch time by function nest level and cache state, JSON output. Under SIS some entries are corrupted (values near 2^32 from the timecounter running backwards), so it passes but its numbers are not a reliable fingerprint. | |
| `tmonetoone` | Cost of one task to task hand off per synchronisation primitive. A performance fingerprint, see below. | |

## SMP

Only on `gr740_smp` and `griscv_smp`. All pass on both.

| Test | What it does |
| --- | --- |
| `smpatomic01` | Atomic operation correctness and a per CPU throughput count. Fingerprint. |
| `smplock01` | Benchmark of the SMP lock implementations. Fingerprint. |
| `smpmrsp01` | MrsP semaphore error path correctness, reports per CPU migration counts. Fingerprint. |
| `smpmutex01` | Thread queue priority FIFO fairness across scheduler instances. Emits no performance numbers. |
| `smpopenmp01` | OpenMP micro benchmark. |

## Validation

| Test | What it does | Not PASS on |
| --- | --- | --- |
| `ts-validation-0` | Main RTEMS validation test suite. | |
| `ts-validation-timecounter-1` | Timecounter validation. | |

# Performance benchmark

SIS is deterministic, so the performance counters these tests emit are stable
run to run and make a fingerprint of the timing model. A timing change moves
them, and the diff is the signal. `perf-baseline.txt` records the baseline and
`./waf test-run --perf` reports the current values against it. This is reported,
never gated: SIS timing work is expected to shift these numbers.

The distilled benchmark is four tests whose numbers are stable and meaningful:

| Test | Board | Metric | Baseline |
| --- | --- | --- | ---: |
| `tmonetoone` | gr740 | yield hand off (lower is faster) | 33022 |
| `tmonetoone` | gr740 | event | 17563 |
| `tmonetoone` | gr740 | self-contained binary semaphore | 17620 |
| `tmonetoone` | gr740 | Classic binary semaphore (FIFO) | 17043 |
| `tmonetoone` | gr740 | Classic binary semaphore (priority) | 16409 |
| `smpatomic01` | gr740_smp | atomic ops per interval, CPU 0 (higher is faster) | 1771949 |
| `smplock01` | gr740_smp | lock acquisitions, section 0 sum | 1384976 |
| `smpmrsp01` | gr740_smp | migrations, CPU 0 | 175022 |

Left out of the fingerprint on purpose:

- `tmcontext01`, its JSON carries corrupted entries under SIS.
- `smpmutex01`, it emits no numbers.
- `tmfine`, named as a candidate but it has no executable here; only `tmfine01`
  exists in the RTEMS sources and it is not built into this directory.

# Gating

`expected.txt` maps each `board exe` to `PASS` or `XFAIL`, most specific line
wins, unmatched runs default to `PASS`. `./waf test-run` runs every executable
through the simulator in parallel, honouring the waf `-j` job count, and
classifies each result:

| Result | Meaning | Gate |
| --- | --- | --- |
| `PASS` | expected PASS, passed | ok |
| `xfail` | expected XFAIL, failed as recorded | ok |
| `REGRESSED` | expected PASS, now fails | fails the run |
| `XPASS` | expected XFAIL, now passes, promote it | fails the run |

`test-run` needs `build/sis`, so run `./waf` first. It is a separate command
and does not run as part of the default build or the coverage flow. Add `--perf`
to also print the performance fingerprint. Every test finishes in seconds except
crypt01, which is CPU bound at about 108 s and built for each board, so with the
core count as the limit the wall time is roughly the number of boards times
108 s over the cores available. Set `-j` no higher than the cores to avoid
contention, or pass `--fast` to skip crypt01, which drops the wall time to
about a minute.

```shell
./waf                       # build build/sis
./waf test-run -j 6         # gate all runs against expected.txt
./waf test-run --fast -j 6  # skip crypt01, quick iteration
./waf test-run --perf -j 6
```
