/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* The event queue of func.cc, driven directly.

   A peripheral schedules its work with event() and takes it back with
   remove_event(), so a queue which drops only some of the entries it was
   asked to drop leaves a callback that fires after the device which armed
   it was turned off.  These cases drive the queue with no device in the
   way.  */

#include "doctest.h"

#include "sis.h"

#include "cpumem.h"

extern void advance_time (uint64 endtime);

namespace
{

/* Records the arguments of every call, in the order they fired.  */
int fired[16];
int nfired;

void
count_event (int32 arg)
{
  if (nfired < 16)
    fired[nfired] = arg;
  nfired++;
}

void
other_event (int32 arg)
{
  (void) arg;
  nfired++;
}

struct event_fixture
{
  const struct memsys *saved_ms;
  const struct cpu_arch *saved_arch;
  float32 saved_freq;

  event_fixture () : saved_ms (ms), saved_arch (arch), saved_freq (ebase.freq)
  {
    ms = &sis_tests::flatmem;
    arch = &sparc32;
    ebase.freq = 1;
    ebase.simtime = 0;
    ebase.simstart = 0;

    /* Rebuilds the free list and drops whatever the case before left
       queued.  */
    reset_all ();

    nfired = 0;
    for (int i = 0; i < 16; i++)
      fired[i] = -1;
  }

  ~event_fixture ()
  {
    reset_all ();
    ms = saved_ms;
    arch = saved_arch;
    ebase.freq = saved_freq;
  }
};

}

TEST_CASE_FIXTURE (event_fixture, "the queue fires its events in time order")
{
  event (count_event, 1, 30);
  event (count_event, 2, 10);
  event (count_event, 3, 20);

  advance_time (40);

  CHECK (nfired == 3);
  CHECK (fired[0] == 2);
  CHECK (fired[1] == 3);
  CHECK (fired[2] == 1);
}

TEST_CASE_FIXTURE (event_fixture, "removing an event takes it off the queue")
{
  event (count_event, 1, 10);
  event (count_event, 2, 20);

  remove_event (count_event, 1);
  advance_time (30);

  CHECK (nfired == 1);
  CHECK (fired[0] == 2);
}

TEST_CASE_FIXTURE (event_fixture,
		   "removing takes every entry the argument names")
{
  /* Two entries for the same callback and argument, queued next to each
     other.  A device which arms the same work twice, then turns off, has to
     be left with neither: one stale callback is enough to act on a device
     that is no longer running.  */
  event (count_event, 7, 10);
  event (count_event, 7, 20);

  remove_event (count_event, 7);
  advance_time (30);

  CHECK (nfired == 0);
}

TEST_CASE_FIXTURE (event_fixture, "a negative argument removes every entry")
{
  /* A negative argument names no single entry, so it takes all of them for
     that callback and leaves the others standing.  */
  event (count_event, 1, 10);
  event (count_event, 2, 20);
  event (other_event, 3, 30);
  event (count_event, 4, 40);

  remove_event (count_event, -1);
  advance_time (50);

  CHECK (nfired == 1);
}

TEST_CASE_FIXTURE (event_fixture,
		   "removing what was never queued does nothing")
{
  event (count_event, 1, 10);

  remove_event (other_event, 1);
  advance_time (20);

  CHECK (nfired == 1);
  CHECK (fired[0] == 1);
}
