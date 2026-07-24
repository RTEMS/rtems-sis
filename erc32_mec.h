/* SPDX-License-Identifier: GPL-3.0-or-later */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* The ERC32 MEC peripheral, written as a template on an environment policy so
   that its state and dependencies are injected rather than reached through
   globals.  The real board (erc32.cc) instantiates it on an environment that
   forwards to the simulator globals; a test instantiates it on an environment
   holding its own state, so a case drives the MEC in isolation.

   The environment must provide:

     bool Verbose ();        whether to print progress, was sis_verbose
     int &Irl ();            the external interrupt request level of the CPU
     void ReportError ();    a parity or hardware error, was mecparerror ()

   This header currently models the MEC interrupt controller.  The remaining
   MEC subsystems move into it one at a time.  */

#ifndef SIS_ERC32_MEC_H
#define SIS_ERC32_MEC_H

#include "sis.h"

#include <cstdio>

namespace erc32
{

template <class Env> class Mec
{
public:
  explicit Mec (Env &env) : env_ (env) { ResetInterrupts (); }

  /* The interrupt controller reset state: every maskable level masked,
     nothing pending, forced, shaped or in test mode.  */
  void
  ResetInterrupts ()
  {
    imr_ = 0x7ffe;
    ipr_ = 0;
    isr_ = 0;
    ifr_ = 0;
    tcr_ = 0;
  }

  uint32
  isr () const
  {
    return isr_;
  }
  uint32
  ipr () const
  {
    return ipr_;
  }
  uint32
  imr () const
  {
    return imr_;
  }
  uint32
  ifr () const
  {
    return ifr_;
  }
  uint32
  tcr () const
  {
    return tcr_;
  }

  /* The interrupt shape register keeps its written value; the reserved bits
     are an error.  */
  void
  WriteIsr (uint32 data)
  {
    if (data & 0xffffe000u)
      env_.ReportError ();
    isr_ = data;
  }

  /* The mask register holds levels 1..14; bit 0, bit 15 and above are
     reserved.  A change re-evaluates the request level.  */
  void
  WriteImr (uint32 data)
  {
    if (data & 0xffff8001u)
      env_.ReportError ();
    imr_ = data & 0x7ffeu;
    ChkIrq ();
  }

  /* The clear register removes the named levels from the pending set.  */
  void
  WriteIcr (uint32 data)
  {
    if (data & 0xffff0001u)
      env_.ReportError ();
    ipr_ &= ~data & 0x0fffeu;
    ChkIrq ();
  }

  /* The force register drives interrupts directly, but only in test mode.  */
  void
  WriteIfr (uint32 data)
  {
    if (tcr_ & 0x080000u)
      {
	if (data & 0xffff0001u)
	  env_.ReportError ();
	ifr_ = data & 0xfffeu;
	ChkIrq ();
      }
  }

  /* The test control register; the force-enable bit lives at 0x80000.  */
  void
  WriteTcr (uint32 data)
  {
    if (data & 0xffe1ffc0u)
      env_.ReportError ();
    tcr_ = data & 0x1e003fu;
  }

  /* Post a pending interrupt at LEVEL and re-evaluate.  */
  void
  Irq (int level)
  {
    ipr_ |= (1u << level);
    ChkIrq ();
  }

  /* Acknowledge LEVEL: clear its forced bit in test mode, otherwise its
     pending bit, then re-evaluate.  */
  void
  Intack (int level)
  {
    if (env_.Verbose ())
      printf ("interrupt %d acknowledged\n", level);
    if ((tcr_ & 0x80000u) && (ifr_ & (1u << level)))
      ifr_ &= ~(1u << level);
    else
      ipr_ &= ~(1u << level);
    ChkIrq ();
  }

  /* Drive the highest pending, unmasked level onto the CPU.  In test mode the
     forced set joins the pending set.  Level 15 is non-maskable because the
     mask register cannot hold its bit.  */
  void
  ChkIrq ()
  {
    int old_irl = env_.Irl ();
    uint32 itmp = (tcr_ & 0x80000u) ? ifr_ : 0;
    itmp = ((ipr_ | itmp) & ~imr_) & 0x0fffeu;
    env_.Irl () = 0;
    if (itmp != 0)
      {
	/* itmp is masked to levels 1..15 and is non-zero, so a set bit is
	   always found at or below 15 and the scan needs no lower bound.  */
	int i = 15;
	while (((itmp >> i) & 1) == 0)
	  i--;
	if (env_.Verbose () && i > old_irl)
	  printf ("IU irl: %d\n", i);
	env_.Irl () = i;
      }
  }

private:
  Env &env_;

  uint32 imr_;
  uint32 ipr_;
  uint32 isr_;
  uint32 ifr_;
  uint32 tcr_;
};

} // namespace erc32

#endif
