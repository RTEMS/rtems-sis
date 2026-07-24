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

TEST_CASE ("sparc trap cost keeps the jitter above the base cost")
{
  /* jitter = (ninst ^ simtime) & 7.  Before the parentheses were corrected
     the mask applied to TRAP_C + jitter, so jitter 5, 6 and 7 wrapped to 0, 1
     and 2 and a trap could cost less than TRAP_C, or nothing at all.  */
  CHECK (trap_icnt (5, 0) == TRAP_C + 5);
  CHECK (trap_icnt (6, 0) == TRAP_C + 6);
  CHECK (trap_icnt (7, 0) == TRAP_C + 7);

  /* The values that never wrapped are unchanged by the fix.  */
  CHECK (trap_icnt (0, 0) == TRAP_C + 0);
  CHECK (trap_icnt (4, 0) == TRAP_C + 4);
}
