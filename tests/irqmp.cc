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

  ~irqmp_fixture ()
  {
    irqmp_extirq = saved_extirq;
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
  irqmp_extirq = 10;

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
