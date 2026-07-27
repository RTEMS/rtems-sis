/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* A fixture for driving one GRLIB IP core with no board.

   A board registers its cores on the shared AHB/APB bus and lets the CPU
   reach them through grlib_read and grlib_write.  A test does not need the
   bus: struct grlib_ipcore exposes the core's own read, write and reset, so
   a case calls those directly and asserts on what the core did.  The bus is
   left out on purpose.  grlib.cc has no way to unregister a core, its
   registration arrays only ever grow, and grlib_ahbs_add and grlib_ahbm_add
   do not bound them, so a case which registered a core would spend an entry
   it could never give back.

   Memory is the flat window of cpumem.h, which is what a core reaches for a
   descriptor or a buffer.

   Two things a case would otherwise inherit from the one before it:

   - A timer the previous case left running.  reset_all clears the event
     queue, so a stale callback cannot fire into this case.
   - An engine flag the previous case left set.  Several cores start an
     engine only on the transition into running, so a leftover flag makes the
     next start silently do nothing.  The core's own reset clears them.

   Both happen in the constructor, so a case never has to remember either.  */

#ifndef SIS_TESTS_GRLIBCORE_H
#define SIS_TESTS_GRLIBCORE_H

#include "sis.h"
#include "grlib.h"

#include "cpumem.h"

/* Runs the event queue forward to a point in simulated time.  func.cc
   exports it, sis.h does not declare it.  */
extern void advance_time (uint64 endtime);

namespace sis_tests
{

struct grlib_core_fixture
{
  const struct grlib_ipcore *core;

  int saved_verbose;
  int saved_ncpu;
  const struct memsys *saved_ms;
  const struct cpu_arch *saved_arch;
  float32 saved_freq;

  grlib_core_fixture (const struct grlib_ipcore *c)
      : core (c), saved_verbose (sis_verbose), saved_ncpu (ncpu),
	saved_ms (ms), saved_arch (arch), saved_freq (ebase.freq)
  {
    ms = &flatmem;
    arch = &sparc32;
    sis_verbose = 0;
    ncpu = 1;

    /* One clock per microsecond, so a case can state a delay in either and
       mean the same thing.  */
    ebase.freq = 1;
    ebase.simtime = 0;
    ebase.simstart = 0;
    flatmem_clear ();

    /* Clears the event queue.  reset_all reaches ms->reset, so ms has to be
       the flat window before this runs.  */
    reset_all ();

    /* A board calls init once and reset on every power cycle.  Some cores
       compute a decode mask in init that reset does not, so a case which
       skipped it would drive a core whose registers read back as zero.
       A core with no state to clear, such as l2c, leaves reset NULL.  */
    if (core->init)
      core->init ();
    if (core->reset)
      core->reset ();
  }

  ~grlib_core_fixture ()
  {
    /* The core may have left a callback queued.  Clearing the queue here as
       well as in the constructor keeps a case which runs outside this
       fixture from inheriting one.  */
    reset_all ();

    ncpu = saved_ncpu;
    sis_verbose = saved_verbose;
    ms = saved_ms;
    arch = saved_arch;
    ebase.freq = saved_freq;
  }

  /* A register access as the CPU makes it.  The size code 2 is a word,
     which is the only width these register files answer.  */
  void
  write (uint32 offset, uint32 value)
  {
    core->write (offset, &value, 2);
  }

  uint32
  read (uint32 offset)
  {
    uint32 data = 0;

    core->read (offset, &data);
    return data;
  }

  /* Runs the event queue on for a span of simulated time, which is how a
     case reaches work a register write only armed.  */
  void
  run (uint64 clocks)
  {
    advance_time (ebase.simtime + clocks);
  }
};

}

#endif
