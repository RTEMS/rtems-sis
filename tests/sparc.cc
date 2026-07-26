/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Tests for sparc.cc trap handling.

   sparc_execute_trap charges a trap TRAP_C cycles plus three bits of jitter
   taken from ninst and simtime.  The jitter must be masked to 0..7 before the
   base cost is added, so a trap always costs at least TRAP_C.  */

#include "doctest.h"

#include "sparc.h"

namespace
{

/* execute_trap takes a below-256 trap through the register-window path.  A
   trap number below 17 skips the interrupt acknowledge, so no board callback
   is reached and a zeroed pstate is enough.  */
uint32
trap_icnt (uint64 ninst, uint64 simtime)
{
  struct pstate s = {};

  s.trap = 5;
  s.psr = PSR_ET;
  s.ninst = ninst;
  s.simtime = simtime;

  uint32 saved = ebase.coven;
  ebase.coven = 0;
  sparc32.execute_trap (&s);
  ebase.coven = saved;

  return s.icnt;
}

} // namespace

TEST_CASE ("sparc trap cost never drops below the base cost")
{
  /* Before the parentheses were corrected the mask applied to TRAP_C + jitter
     rather than to the jitter alone, so a trap could cost less than TRAP_C,
     or nothing at all.  Walk a whole jitter sequence over one processor.  */
  struct pstate s = {};
  bool          seen_above_base = false;

  for (int i = 0; i < 64; ++i)
    {
      s.icnt = 0;
      s.trap = 5;
      s.psr = PSR_ET;
      sparc32.execute_trap (&s);

      CHECK (s.icnt >= TRAP_C);
      CHECK (s.icnt <= TRAP_C + 7);

      if (s.icnt > TRAP_C)
        seen_above_base = true;
    }

  /* The jitter is meant to vary, not to sit at zero.  */
  CHECK (seen_above_base);
}

TEST_CASE ("sparc trap jitter does not follow the simulated time")
{
  /* Test code which hunts for a race by varying a busy wait moves both ninst
     and simtime.  A jitter derived from them would track the search variable
     instead of being independent of it, and the race window would never be
     found.  Two processors at the same point of the jitter sequence must be
     charged the same, whatever their time and instruction count.  */
  CHECK (trap_icnt (0, 0) == trap_icnt (12345, 67890));
  CHECK (trap_icnt (5, 0) == trap_icnt (0, 5));
  CHECK (trap_icnt (7, 7) == trap_icnt (1, 2));
}
