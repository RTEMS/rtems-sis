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

#include "erc32_cfg.h"
#include "erc32_error.h"
#include "erc32_mec.h"
#include "erc32_timer.h"

#include <stdio.h>
#include <string.h>
#include <string>
#include <unistd.h>
#include <vector>

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
const uint32 R_WDOG = 0x060;
const uint32 R_TRAPD = 0x064;
const uint32 R_RTC_COUNTER = 0x080;
const uint32 R_RTC_SCALER = 0x084;
const uint32 R_GPT_COUNTER = 0x088;
const uint32 R_GPT_SCALER = 0x08c;
const uint32 R_TIMER_CTRL = 0x098;
const uint32 R_SFSR = 0x0a0;
const uint32 R_FFAR = 0x0a4;
const uint32 R_ERSR = 0x0b0;
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

/* The two timers as erc32.cc instantiates them.  The manual gives the real
   time clock an 8 bit scaler and the general purpose timer a 16 bit one, and
   puts their control fields at bit 8 and bit 0 of the timer control
   register.  */
constexpr erc32::TimerSpec RTC_SPEC = { .scaler_mask = 0x0ff,
					.level = erc32::kRtcLevel,
					.ctrl_shift = 8,
					.reload_at_zero_reset = true,
					.name = "RTC" };

constexpr erc32::TimerSpec GPT_SPEC = { .scaler_mask = 0x0ffff,
					.level = erc32::kGptLevel,
					.ctrl_shift = 0,
					.reload_at_zero_reset = false,
					.name = "GPT" };

/* Store-size encoding for a word and the test-mode / IFR-enable bit of the
   MEC test control register.  */
const int32 SZ_WORD = 2;
const uint32 TCR_IFR_EN = 0x80000;
const uint32 TCR_ERROR_WRITE_EN = 0x100000;

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
  std::string log;

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
  void
  Log (const char *msg)
  {
    log += msg;
  }
};

/* A test environment for the timer templates: it owns the simulated time,
   the ticks that were scheduled, the interrupts that were raised and whether
   the watchdog reset the processor, so a case drives erc32::Timer and
   erc32::Watchdog with no simulator globals.  */
struct TimerTestEnv
{
  bool verbose = false;
  uint64 time = 0;
  std::string log;
  std::vector<int> irqs;
  std::vector<uint64> ticks;
  int resets = 0;

  bool
  Verbose ()
  {
    return verbose;
  }
  uint64
  Now ()
  {
    return time;
  }
  void
  Irq (int level)
  {
    irqs.push_back (level);
  }
  void
  ScheduleTick (uint64 delta)
  {
    ticks.push_back (delta);
  }
  void
  Log (const char *msg)
  {
    log += msg;
  }
  void
  WatchdogReset ()
  {
    ++resets;
  }
};

/* A test environment for the error handler template: it owns the verbosity,
   the interrupts raised, whether the processor was reset or halted, and
   whether fault injection is armed, so a case drives erc32::ErrorHandler
   with no simulator globals.  */
struct ErrorTestEnv
{
  bool verbose = false;
  bool error_write_enabled = false;
  std::string log;
  std::vector<int> irqs;
  int resets = 0;
  int halts = 0;

  bool
  Verbose ()
  {
    return verbose;
  }
  void
  Log (const char *msg)
  {
    log += msg;
  }
  void
  Irq (int level)
  {
    irqs.push_back (level);
  }
  void
  SysReset ()
  {
    ++resets;
  }
  void
  SysHalt ()
  {
    ++halts;
  }
  bool
  ErrorWriteEnabled ()
  {
    return error_write_enabled;
  }
};

/* A test environment for the configuration template: it owns the verbosity,
   the PROM width of the board, the error count, and what the configuration
   asked the rest of the machine to do, so a case drives erc32::Config with no
   simulator globals.  */
struct ConfigTestEnv
{
  bool verbose = false;
  bool rom8 = false;
  std::string log;
  int errors = 0;
  uint32 error_mask = 0;
  int resets = 0;
  int power_downs = 0;
  int power_down_timings = 0;

  bool
  Verbose ()
  {
    return verbose;
  }
  void
  Log (const char *msg)
  {
    log += msg;
  }
  void
  ReportError ()
  {
    ++errors;
  }
  bool
  Rom8 ()
  {
    return rom8;
  }
  void
  SetErrorMask (uint32 mcr)
  {
    error_mask = mcr;
  }
  void
  SoftwareReset ()
  {
    ++resets;
  }
  void
  EnterPowerDown ()
  {
    ++power_downs;
  }
  void
  StartPowerDownTiming ()
  {
    ++power_down_timings;
  }
};

/* The RAM window of the ERC32 board, as erc32.cc hands it over.  */
constexpr erc32::MemoryGeometry TEST_GEOMETRY = { .ram_start = 0x02000000,
						  .ram_end = 0x03000000,
						  .ram_mask = 0x00ffffff };

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

  mec.WriteTcr (TCR_IFR_EN);
  mec.WriteImr (0);
  env.irl = 0;
  mec.WriteIfr (1u << 7); /* raises the level from 0 to 7 */
  mec.WriteImr (0);	  /* re-evaluates with the level already 7 */
  mec.Intack (7);

  /* The narration reaches the injected log, not the real stdout.  */
  CHECK (env.log.find ("IU irl: 7") != std::string::npos);
  CHECK (env.log.find ("interrupt 7 acknowledged") != std::string::npos);
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

/* The timers are tested through erc32::Timer and erc32::Watchdog on a
   TimerTestEnv, with no simulator globals: the environment owns the
   simulated time, the ticks that were scheduled and the interrupts that were
   raised.  Register layouts, reset values and interrupt levels come from the
   TSC693E manual, sections 3.13 and 3.14 and the register descriptions for
   01F8 0060 through 01F8 0098.  */

TEST_CASE ("Timer resets to the values the manual documents")
{
  TimerTestEnv env;
  erc32::Timer<TimerTestEnv> rtc (env, RTC_SPEC);
  erc32::Timer<TimerTestEnv> gpt (env, GPT_SPEC);

  /* Counter and programmed counter reset to FFFFFFFF, and the timer is not
     running: the manual states it must be programmed after reset.  */
  CHECK (rtc.counter () == 0xffffffffu);
  CHECK (rtc.reload () == 0xffffffffu);
  CHECK (rtc.enabled () == false);
  CHECK (gpt.counter () == 0xffffffffu);
  CHECK (gpt.reload () == 0xffffffffu);
  CHECK (gpt.enabled () == false);

  /* The real time clock has an 8 bit scaler resetting to FF, the general
     purpose timer a 16 bit scaler resetting to FFFF.  */
  CHECK (rtc.scaler () == 0xffu);
  CHECK (gpt.scaler () == 0xffffu);

  /* RTCCR, bit 8 of the timer control register, is the one control bit whose
     reset value is one; GCR, bit 0, resets to zero.  */
  CHECK (rtc.reload_at_zero () == true);
  CHECK (gpt.reload_at_zero () == false);

  /* Nothing is scheduled and nothing is requested by a reset timer.  */
  CHECK (env.ticks.empty ());
  CHECK (env.irqs.empty ());
}

TEST_CASE ("Timer truncates a scaler to its programmed width")
{
  TimerTestEnv env;
  erc32::Timer<TimerTestEnv> rtc (env, RTC_SPEC);
  erc32::Timer<TimerTestEnv> gpt (env, GPT_SPEC);

  /* Bits above the scaler width are not part of the register.  */
  rtc.SetScaler (0x1a5);
  CHECK (rtc.scaler () == 0xa5);

  gpt.SetScaler (0x1beef);
  CHECK (gpt.scaler () == 0xbeef);
}

TEST_CASE ("Timer control loads the counter from its programmed value")
{
  TimerTestEnv env;
  erc32::Timer<TimerTestEnv> rtc (env, RTC_SPEC);

  rtc.SetReload (0x1234);
  CHECK (rtc.reload () == 0x1234);
  CHECK (rtc.counter () == 0xffffffffu); /* not loaded yet */

  rtc.WriteControl (TC_RTC_LOAD);
  CHECK (rtc.counter () == 0x1234);
  CHECK (rtc.enabled () == false); /* loading does not start it */
  CHECK (env.ticks.empty ());
}

TEST_CASE ("Timer control reads its own field of the control register")
{
  TimerTestEnv env;
  erc32::Timer<TimerTestEnv> rtc (env, RTC_SPEC);
  erc32::Timer<TimerTestEnv> gpt (env, GPT_SPEC);

  /* One register carries both timers, so the general purpose timer's bits
     must leave the real time clock alone and the other way round.  */
  rtc.SetReload (0x11);
  gpt.SetReload (0x22);

  rtc.WriteControl (TC_GPT_LOAD);
  gpt.WriteControl (TC_GPT_LOAD);
  CHECK (rtc.counter () == 0xffffffffu);
  CHECK (gpt.counter () == 0x22);

  rtc.WriteControl (TC_RTC_LOAD);
  gpt.WriteControl (TC_RTC_LOAD);
  CHECK (rtc.counter () == 0x11);
  CHECK (gpt.counter () == 0x22); /* unchanged by the RTC's load */

  /* The reload bit follows the same split.  */
  rtc.WriteControl (TC_RTC_RELOAD);
  gpt.WriteControl (TC_RTC_RELOAD);
  CHECK (rtc.reload_at_zero () == true);
  CHECK (gpt.reload_at_zero () == false);
}

TEST_CASE ("Timer start schedules one scaler period ahead")
{
  TimerTestEnv env;
  erc32::Timer<TimerTestEnv> gpt (env, GPT_SPEC);

  /* The counter is decremented once per scaler period, so the timeout is
     counter times scaler plus one clocks.  */
  env.time = 500;
  gpt.SetScaler (49);
  gpt.WriteControl (TC_GPT_SCALER_EN);

  CHECK (gpt.enabled () == true);
  REQUIRE (env.ticks.size () == 1);
  CHECK (env.ticks[0] == 50);

  /* Enabling an already running timer does not restart it.  */
  gpt.WriteControl (TC_GPT_SCALER_EN);
  CHECK (env.ticks.size () == 1);
}

TEST_CASE ("Timer scaler reads down while running")
{
  TimerTestEnv env;
  erc32::Timer<TimerTestEnv> gpt (env, GPT_SPEC);

  env.time = 100;
  gpt.SetScaler (50);
  gpt.WriteControl (TC_GPT_SCALER_EN);

  /* A running scaler reads its programmed value less the elapsed time.  */
  CHECK (gpt.ScalerRead () == 50);
  env.time = 110;
  CHECK (gpt.ScalerRead () == 40);
  env.time = 150;
  CHECK (gpt.ScalerRead () == 0);
}

TEST_CASE ("Timer scaler reads its programmed value while stopped")
{
  TimerTestEnv env;
  erc32::Timer<TimerTestEnv> rtc (env, RTC_SPEC);

  env.time = 900;
  rtc.SetScaler (0x20);
  CHECK (rtc.ScalerRead () == 0x20);
}

TEST_CASE ("Timer tick counts a non-zero counter down and re-arms")
{
  TimerTestEnv env;
  erc32::Timer<TimerTestEnv> rtc (env, RTC_SPEC);

  rtc.SetScaler (7);
  rtc.SetReload (5);
  rtc.WriteControl (TC_RTC_LOAD | TC_RTC_SCALER_EN);
  REQUIRE (env.ticks.size () == 1);

  env.time = 8;
  rtc.Tick ();

  CHECK (rtc.counter () == 4);
  CHECK (rtc.enabled () == true);
  REQUIRE (env.ticks.size () == 2);
  CHECK (env.ticks[1] == 8); /* one scaler period again */
  CHECK (env.irqs.empty ()); /* no interrupt above zero */
}

TEST_CASE ("Timer tick at zero interrupts at the level of the timer")
{
  TimerTestEnv env;
  erc32::Timer<TimerTestEnv> rtc (env, RTC_SPEC);
  erc32::Timer<TimerTestEnv> gpt (env, GPT_SPEC);

  /* Table 5 of the manual assigns the real time clock interrupt level 13 and
     the general purpose timer level 12.  */
  rtc.SetScaler (0);
  rtc.SetReload (0);
  rtc.WriteControl (TC_RTC_LOAD | TC_RTC_RELOAD | TC_RTC_SCALER_EN);
  rtc.Tick ();

  gpt.SetScaler (0);
  gpt.SetReload (0);
  gpt.WriteControl (TC_GPT_LOAD | TC_GPT_RELOAD | TC_GPT_SCALER_EN);
  gpt.Tick ();

  REQUIRE (env.irqs.size () == 2);
  CHECK (env.irqs[0] == 13);
  CHECK (env.irqs[1] == 12);
}

TEST_CASE ("Timer reloads at zero and keeps running when told to")
{
  TimerTestEnv env;
  erc32::Timer<TimerTestEnv> gpt (env, GPT_SPEC);

  gpt.SetScaler (3);
  gpt.SetReload (9);
  gpt.WriteControl (TC_GPT_LOAD | TC_GPT_RELOAD | TC_GPT_SCALER_EN);
  gpt.SetReload (7); /* reprogrammed while running */

  /* Walk the counter down to zero and one tick past it.  */
  for (int i = 0; i < 9; ++i)
    gpt.Tick ();
  CHECK (gpt.counter () == 0);
  CHECK (env.irqs.empty ());

  gpt.Tick ();
  CHECK (env.irqs.size () == 1);
  CHECK (gpt.counter () == 7); /* reloaded from the programmed value */
  CHECK (gpt.enabled () == true);
}

TEST_CASE ("Timer stops at zero when reload is not set")
{
  TimerTestEnv env;
  erc32::Timer<TimerTestEnv> gpt (env, GPT_SPEC);

  gpt.SetScaler (2);
  gpt.SetReload (0);
  gpt.WriteControl (TC_GPT_LOAD | TC_GPT_SCALER_EN); /* single shot */
  size_t armed = env.ticks.size ();

  gpt.Tick ();

  CHECK (env.irqs.size () == 1);
  CHECK (gpt.enabled () == false);
  CHECK (env.ticks.size () == armed); /* not re-armed */
  CHECK (gpt.ScalerRead () == 2);     /* stopped, so the static value */
}

TEST_CASE ("Timer narrates its start and stop when verbose")
{
  TimerTestEnv env;
  env.verbose = true;
  erc32::Timer<TimerTestEnv> rtc (env, RTC_SPEC);

  rtc.SetScaler (9);
  rtc.SetReload (0);
  rtc.WriteControl (TC_RTC_LOAD | TC_RTC_SCALER_EN);
  CHECK (env.log == "RTC started (period 10)\n\r");

  rtc.Tick (); /* at zero without reload, so it stops */
  CHECK (env.log == "RTC started (period 10)\n\rRTC stopped\n\r");
}

/* The watch dog, from section 3.14 and Figure 7 of the manual.  */

TEST_CASE ("Watchdog resets to the values the manual documents")
{
  TimerTestEnv env;
  erc32::Watchdog<TimerTestEnv> wdog (env);

  /* The program register resets with every field at its maximum: counter
     FFFF, scaler FF, reset counter FF.  */
  CHECK (wdog.counter () == 0xffffu);
  CHECK (wdog.scaler () == 0xffu);
  CHECK (wdog.rst_delay () == 0xffu);
  CHECK (wdog.state () == erc32::WatchdogState::Init);
}

TEST_CASE ("Watchdog start schedules one scaler period and narrates")
{
  TimerTestEnv env;
  env.verbose = true;
  erc32::Watchdog<TimerTestEnv> wdog (env);

  wdog.Start ();

  REQUIRE (env.ticks.size () == 1);
  CHECK (env.ticks[0] == 256); /* scaler plus one */
  CHECK (env.log == "Watchdog started, scaler = 255, counter = 65535\n");
}

TEST_CASE ("Watchdog program register decodes its three fields")
{
  TimerTestEnv env;
  erc32::Watchdog<TimerTestEnv> wdog (env);

  /* Counter in bits 15-0, scaler in bits 23-16, reset counter in 31-24.  */
  wdog.WriteProgram (0xa5c31234u);

  CHECK (wdog.counter () == 0x1234u);
  CHECK (wdog.scaler () == 0xc3u);
  CHECK (wdog.rst_delay () == 0xa5u);
  CHECK (wdog.state () == erc32::WatchdogState::Enabled);
}

TEST_CASE ("Watchdog trap door disables it from the reset state")
{
  TimerTestEnv env;
  env.verbose = true;
  erc32::Watchdog<TimerTestEnv> wdog (env);

  wdog.WriteTrapDoor ();
  CHECK (wdog.state () == erc32::WatchdogState::Disabled);
  CHECK (env.log == "Watchdog disabled\n");
}

TEST_CASE ("Watchdog cannot be disabled once it has been programmed")
{
  TimerTestEnv env;
  erc32::Watchdog<TimerTestEnv> wdog (env);

  wdog.WriteProgram (0x00010001u);
  REQUIRE (wdog.state () == erc32::WatchdogState::Enabled);

  wdog.WriteTrapDoor ();
  CHECK (wdog.state () == erc32::WatchdogState::Enabled);
}

TEST_CASE ("Watchdog cannot be disabled once it has elapsed")
{
  TimerTestEnv env;
  erc32::Watchdog<TimerTestEnv> wdog (env);

  /* The trap door window runs from reset until the watchdog elapses, so a
     timeout closes it even though the program register was never written.
     Figure 7 gives the timeout state no trap door transition.

     Walk the reset counter of FFFF all the way down, so that the watchdog
     reaches its timeout having only ever been in the reset state.  */
  REQUIRE (wdog.state () == erc32::WatchdogState::Init);
  for (uint32 i = 0; i < 0xffffu; ++i)
    wdog.Tick ();
  REQUIRE (wdog.counter () == 0);
  REQUIRE (wdog.state () == erc32::WatchdogState::Init); /* still open */
  CHECK (env.irqs.empty ());

  wdog.Tick (); /* the timeout */
  REQUIRE (env.irqs.size () == 1);
  CHECK (env.irqs[0] == 15);

  wdog.WriteTrapDoor ();
  CHECK (wdog.state () == erc32::WatchdogState::Enabled);
}

TEST_CASE ("Watchdog disabled stops at its next tick")
{
  TimerTestEnv env;
  erc32::Watchdog<TimerTestEnv> wdog (env);

  wdog.WriteTrapDoor ();
  wdog.Tick ();

  CHECK (wdog.state () == erc32::WatchdogState::Stopped);
  CHECK (env.ticks.empty ()); /* not re-armed */
  CHECK (env.irqs.empty ());

  /* Programming it again after it stopped starts it counting.  */
  wdog.WriteProgram (0x00020003u);
  CHECK (wdog.state () == erc32::WatchdogState::Enabled);
  REQUIRE (env.ticks.size () == 1);
  CHECK (env.ticks[0] == 3); /* the new scaler plus one */
}

TEST_CASE ("Watchdog counts down and re-arms each scaler period")
{
  TimerTestEnv env;
  erc32::Watchdog<TimerTestEnv> wdog (env);

  wdog.WriteProgram (0x00040002u); /* reset delay 0, scaler 4, counter 2 */

  wdog.Tick ();
  CHECK (wdog.counter () == 1);
  wdog.Tick ();
  CHECK (wdog.counter () == 0);

  REQUIRE (env.ticks.size () == 2);
  CHECK (env.ticks[0] == 5);
  CHECK (env.ticks[1] == 5);
  CHECK (env.irqs.empty ());
}

TEST_CASE ("Watchdog timeout interrupts and starts the reset delay")
{
  TimerTestEnv env;
  erc32::Watchdog<TimerTestEnv> wdog (env);

  /* Reset counter 7, scaler 0, counter 0: the next tick is the timeout.  */
  wdog.WriteProgram (0x07000000u);
  wdog.Tick ();

  /* Table 5 assigns the watch dog time-out interrupt level 15.  */
  REQUIRE (env.irqs.size () == 1);
  CHECK (env.irqs[0] == 15);

  /* The counter restarts from the reset counter, and the watchdog keeps
     running.  */
  CHECK (wdog.counter () == 7);
  CHECK (env.ticks.size () == 1);
}

TEST_CASE ("Watchdog resets the processor if the timeout is not acknowledged")
{
  TimerTestEnv env;
  erc32::Watchdog<TimerTestEnv> wdog (env);

  wdog.WriteProgram (0); /* every field zero, so each tick is a timeout */
  wdog.Tick ();		 /* timeout: interrupt, reset delay starts */
  REQUIRE (env.irqs.size () == 1);
  CHECK (env.resets == 0);

  wdog.Tick (); /* the reset delay elapses unacknowledged */

  CHECK (env.resets == 1);
  CHECK (env.log == "Watchdog reset!\n");
  CHECK (env.ticks.size () == 1); /* the reset ends the counting */
}

TEST_CASE ("Watchdog acknowledge before the reset delay restarts it")
{
  TimerTestEnv env;
  erc32::Watchdog<TimerTestEnv> wdog (env);

  wdog.WriteProgram (0x01000000u); /* reset delay 1, scaler 0, counter 0 */
  wdog.Tick ();			   /* timeout */
  REQUIRE (env.irqs.size () == 1);

  /* The manual: acknowledging with a new value before the reset timeout
     elapses restarts the counting instead of resetting.  */
  wdog.WriteProgram (0x00000005u);
  wdog.Tick ();
  wdog.Tick ();

  CHECK (env.resets == 0);
  CHECK (wdog.counter () == 3);
}

/* One case through the board's register dispatch, so that the MEC register
   window arcs in erc32.cc are covered too.  The logic itself is covered by
   the isolated cases above.  */

TEST_CASE_FIXTURE (mec_fixture, "MEC dispatches the timer registers")
{
  /* Programmed values read back through the counter and scaler offsets.  */
  wr (R_RTC_COUNTER, 0x1234); /* the counter offset is the reload on write */
  wr (R_TIMER_CTRL, TC_RTC_LOAD);
  CHECK (rd (R_RTC_COUNTER) == 0x1234);

  wr (R_GPT_COUNTER, 0x5678);
  wr (R_TIMER_CTRL, TC_GPT_LOAD);
  CHECK (rd (R_GPT_COUNTER) == 0x5678);

  /* A running scaler reads down through the board as well.  */
  ebase.simtime = 100;
  wr (R_GPT_SCALER, 50);
  wr (R_TIMER_CTRL, TC_GPT_SCALER_EN);
  ebase.simtime = 110;
  CHECK (rd (R_GPT_SCALER) == 40);

  ebase.simtime = 200;
  wr (R_RTC_SCALER, 100);
  wr (R_TIMER_CTRL, TC_RTC_SCALER_EN);
  ebase.simtime = 230;
  CHECK (rd (R_RTC_SCALER) == 70);
}

TEST_CASE_FIXTURE (mec_fixture, "MEC ticks the timers from the event queue")
{
  /* The board re-enters the timers through the event queue, so a scheduled
     tick must reach them and post the interrupt.  */
  wr (R_RTC_SCALER, 0);
  wr (R_RTC_COUNTER, 0);
  wr (R_TIMER_CTRL, TC_RTC_LOAD | TC_RTC_RELOAD | TC_RTC_SCALER_EN);

  wr (R_GPT_SCALER, 0);
  wr (R_GPT_COUNTER, 0);
  wr (R_TIMER_CTRL, TC_GPT_LOAD | TC_GPT_RELOAD | TC_GPT_SCALER_EN);

  advance_time (1);

  CHECK ((rd (R_IPR) & (1u << 13)) != 0);
  CHECK ((rd (R_IPR) & (1u << 12)) != 0);
}

TEST_CASE_FIXTURE (mec_fixture, "MEC ticks the watchdog from the event queue")
{
  /* The watchdog is started by the board reset, so it is already armed one
     scaler period ahead.  Disabling it through the trap door stops it at that
     tick, and programming it again restarts it.  */
  wr (R_TRAPD, 0);
  advance_time (256);

  wr (R_WDOG, 0x00000001u); /* restart with scaler 0, counter 1 */
  advance_time (257);
  CHECK ((rd (R_IPR) & (1u << 15)) == 0); /* counted down, not yet timed out */

  advance_time (258);
  CHECK ((rd (R_IPR) & (1u << 15)) != 0); /* timed out at level 15 */
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

/* The error handler, driven in isolation.  The expectations come from the
   TSC693E manual, section 3.17 and the register descriptions for 01F8 00A0,
   01F8 00A4 and 01F8 00B0: the MEC control register carries one mask bit and
   one reset-or-halt bit per error source, and an unmasked error halts by
   default.  */

/* The configuration registers, driven in isolation.  The expectations come
   from the TSC693E manual: the reset values of each register, the size and
   waitstate encodings, and the two modes the control register gates.  */

TEST_CASE ("Config resets to the values the manual documents")
{
  ConfigTestEnv env;
  erc32::Config<ConfigTestEnv> cfg (env, TEST_GEOMETRY);

  /* The control register resets with the bus timeout, the watchdog
     prescaler, DMA, the DMA session timeout, the UART parity and clock and a
     UART scaler of one.  */
  CHECK (cfg.mcr () == 0x01b50014);

  /* The memory configuration register resets with PROM writes enabled and
     both size fields at their minimum, which is 256 Kbyte of RAM and 128
     Kbyte of PROM.  The PROM width bit follows the board.  */
  CHECK (cfg.memcfg () == (erc32::kMemcfgPromWrite | erc32::kMemcfgProm40Bit));
  CHECK (cfg.ram_size () == 256 * 1024);
  CHECK (cfg.rom_size () == 128 * 1024);
  CHECK (cfg.prom_write ());
  CHECK (cfg.prom_40bit ());

  /* The waitstate register resets to all ones, the maximum of each field.  */
  CHECK (cfg.ram_read_ws () == 3);
  CHECK (cfg.ram_write_ws () == 3);
  CHECK (cfg.rom_read_ws () == 14);
  CHECK (cfg.rom_write_ws () == 14);

  /* No I/O unit enabled and no segment protected.  */
  CHECK (cfg.iocr () == 0);
  CHECK (cfg.access_protect () == false);
  CHECK (cfg.block_protect () == false);
  for (int i = 0; i < erc32::kSegments; i++)
    {
      CHECK (cfg.seg_base (i) == 0);
      CHECK (cfg.seg_end (i) == 0);
      CHECK (cfg.seg_mode (i) == 0);
    }

  /* The RAM window comes from the board, not from a register.  */
  CHECK (cfg.ram_start () == 0x02000000);
  CHECK (cfg.ram_end () == 0x03000000);
  CHECK (cfg.ram_mask () == 0x00ffffff);

  /* The error handler is given the control register.  */
  CHECK (env.error_mask == cfg.mcr ());
}

TEST_CASE ("Config decodes every memory size the fields select")
{
  ConfigTestEnv env;
  erc32::Config<ConfigTestEnv> cfg (env, TEST_GEOMETRY);

  /* The RAM size field starts at 256 Kbyte and doubles, the PROM size field
     starts at 128 Kbyte and doubles.  */
  for (uint32 i = 0; i < 8; i++)
    {
      cfg.WriteMemcfg (i << 10);
      CHECK (cfg.ram_size () == (256u * 1024u << i));

      cfg.WriteMemcfg (i << 18);
      CHECK (cfg.rom_size () == (128u * 1024u << i));
    }
}

TEST_CASE ("Config takes the PROM width from the board")
{
  ConfigTestEnv env;

  /* The PROM width bit is read only and follows the board's PROM8 pin, so a
     write of the opposite value has no effect.  */
  erc32::Config<ConfigTestEnv> wide (env, TEST_GEOMETRY);
  wide.WriteMemcfg (0);
  CHECK (wide.prom_40bit ());

  env.rom8 = true;
  erc32::Config<ConfigTestEnv> narrow (env, TEST_GEOMETRY);
  narrow.WriteMemcfg (erc32::kMemcfgProm40Bit);
  CHECK (narrow.prom_40bit () == false);
}

TEST_CASE ("Config decodes the RAM waitstate fields")
{
  ConfigTestEnv env;
  erc32::Config<ConfigTestEnv> cfg (env, TEST_GEOMETRY);

  /* Two bits each, counting waitstates directly.  */
  cfg.WriteWcr (0x2 | (0x1 << 2));
  CHECK (cfg.ram_read_ws () == 2);
  CHECK (cfg.ram_write_ws () == 1);
}

TEST_CASE ("Config decodes no PROM waitstates at zero and at one")
{
  ConfigTestEnv env;
  erc32::Config<ConfigTestEnv> cfg (env, TEST_GEOMETRY);

  /* The PROM fields encode no waitstates twice, so the count is one below the
     field from two upwards.  */
  const uint32 expect[16] = { 0, 0, 1, 2,  3,  4,  5,  6,
			      7, 8, 9, 10, 11, 12, 13, 14 };

  for (uint32 f = 0; f < 16; f++)
    {
      cfg.WriteWcr ((f << 4) | (f << 8));
      CHECK (cfg.rom_read_ws () == expect[f]);
      CHECK (cfg.rom_write_ws () == expect[f]);
    }
}

TEST_CASE ("Config stretches the PROM read for an 8-bit PROM")
{
  ConfigTestEnv env;
  env.rom8 = true;
  erc32::Config<ConfigTestEnv> cfg (env, TEST_GEOMETRY);

  /* A word takes four accesses from an 8 bit PROM, so the decoded waitstates
     are four times the count plus the accesses themselves.  The write path is
     unaffected.  */
  cfg.WriteWcr ((3u << 4) | (3u << 8));
  CHECK (cfg.rom_read_ws () == 5 + 4 * 2);
  CHECK (cfg.rom_write_ws () == 2);
}

TEST_CASE ("Config keeps the two protection segments apart")
{
  ConfigTestEnv env;
  erc32::Config<ConfigTestEnv> cfg (env, TEST_GEOMETRY);

  /* A segment address counts words and reads back with its two mode bits.  */
  cfg.WriteSegmentBase (0, 0x1000 | (erc32::kSegUser << erc32::kSegModeShift));
  cfg.WriteSegmentEnd (0, 0x2000);
  cfg.WriteSegmentBase (
      1, 0x3000 | (erc32::kSegSupervisor << erc32::kSegModeShift));
  cfg.WriteSegmentEnd (1, 0x4000);

  CHECK (cfg.seg_base (0) == 0x1000);
  CHECK (cfg.seg_end (0) == 0x2000);
  CHECK (cfg.seg_mode (0) == erc32::kSegUser);
  CHECK (cfg.ReadSegmentBase (0) ==
	 (0x1000u | (erc32::kSegUser << erc32::kSegModeShift)));

  CHECK (cfg.seg_base (1) == 0x3000);
  CHECK (cfg.seg_end (1) == 0x4000);
  CHECK (cfg.seg_mode (1) == erc32::kSegSupervisor);
}

TEST_CASE ("Config protects while either segment has a mode")
{
  ConfigTestEnv env;
  erc32::Config<ConfigTestEnv> cfg (env, TEST_GEOMETRY);

  /* Both mode bits clear disables the protection for that segment, so the
     protection is on while either segment still has one.  */
  CHECK (cfg.access_protect () == false);

  cfg.WriteSegmentBase (0, erc32::kSegUser << erc32::kSegModeShift);
  CHECK (cfg.access_protect ());

  cfg.WriteSegmentBase (1, erc32::kSegSupervisor << erc32::kSegModeShift);
  CHECK (cfg.access_protect ());

  cfg.WriteSegmentBase (0, 0);
  CHECK (cfg.access_protect ());

  cfg.WriteSegmentBase (1, 0);
  CHECK (cfg.access_protect () == false);
}

/* The segment write protection.  The manual makes a segment one range, from
   0x02000000 + SEGBASE * 4 up to but not including 0x02000000 + SEGEND * 4,
   and gives each segment a mode bit per processor mode.  */

TEST_CASE ("Config protects everything outside the segments")
{
  ConfigTestEnv env;
  erc32::Config<ConfigTestEnv> cfg (env, TEST_GEOMETRY);

  /* Words 0x100 to 0x200, which is RAM byte offset 0x400 to 0x800, protected
     for supervisor accesses.  */
  cfg.WriteSegmentBase (
      0, 0x100 | (erc32::kSegSupervisor << erc32::kSegModeShift));
  cfg.WriteSegmentEnd (0, 0x200);

  CHECK (cfg.WriteProtected (0x02000600, true, 2) == false); /* inside */
  CHECK (cfg.WriteProtected (0x02000400, true, 2) == false); /* first word */
  CHECK (cfg.WriteProtected (0x020007fc, true, 2) == false); /* last word */
  CHECK (cfg.WriteProtected (0x020003fc, true, 2));	     /* just below */
  CHECK (cfg.WriteProtected (0x02000800, true, 2));	     /* just above */
}

TEST_CASE ("Config protects a segment once, not every 8 Mbyte")
{
  ConfigTestEnv env;
  erc32::Config<ConfigTestEnv> cfg (env, TEST_GEOMETRY);

  cfg.WriteSegmentBase (
      0, 0x100 | (erc32::kSegSupervisor << erc32::kSegModeShift));
  cfg.WriteSegmentEnd (0, 0x200);

  /* The same offset 8 Mbyte higher is a different address and lies outside
     the segment, so it is protected.  */
  CHECK (cfg.WriteProtected (0x02800600, true, 2));
  CHECK (cfg.WriteProtected (0x02800400, true, 2));
}

TEST_CASE ("Config protects each mode separately")
{
  ConfigTestEnv env;
  erc32::Config<ConfigTestEnv> cfg (env, TEST_GEOMETRY);

  /* A segment enabled for supervisor accesses only leaves a user access
     matching no segment, so the user write is protected either way.  */
  cfg.WriteSegmentBase (
      0, 0x100 | (erc32::kSegSupervisor << erc32::kSegModeShift));
  cfg.WriteSegmentEnd (0, 0x200);

  CHECK (cfg.WriteProtected (0x02000600, true, 2) == false);
  CHECK (cfg.WriteProtected (0x02000600, false, 2));

  /* Enabled for both modes, either access is allowed inside it.  */
  cfg.WriteSegmentBase (0, 0x100 | ((erc32::kSegSupervisor | erc32::kSegUser)
				    << erc32::kSegModeShift));
  CHECK (cfg.WriteProtected (0x02000600, true, 2) == false);
  CHECK (cfg.WriteProtected (0x02000600, false, 2) == false);
}

TEST_CASE ("Config catches a double word write leaving a segment")
{
  ConfigTestEnv env;
  erc32::Config<ConfigTestEnv> cfg (env, TEST_GEOMETRY);

  /* A segment ending on an odd word, so its last word is 0x1fe.  */
  cfg.WriteSegmentBase (
      0, 0x100 | (erc32::kSegSupervisor << erc32::kSegModeShift));
  cfg.WriteSegmentEnd (0, 0x1ff);

  /* A double word is eight byte aligned and covers two words, so one placed
     on the last word of the segment writes one word past its end.  The word
     write at the same address stays inside.  */
  CHECK (cfg.WriteProtected (0x020007f8, true, 2) == false);
  CHECK (cfg.WriteProtected (0x020007f8, true, 3));
}

TEST_CASE ("Config inverts the protection for block protection")
{
  ConfigTestEnv env;
  erc32::Config<ConfigTestEnv> cfg (env, TEST_GEOMETRY);

  cfg.WriteSegmentBase (
      0, 0x100 | (erc32::kSegSupervisor << erc32::kSegModeShift));
  cfg.WriteSegmentEnd (0, 0x200);
  cfg.WriteMcr (erc32::kMcrBlockProtect);

  /* Inside the segment is now the protected part and everything else is
     writable.  */
  CHECK (cfg.WriteProtected (0x02000600, true, 2));
  CHECK (cfg.WriteProtected (0x020003fc, true, 2) == false);
  CHECK (cfg.WriteProtected (0x02800600, true, 2) == false);
}

TEST_CASE ("Config protects nothing while no segment is enabled")
{
  ConfigTestEnv env;
  erc32::Config<ConfigTestEnv> cfg (env, TEST_GEOMETRY);

  /* A segment with neither mode bit set disables the protection, so a write
     anywhere is allowed even though the addresses fall inside it.  */
  cfg.WriteSegmentBase (0, 0x100);
  cfg.WriteSegmentEnd (0, 0x200);
  CHECK (cfg.access_protect () == false);
  CHECK (cfg.WriteProtected (0x02000600, true, 2) == false);
}

TEST_CASE ("Config protects across both segments")
{
  ConfigTestEnv env;
  erc32::Config<ConfigTestEnv> cfg (env, TEST_GEOMETRY);

  /* Two enabled segments leave everything outside both protected.  */
  cfg.WriteSegmentBase (
      0, 0x100 | (erc32::kSegSupervisor << erc32::kSegModeShift));
  cfg.WriteSegmentEnd (0, 0x200);
  cfg.WriteSegmentBase (
      1, 0x300 | (erc32::kSegSupervisor << erc32::kSegModeShift));
  cfg.WriteSegmentEnd (1, 0x400);

  CHECK (cfg.WriteProtected (0x02000600, true, 2) == false); /* segment 1 */
  CHECK (cfg.WriteProtected (0x02000e00, true, 2) == false); /* segment 2 */
  CHECK (cfg.WriteProtected (0x02000a00, true, 2));	     /* between */

  /* With block protection, both segments are the protected part.  */
  cfg.WriteMcr (erc32::kMcrBlockProtect);
  CHECK (cfg.WriteProtected (0x02000600, true, 2));
  CHECK (cfg.WriteProtected (0x02000e00, true, 2));
  CHECK (cfg.WriteProtected (0x02000a00, true, 2) == false);
}

TEST_CASE ("Config takes the block protection bit from the control register")
{
  ConfigTestEnv env;
  erc32::Config<ConfigTestEnv> cfg (env, TEST_GEOMETRY);

  cfg.WriteMcr (erc32::kMcrBlockProtect);
  CHECK (cfg.block_protect ());
  CHECK (env.error_mask == erc32::kMcrBlockProtect);

  cfg.WriteMcr (0);
  CHECK (cfg.block_protect () == false);
}

TEST_CASE ("Config reports the reserved bits of every write")
{
  ConfigTestEnv env;
  erc32::Config<ConfigTestEnv> cfg (env, TEST_GEOMETRY);

  /* A write that stays inside the fields is kept and reports nothing.  */
  cfg.WriteIocr (0x0f);
  CHECK (cfg.iocr () == 0x0f);
  CHECK (env.errors == 0);

  cfg.WriteMemcfg (erc32::kMemcfgReserved);
  CHECK (env.errors == 1);

  cfg.WriteIocr (erc32::kIocrReserved);
  CHECK (env.errors == 2);
  CHECK (cfg.iocr () == erc32::kIocrReserved);

  cfg.WriteSegmentBase (0, erc32::kSegBaseReserved);
  CHECK (env.errors == 3);

  cfg.WriteSegmentEnd (0, erc32::kSegEndReserved);
  CHECK (env.errors == 4);

  /* The control register's reserved bit 15 is what the simulator uses to
     inject a MEC hardware error.  */
  cfg.WriteMcr (erc32::kMcrHardwareError);
  CHECK (env.errors == 5);
}

TEST_CASE ("Config resets the processor only when the control register allows")
{
  ConfigTestEnv env;
  erc32::Config<ConfigTestEnv> cfg (env, TEST_GEOMETRY);

  cfg.WriteMcr (0);
  cfg.WriteSoftwareReset ();
  CHECK (env.resets == 0);

  cfg.WriteMcr (erc32::kMcrSoftwareReset);
  cfg.WriteSoftwareReset ();
  CHECK (env.resets == 1);
}

TEST_CASE ("Config enters power down only when the control register allows")
{
  ConfigTestEnv env;
  erc32::Config<ConfigTestEnv> cfg (env, TEST_GEOMETRY);

  /* The timing of the power down is started either way; only entering it is
     gated.  */
  cfg.WriteMcr (0);
  cfg.WritePowerDown ();
  CHECK (env.power_downs == 0);
  CHECK (env.power_down_timings == 1);

  cfg.WriteMcr (erc32::kMcrPowerDown);
  cfg.WritePowerDown ();
  CHECK (env.power_downs == 1);
  CHECK (env.power_down_timings == 2);

  /* The simulator's boot loader enables it without a register write.  */
  cfg.WriteMcr (0);
  cfg.EnablePowerDown ();
  cfg.WritePowerDown ();
  CHECK (env.power_downs == 2);
}

TEST_CASE ("Config narrates what it decodes when verbose")
{
  ConfigTestEnv env;
  erc32::Config<ConfigTestEnv> cfg (env, TEST_GEOMETRY);
  env.verbose = true;

  cfg.WriteMemcfg (0);
  CHECK (env.log.find ("RAM start: 0x2000000, RAM size: 256 K, ROM size: 128 "
		       "K\n") != std::string::npos);

  env.log.clear ();
  cfg.WriteWcr (0);
  CHECK (
      env.log ==
      "Waitstates = RAM read: 0, RAM write: 0, ROM read: 0, ROM write: 0\n");

  env.log.clear ();
  cfg.WriteSegmentBase (0, 0x40 | (erc32::kSegUser << erc32::kSegModeShift));
  CHECK (env.log ==
	 "Segment 1 memory protection enabled (0x02000100 - 0x02000000)\n");

  env.log.clear ();
  cfg.WriteMcr (erc32::kMcrSoftwareReset | erc32::kMcrPowerDown);
  CHECK (env.log == "Memory block write protection enabled\n"
		    "Software reset enabled\n"
		    "Power-down mode enabled\n");

  env.log.clear ();
  cfg.WriteSoftwareReset ();
  CHECK (env.log == " Software reset issued\n");
}

TEST_CASE ("Config narrates only what is enabled")
{
  ConfigTestEnv env;
  erc32::Config<ConfigTestEnv> cfg (env, TEST_GEOMETRY);
  env.verbose = true;

  /* Verbose, but with no segment protected and neither mode enabled, there is
     nothing to announce.  */
  cfg.WriteMcr (0);
  CHECK (env.log.empty ());

  /* A segment write with neither mode bit set protects nothing and announces
     nothing.  */
  cfg.WriteSegmentBase (1, 0x100);
  CHECK (env.log.empty ());

  /* The hardware error test bit is reported without being narrated.  */
  cfg.WriteMcr (erc32::kMcrHardwareError);
  CHECK (env.errors == 1);
  CHECK (env.log.empty ());
}

TEST_CASE ("Config stays quiet when not verbose")
{
  ConfigTestEnv env;
  erc32::Config<ConfigTestEnv> cfg (env, TEST_GEOMETRY);

  cfg.WriteMemcfg (0);
  cfg.WriteWcr (0);
  cfg.WriteSegmentBase (0, erc32::kSegUser << erc32::kSegModeShift);
  cfg.WriteMcr (erc32::kMcrSoftwareReset | erc32::kMcrPowerDown);
  cfg.WriteSoftwareReset ();
  CHECK (env.log.empty ());
}

TEST_CASE ("ErrorHandler resets to the values the manual documents")
{
  ErrorTestEnv env;
  erc32::ErrorHandler<ErrorTestEnv> err (env);

  /* No error latched, no address latched, and the fault type field all
     ones.  */
  CHECK (err.ersr () == 0);
  CHECK (err.ffar () == 0);
  CHECK (err.sfsr () == 0x078);
}

TEST_CASE ("ErrorHandler halts on an unmasked error by default")
{
  ErrorTestEnv env;
  erc32::ErrorHandler<ErrorTestEnv> err (env);

  /* The reset value of the mask and reset-or-halt bits is zero, which the
     manual describes as an error leading to a processor halt.  */
  err.IuErrorMode ();

  CHECK (env.halts == 1);
  CHECK (env.resets == 0);
  CHECK (env.irqs.empty ());
  CHECK (err.ersr () == (erc32::kErrIuErrorMode | erc32::kErrHalted));
}

TEST_CASE ("ErrorHandler resets when the control register says so")
{
  ErrorTestEnv env;
  erc32::ErrorHandler<ErrorTestEnv> err (env);

  /* RHIUEM, bit 6 of the control register, turns the halt into a reset.  The
     register is left holding the reset cause and nothing else.  */
  err.SetControl (1u << 6);
  err.IuErrorMode ();

  CHECK (env.resets == 1);
  CHECK (env.halts == 0);
  CHECK (err.ersr () == erc32::kResetError);
}

TEST_CASE ("ErrorHandler interrupts when the error is masked")
{
  ErrorTestEnv env;
  erc32::ErrorHandler<ErrorTestEnv> err (env);

  /* IUEMMSK, bit 5, masks the error.  A masked error raises the masked
     hardware error interrupt instead of stopping the processor, and stays
     latched.  */
  err.SetControl (1u << 5);
  err.IuErrorMode ();

  CHECK (env.halts == 0);
  CHECK (env.resets == 0);
  REQUIRE (env.irqs.size () == 1);
  CHECK (env.irqs[0] == erc32::kMaskedErrorLevel);
  CHECK (err.ersr () == erc32::kErrIuErrorMode);
}

TEST_CASE ("ErrorHandler acts on every source the MEC handles")
{
  /* The five error sources the manual gives the MEC an action for, each with
     the position of its mask bit in the control register.  */
  struct
  {
    uint32 bit;
    unsigned mask_shift;
  } sources[] = { { erc32::kErrIuErrorMode, 5 },
		  { erc32::kErrIuHwError, 7 },
		  { erc32::kErrIuCmpError, 9 },
		  { erc32::kErrFpuCmpError, 11 },
		  { erc32::kErrMecHwError, 13 } };

  for (auto src : sources)
    {
      ErrorTestEnv env;
      env.error_write_enabled = true;
      erc32::ErrorHandler<ErrorTestEnv> err (env);

      /* Unmasked, the source halts.  */
      err.WriteErsr (src.bit);
      CHECK (env.halts == 1);

      /* Masked, the same source interrupts instead.  */
      ErrorTestEnv masked;
      masked.error_write_enabled = true;
      erc32::ErrorHandler<ErrorTestEnv> err2 (masked);
      err2.SetControl (1u << src.mask_shift);
      err2.WriteErsr (src.bit);
      CHECK (masked.halts == 0);
      REQUIRE (masked.irqs.size () == 1);
      CHECK (masked.irqs[0] == erc32::kMaskedErrorLevel);

      /* Unmasked and told to reset, it resets.  */
      ErrorTestEnv resetting;
      resetting.error_write_enabled = true;
      erc32::ErrorHandler<ErrorTestEnv> err3 (resetting);
      err3.SetControl (2u << src.mask_shift);
      err3.WriteErsr (src.bit);
      CHECK (resetting.resets == 1);
      CHECK (err3.ersr () == erc32::kResetError);
    }
}

TEST_CASE ("ErrorHandler takes no action on an FPU hardware error")
{
  ErrorTestEnv env;
  env.error_write_enabled = true;
  erc32::ErrorHandler<ErrorTestEnv> err (env);

  /* FPUHE is the one error bit the manual marks as no MEC action.  It
     latches and nothing else happens.  */
  err.WriteErsr (erc32::kErrFpuHwError);

  CHECK (env.halts == 0);
  CHECK (env.resets == 0);
  CHECK (env.irqs.empty ());
  CHECK (err.ersr () == erc32::kErrFpuHwError);
}

TEST_CASE ("ErrorHandler leaves a later source the cleared register")
{
  ErrorTestEnv env;
  env.error_write_enabled = true;
  erc32::ErrorHandler<ErrorTestEnv> err (env);

  /* A reset clears the register, so the second of two latched errors finds
     nothing left to act on.  */
  err.SetControl (2u << 5); /* the IU error mode source resets */
  err.WriteErsr (erc32::kErrIuErrorMode | erc32::kErrMecHwError);

  CHECK (env.resets == 1);
  CHECK (env.halts == 0);
  CHECK (err.ersr () == erc32::kResetError);
}

TEST_CASE ("ErrorHandler names the error it acted on")
{
  ErrorTestEnv env;
  env.verbose = true;
  erc32::ErrorHandler<ErrorTestEnv> err (env);

  err.IuErrorMode ();
  CHECK (env.log == "Error manager halt - IU in error mode\n");

  ErrorTestEnv resetting;
  resetting.verbose = true;
  erc32::ErrorHandler<ErrorTestEnv> err2 (resetting);
  err2.SetControl (2u << 13); /* the MEC hardware error source resets */
  err2.MecHwError ();
  CHECK (resetting.log == "Error manager reset - MEC hardware error\n");
}

TEST_CASE ("ErrorHandler stays quiet when not verbose")
{
  ErrorTestEnv env;
  erc32::ErrorHandler<ErrorTestEnv> err (env);

  err.MecHwError ();
  CHECK (env.log.empty ());
}

TEST_CASE ("ErrorHandler writes the error bits only in test mode")
{
  ErrorTestEnv env;
  erc32::ErrorHandler<ErrorTestEnv> err (env);

  /* The manual makes bits 5-0 writable only while the error write enable bit
     of the test control register is set.  Without it the write is dropped
     and no error is injected.  */
  err.WriteErsr (erc32::kErrIuErrorMode);
  CHECK (err.ersr () == 0);
  CHECK (env.halts == 0);

  /* With it, the write injects the error and the handler acts on it.  */
  env.error_write_enabled = true;
  err.WriteErsr (erc32::kErrIuErrorMode);
  CHECK (env.halts == 1);
  CHECK (err.ersr () == (erc32::kErrIuErrorMode | erc32::kErrHalted));
}

TEST_CASE ("ErrorHandler keeps the read-only fields across a write")
{
  ErrorTestEnv env;
  erc32::ErrorHandler<ErrorTestEnv> err (env);

  /* The reset cause is read only, so it survives a write of the system
     availability bit, which is not.  */
  err.SetResetCause (erc32::kResetWatchdog);
  err.WriteErsr (erc32::kErrSysAvailable);
  CHECK (err.ersr () == (erc32::kResetWatchdog | erc32::kErrSysAvailable));

  err.WriteErsr (0);
  CHECK (err.ersr () == erc32::kResetWatchdog);
}

TEST_CASE ("ErrorHandler reports the reserved bits of an injected error")
{
  ErrorTestEnv env;
  env.error_write_enabled = true;
  erc32::ErrorHandler<ErrorTestEnv> err (env);

  /* Bit 6 is outside the writable fields.  Writing it in test mode latches a
     MEC hardware error, which then halts the processor.  */
  err.WriteErsr (1u << 6);

  CHECK (env.halts == 1);
  CHECK ((err.ersr () & erc32::kErrMecHwError) != 0);
}

TEST_CASE ("ErrorHandler records a supervisor load fault")
{
  ErrorTestEnv env;
  erc32::ErrorHandler<ErrorTestEnv> err (env);

  /* An access to an unimplemented area under the supervisor data ASI: the
     fault type in bits 6-3, the data fault valid bit, and the access type
     field marking a supervisor load.  */
  err.SetFault (erc32::kFaultUnimplemented, 0x02001234, 0xb, 1);

  CHECK (err.ffar () == 0x02001234);
  CHECK (err.sfsr () ==
	 ((erc32::kFaultUnimplemented << 3) | erc32::kSfsrDataFaultValid |
	  erc32::kSfsrAtSupervisor));
}

TEST_CASE ("ErrorHandler records a user store fault")
{
  ErrorTestEnv env;
  erc32::ErrorHandler<ErrorTestEnv> err (env);

  /* A protected-area write under the user data ASI: the access type field
     marks a store and leaves the supervisor bit clear.  */
  err.SetFault (erc32::kFaultProtection, 0x02005678, 0xa, 0);

  CHECK (err.ffar () == 0x02005678);
  CHECK (err.sfsr () == ((erc32::kFaultProtection << 3) |
			 erc32::kSfsrDataFaultValid | erc32::kSfsrAtStore));
}

TEST_CASE ("ErrorHandler ignores a fault on an instruction fetch")
{
  ErrorTestEnv env;
  erc32::ErrorHandler<ErrorTestEnv> err (env);

  /* Only a data access latches.  An instruction fetch carries ASI 8 or 9 and
     leaves both registers at their reset values.  */
  err.SetFault (erc32::kFaultUnimplemented, 0x02001234, 0x9, 1);

  CHECK (err.ffar () == 0);
  CHECK (err.sfsr () == 0x078);
}

TEST_CASE ("ErrorHandler clears the fault status on any write")
{
  ErrorTestEnv env;
  erc32::ErrorHandler<ErrorTestEnv> err (env);

  err.SetFault (erc32::kFaultMecRegister, 0x01f8000c, 0xb, 1);
  REQUIRE (err.sfsr () != 0x078);

  /* The manual makes the data irrelevant: the register goes back to its
     reset value.  The failing address keeps its value.  */
  err.WriteSfsr (0x1234);
  CHECK (err.sfsr () == 0x078);
  CHECK (err.ffar () == 0x01f8000c);
}

TEST_CASE ("ErrorHandler reports the reserved bits of a fault status write")
{
  ErrorTestEnv env;
  erc32::ErrorHandler<ErrorTestEnv> err (env);

  /* Bit 11 is reserved, so writing it is a MEC hardware error and halts.  */
  err.WriteSfsr (0x0800);

  CHECK (env.halts == 1);
  CHECK (err.sfsr () == 0x078);
}

/* The board dispatch: the same registers reached the way the processor
   reaches them.  */

TEST_CASE_FIXTURE (mec_fixture, "MEC dispatches the error registers")
{
  /* Route errors to interrupt level 1 so a case does not stop the
     simulator.  */
  wr (R_MCR, MCR_HWERR_IRQ);

  CHECK (rd (R_SFSR) == 0x78);
  CHECK (rd (R_FFAR) == 0);
  CHECK (rd (R_ERSR) == 0);

  /* Without the error write enable bit an injected error is dropped.  */
  wr (R_ERSR, erc32::kErrMecHwError);
  CHECK (rd (R_ERSR) == 0);

  /* With it, the error is latched and raises the masked hardware error
     interrupt.  */
  wr (R_TCR, TCR_ERROR_WRITE_EN);
  wr (R_ERSR, erc32::kErrMecHwError);
  CHECK ((rd (R_ERSR) & erc32::kErrMecHwError) != 0);
  CHECK ((rd (R_IPR) & (1u << erc32::kMaskedErrorLevel)) != 0);
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
