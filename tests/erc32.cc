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

/* Drains the event queue up to a simulator time, running each due callback.
   Internal to func.cc, declared here to fire the timer interrupts a test
   schedules.  */
extern void advance_time (uint64 endtime);

namespace
{

/* MEC register window and the interrupt controller register offsets, from
   erc32.cc (the defines there are file-local).  */
const uint32 MEC = 0x01f80000;
const uint32 R_MCR = 0x000;
const uint32 R_SFR = 0x004;
const uint32 R_MEMCFG = 0x010;
const uint32 R_IOCR = 0x014;
const uint32 R_WCR = 0x018;
const uint32 R_SSA1 = 0x020;
const uint32 R_SEA1 = 0x024;
const uint32 R_SSA2 = 0x028;
const uint32 R_SEA2 = 0x02c;
const uint32 R_ISR = 0x044;
const uint32 R_IPR = 0x048;
const uint32 R_IMR = 0x04c;
const uint32 R_ICR = 0x050;
const uint32 R_IFR = 0x054;
const uint32 R_RTC_COUNTER = 0x080;
const uint32 R_RTC_SCALER = 0x084;
const uint32 R_GPT_COUNTER = 0x088;
const uint32 R_GPT_SCALER = 0x08c;
const uint32 R_TIMER_CTRL = 0x098;
const uint32 R_SFSR = 0x0a0;
const uint32 R_FFAR = 0x0a4;
const uint32 R_TCR = 0x0d0;

/* MEC timer control register bits: RTC and GPT counter reload, counter load,
   scaler enable.  */
const uint32 TC_GPT_RELOAD = 0x001;
const uint32 TC_GPT_LOAD = 0x002;
const uint32 TC_GPT_SCALER_EN = 0x004;
const uint32 TC_RTC_RELOAD = 0x100;
const uint32 TC_RTC_LOAD = 0x200;
const uint32 TC_RTC_SCALER_EN = 0x400;

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

TEST_CASE_FIXTURE (mec_fixture, "MEC reads the running GPT scaler down")
{
  /* A running scaler reads back as its start value minus the elapsed time.
     The GPT scaler read must follow the GPT's own enable, not the RTC's: with
     the GPT running and the RTC stopped it still counts down.  */
  ebase.simtime = 100;
  wr (R_GPT_SCALER, 50);
  wr (R_TIMER_CTRL, TC_GPT_SCALER_EN); /* start the GPT scaler only */

  ebase.simtime = 110;
  CHECK (rd (R_GPT_SCALER) == 40);
}

TEST_CASE_FIXTURE (mec_fixture, "MEC timer counters load from their reloads")
{
  /* A counter load copies the reload register into the counter, which then
     reads back.  */
  wr (R_RTC_COUNTER, 0x1234); /* the counter offset is the reload on write */
  wr (R_TIMER_CTRL, TC_RTC_LOAD);
  CHECK (rd (R_RTC_COUNTER) == 0x1234);

  wr (R_GPT_COUNTER, 0x5678);
  wr (R_TIMER_CTRL, TC_GPT_LOAD);
  CHECK (rd (R_GPT_COUNTER) == 0x5678);
}

TEST_CASE_FIXTURE (mec_fixture, "MEC reads the RTC scaler down while running")
{
  /* The running RTC scaler reads down from its start; a stopped GPT scaler
     reads its static value.  */
  ebase.simtime = 200;
  wr (R_RTC_SCALER, 100);
  wr (R_TIMER_CTRL, TC_RTC_SCALER_EN);

  ebase.simtime = 230;
  CHECK (rd (R_RTC_SCALER) == 70);
  CHECK (rd (R_GPT_SCALER) == 0xffff); /* GPT stopped, reset scaler value */
}

TEST_CASE_FIXTURE (mec_fixture, "MEC timer writes reject their reserved bits")
{
  /* The scaler and control writes route their reserved bits through the
     error manager, which is set to raise level 1.  */
  wr (R_MCR, MCR_HWERR_IRQ);

  wr (R_GPT_SCALER, 0x10000); /* above the 16-bit scaler */
  CHECK ((rd (R_IPR) & (1u << 1)) != 0);
  wr (R_ICR, 1u << 1);

  wr (R_RTC_SCALER, 0x100); /* above the 8-bit scaler */
  CHECK ((rd (R_IPR) & (1u << 1)) != 0);
  wr (R_ICR, 1u << 1);

  wr (R_TIMER_CTRL, 0x10); /* not a control bit */
  CHECK ((rd (R_IPR) & (1u << 1)) != 0);
}

TEST_CASE_FIXTURE (mec_fixture, "MEC RTC tick decrements a non-zero counter")
{
  /* A tick with a non-zero counter decrements it and, with the scaler still
     enabled, re-arms.  */
  wr (R_RTC_SCALER, 0); /* fire on the next tick */
  wr (R_RTC_COUNTER, 5);
  wr (R_TIMER_CTRL, TC_RTC_RELOAD | TC_RTC_LOAD | TC_RTC_SCALER_EN);

  advance_time (1);
  CHECK (rd (R_RTC_COUNTER) == 4);

  /* Re-enabling while already running does not restart it.  */
  wr (R_TIMER_CTRL, TC_RTC_SCALER_EN);
}

TEST_CASE_FIXTURE (mec_fixture, "MEC RTC underflow reloads and interrupts")
{
  /* A tick at zero raises interrupt 13, and with reload enabled the counter
     reloads and the timer keeps running.  */
  wr (R_RTC_SCALER, 0);
  wr (R_RTC_COUNTER, 0);
  wr (R_TIMER_CTRL, TC_RTC_RELOAD | TC_RTC_LOAD | TC_RTC_SCALER_EN);

  advance_time (1);
  CHECK ((rd (R_IPR) & (1u << 13)) != 0);
}

TEST_CASE_FIXTURE (mec_fixture, "MEC RTC underflow without reload stops")
{
  /* A tick at zero with reload disabled raises the interrupt and stops the
     timer.  */
  wr (R_RTC_SCALER, 0);
  wr (R_RTC_COUNTER, 0);
  wr (R_TIMER_CTRL, TC_RTC_LOAD | TC_RTC_SCALER_EN); /* no reload bit */

  advance_time (1);
  CHECK ((rd (R_IPR) & (1u << 13)) != 0);
  /* Stopped: the scaler now reads its static value, not a running-down one. */
  CHECK (rd (R_RTC_SCALER) == 0);
}

TEST_CASE_FIXTURE (mec_fixture, "MEC GPT tick decrements a non-zero counter")
{
  wr (R_GPT_SCALER, 0);
  wr (R_GPT_COUNTER, 5);
  wr (R_TIMER_CTRL, TC_GPT_RELOAD | TC_GPT_LOAD | TC_GPT_SCALER_EN);

  advance_time (1);
  CHECK (rd (R_GPT_COUNTER) == 4);

  wr (R_TIMER_CTRL, TC_GPT_SCALER_EN); /* already running, no restart */
}

TEST_CASE_FIXTURE (mec_fixture, "MEC GPT underflow reloads and interrupts")
{
  wr (R_GPT_SCALER, 0);
  wr (R_GPT_COUNTER, 0);
  wr (R_TIMER_CTRL, TC_GPT_RELOAD | TC_GPT_LOAD | TC_GPT_SCALER_EN);

  advance_time (1);
  CHECK ((rd (R_IPR) & (1u << 12)) != 0);
}

TEST_CASE_FIXTURE (mec_fixture, "MEC GPT underflow without reload stops")
{
  wr (R_GPT_SCALER, 0);
  wr (R_GPT_COUNTER, 0);
  wr (R_TIMER_CTRL, TC_GPT_LOAD | TC_GPT_SCALER_EN);

  advance_time (1);
  CHECK ((rd (R_IPR) & (1u << 12)) != 0);
  CHECK (rd (R_GPT_SCALER) == 0);
}

TEST_CASE_FIXTURE (mec_fixture,
		   "MEC narrates timer starts and stops when verbose")
{
  sis_verbose = 1;
  stdout_capture cap;

  /* Start both timers with a zero counter and no reload, then let each tick
     once so it stops.  */
  wr (R_RTC_SCALER, 0);
  wr (R_RTC_COUNTER, 0);
  wr (R_TIMER_CTRL, TC_RTC_LOAD | TC_RTC_SCALER_EN);
  wr (R_GPT_SCALER, 0);
  wr (R_GPT_COUNTER, 0);
  wr (R_TIMER_CTRL, TC_GPT_LOAD | TC_GPT_SCALER_EN);
  advance_time (1);

  std::string out = cap.str ();
  CHECK (out.find ("RTC started") != std::string::npos);
  CHECK (out.find ("GPT started") != std::string::npos);
  CHECK (out.find ("RTC stopped") != std::string::npos);
  CHECK (out.find ("GPT stopped") != std::string::npos);
}

TEST_CASE_FIXTURE (mec_fixture, "MEC configuration registers round-trip")
{
  /* Route errors to the interrupt, then exercise the read-back and the
     reserved-bit rejection of the configuration registers.  */
  wr (R_MCR, MCR_HWERR_IRQ);

  wr (R_IOCR, 0);
  CHECK (rd (R_IOCR) == 0);

  wr (R_WCR, 0x1234);
  wr (R_MEMCFG, (3u << 18) | (4u << 10));

  wr (R_SSA1, 0x1000);
  CHECK ((rd (R_SSA1) & 0x7fffff) == 0x1000);
  wr (R_SEA1, 0x2000);
  CHECK (rd (R_SEA1) == 0x2000);
  wr (R_SSA2, 0x3000);
  CHECK ((rd (R_SSA2) & 0x7fffff) == 0x3000);
  wr (R_SEA2, 0x4000);
  CHECK (rd (R_SEA2) == 0x4000);

  wr (R_SFSR, 0);
  CHECK (rd (R_SFSR) == 0x78);
  rd (R_FFAR);
  rd (R_MCR);
  rd (R_MEMCFG);
}

TEST_CASE_FIXTURE (mec_fixture,
		   "MEC configuration registers reject reserved bits")
{
  /* Every configuration write rejects its reserved bits through the error
     manager, routed here to interrupt level 1.  */
  wr (R_MCR, MCR_HWERR_IRQ);

  wr (R_IOCR, 0xc0000000);
  CHECK ((rd (R_IPR) & (1u << 1)) != 0);
  wr (R_ICR, 1u << 1);

  wr (R_SSA1, 0xfe000000);
  CHECK ((rd (R_IPR) & (1u << 1)) != 0);
  wr (R_ICR, 1u << 1);

  wr (R_SEA1, 0xff800000);
  CHECK ((rd (R_IPR) & (1u << 1)) != 0);
  wr (R_ICR, 1u << 1);

  wr (R_SSA2, 0xfe000000);
  CHECK ((rd (R_IPR) & (1u << 1)) != 0);
  wr (R_ICR, 1u << 1);

  wr (R_SEA2, 0xff800000);
  CHECK ((rd (R_IPR) & (1u << 1)) != 0);
  wr (R_ICR, 1u << 1);

  wr (R_SFSR, 0x0800);
  CHECK ((rd (R_IPR) & (1u << 1)) != 0);
  wr (R_ICR, 1u << 1);

  wr (R_MEMCFG, 0x80000000);
  CHECK ((rd (R_IPR) & (1u << 1)) != 0);
}

TEST_CASE_FIXTURE (mec_fixture,
		   "MEC control register write triggers the hardware error")
{
  /* Setting the MCR hardware-error test bit raises a MEC error, routed to the
     interrupt.  */
  wr (R_MCR, MCR_HWERR_IRQ | 0x8000);
  CHECK ((rd (R_IPR) & (1u << 1)) != 0);
}

TEST_CASE_FIXTURE (mec_fixture,
		   "MEC reports protection and mode changes verbose")
{
  sis_verbose = 1;
  stdout_capture cap;

  /* Enabling the two write-protection segments, then the block-protection,
     software-reset and power-down mode bits, each announce themselves.  */
  wr (R_SSA1, (1u << 23) | 0x1000);
  wr (R_SSA2, (2u << 23) | 0x1000);
  wr (R_MCR, 0x08); /* block write protection */
  wr (R_MCR, 0x02); /* software reset enabled */
  wr (R_MCR, 0x01); /* power-down mode enabled */

  std::string out = cap.str ();
  CHECK (out.find ("Segment 1 memory protection enabled") !=
	 std::string::npos);
  CHECK (out.find ("Segment 2 memory protection enabled") !=
	 std::string::npos);
  CHECK (out.find ("Memory block write protection enabled") !=
	 std::string::npos);
  CHECK (out.find ("Software reset enabled") != std::string::npos);
  CHECK (out.find ("Power-down mode enabled") != std::string::npos);
}

TEST_CASE_FIXTURE (mec_fixture, "MEC decodes an 8-bit ROM configuration")
{
  /* With an 8-bit ROM the memory and waitstate decoders take their rom8
     branches.  */
  int saved_rom8 = rom8;
  rom8 = 1;
  sis_verbose = 1;
  stdout_capture cap;

  wr (R_MEMCFG, (3u << 18) | (4u << 10));
  wr (R_WCR, 0x0010); /* a non-zero ROM read waitstate field */
  wr (R_WCR, 0x0000); /* a zero ROM read waitstate field */

  std::string out = cap.str ();
  CHECK (out.find ("RAM size") != std::string::npos);
  CHECK (out.find ("Waitstates") != std::string::npos);
  rom8 = saved_rom8;
}

TEST_CASE_FIXTURE (mec_fixture, "MEC software reset register")
{
  /* An SFR write resets the system only when software reset is enabled in the
     control register.  */
  wr (R_MCR, MCR_HWERR_IRQ);
  wr (R_SFR, 0); /* software reset disabled, no effect */

  wr (R_MCR, 0x2); /* enable software reset */
  wr (R_SFR, 0);   /* resets the system, quietly */

  sregs[0].psr |= 0x80; /* the reset cleared the supervisor bit */
  wr (R_MCR, 0x2);
  sis_verbose = 1;
  stdout_capture cap;
  wr (R_SFR, 0); /* resets again, this time reported */

  std::string out = cap.str ();
  CHECK (out.find ("Software reset issued") != std::string::npos);
}

TEST_CASE_FIXTURE (mec_fixture, "MEC write protection follows either segment")
{
  /* mem_accprot is set when either segment enables protection, so a segment 2
     protection with segment 1 open still turns it on.  */
  wr (R_MCR, MCR_HWERR_IRQ);
  wr (R_SSA1, 0x1000);		    /* segment 1 open */
  wr (R_SSA2, (2u << 23) | 0x1000); /* segment 2 protected */
  wr (R_SSA1, 0x1000);		    /* rewrite segment 1: 0 || protected */
}

TEST_CASE_FIXTURE (mec_fixture,
		   "MEC traces writes and empty protection verbose")
{
  /* A verbosity above one traces every MEC write.  A control write with no
     protection enabled and segment writes that enable none take the
     accprot-false and wpr-false paths.  */
  sis_verbose = 2;
  stdout_capture cap;

  wr (R_MCR, 0x01);    /* verbose write, no write protection active */
  wr (R_SSA1, 0x1000); /* segment 1 with no protection bits */
  wr (R_SSA2, 0x1000); /* segment 2 with no protection bits */

  std::string out = cap.str ();
  CHECK (out.find ("MEC write") != std::string::npos);
}
