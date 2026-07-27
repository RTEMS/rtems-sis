/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* The event queue of func.cc, driven directly.

   A peripheral schedules its work with event() and takes it back with
   remove_event(), so a queue which drops only some of the entries it was
   asked to drop leaves a callback that fires after the device which armed
   it was turned off.  These cases drive the queue with no device in the
   way.  */

#include "doctest.h"

#include <stddef.h>

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

/* Walks both lists and reports whether every cell of evbuf appears at most
   once across the two of them.  A cell on the queue and the free list at the
   same time is the corruption which makes a removal link a cell to itself and
   the next insertion walk forever, so the cases check the invariant rather
   than hanging when it breaks.  The walks are bounded by MAX_EVENT, so a
   cycle is reported instead of spun on.  */
bool
queue_is_well_formed ()
{
  int seen[MAX_EVENT];
  int steps;

  for (int i = 0; i < MAX_EVENT; i++)
    seen[i] = 0;

  const struct evcell *ev = ebase.eq.nxt;
  for (steps = 0; ev != NULL && steps <= MAX_EVENT; steps++)
    {
      if (++seen[ev - evbuf] > 1)
	return false;
      ev = ev->nxt;
    }
  if (steps > MAX_EVENT)
    return false;

  ev = ebase.freeq;
  for (steps = 0; ev != NULL && steps <= MAX_EVENT; steps++)
    {
      if (++seen[ev - evbuf] > 1)
	return false;
      ev = ev->nxt;
    }

  return steps <= MAX_EVENT;
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
		   "removing takes out the entry behind the one it took out")
{
  /* Two entries for the same callback and argument, queued next to each
     other.  A device which arms the same work twice and then turns off has
     to be left with neither.  The entry behind the one removed moves up into
     its place, so a walk which steps on after unlinking passes over it.  */
  event (count_event, 7, 10);
  event (count_event, 7, 20);

  remove_event (count_event, 7);
  CHECK (queue_is_well_formed ());
  advance_time (30);

  CHECK (nfired == 0);
  CHECK (queue_is_well_formed ());
}

TEST_CASE_FIXTURE (event_fixture,
		   "a run of entries is removed to the last one")
{
  /* Every entry names the same callback, so the queue has to come back
     holding none of them however long the run is.  */
  for (int i = 0; i < 8; i++)
    event (count_event, 7, 10 + i);

  remove_event (count_event, 7);
  CHECK (queue_is_well_formed ());
  advance_time (100);

  CHECK (nfired == 0);
  CHECK (queue_is_well_formed ());
}

TEST_CASE_FIXTURE (event_fixture,
		   "a negative argument removes every entry for the callback")
{
  /* A negative argument names no single entry, so it takes all of them for
     that callback and leaves the others standing.  */
  event (count_event, 1, 10);
  event (count_event, 2, 20);
  event (other_event, 3, 30);
  event (count_event, 4, 40);

  remove_event (count_event, -1);
  CHECK (queue_is_well_formed ());
  advance_time (50);

  /* Only the entry naming the other callback is left.  */
  CHECK (nfired == 1);
  CHECK (queue_is_well_formed ());
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
