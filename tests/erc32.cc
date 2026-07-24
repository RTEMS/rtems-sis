/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Tests for erc32.cc, the ERC32 board (SPARC V8 IU plus the MEC peripheral
   set).

   The MEC registers are static to erc32.cc, so a case drives them the way
   the running processor does: word writes and reads through the board's
   memory_write/memory_read callbacks at the MEC register window.  A MEC
   register access must be a supervisor word access (ASI 0xb, size 2), so the
   fixture sets the supervisor bit in the PSR of CPU 0.

   The interrupt behaviour asserted here follows the SPARC V8 / TSC691E
   interrupt model (IU manual section 3.8.3): the external interrupt request
   level is the highest pending, unmasked level; level 15 is non-maskable;
   and an interrupt acknowledge clears the acknowledged source.  */

#include "doctest.h"
#include "support.h"

#include "config.h"
#include "sis.h"

#include <string>

using sis_tests::stdout_capture;

namespace
{

/* MEC register window and the interrupt controller register offsets, from
   erc32.cc (the defines there are file-local).  */
const uint32 MEC = 0x01f80000;
const uint32 R_MCR = 0x000;
const uint32 R_ISR = 0x044;
const uint32 R_IPR = 0x048;
const uint32 R_IMR = 0x04c;
const uint32 R_ICR = 0x050;
const uint32 R_IFR = 0x054;
const uint32 R_TCR = 0x0d0;

/* Store-size encoding for a word and the test-mode / IFR-enable bit of the
   MEC test control register.  */
const int32 SZ_WORD = 2;
const uint32 TCR_IFR_EN = 0x80000;

/* The MEC error manager sends a parity or hardware error to reset, to halt,
   or to interrupt request level 1, chosen by MCR bits.  Routing it to the
   interrupt keeps a reserved-bit write observable in a unit test instead of
   stopping the simulator.  */
const uint32 MCR_HWERR_IRQ = 0x2000;

struct mec_fixture
{
  int saved_verbose;
  int saved_cputype;
  int saved_archtype;

  mec_fixture ()
      : saved_verbose (sis_verbose), saved_cputype (cputype),
	saved_archtype (archtype)
  {
    cputype = CPU_ERC32;
    archtype = CPU_SPARC;
    ms = &erc32sys;
    ebase.freq = 14;
    ebase.simtime = 0;
    ebase.simstart = 0;
    reset_all ();
    init_bpt (sregs);
    ms->init_sim ();
    sis_verbose = 0;
    sregs[0].psr |= 0x80; /* supervisor, so a MEC access uses ASI 0xb */
    ext_irl[0] = 0;
  }

  ~mec_fixture ()
  {
    sis_verbose = saved_verbose;
    cputype = saved_cputype;
    archtype = saved_archtype;
  }

  int
  wr (uint32 off, uint32 val)
  {
    uint32 d = val;
    int32 ws;
    return ms->memory_write (MEC + off, &d, SZ_WORD, &ws);
  }

  uint32
  rd (uint32 off)
  {
    uint32 d = 0;
    int32 ws;
    ms->memory_read (MEC + off, &d, &ws);
    return d;
  }
};

} // namespace

TEST_CASE_FIXTURE (mec_fixture,
		   "MEC interrupt registers reset masked and idle")
{
  /* Reset masks every maskable level (bits 1..14) and leaves nothing
     pending, forced or in service.  */
  CHECK (rd (R_IMR) == 0x7ffe);
  CHECK (rd (R_IPR) == 0);
  CHECK (rd (R_IFR) == 0);
  CHECK (rd (R_ISR) == 0);
  CHECK (ext_irl[0] == 0);
}

TEST_CASE_FIXTURE (mec_fixture,
		   "MEC drives the highest unmasked level onto the IU")
{
  /* Force levels 5 and 13 with every level unmasked.  The IU sees the
     higher-priority level 13 (IU manual 3.8.3.1).  */
  wr (R_TCR, TCR_IFR_EN);
  wr (R_IMR, 0);
  wr (R_IFR, (1u << 13) | (1u << 5));
  CHECK (ext_irl[0] == 13);
}

TEST_CASE_FIXTURE (mec_fixture, "MEC leaves level 15 non-maskable")
{
  /* Level 15 is non-maskable: the mask register cannot hold its bit, so it
     reaches the IU even with the reset mask in place.  */
  wr (R_TCR, TCR_IFR_EN);
  wr (R_IMR, 0x7ffe);
  wr (R_IFR, 1u << 15);
  CHECK (ext_irl[0] == 15);
}

TEST_CASE_FIXTURE (mec_fixture, "MEC withholds a masked level from the IU")
{
  /* A masked level does not reach the IU; the request level stays at 0.  */
  wr (R_TCR, TCR_IFR_EN);
  wr (R_IMR, 0x7ffe);
  wr (R_IFR, 1u << 5);
  CHECK (ext_irl[0] == 0);
}

TEST_CASE_FIXTURE (mec_fixture, "MEC interrupt acknowledge clears the source")
{
  /* Acknowledge of the top level (IU manual 3.8.3.3) clears its forced bit
     and re-evaluates the request level down to the next pending one.  */
  wr (R_TCR, TCR_IFR_EN);
  wr (R_IMR, 0);
  wr (R_IFR, (1u << 13) | (1u << 5));
  REQUIRE (ext_irl[0] == 13);

  sregs[0].intack (13, 0);
  CHECK ((rd (R_IFR) & (1u << 13)) == 0);
  CHECK (ext_irl[0] == 5);

  /* Acknowledging a level that is not forced in test mode falls back to the
     pending register, which has no such bit, so the forced level 5 stays.  */
  sregs[0].intack (9, 0);
  CHECK (ext_irl[0] == 5);
}

TEST_CASE_FIXTURE (
    mec_fixture, "MEC error interrupt posts, drives and acknowledges level 1")
{
  /* Route a MEC error to the interrupt, then trigger one with a reserved-bit
     ISR write.  The error posts pending level 1, which reaches the IU and is
     cleared by an acknowledge.  */
  wr (R_MCR, MCR_HWERR_IRQ);
  wr (R_IMR, 0);
  wr (R_ISR, 0x2000); /* reserved bit -> MEC hardware error */

  CHECK ((rd (R_IPR) & (1u << 1)) != 0);
  CHECK (ext_irl[0] == 1);

  sregs[0].intack (1, 0);
  CHECK ((rd (R_IPR) & (1u << 1)) == 0);
  CHECK (ext_irl[0] == 0);
}

TEST_CASE_FIXTURE (mec_fixture,
		   "MEC accepts the valid interrupt register writes")
{
  /* The non-error write paths: a shape write with no reserved bits, a clear
     that removes a pending level, and an IFR write that is ignored unless the
     force mode is enabled.  */
  wr (R_MCR, MCR_HWERR_IRQ);
  wr (R_IMR, 0);

  wr (R_ISR, 0x1000);
  CHECK (rd (R_ISR) == 0x1000);

  /* Post pending level 1 through a MEC error, then clear it with a valid
     ICR write (the clear register acts on the pending register).  */
  wr (R_ISR, 0x2000);
  REQUIRE ((rd (R_IPR) & (1u << 1)) != 0);
  wr (R_ICR, 1u << 1);
  CHECK ((rd (R_IPR) & (1u << 1)) == 0);
  CHECK (ext_irl[0] == 0);

  /* With force mode off, an IFR write does not change the forced set.  */
  wr (R_TCR, 0);
  uint32 before = rd (R_IFR);
  wr (R_IFR, 0xffff);
  CHECK (rd (R_IFR) == before);
}

TEST_CASE_FIXTURE (mec_fixture,
		   "MEC reserved bits in the interrupt writes raise an error")
{
  /* Every interrupt register rejects its reserved bits through the error
     manager.  With the error routed to level 1, each reserved-bit write
     leaves level 1 pending.  */
  wr (R_MCR, MCR_HWERR_IRQ);

  wr (R_IMR, 1); /* bit 0 reserved */
  CHECK ((rd (R_IPR) & (1u << 1)) != 0);

  wr (R_ICR, 1); /* bit 0 reserved */
  CHECK ((rd (R_IPR) & (1u << 1)) != 0);

  wr (R_TCR, TCR_IFR_EN);
  wr (R_IFR, 1); /* bit 0 reserved, force mode on */
  CHECK ((rd (R_IPR) & (1u << 1)) != 0);
}

TEST_CASE_FIXTURE (mec_fixture, "MEC narrates interrupt changes when verbose")
{
  /* The verbose report of a rising request level and of an acknowledge.  A
     re-evaluation that does not raise the level prints nothing.  */
  sis_verbose = 1;
  stdout_capture cap;

  wr (R_TCR, TCR_IFR_EN);
  wr (R_IMR, 0);
  ext_irl[0] = 0;
  wr (R_IFR, 1u << 7); /* raises the level from 0 to 7 */
  wr (R_IMR, 0);       /* re-evaluates with the level already 7 */
  sregs[0].intack (7, 0);

  std::string out = cap.str ();
  CHECK (out.find ("IU irl: 7") != std::string::npos);
  CHECK (out.find ("interrupt 7 acknowledged") != std::string::npos);
}
