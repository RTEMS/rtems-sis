/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Tests for the IRQMP multiprocessor interrupt controller in grlib.cc.

   The controller is what every other GRLIB core reaches through
   grlib_set_irq, and what turns a pending interrupt into the level the CPU
   sees in ext_irl.  A case drives it through its own register file with no
   board and asserts on ext_irl, which is the whole of its output.

   The expectations come from the GRLIB IP core manual in ref/, chapter 96,
   which specifies the register layout in sections 96.3.1 to 96.3.12.  */

#include "doctest.h"
#include "support.h"

#include "config.h"
#include "grlibcore.h"

using sis_tests::stdout_capture;

namespace
{

/* Register offsets, from the tables of chapter 96.  grlib.cc keeps its own
   copies as file-local macros, so these are transcribed from the manual
   rather than shared with it.  */
const uint32 IPEND = 0x04;
const uint32 IFORCE = 0x08;
const uint32 ICLEAR = 0x0c;
const uint32 MPSTAT = 0x10;
const uint32 BROADCAST = 0x14;
const uint32 IMASK = 0x40;
const uint32 IMASK1 = 0x44;
const uint32 IMASK2 = 0x48;
const uint32 IMASK3 = 0x4c;
const uint32 IFORCE0 = 0x80;
const uint32 IFORCE1 = 0x84;
const uint32 IFORCE2 = 0x88;
const uint32 IFORCE3 = 0x8c;
const uint32 PEXTACK0 = 0xc0;
const uint32 PEXTACK1 = 0xc4;
const uint32 PEXTACK2 = 0xc8;
const uint32 PEXTACK3 = 0xcc;

struct irqmp_fixture : sis_tests::grlib_core_fixture
{
  int saved_extirq;

  irqmp_fixture () : sis_tests::grlib_core_fixture (&irqmp), saved_extirq (0)
  {
    /* The fixture already ran init.  A board picks the extended interrupt
       line before that, so record what the default left and drive from a
       known one.  */
    saved_extirq = irqmp_extirq;
    for (int i = 0; i < NCPU; i++)
      ext_irl[i] = 0;
  }

  /* A board picks the extended interrupt line and only then runs init,
     which is where the interrupt mask is computed from it.  A case which
     set the line afterwards would drive a mask built from the default.  */
  void
  use_extirq (int line)
  {
    irqmp_extirq = line;
    core->init ();
  }

  ~irqmp_fixture ()
  {
    /* Put the line back through init, not by assignment.  The interrupt
       mask is computed from the line at init and nowhere else, so
       restoring one without the other leaves a mask that admits extended
       interrupts and a line that cannot carry them.  chk_irq then shifts
       by the unset line, which is undefined, and the next case in the
       process is the one that pays for it.  */
    irqmp_extirq = saved_extirq;
    core->init ();
    for (int i = 0; i < NCPU; i++)
      ext_irl[i] = 0;
  }
};

}

TEST_CASE_FIXTURE (irqmp_fixture, "IRQMP a masked interrupt reaches the CPU")
{
  /* Section 96.3.2 gives one pending bit per interrupt in IP[15:1], and
     96.3.8 one mask bit per interrupt.  The CPU sees a level only where the
     two agree.  */
  write (IPEND, 1 << 5);
  CHECK (ext_irl[0] == 0);

  write (IMASK, 1 << 5);
  CHECK (ext_irl[0] == 5);

  /* Clearing the mask takes the level away again.  */
  write (IMASK, 0);
  CHECK (ext_irl[0] == 0);
}

TEST_CASE_FIXTURE (irqmp_fixture, "IRQMP the highest pending level wins")
{
  /* Chapter 96 says the controller presents the highest unmasked interrupt,
     interrupt 15 being the most urgent.  */
  write (IMASK, 0xfffe);
  write (IPEND, (1 << 3) | (1 << 11) | (1 << 7));
  CHECK (ext_irl[0] == 11);

  /* With the highest one taken away the next one shows.  */
  write (ICLEAR, 1 << 11);
  CHECK (ext_irl[0] == 7);

  write (ICLEAR, 1 << 7);
  CHECK (ext_irl[0] == 3);
}

TEST_CASE_FIXTURE (irqmp_fixture, "IRQMP interrupt zero does not exist")
{
  /* Every register of chapter 96 reserves bit 0 and reads it as zero, since
     there is no interrupt 0 on this bus.  */
  write (IMASK, 0xffff);
  write (IPEND, 0xffff);

  CHECK ((read (IPEND) & 1) == 0);
  CHECK ((read (IMASK) & 1) == 0);
  CHECK (ext_irl[0] != 0);
}

TEST_CASE_FIXTURE (irqmp_fixture, "IRQMP a clear write takes a pending bit")
{
  /* Section 96.3.4 makes ICLEAR write only: a one clears the matching
     pending bit and a zero leaves it alone.  */
  write (IMASK, 0xfffe);
  write (IPEND, (1 << 4) | (1 << 9));
  CHECK (read (IPEND) == ((1u << 4) | (1u << 9)));

  write (ICLEAR, 1 << 4);
  CHECK (read (IPEND) == (1u << 9));

  /* Clearing a bit which was not pending changes nothing.  */
  write (ICLEAR, 1 << 12);
  CHECK (read (IPEND) == (1u << 9));
}

TEST_CASE_FIXTURE (irqmp_fixture, "IRQMP a forced interrupt is per CPU")
{
  /* Section 96.3.3 gives each CPU its own force register, which raises an
     interrupt on that CPU alone.  The pending register is shared.  */
  write (IMASK, 0xfffe);
  write (IFORCE, 1 << 6);

  CHECK (ext_irl[0] == 6);
  CHECK (read (IPEND) == 0);
  CHECK (read (IFORCE) == (1u << 6));
}

TEST_CASE_FIXTURE (irqmp_fixture, "IRQMP the status register reports the CPUs")
{
  /* Section 96.3.5 puts the CPU count less one at the top of MPSTAT, the
     extended interrupt line in bits 19 to 16, and one power down status bit
     per CPU at the bottom.  */
  use_extirq (10);

  sregs[0].pwd_mode = 0;
  CHECK (((read (MPSTAT) >> 28) & 0xf) == (uint32) (ncpu - 1));
  CHECK (((read (MPSTAT) >> 16) & 0xf) == 10);
  CHECK ((read (MPSTAT) & 1) == 0);

  /* A halted CPU shows in its own status bit.  */
  sregs[0].pwd_mode = 1;
  CHECK ((read (MPSTAT) & 1) == 1);

  /* Writing a one to a CPU's bit starts it again.  */
  sregs[0].pwdstart = ebase.simtime;
  write (MPSTAT, 1);
  CHECK (sregs[0].pwd_mode == 0);

  sregs[0].pwd_mode = 0;
}

TEST_CASE_FIXTURE (irqmp_fixture, "IRQMP a broadcast interrupt is forced")
{
  /* Section 96.3.6 says an interrupt named in the broadcast register is
     delivered to every CPU's force register rather than to the shared
     pending register.  */
  write (IMASK, 0xfffe);
  write (BROADCAST, 1 << 8);
  CHECK (read (BROADCAST) == (1u << 8));

  grlib_set_irq (8);
  CHECK (read (IFORCE) == (1u << 8));
  CHECK (read (IPEND) == 0);
  CHECK (ext_irl[0] == 8);

  /* An interrupt outside the broadcast set still goes to the shared pending
     register.  */
  grlib_set_irq (9);
  CHECK (read (IPEND) == (1u << 9));
}

TEST_CASE_FIXTURE (irqmp_fixture, "IRQMP a core raises through grlib_set_irq")
{
  /* This is the path every other GRLIB core takes, and the reason a test of
     one of them can assert on ext_irl.  */
  write (IMASK, 0xfffe);

  grlib_set_irq (13);
  CHECK (read (IPEND) == (1u << 13));
  CHECK (ext_irl[0] == 13);
}

TEST_CASE_FIXTURE (irqmp_fixture,
		   "IRQMP an extended interrupt forwards through eirq")
{
  /* Section 96.3.2 puts extended interrupts 16 to 31 in IPEND[31:16].
     chk_irq folds any of those bits into the eirq line named by MPSTAT's
     EIRQ field (96.3.5), which is what actually reaches the CPU.  */
  use_extirq (14);

  write (IMASK, (1 << 14) | (1 << 20));
  write (IPEND, 1 << 20);

  CHECK (ext_irl[0] == 14);
  CHECK ((read (IPEND) & (1u << 20)) != 0);
}

TEST_CASE_FIXTURE (irqmp_fixture,
		   "IRQMP intack finds the pending extended interrupt")
{
  /* Section 96.3.10: acknowledging the eirq level records the highest
     pending extended interrupt's ID in PEXTACK and clears its IPEND bit.  */
  use_extirq (14);

  write (IMASK, (1 << 14) | (1 << 20));
  write (IPEND, 1 << 20);
  CHECK (ext_irl[0] == 14);

  sregs[0].intack (14, 0);

  CHECK (read (PEXTACK0) == 20);
  CHECK ((read (IPEND) & (1u << 20)) == 0);
  CHECK (ext_irl[0] == 0);
}

TEST_CASE_FIXTURE (
    irqmp_fixture,
    "IRQMP intack on eirq with no extended interrupt pending clears force")
{
  /* Section 96.3.10: PEXTACK reads 0 when the eirq assertion did not come
     from an extended interrupt.  Here eirq itself was forced directly, so
     the acknowledge still has to clear that plain force bit.  */
  use_extirq (14);

  write (IMASK, 1 << 14);
  write (IFORCE, 1 << 14);
  CHECK (ext_irl[0] == 14);

  sregs[0].intack (14, 0);

  CHECK (read (PEXTACK0) == 0);
  CHECK (read (IFORCE) == 0);
  CHECK (ext_irl[0] == 0);
}

TEST_CASE_FIXTURE (irqmp_fixture, "IRQMP intack clears a forced interrupt")
{
  /* A plain (non-extended) interrupt raised through the force register is
     cleared from IFORCE, not from IPEND, on acknowledge.  */
  use_extirq (14);

  write (IMASK, 1 << 6);
  write (IFORCE, 1 << 6);
  CHECK (ext_irl[0] == 6);

  sregs[0].intack (6, 0);

  CHECK (read (IFORCE) == 0);
  CHECK (read (IPEND) == 0);
  CHECK (ext_irl[0] == 0);
}

TEST_CASE_FIXTURE (irqmp_fixture, "IRQMP intack clears a pending interrupt")
{
  /* The same plain interrupt raised through IPEND instead is cleared from
     IPEND on acknowledge, not from IFORCE.  */
  use_extirq (14);

  write (IMASK, 1 << 6);
  write (IPEND, 1 << 6);
  CHECK (ext_irl[0] == 6);

  sregs[0].intack (6, 0);

  CHECK (read (IPEND) == 0);
  CHECK (ext_irl[0] == 0);
}

TEST_CASE_FIXTURE (irqmp_fixture,
		   "IRQMP processor N extended acknowledge is per CPU")
{
  /* Section 96.3.10 gives every CPU its own PEXTACK register at 0xC0 +
     4*n.  Acknowledging on CPU 1 must only touch PEXTACK1.  */
  use_extirq (14);

  write (IMASK1, (1 << 14) | (1 << 22));
  write (IPEND, 1 << 22);

  sregs[1].intack (14, 1);

  CHECK (read (PEXTACK0) == 0);
  CHECK (read (PEXTACK1) == 22);
  CHECK (read (PEXTACK2) == 0);
  CHECK (read (PEXTACK3) == 0);
}

TEST_CASE_FIXTURE (irqmp_fixture, "IRQMP per-CPU masks are independent")
{
  /* Section 96.3.8 gives each CPU its own PIMASK register at 0x40 + 4*n.
     The same IPEND bits can present a different level to each CPU.  */
  int saved_ncpu = ncpu;
  ncpu = 4;

  write (IMASK, 0xfffe);
  write (IMASK1, 1 << 5);
  write (IMASK2, 1 << 5);
  write (IMASK3, 1 << 5);
  write (IPEND, (1 << 5) | (1 << 9));

  CHECK (read (IMASK1) == (1u << 5));
  CHECK (read (IMASK2) == (1u << 5));
  CHECK (read (IMASK3) == (1u << 5));
  CHECK (ext_irl[0] == 9);
  CHECK (ext_irl[1] == 5);
  CHECK (ext_irl[2] == 5);
  CHECK (ext_irl[3] == 5);

  ncpu = saved_ncpu;
}

TEST_CASE_FIXTURE (irqmp_fixture,
		   "IRQMP per-CPU force registers are independent")
{
  /* Section 96.3.9 gives each CPU its own PIFORCE register at 0x80 + 4*n.
     Forcing one CPU must not raise the others.  */
  int saved_ncpu = ncpu;
  ncpu = 4;

  write (IMASK, 0xfffe);
  write (IMASK1, 0xfffe);
  write (IMASK2, 0xfffe);
  write (IMASK3, 0xfffe);

  write (IFORCE0, 1 << 3);
  write (IFORCE1, 1 << 7);
  write (IFORCE2, 1 << 10);
  write (IFORCE3, 1 << 12);

  CHECK (read (IFORCE0) == (1u << 3));
  CHECK (read (IFORCE1) == (1u << 7));
  CHECK (read (IFORCE2) == (1u << 10));
  CHECK (read (IFORCE3) == (1u << 12));

  CHECK (ext_irl[0] == 3);
  CHECK (ext_irl[1] == 7);
  CHECK (ext_irl[2] == 10);
  CHECK (ext_irl[3] == 12);

  ncpu = saved_ncpu;
}

TEST_CASE_FIXTURE (irqmp_fixture,
		   "IRQMP init masks extended interrupts off when eirq is 0")
{
  /* irqmp_init picks the interrupt mask from irqmp_extirq: nonzero (the
     default -1 included) allows the extended interrupt bits 31:16, and
     zero (eirq disabled) restricts the mask to the plain interrupts
     15:1, per section 96.3.2.  The fixture only runs init once with its
     own default, so re-run it here with eirq explicitly disabled.  */
  use_extirq (0);

  write (IMASK, 0xffffffff);
  CHECK (read (IMASK) == 0xfffe);

  write (IPEND, 0xffffffff);
  CHECK (read (IPEND) == 0xfffe);
}

TEST_CASE_FIXTURE (irqmp_fixture, "IRQMP reading an unmapped offset is zero")
{
  /* ILEVEL (96.3.1) and IRQMAP (96.3.12) are registers the manual defines
     but grlib.cc does not implement; any offset outside the cases irqmp_read
     matches falls through to a zero read.  */
  CHECK (read (0x00) == 0);
}

TEST_CASE_FIXTURE (irqmp_fixture,
		   "IRQMP a broadcast interrupt reaches every CPU")
{
  /* Section 96.3.6: a broadcast interrupt is written to the force register
     of every CPU, not just CPU 0.  */
  int saved_ncpu = ncpu;
  ncpu = 4;

  write (IMASK, 0xfffe);
  write (IMASK1, 0xfffe);
  write (IMASK2, 0xfffe);
  write (IMASK3, 0xfffe);
  write (BROADCAST, 1 << 8);

  grlib_set_irq (8);

  CHECK (read (IFORCE0) == (1u << 8));
  CHECK (read (IFORCE1) == (1u << 8));
  CHECK (read (IFORCE2) == (1u << 8));
  CHECK (read (IFORCE3) == (1u << 8));
  CHECK (read (IPEND) == 0);

  CHECK (ext_irl[0] == 8);
  CHECK (ext_irl[1] == 8);
  CHECK (ext_irl[2] == 8);
  CHECK (ext_irl[3] == 8);

  ncpu = saved_ncpu;
}

TEST_CASE_FIXTURE (irqmp_fixture,
		   "IRQMP verbose tracing follows the same interrupt path")
{
  /* Same scenario as "IRQMP intack finds the pending extended interrupt"
     (96.3.10), with sis_verbose raised so the trace prints chk_irq and
     irqmp_intack guard with "sis_verbose > 2" also execute.  The
     interrupt-visible behaviour is identical to that case; only the trace
     output differs, so this case exists to reach those prints rather than
     to claim anything chapter 96 does not already cover above.  */
  sis_verbose = 3;

  use_extirq (14);
  write (IMASK, (1 << 14) | (1 << 20));

  std::string out;
  {
    stdout_capture cap;
    write (IPEND, 1 << 20);
    CHECK (ext_irl[0] == 14);
    out = cap.str ();
  }
  /* chk_irq forwarding the extended interrupt (grlib.cc:583-585).  */
  CHECK (out.find ("forward extended interrupt") != std::string::npos);
  /* chk_irq reporting the new irl (grlib.cc:595-597).  */
  CHECK (out.find ("irl:") != std::string::npos);

  /* A second write that leaves the highest pending level unchanged makes
     chk_irq recompute the same irl it already reported, which is the
     "i != old_irl" half of the guard at grlib.cc:595 reading false: the
     level does not change, so it must not print again.  */
  {
    stdout_capture cap;
    write (IMASK, (1 << 14) | (1 << 20));
    CHECK (ext_irl[0] == 14);
    out = cap.str ();
  }
  CHECK (out.find ("irl:") == std::string::npos);

  {
    stdout_capture cap;
    sregs[0].intack (14, 0);
    out = cap.str ();
  }

  CHECK (read (PEXTACK0) == 20);
  CHECK ((read (IPEND) & (1u << 20)) == 0);
  CHECK (ext_irl[0] == 0);

  /* irqmp_intack's general acknowledge log (grlib.cc:541-543).  */
  CHECK (out.find ("acknowledged") != std::string::npos);
  /* irqmp_intack finding the pending extended interrupt (grlib.cc:553-556). */
  CHECK (out.find ("set extended interrupt acknowledge to") !=
	 std::string::npos);
}

TEST_CASE_FIXTURE (irqmp_fixture,
		   "IRQMP writing an unmapped offset changes nothing")
{
  /* irqmp_write's switch has no default case (unlike irqmp_read's, already
     covered by "IRQMP reading an unmapped offset is zero" above): an
     offset chapter 96 does not define, such as 0x00 (ILEVEL, 96.3.1), is a
     silent no-op on the write side too.  */
  write (IMASK, 0xfffe);
  write (IPEND, 1 << 5);
  CHECK (ext_irl[0] == 5);

  write (0x00, 0xffffffff);

  CHECK (ext_irl[0] == 5);
  CHECK (read (IPEND) == (1u << 5));
  CHECK (read (IMASK) == 0xfffe);
}

TEST_CASE_FIXTURE (irqmp_fixture,
		   "IRQMP writing MPSTAT only starts the CPUs it names")
{
  /* Section 96.3.5: a one written to MPSTAT starts the processor at that
     bit position if it is powered down.  irqmp_write's ISR/MPSTAT case
     loops the written bits (96.3.5's per-CPU status field), so with
     ncpu > 1 -- which "IRQMP the status register reports the CPUs" above
     does not reach -- a bit left clear must leave its CPU alone, and a bit
     for a CPU that is not powered down must be a no-op.  */
  int saved_ncpu = ncpu;
  ncpu = 4;

  sregs[0].pwd_mode = 1; /* named and halted: starts */
  sregs[0].pwdstart = ebase.simtime;
  sregs[1].pwd_mode = 1; /* halted but its bit is not written: untouched */
  sregs[1].pwdstart = ebase.simtime;
  sregs[2].pwd_mode = 0; /* named but already running: no-op */

  sis_verbose = 2;
  stdout_capture cap;

  write (MPSTAT, (1 << 0) | (1 << 2));

  std::string out = cap.str ();

  CHECK (sregs[0].pwd_mode == 0);
  CHECK (out.find ("cpu 0 starting") != std::string::npos);

  CHECK (sregs[1].pwd_mode == 1);

  CHECK (sregs[2].pwd_mode == 0);
  CHECK (out.find ("cpu 2 starting") == std::string::npos);

  sregs[0].pwd_mode = 0;
  sregs[1].pwd_mode = 0;
  ncpu = saved_ncpu;
}

TEST_CASE_FIXTURE (irqmp_fixture, "IRQMP verbose add announces the controller")
{
  /* irqmp_add is grlib.cc's board registration hook, not part of chapter
     96, and the fixture never calls it since the core is driven directly
     with no bus.  It only touches the plug&play table grlib_apbpp_add
     feeds, which this fixture does not expose, so the only observable
     effect of calling it here is the verbose trace, on or off.  */
  sis_verbose = 0;
  {
    stdout_capture quiet;
    core->add (0, 0x80000f00, 0xfff);
    CHECK (quiet.str ().empty ());
  }

  sis_verbose = 1;
  stdout_capture cap;

  core->add (0, 0x80000f00, 0xfff);

  CHECK (cap.str ().find ("IRQMP Interrupt controller") != std::string::npos);
}

TEST_CASE_FIXTURE (
    irqmp_fixture,
    "IRQMP (current behaviour) an eirq line above 15 drops the interrupt")
{
  /* Not from chapter 96: MPSTAT's EIRQ field (96.3.5) is bits 19:16, four
     bits wide, so the manual only ever describes eirq lines 0-15.
     grlib.cc never range-checks irqmp_extirq -- it is set from the -eirq
     command line option (sis.cc) with no bound -- so a line above 15 makes
     chk_irq forward a bit into a position its own scan of bits 1-15
     (96.3.2's plain interrupt range) can never see.  The pending extended
     interrupt is then silently lost instead of reaching the CPU.  Named
     "current behaviour" because the manual does not define this input at
     all, not because it is a documented interface.  */
  use_extirq (20);

  write (IMASK, 1u << 20);
  write (IPEND, 1u << 20);

  CHECK ((read (IPEND) & (1u << 20)) != 0);
  CHECK (ext_irl[0] == 0);
}
