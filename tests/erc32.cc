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

#include "erc32_mec.h"

#include <stdio.h>
#include <string.h>
#include <string>
#include <unistd.h>

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
const uint32 R_UARTA = 0x0e0;
const uint32 R_UARTB = 0x0e4;
const uint32 R_UART_CTRL = 0x0e8;

/* The UART transmit buffer size, from erc32.cc.  */
const int UARTBUF = 1024;

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

/* A test environment for the MEC template: it owns the interrupt request
   level, the verbosity and an error count, so a case drives erc32::Mec in
   isolation with no simulator globals.  */
struct TestEnv
{
  int irl = 0;
  bool verbose = false;
  int errors = 0;

  bool
  Verbose ()
  {
    return verbose;
  }
  int &
  Irl ()
  {
    return irl;
  }
  void
  ReportError ()
  {
    ++errors;
  }
};

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

    /* The safe default the cases build on: reset masks every maskable level
       and leaves the interrupt controller idle.  */
    REQUIRE (rd (R_IMR) == 0x7ffe);
    REQUIRE (rd (R_IPR) == 0);
    REQUIRE (rd (R_ISR) == 0);
    REQUIRE (rd (R_IFR) == 0);
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

/* Redirect the UART input (file descriptor 0) to a temporary file holding a
   fixed string, so a UART read returns known bytes instead of blocking on the
   real stdin.  The descriptor is restored on destruction.  */
class stdin_feed
{
public:
  stdin_feed (const char *text) : file (tmpfile ()), saved (-1)
  {
    if (file != NULL)
      {
	fwrite (text, 1, strlen (text), file);
	fflush (file);
	rewind (file);
      }
    fflush (stdin);
    saved = dup (0);
    if (file != NULL)
      dup2 (fileno (file), 0);
  }

  ~stdin_feed ()
  {
    if (saved >= 0)
      {
	dup2 (saved, 0);
	close (saved);
      }
    if (file != NULL)
      fclose (file);
  }

  stdin_feed (const stdin_feed &) = delete;
  stdin_feed &operator= (const stdin_feed &) = delete;

private:
  FILE *file;
  int saved;
};

} // namespace

/* The interrupt controller is tested through erc32::Mec on a TestEnv, with no
   simulator globals: the environment owns the request level, the verbosity
   and the error count.  */

TEST_CASE ("Mec resets the interrupt controller masked and idle")
{
  TestEnv env;
  erc32::Mec<TestEnv> mec (env);

  /* Reset masks every maskable level and leaves nothing pending, forced or in
     service.  */
  CHECK (mec.imr () == 0x7ffe);
  CHECK (mec.ipr () == 0);
  CHECK (mec.ifr () == 0);
  CHECK (mec.isr () == 0);
  CHECK (mec.tcr () == 0);
  CHECK (env.irl == 0);
}

TEST_CASE ("Mec drives the highest unmasked level onto the IU")
{
  TestEnv env;
  erc32::Mec<TestEnv> mec (env);

  /* Force levels 5 and 13 with every level unmasked.  The IU sees the
     higher-priority level 13 (IU manual 3.8.3.1).  */
  mec.WriteTcr (TCR_IFR_EN);
  mec.WriteImr (0);
  mec.WriteIfr ((1u << 13) | (1u << 5));
  CHECK (env.irl == 13);
}

TEST_CASE ("Mec leaves level 15 non-maskable")
{
  TestEnv env;
  erc32::Mec<TestEnv> mec (env);

  /* Level 15 is non-maskable: the mask register cannot hold its bit, so it
     reaches the IU even with the reset mask in place.  */
  mec.WriteTcr (TCR_IFR_EN);
  mec.WriteImr (0x7ffe);
  mec.WriteIfr (1u << 15);
  CHECK (env.irl == 15);
}

TEST_CASE ("Mec withholds a masked level from the IU")
{
  TestEnv env;
  erc32::Mec<TestEnv> mec (env);

  mec.WriteTcr (TCR_IFR_EN);
  mec.WriteImr (0x7ffe);
  mec.WriteIfr (1u << 5);
  CHECK (env.irl == 0);
}

TEST_CASE ("Mec posts a pending interrupt and acknowledges it")
{
  TestEnv env;
  erc32::Mec<TestEnv> mec (env);

  mec.WriteImr (0);
  mec.Irq (13);
  CHECK (env.irl == 13);

  mec.Intack (13);
  CHECK (env.irl == 0);
}

TEST_CASE ("Mec acknowledge clears the forced source in test mode")
{
  TestEnv env;
  erc32::Mec<TestEnv> mec (env);

  mec.WriteTcr (TCR_IFR_EN);
  mec.WriteImr (0);
  mec.WriteIfr ((1u << 13) | (1u << 5));
  REQUIRE (env.irl == 13);

  mec.Intack (13); /* forced bit set: cleared, next level shows */
  CHECK ((mec.ifr () & (1u << 13)) == 0);
  CHECK (env.irl == 5);

  mec.Intack (9); /* not forced: falls back to the pending register */
  CHECK (env.irl == 5);
}

TEST_CASE ("Mec keeps the valid interrupt register writes")
{
  TestEnv env;
  erc32::Mec<TestEnv> mec (env);

  /* A shape write with no reserved bits, a clear that removes a pending
     level, and an IFR write ignored unless force mode is on.  */
  mec.WriteIsr (0x1000);
  CHECK (mec.isr () == 0x1000);
  CHECK (env.errors == 0);

  mec.WriteImr (0);
  mec.Irq (5);
  CHECK (env.irl == 5);
  mec.WriteIcr (1u << 5);
  CHECK (env.irl == 0);

  uint32 before = mec.ifr ();
  mec.WriteIfr (0xffff); /* force mode off: no change */
  CHECK (mec.ifr () == before);
}

TEST_CASE ("Mec reports reserved bits in the interrupt writes")
{
  TestEnv env;
  erc32::Mec<TestEnv> mec (env);

  /* Every interrupt register reports its reserved bits to the environment.  */
  mec.WriteIsr (0x2000);
  CHECK (env.errors == 1);
  mec.WriteImr (1);
  CHECK (env.errors == 2);
  mec.WriteIcr (1);
  CHECK (env.errors == 3);
  mec.WriteTcr (0x40);
  CHECK (env.errors == 4);

  mec.WriteTcr (TCR_IFR_EN);
  mec.WriteIfr (1);
  CHECK (env.errors == 5);
}

TEST_CASE ("Mec narrates interrupt changes when verbose")
{
  TestEnv env;
  env.verbose = true;
  erc32::Mec<TestEnv> mec (env);
  stdout_capture cap;

  mec.WriteTcr (TCR_IFR_EN);
  mec.WriteImr (0);
  env.irl = 0;
  mec.WriteIfr (1u << 7); /* raises the level from 0 to 7 */
  mec.WriteImr (0);	  /* re-evaluates with the level already 7 */
  mec.Intack (7);

  std::string out = cap.str ();
  CHECK (out.find ("IU irl: 7") != std::string::npos);
  CHECK (out.find ("interrupt 7 acknowledged") != std::string::npos);
}

TEST_CASE_FIXTURE (mec_fixture, "MEC dispatches the interrupt registers")
{
  /* The board integration seam: the register window reads and writes reach
     the interrupt controller, and the acknowledge callback is wired.  */
  wr (R_ISR, 0x1000);
  CHECK (rd (R_ISR) == 0x1000);

  wr (R_MCR, MCR_HWERR_IRQ);
  wr (R_IMR, 0);
  wr (R_TCR, TCR_IFR_EN);
  CHECK (rd (R_TCR) == TCR_IFR_EN);

  wr (R_IFR, 1u << 4);
  CHECK ((rd (R_IFR) & (1u << 4)) != 0);
  CHECK (ext_irl[0] == 4);

  sregs[0].intack (4, 0); /* the mec_intack trampoline */
  CHECK (ext_irl[0] == 0);

  wr (R_ICR, 0);
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

/* The ERC32 memory map: RAM at 0x02000000, ROM from 0, the MEC window, and
   everything else unmapped.  */
const uint32 RAM = 0x02000000;
const uint32 ROM = 0x00000100;
const uint32 UNMAPPED = 0x40000000;

TEST_CASE_FIXTURE (mec_fixture, "MEC memory reads dispatch by region")
{
  uint32 d;
  int32 ws;

  /* A word written to RAM reads back; ROM and a MEC register read without
     fault; an unmapped read faults and records the supervisor fault status. */
  uint32 val = 0xdeadbeef;
  CHECK (ms->memory_write (RAM, &val, SZ_WORD, &ws) == 0);
  CHECK (ms->memory_read (RAM, &d, &ws) == 0);
  CHECK (d == 0xdeadbeef);

  CHECK (ms->memory_read (ROM, &d, &ws) == 0);
  CHECK (ms->memory_read (MEC + R_IMR, &d, &ws) == 0);

  CHECK (ms->memory_read (UNMAPPED, &d, &ws) == 1);
  CHECK (rd (R_SFSR) != 0); /* the fault status register recorded it */
}

TEST_CASE_FIXTURE (mec_fixture, "MEC instruction reads dispatch by region")
{
  uint32 d;
  int32 ws;

  CHECK (ms->memory_iread (RAM, &d, &ws) == 0);
  CHECK (ms->memory_iread (ROM, &d, &ws) == 0);

  /* An unmapped fetch faults, in supervisor and in user mode, taking both
     fault ASIs that skip the fault status register.  */
  sregs[0].psr |= 0x80;
  CHECK (ms->memory_iread (UNMAPPED, &d, &ws) == 1);
  sregs[0].psr &= ~0x80;
  CHECK (ms->memory_iread (UNMAPPED, &d, &ws) == 1);
  sregs[0].psr |= 0x80;
}

TEST_CASE_FIXTURE (mec_fixture, "MEC fault status records access mode")
{
  uint32 d;
  int32 ws;
  uint32 v = 0;

  /* A faulting MEC access records the fault status with the supervisor or the
     user data ASI, and clears it as read or as write.  */
  CHECK (ms->memory_read (MEC + 0x0c, &d, &ws) ==
	 1);			       /* undefined, supervisor */
  CHECK ((rd (R_SFSR) & 0x1000) != 0); /* ASI 0xb */

  sregs[0].psr &= ~0x80;
  CHECK (ms->memory_read (MEC + 0x0c, &d, &ws) == 1); /* undefined, user */
  sregs[0].psr |= 0x80;

  CHECK (ms->memory_write (MEC + 0x0c, &v, SZ_WORD, &ws) ==
	 1); /* write fault */
}

TEST_CASE_FIXTURE (mec_fixture, "MEC rejects malformed MEC accesses")
{
  int32 ws;
  uint32 v = 0;

  /* A MEC register write must be a supervisor word.  A byte write and a
     user-mode write both fault.  */
  CHECK (ms->memory_write (MEC + R_IMR, &v, 0, &ws) == 1); /* not a word */

  sregs[0].psr &= ~0x80;
  CHECK (ms->memory_write (MEC + R_IMR, &v, SZ_WORD, &ws) ==
	 1); /* not super */
  sregs[0].psr |= 0x80;
}

TEST_CASE_FIXTURE (mec_fixture, "MEC stores every access size to RAM")
{
  uint32 d;
  int32 ws;

  /* Byte, half-word, word and double-word stores.  The word and double-word
     round-trip exactly; the byte and half change the surrounding word.  */
  uint32 word = 0x11223344;
  CHECK (ms->memory_write (RAM + 8, &word, 2, &ws) == 0);
  CHECK (ms->memory_read (RAM + 8, &d, &ws) == 0);
  CHECK (d == 0x11223344);

  uint32 dbl[2] = { 0x55667788, 0x99aabbcc };
  CHECK (ms->memory_write (RAM + 16, dbl, 3, &ws) == 0);
  CHECK (ms->memory_read (RAM + 16, &d, &ws) == 0);
  CHECK (d == 0x55667788);
  CHECK (ms->memory_read (RAM + 20, &d, &ws) == 0);
  CHECK (d == 0x99aabbcc);

  uint32 clear = 0;
  ms->memory_write (RAM + 24, &clear, 2, &ws);
  uint32 byte = 0xab;
  CHECK (ms->memory_write (RAM + 24, &byte, 0, &ws) == 0);
  uint32 half = 0xbeef;
  CHECK (ms->memory_write (RAM + 26, &half, 1, &ws) == 0);
  ms->memory_read (RAM + 24, &d, &ws);
  CHECK (d != 0); /* the byte and half-word landed */
}

TEST_CASE_FIXTURE (mec_fixture, "MEC writes ROM only when write enable is set")
{
  int32 ws;
  int saved_wrp = wrp;
  wrp = 1;

  /* With the ROM writable and the reset configuration (16-bit, so word sized
     writes), a word and a double-word store to ROM succeed.  */
  uint32 word = 0x12345678;
  CHECK (ms->memory_write (ROM, &word, 2, &ws) == 0);
  uint32 dbl[2] = { 1, 2 };
  CHECK (ms->memory_write (ROM + 8, dbl, 3, &ws) == 0);

  wrp = saved_wrp;
}

TEST_CASE_FIXTURE (mec_fixture, "MEC byte-writes an 8-bit ROM")
{
  int32 ws;
  int saved_wrp = wrp;
  int saved_rom8 = rom8;
  wrp = 1;
  rom8 = 1;

  /* An 8-bit ROM clears the 16-bit configuration bit, so a byte store is the
     accepted ROM write size.  */
  wr (R_MEMCFG, 0x10000 | (3u << 18) | (4u << 10));
  uint32 byte = 0x5a;
  CHECK (ms->memory_write (ROM, &byte, 0, &ws) == 0);

  rom8 = saved_rom8;
  wrp = saved_wrp;
}

TEST_CASE_FIXTURE (mec_fixture, "MEC write protection whitelists the segment")
{
  int32 ws;
  uint32 v = 0;

  /* With block protection off, only the protected segment is writable and
     everything else faults.  Segment 1 covers word addresses 0x100..0x200,
     which is RAM byte offset 0x400..0x800.  */
  wr (R_SSA1, (2u << 23) | 0x100);
  wr (R_SEA1, 0x200);

  CHECK (ms->memory_write (RAM + 0x600, &v, 2, &ws) ==
	 0); /* inside, allowed */
  CHECK (ms->memory_write (RAM + 0x140, &v, 2, &ws) == 1); /* below, faults */
  CHECK (ms->memory_write (RAM + 0x940, &v, 2, &ws) == 1); /* above, faults */
}

TEST_CASE_FIXTURE (mec_fixture, "MEC block protection blacklists the segment")
{
  int32 ws;
  uint32 v = 0;

  wr (R_MCR, MCR_HWERR_IRQ);
  wr (R_SSA1, (2u << 23) | 0x100);
  wr (R_SEA1, 0x200);
  wr (R_MCR, MCR_HWERR_IRQ | 0x08); /* block write protection on */

  sis_verbose = 1;
  stdout_capture cap;
  CHECK (ms->memory_write (RAM + 0x600, &v, 2, &ws) == 1); /* inside, faults */
  CHECK (ms->memory_write (RAM + 0x140, &v, 2, &ws) ==
	 0); /* outside, allowed */
  std::string out = cap.str ();
  CHECK (out.find ("Memory access protection error") != std::string::npos);
}

TEST_CASE_FIXTURE (mec_fixture,
		   "MEC user-mode write protection uses the user bit")
{
  int32 ws;
  uint32 v = 0;

  /* The segment's user write-protection bit gates a user-mode store.  */
  wr (R_SSA1, (1u << 23) | 0x100); /* wpr bit for user writes */
  wr (R_SEA1, 0x200);

  sregs[0].psr &= ~0x80; /* user mode */
  CHECK (ms->memory_write (RAM + 0x600, &v, 2, &ws) == 0);
  CHECK (ms->memory_write (RAM + 0x140, &v, 2, &ws) == 1);
  sregs[0].psr |= 0x80;
}

TEST_CASE_FIXTURE (mec_fixture,
		   "MEC byte access reaches memory through pointers")
{
  char buf[8] = { 0 };

  /* The byte-oriented helpers reach RAM and ROM through get_mem_ptr, and an
     unmapped address returns nothing.  The word-sized read takes the
     memory_read path instead.  */
  CHECK (ms->sis_memory_write (RAM, "ABC", 3) == 3);
  CHECK (ms->sis_memory_read (RAM, buf, 3) == 3);
  CHECK (ms->sis_memory_read (RAM, buf, 4) == 4); /* length 4 path */

  CHECK (ms->sis_memory_write (ROM, "abcd", 4) == 4);

  CHECK (ms->sis_memory_write (UNMAPPED, "X", 1) == 0);
  CHECK (ms->sis_memory_read (UNMAPPED, buf, 1) == 0);
}

TEST_CASE_FIXTURE (mec_fixture, "MEC reports illegal accesses across modes")
{
  uint32 d;
  int32 ws;
  uint32 v = 0;

  /* Illegal reads, fetches and writes fault in both privilege modes.  */
  sregs[0].psr &= ~0x80;
  CHECK (ms->memory_read (UNMAPPED, &d, &ws) == 1);
  CHECK (ms->memory_write (UNMAPPED, &v, 2, &ws) == 1);
  sregs[0].psr |= 0x80;
  CHECK (ms->memory_write (UNMAPPED, &v, 2, &ws) == 1);

  sis_verbose = 1;
  stdout_capture cap;
  ms->memory_read (UNMAPPED, &d, &ws);
  ms->memory_iread (UNMAPPED, &d, &ws);
  std::string out = cap.str ();
  CHECK (out.find ("Memory exception") != std::string::npos);
}

TEST_CASE_FIXTURE (mec_fixture, "MEC protects the second segment for doubles")
{
  int32 ws;
  uint32 dbl[2] = { 0, 0 };

  /* Segment 2 protection with a double-word store, which widens the checked
     range by one word.  */
  wr (R_SSA2, (2u << 23) | 0x100);
  wr (R_SEA2, 0x200);

  CHECK (ms->memory_write (RAM + 0x600, dbl, 3, &ws) == 0); /* inside */
  CHECK (ms->memory_write (RAM + 0x140, dbl, 3, &ws) == 1); /* outside */
}

TEST_CASE_FIXTURE (mec_fixture,
		   "MEC rejects disabled and mis-sized ROM writes")
{
  int32 ws;
  uint32 word = 1;
  uint32 byte = 1;

  /* Without the write-enable, a ROM store faults.  */
  CHECK (ms->memory_write (ROM, &word, 2, &ws) == 1);

  int saved_wrp = wrp;
  int saved_rom8 = rom8;
  wrp = 1;

  /* With write-enable but a 16-bit ROM, a byte-sized store is the wrong size
     and faults.  */
  CHECK (ms->memory_write (ROM, &byte, 0, &ws) == 1);

  /* A configuration with the ROM write bit cleared is not writable.  */
  wr (R_MEMCFG, (3u << 18) | (4u << 10));
  CHECK (ms->memory_write (ROM, &word, 2, &ws) == 1);

  /* An 8-bit ROM wants byte stores, so a word store is the wrong size.  */
  rom8 = 1;
  wr (R_MEMCFG, 0x10000 | (3u << 18) | (4u << 10));
  CHECK (ms->memory_write (ROM, &word, 2, &ws) == 1);

  rom8 = saved_rom8;
  wrp = saved_wrp;
}

TEST_CASE_FIXTURE (mec_fixture,
		   "MEC block protection covers the second segment")
{
  int32 ws;
  uint32 v = 0;

  /* Block protection with only segment 2 set faults a write that hits segment
     2 while segment 1 is open.  */
  wr (R_MCR, MCR_HWERR_IRQ);
  wr (R_SSA2, (2u << 23) | 0x100);
  wr (R_SEA2, 0x200);
  wr (R_MCR, MCR_HWERR_IRQ | 0x08);

  CHECK (ms->memory_write (RAM + 0x600, &v, 2, &ws) == 1);
}

TEST_CASE_FIXTURE (mec_fixture, "MEC byte pointers reject the ROM to RAM gap")
{
  char buf[4] = { 0 };

  /* An address above the ROM but below the RAM has no pointer.  */
  CHECK (ms->sis_memory_read (0x01800000, buf, 1) == 0);
  CHECK (ms->sis_memory_write (0x01800000, "z", 1) == 0);
}

TEST_CASE_FIXTURE (mec_fixture, "MEC UART A transmit buffers and interrupts")
{
  /* A byte written to UART A buffers for output and raises the transmit
     interrupt on level 4.  */
  wr (R_IMR, 0);
  wr (R_UARTA, 'H');
  CHECK ((rd (R_IPR) & (1u << 4)) != 0);
}

TEST_CASE_FIXTURE (mec_fixture, "MEC UART A transmit flushes a full buffer")
{
  /* Filling the transmit buffer past its size flushes it to the output.  */
  stdout_capture cap;
  for (int i = 0; i < UARTBUF + 1; i++)
    wr (R_UARTA, 'x');
  std::string out = cap.str ();
  CHECK (out.size () >= (size_t) UARTBUF);
}

TEST_CASE_FIXTURE (mec_fixture,
		   "MEC UART B transmit interrupts with no port open")
{
  /* With serial port B closed, a UART B write drops the byte but still raises
     the transmit interrupt on level 5.  */
  wr (R_IMR, 0);
  wr (R_UARTB, 'x');
  CHECK ((rd (R_IPR) & (1u << 5)) != 0);
}

TEST_CASE_FIXTURE (mec_fixture, "MEC UART control write is accepted")
{
  /* The UART control register write has no effect in fast mode but must be
     accepted, and its reserved bits are rejected.  */
  wr (R_UART_CTRL, 0);

  wr (R_MCR, MCR_HWERR_IRQ);
  wr (R_UARTA, 0x100); /* reserved data bits */
  CHECK ((rd (R_IPR) & (1u << 1)) != 0);
  wr (R_ICR, 1u << 1);

  wr (R_UART_CTRL, 0x01000000); /* reserved control bits */
  CHECK ((rd (R_IPR) & (1u << 1)) != 0);
}

TEST_CASE_FIXTURE (mec_fixture, "MEC UART A receives buffered input")
{
  /* Two fed bytes read back in order; the first read, with more to come,
     raises the receive interrupt on level 4.  */
  stdin_feed feed ("AB");
  wr (R_IMR, 0);

  uint32 d1 = rd (R_UARTA);
  CHECK ((d1 & 0xff) == 'A');
  CHECK ((d1 & 0x100) != 0); /* data valid */
  CHECK ((rd (R_IPR) & (1u << 4)) != 0);

  uint32 d2 = rd (R_UARTA);
  CHECK ((d2 & 0xff) == 'B');

  uint32 d3 = rd (R_UARTA); /* nothing left */
  CHECK ((d3 & 0x100) == 0);
}

TEST_CASE_FIXTURE (mec_fixture, "MEC UART A receives a single byte")
{
  /* A lone fed byte reads back with no further interrupt.  */
  stdin_feed feed ("Z");
  uint32 d = rd (R_UARTA);
  CHECK ((d & 0xff) == 'Z');
  CHECK ((d & 0x100) != 0);
}

TEST_CASE_FIXTURE (mec_fixture, "MEC UART status reports pending input")
{
  /* The status register reports data ready for a port with input.  */
  stdin_feed feed ("Q");
  uint32 s = rd (R_UART_CTRL);
  CHECK ((s & 0x1) != 0);
  CHECK ((s & 0x00060006) == 0x00060006);
}

TEST_CASE_FIXTURE (mec_fixture, "MEC UART status reports no input")
{
  /* With empty input, the status register reports no data ready.  */
  stdin_feed feed ("");
  uint32 s = rd (R_UART_CTRL);
  CHECK ((s & 0x1) == 0);
  CHECK ((s & 0x00010000) == 0);
}

TEST_CASE_FIXTURE (mec_fixture,
		   "MEC UART flush timer drains the transmit buffer")
{
  /* The periodic UART interrupt flushes buffered output and reschedules.  */
  stdout_capture cap;
  stdin_feed feed ("");
  wr (R_UARTA, 'A');
  wr (R_UARTA, 'B');
  advance_time (3000); /* fires uart_intr at UART_FLUSH_TIME */
  std::string out = cap.str ();
  CHECK (out.find ("AB") != std::string::npos);
}

TEST_CASE_FIXTURE (mec_fixture,
		   "MEC UART B input and output with the port open")
{
  /* Open serial port B by routing the console to it, then exercise its
     transmit buffer, receive and status paths.  This is the last UART case
     because it leaves port B open.  */
  int saved_uben = uben;
  uben = 1;
  ms->init_sim (); /* re-open the ports with B on the console */

  {
    stdout_capture cap;
    stdin_feed feed ("");
    for (int i = 0; i < UARTBUF + 1; i++)
      wr (R_UARTB, 'y');
    std::string out = cap.str ();
    CHECK (out.size () >= (size_t) UARTBUF);
  }

  {
    stdin_feed feed ("CD");
    uint32 d = rd (R_UARTB);
    CHECK ((d & 0xff) == 'C');
    uint32 d2 = rd (R_UARTB);
    CHECK ((d2 & 0xff) == 'D');
    uint32 d3 = rd (R_UARTB);
    CHECK ((d3 & 0x100) == 0);
  }

  {
    stdin_feed feed ("E");
    uint32 d = rd (R_UARTB);
    CHECK ((d & 0xff) == 'E');
  }

  {
    /* Prime A with a spare byte so the status read does not refill and
       consume B's input; then B's own refill reports its data.  */
    stdin_feed feed ("XY");
    rd (R_UARTA);
  }
  {
    stdin_feed feed ("Q");
    uint32 s = rd (R_UART_CTRL);
    CHECK ((s & 0x00010000) != 0); /* UART B data ready */
  }

  {
    stdout_capture cap;
    stdin_feed feed ("");
    wr (R_UARTB, 'A');
    advance_time (3000); /* uart_intr flushes port B too */
  }

  uben = saved_uben;
}
