/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Tests for the two SPARC GRLIB board files: leon2.cc and leon3.cc.

   LEON2 (leon2.cc) predates grlib.cc's shared AHB/APB bus, the same way
   erc32.cc does: it is self-contained, with its own interrupt controller,
   timer unit and UART register files as static file-local state, reached
   only through the board's memory_read/memory_write callbacks at the APB
   register window 0x80000000-0x800000ff.  A register access is a
   supervisor word access, matching the fixtures below.  Specification
   citations are to ref/leon2-ip-core-manual.md, sections 7.2 (interrupt
   controller), 7.4 (timer unit) and 7.5 (UARTs); see
   ref/leon2-ip-core-manual.toc.md.  Cases with no citation, or marked
   "current behaviour", pin what the code does rather than a documented
   interface.

   LEON3 (leon3.cc) is built on grlib.cc's shared AHB/APB bus instead: its
   init_sim registers the real apbmst bridge, sdctrl, dsu, leon3s and, on
   the bridge, the real apbuart, irqmp, gptimer and greth cores, at fixed
   addresses (APBSTART + 0x100/0x200/0x300/0xB00).  tests/grlibcores.cc
   already mounts its own fake cores on the very same shared bridge 0, at
   APBSTART + 0x200 and APBSTART + 0x300 ("APBMST forwards accesses...",
   "APBMST init/reset/read/write skip a mounted core's null callbacks"),
   explicitly chosen, per that file's own comments, to be "the same
   relative offset leon3.cc uses for irqmp".  grlib.cc has no way to
   unregister a core, and apbbus_read/apbbus_write resolve by first match
   in registration order (grlib.cc's apbbus_read/apbbus_write, the `break`
   after the first address match), with no way to unregister a match found
   first.  Registering leon3's real cores from this file races
   tests/grlibcores.cc's fake ones for those addresses under
   --order-by=rand: confirmed by a throwaway probe that called
   leon3.init_sim() once and reran the suite with `--rand-seed=1`, which
   failed two CHECKs in "APBMST init/reset/read/write skip a mounted core's
   null callbacks" (grlibcores.cc:493, grlibcores.cc:505) because the real
   irqmp/gptimer/apbuart cores had claimed the address ahead of
   grlibcores.cc's fakes.  Since tests/grlibcores.cc may not be modified
   and leon3.cc may not be modified, there is no safe way to call the real,
   unmodified leon3.init_sim() from this shared test binary.  This is
   reported as a suspected defect in the final report; leon3.cc's init_sim
   is therefore the one entry point this file does not exercise, and
   leon3.cc does not reach 100% coverage as a result.  Everything else
   leon3.cc does -- memory_read/memory_write's own address decode,
   get_mem_ptr, sis_memory_read/write, boot_init, reset, error_mode,
   sim_halt, exit_sim -- is covered here, using a private fake AHB slave
   this file registers itself (at 0x50000000, clear of every address any
   other test file or board uses) to reach the "outside RAM/ROM" dispatch
   path without ever registering anything at the addresses above.  */

#include "doctest.h"
#include "support.h"

#include "config.h"
#include "sis.h"
#include "grlib.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

using sis_tests::stdout_capture;

extern void advance_time (uint64 endtime);

namespace
{

/* ===================================================================== */
/* LEON2                                                                  */
/* ===================================================================== */

/* The APB register window and register offsets, from leon2.cc (the
   defines there are file-local).  */
const uint32 APB = 0x80000000;
const uint32 R_CACHE_CTRL = 0x014;
const uint32 R_POWER_DOWN = 0x018;
const uint32 R_LEON2_CONFIG = 0x024;
const uint32 R_TIMER_TIMER1 = 0x040;
const uint32 R_TIMER_RELOAD1 = 0x044;
const uint32 R_TIMER_CTRL1 = 0x048;
const uint32 R_TIMER_TIMER2 = 0x050;
const uint32 R_TIMER_RELOAD2 = 0x054;
const uint32 R_TIMER_CTRL2 = 0x058;
const uint32 R_TIMER_SCALER = 0x060;
const uint32 R_TIMER_SCLOAD = 0x064;
const uint32 R_APBUART_RXTX = 0x070;
const uint32 R_APBUART_STATUS = 0x074;
const uint32 R_IRQCTRL_IMR = 0x090;
const uint32 R_IRQCTRL_IPR = 0x094;
const uint32 R_IRQCTRL_IFR = 0x098;
const uint32 R_IRQCTRL_ICR = 0x09C;

const int32 SZ_WORD = 2;
const uint32 UARTBUF = 1024;

/* Timer control register bits, figure 20: load, reload, enable.  */
const uint32 TC_LD = 0x4;
const uint32 TC_RL = 0x2;
const uint32 TC_EN = 0x1;

int
leon2_wr (uint32 off, uint32 val)
{
  uint32 d = val;
  int32 ws;
  return leon2.memory_write (APB + off, &d, SZ_WORD, &ws);
}

uint32
leon2_rd (uint32 off)
{
  uint32 d = 0;
  int32 ws;
  leon2.memory_read (APB + off, &d, &ws);
  return d;
}

/* Common per-case setup, shared by every LEON2 case that does not care
   about the UART port: sets ms/cputype/archtype, clears the event queue
   and register file, and starts from a clean external interrupt request
   (init_regs, unlike tests/erc32.cc's fixture, does not clear ext_irl).
   Deliberately does not call leon2.init_sim: that opens the board's own
   UART port (port_init), and the "LEON2 UART" TEST_CASE below needs that
   to happen at a controlled point instead (see its header comment).  */
struct leon2_regs_fixture
{
  int saved_verbose;
  int saved_cputype;
  int saved_archtype;

  leon2_regs_fixture ()
      : saved_verbose (sis_verbose), saved_cputype (cputype),
	saved_archtype (archtype)
  {
    cputype = CPU_LEON2;
    archtype = CPU_SPARC;
    ms = &leon2;
    ebase.freq = 14;
    ebase.simtime = 0;
    ebase.simstart = 0;
    reset_all ();
    init_bpt (sregs);
    sis_verbose = 0;
    for (int i = 0; i < NCPU; i++)
      ext_irl[i] = 0;
  }

  ~leon2_regs_fixture ()
  {
    sis_verbose = saved_verbose;
    cputype = saved_cputype;
    archtype = saved_archtype;
  }
};

/* Creates a private temporary file and points the global uart_dev1 at it,
   the way tests/apbuart.cc's apbuart_host_file does for grlib.cc's shared
   apbuart port.  LEON2's port ("porta" in leon2.cc) is a separate static
   variable local to that file, not shared with grlib.cc's, so there is no
   cross-file port to protect; this only has to keep leon2.cc's port off
   the real console.  */
struct leon2_uart_host_file
{
  char path[64];
  int fd;
  char saved_uart_dev1[128];

  leon2_uart_host_file () : fd (-1)
  {
    strcpy (path, "/tmp/sis-leon2uart-XXXXXX");
    fd = mkstemp (path);
    REQUIRE (fd >= 0);

    strcpy (saved_uart_dev1, uart_dev1);
    strcpy (uart_dev1, path);
  }

  ~leon2_uart_host_file ()
  {
    strcpy (uart_dev1, saved_uart_dev1);
    if (fd >= 0)
      close (fd);
    unlink (path);
  }

  void
  host_send (const char *data, size_t len)
  {
    REQUIRE (write (fd, data, len) == (ssize_t) len);
  }

  int
  host_recv (char *buf, size_t len)
  {
    int n = 0;
    while (n < (int) len)
      {
	ssize_t r = ::read (fd, buf + n, len - n);
	if (r <= 0)
	  break;
	n += (int) r;
      }
    return n;
  }
};

/* Opens leon2's own UART port on the private file above.  Every case in
   the "LEON2 UART" TEST_CASE but the first constructs one of these; the
   first must not, so that it is the one case in the whole binary where
   the port has never been opened (mirrors tests/apbuart.cc's own first
   SUBCASE for the same reason).  */
struct leon2_uart_env : leon2_uart_host_file
{
  int saved_verbose;
  int saved_cputype;
  int saved_archtype;

  leon2_uart_env ()
      : saved_verbose (sis_verbose), saved_cputype (cputype),
	saved_archtype (archtype)
  {
    cputype = CPU_LEON2;
    archtype = CPU_SPARC;
    ms = &leon2;
    ebase.freq = 14;
    ebase.simtime = 0;
    ebase.simstart = 0;
    sis_verbose = 0;
    leon2.init_sim (); /* opens porta on the private file above */
    reset_all ();
    init_bpt (sregs);
    for (int i = 0; i < NCPU; i++)
      ext_irl[i] = 0;
  }

  ~leon2_uart_env ()
  {
    sis_verbose = saved_verbose;
    cputype = saved_cputype;
    archtype = saved_archtype;
  }
};

/* ===================================================================== */
/* LEON3                                                                  */
/* ===================================================================== */

const uint32 LEON3_RAM_START = 0x40000000;
const uint32 LEON3_RAM_END = 0x44000000;
const uint32 LEON3_ROM_END = 0x01000000;

/* A private fake AHB slave, registered on the real grlib.cc bus at an
   address no other test file or real board uses (checked against every
   address literal in tests/grlibbus.cc, tests/grlibcores.cc and the real
   boards: erc32 has no AHB bus at all, leon2 does not touch grlib.cc,
   leon3 uses 0x40000000-0x43ffffff/0x80000000-0x800fffff/0x90000000-
   0x900fffff, and tests/grlibbus.cc uses 0x20000000-0x22ffffff).  This is
   what lets leon3.memory_read/memory_write's own "outside RAM/ROM, reach
   grlib_read/grlib_write" branch be exercised, both the "a slave answers"
   and "no slave claims it" sides, without ever registering anything at
   the addresses tests/grlibcores.cc's APBMST cases depend on (see the
   file header).  Registered once for the whole binary, per
   tests/grlibcore.h's rule that grlib.cc's registration arrays only ever
   grow.  */
const uint32 FAKE_AHB_BASE = 0x50000000;

int fake_ahb_read_calls;
int fake_ahb_write_calls;

int
fake_ahb_read (uint32 addr, uint32 *data)
{
  fake_ahb_read_calls++;
  *data = 0x5eed0000u | addr;
  return 1;
}

int
fake_ahb_write (uint32 addr, uint32 *data, uint32 size)
{
  (void) size;
  fake_ahb_write_calls++;
  (void) addr;
  (void) data;
  return 1;
}

void
fake_ahb_add (int irq, uint32 addr, uint32 mask)
{
  (void) irq;
  (void) addr;
  (void) mask;
}

/* grlib_ahbs_add only records start/end/mask -- and so only claims any
   address at all -- when the core has a non-NULL add (grlib.cc's own
   "if (core->add)" guard); a NULL add leaves the slot unclaimed, the way
   tests/grlibbus.cc's ahbs_no_add deliberately pins.  This fake needs a
   real (if empty) add to actually be reachable.  */
const struct grlib_ipcore fake_ahb_slave = { NULL, NULL, fake_ahb_read,
					     fake_ahb_write, fake_ahb_add };

void
ensure_fake_ahb_registered ()
{
  static bool done = false;

  if (done)
    return;
  done = true;

  grlib_ahbs_add (&fake_ahb_slave, 0, FAKE_AHB_BASE, 0xfff);
}

/* Common per-case setup for LEON3.  Does not call leon3.init_sim (see the
   file header); memory_read/memory_write/get_mem_ptr/sis_memory_read/
   sis_memory_write/boot_init/reset/error_mode/sim_halt/exit_sim need
   nothing it would provide beyond what ensure_fake_ahb_registered already
   sets up once for the whole binary.  */
struct leon3_fixture
{
  int saved_verbose;
  int saved_cputype;
  int saved_archtype;

  leon3_fixture ()
      : saved_verbose (sis_verbose), saved_cputype (cputype),
	saved_archtype (archtype)
  {
    ensure_fake_ahb_registered ();

    cputype = CPU_LEON3;
    archtype = CPU_SPARC;
    ms = &leon3;
    ebase.freq = 14;
    ebase.simtime = 0;
    ebase.simstart = 0;
    reset_all ();
    init_bpt (sregs);
    sis_verbose = 0;
  }

  ~leon3_fixture ()
  {
    sis_verbose = saved_verbose;
    cputype = saved_cputype;
    archtype = saved_archtype;
  }
};

}

/* ===================================================================== */
/* LEON2 interrupt controller                                            */
/* ===================================================================== */

TEST_CASE_FIXTURE (leon2_regs_fixture,
		   "LEON2 interrupt controller resets idle")
{
  /* leon2_reset clears pending, forced and (unlike the manual's own IMR
     reset value discussion in 7.2.1, which only promises the mask starts
     all zero) the mask too.  */
  CHECK (leon2_rd (R_IRQCTRL_IPR) == 0);
  CHECK (leon2_rd (R_IRQCTRL_IFR) == 0);
  CHECK (leon2_rd (R_IRQCTRL_IMR) == 0);
  CHECK (ext_irl[0] == 0);
}

TEST_CASE_FIXTURE (
    leon2_regs_fixture,
    "spec (7.2.1) interrupt 15 has the highest priority, 1 the lowest")
{
  /* "the interrupts are prioritised within each level, with interrupt 15
     having the highest priority and interrupt 1 the lowest": force levels
     5 and 14 (avoiding 15, whose mask bit the code can never set -- see
     the dedicated case below) with everything unmasked, and the higher
     level wins.  */
  leon2_wr (R_IRQCTRL_IMR, 0x7ffe);
  leon2_wr (R_IRQCTRL_IFR, (1u << 14) | (1u << 5));
  CHECK (ext_irl[0] == 14);
}

TEST_CASE_FIXTURE (leon2_regs_fixture,
		   "LEON2 level 1, the lowest maskable level, still reaches "
		   "the IU on its own")
{
  leon2_wr (R_IRQCTRL_IMR, 0x7ffe);
  leon2_wr (R_IRQCTRL_IFR, 1u << 1);
  CHECK (ext_irl[0] == 1);
}

TEST_CASE_FIXTURE (leon2_regs_fixture,
		   "spec (figure 8) the mask register carries interrupt 15")
{
  /* Figure 8 puts the interrupt mask in bits 15 to 1.  The write used to
     keep only bits 14 to 1, so bit 15 could never be set and chk_irq's
     "(ipr|ifr) & imr" cleared the highest interrupt out of every request,
     whatever the pending and forced registers held.  */
  leon2_wr (R_IRQCTRL_IMR, 0xffff); /* every bit, including 15 */
  CHECK (leon2_rd (R_IRQCTRL_IMR) == 0xfffe);

  leon2_wr (R_IRQCTRL_IFR, 1u << 15);
  CHECK (ext_irl[0] == 15);
}

TEST_CASE_FIXTURE (leon2_regs_fixture,
		   "spec (7.2.3, figure 11) ICR clears a pending bit, "
		   "reads back as zero")
{
  /* IFR (figure 10) is the *forced* register; a genuinely *pending* bit
     (figure 9, IPEND) can only be raised through set_irq, e.g. by a UART
     write (table 11: UART 1 is interrupt 3).  ICR only ever clears IPR
     (irqctrl_intack's own if/else routes a forced bit's acknowledge to
     IFR instead; see the dedicated case for that split).  */
  leon2_wr (R_IRQCTRL_IMR, 0x7ffe);
  leon2_wr (R_APBUART_RXTX, 'a'); /* set_irq (3): a genuinely pending bit */
  REQUIRE (ext_irl[0] == 3);

  leon2_wr (R_IRQCTRL_ICR, 1u << 3);
  CHECK (ext_irl[0] == 0);
  CHECK (leon2_rd (R_IRQCTRL_ICR) == 0); /* write-only: reads as the default */
}

TEST_CASE_FIXTURE (leon2_regs_fixture,
		   "spec (figures 8 to 11) reserved IFR/IMR/ICR bit 0 "
		   "is dropped on every write")
{
  /* Bit 0 is reserved in all four registers and there is no interrupt
     zero, so every write drops it and keeps bits 15 to 1.  */
  leon2_wr (R_IRQCTRL_IFR, 0xffffffffu);
  CHECK (leon2_rd (R_IRQCTRL_IFR) == 0xfffe);

  leon2_wr (R_IRQCTRL_IMR, 0xffffffffu);
  CHECK (leon2_rd (R_IRQCTRL_IMR) == 0xfffe);
}

TEST_CASE_FIXTURE (leon2_regs_fixture,
		   "LEON2 acknowledging an interrupt clears its pending bit")
{
  leon2_wr (R_IRQCTRL_IMR, 0x7ffe);
  leon2_wr (R_IRQCTRL_IFR, 1u << 9);
  REQUIRE (ext_irl[0] == 9);

  sregs[0].intack (9, 0); /* the irqctrl_intack trampoline, via reset() */
  CHECK (ext_irl[0] == 0);
  CHECK ((leon2_rd (R_IRQCTRL_IFR) & (1u << 9)) == 0);
}

TEST_CASE_FIXTURE (leon2_regs_fixture,
		   "LEON2 (current behaviour) acknowledging a pending, "
		   "unforced interrupt clears IPR instead of IFR")
{
  /* set_irq (reached here through the UART/timer paths in other cases;
     driven directly through the IPR side effect chk_irq shares) posts to
     irqctrl_ipr, not irqctrl_ifr; the manual's own IPEND/IFORCE split
     (7.2.1, "interrupt can also be forced... IU acknowledgement will
     clear the force bit rather than the pending bit") is exactly what
     irqctrl_intack's if/else implements, using the IFR bit to decide which
     side to clear.  */
  leon2_wr (R_IRQCTRL_IMR, 0x7ffe);
  leon2_wr (R_TIMER_RELOAD1, 0);
  leon2_wr (R_TIMER_CTRL1, TC_LD | TC_RL | TC_EN);
  /* The prescaler's own reset value is 0xffff (65536 clocks a tick); the
     scaler event armed by leon2.reset is what has to fire for gpt_intr to
     run at all, so this waits out one whole period rather than picking a
     shorter one.  */
  advance_time (0x10000); /* ticks the timer to zero: set_irq (TIMER_IRQ) */
  REQUIRE (ext_irl[0] == 8);

  sregs[0].intack (8, 0);
  CHECK (ext_irl[0] == 0);
  CHECK ((leon2_rd (R_IRQCTRL_IPR) & (1u << 8)) == 0);
}

TEST_CASE_FIXTURE (leon2_regs_fixture,
		   "LEON2 narrates a rising interrupt level once, at "
		   "verbosity above 2")
{
  sis_verbose = 3;
  leon2_wr (R_IRQCTRL_IMR, 0x7ffe);

  stdout_capture cap;
  leon2_wr (R_IRQCTRL_IFR, (1u << 7) | (1u << 3));
  std::string out = cap.str ();
  CHECK (out.find ("IU irl: 7") != std::string::npos);

  /* Level 3 was also raised in the same write, but since it is lower than
     the already-reported level 7, chk_irq's own "i > old_irl" branch is
     false the second time: not narrated.  */
  CHECK (out.find ("IU irl: 3") == std::string::npos);

  /* Bit 7 was raised through IFR (forced), so acknowledging it, not ICR
     (which only ever clears IPR; see the dedicated case above), is what
     clears it: level 3 becomes the highest, but that is a falling
     transition (3 is not > the old level 7), so chk_irq's own "i >
     old_irl" narrates nothing for it either.  irqctrl_intack has its own
     "interrupt N acknowledged" line at the same verbosity, so this checks
     for the absence of chk_irq's own text rather than an empty capture.  */
  stdout_capture cap2;
  sregs[0].intack (7, 0);
  CHECK (ext_irl[0] == 3);
  CHECK (cap2.str ().find ("IU irl") == std::string::npos);

  /* A later, genuinely higher level is narrated.  */
  stdout_capture cap3;
  leon2_wr (R_IRQCTRL_IFR, 1u << 9);
  CHECK (cap3.str ().find ("IU irl: 9") != std::string::npos);
}

TEST_CASE_FIXTURE (leon2_regs_fixture,
		   "LEON2 does not narrate below verbosity 3")
{
  /* Verbosity 2 still triggers apb_write/apb_read's own "sis_verbose > 1"
     diagnostic (a separate case below covers that), so this checks for
     the narration text specifically rather than an empty capture.  */
  sis_verbose = 2;
  leon2_wr (R_IRQCTRL_IMR, 0x7ffe);

  stdout_capture cap;
  leon2_wr (R_IRQCTRL_IFR, 1u << 7);
  CHECK (cap.str ().find ("IU irl") == std::string::npos);
}

/* ===================================================================== */
/* LEON2 timer unit                                                      */
/* ===================================================================== */

TEST_CASE_FIXTURE (leon2_regs_fixture, "LEON2 timer 1 resets idle")
{
  CHECK (leon2_rd (R_TIMER_TIMER1) == 0xffffffffu);
  CHECK (leon2_rd (R_TIMER_RELOAD1) == 0xffffffffu);
  CHECK (leon2_rd (R_TIMER_CTRL1) == 0);
  CHECK (leon2_rd (R_TIMER_CTRL2) == 0);
  CHECK (leon2_rd (R_TIMER_SCLOAD) == 0xffffu);
}

TEST_CASE_FIXTURE (
    leon2_regs_fixture,
    "LEON2 (current behaviour, suspected defect) leon2_reset does not "
    "reset timer 2's counter or reload register")
{
  /* leon2_reset resets gpt_counter[0], gpt_reload[0] and gpt_scaler (timer
     1 and the shared prescaler) plus both gpt_ctrl entries, but never
     touches gpt_counter[1] or gpt_reload[1] (leon2.cc's leon2_reset): timer
     2's counter and reload keep whatever an earlier reset cycle, in this
     process or a previous one, left them at instead of returning to a
     known value.  Demonstrated by programming timer 2, resetting, and
     finding the programmed value survives; not fixed here, see the final
     report.  */
  leon2_wr (R_TIMER_RELOAD2, 0x4321);
  leon2_wr (R_TIMER_CTRL2, TC_LD);
  REQUIRE (leon2_rd (R_TIMER_TIMER2) == 0x4321);

  reset_all ();

  CHECK (leon2_rd (R_TIMER_TIMER2) == 0x4321); /* survives the reset */
  CHECK (leon2_rd (R_TIMER_RELOAD2) == 0x4321);
}

TEST_CASE_FIXTURE (leon2_regs_fixture,
		   "LEON2 the counter registers are also writable directly")
{
  leon2_wr (R_TIMER_TIMER1, 0x2222);
  CHECK (leon2_rd (R_TIMER_TIMER1) == 0x2222);
  leon2_wr (R_TIMER_TIMER2, 0x3333);
  CHECK (leon2_rd (R_TIMER_TIMER2) == 0x3333);
}

TEST_CASE_FIXTURE (leon2_regs_fixture,
		   "spec (7.4.2, figure 20) LD loads the counter from the "
		   "reload register")
{
  leon2_wr (R_TIMER_RELOAD1, 0x1234);
  leon2_wr (R_TIMER_CTRL1, TC_LD);
  CHECK (leon2_rd (R_TIMER_TIMER1) == 0x1234);
  CHECK ((leon2_rd (R_TIMER_CTRL1) & TC_EN) == 0); /* LD alone: not started */
}

TEST_CASE_FIXTURE (
    leon2_regs_fixture,
    "LEON2 (current behaviour) the control register keeps bit 3 along "
    "with EN and RL, dropping only LD")
{
  /* timer_ctrl masks with 0xb (0b1011): EN (bit 0) and RL (bit 1), figure
     20's documented read-write fields, plus the undocumented bit 3; LD
     (bit 2) is consumed as a one-shot action rather than stored, matching
     "always reads as a zero" for that one bit.  Bit 3 has no meaning in
     figure 20 at all (reserved); pinning that the mask still keeps it.  */
  leon2_wr (R_TIMER_CTRL1, TC_LD | TC_RL | TC_EN | 0x8);
  CHECK (leon2_rd (R_TIMER_CTRL1) == (TC_RL | TC_EN | 0x8));
}

TEST_CASE_FIXTURE (leon2_regs_fixture,
		   "LEON2 a control write without LD leaves the counter as "
		   "it was")
{
  leon2_wr (R_TIMER_RELOAD1, 0x1234);
  leon2_wr (R_TIMER_CTRL1, TC_LD);	       /* load once */
  leon2_wr (R_TIMER_RELOAD1, 0x9999);	       /* reprogram the reload only */
  leon2_wr (R_TIMER_CTRL1, TC_EN);	       /* no LD this time */
  CHECK (leon2_rd (R_TIMER_TIMER1) == 0x1234); /* unaffected by the reload */
}

TEST_CASE_FIXTURE (
    leon2_regs_fixture,
    "LEON2 (current behaviour, diverges from spec 7.4.1) an underflowing "
    "timer without reload set interrupts but neither stops nor clears "
    "its enable bit")
{
  /* 7.4.1: "otherwise it will stop (at 0xffffffff) and reset the enable
     bit".  gpt_intr decrements gpt_counter[i] and checks it against -1
     when gpt_ctrl[i] & 1 (EN) is set, but its own reload branch
     ("if (gpt_ctrl[i] & 2) gpt_counter[i] = gpt_reload[i]") is the only
     place that ever touches gpt_ctrl[i] or gpt_counter[i] again: with RL
     clear, neither runs, so EN is never cleared and the counter is left
     exactly at the 0xffffffff the underflow produced, still enabled to
     keep counting down from there on the next tick.  Not fixed here; see
     the final report.  */
  leon2_wr (R_TIMER_RELOAD1, 0);
  leon2_wr (R_TIMER_CTRL1, TC_LD | TC_EN); /* no RL: single shot, per spec */
  leon2_wr (R_IRQCTRL_IMR, 0x7ffe);

  advance_time (0x10000); /* one gpt tick: TIMER1 underflows */

  CHECK (ext_irl[0] == 8); /* table 11: Timer 1 is interrupt 8 */
  CHECK ((leon2_rd (R_TIMER_CTRL1) & TC_EN) != 0); /* spec: should be 0 */
  CHECK (leon2_rd (R_TIMER_TIMER1) == 0xffffffffu);
}

TEST_CASE_FIXTURE (leon2_regs_fixture,
		   "spec (7.4.1) an underflowing timer with reload set "
		   "reloads and keeps running")
{
  leon2_wr (R_TIMER_RELOAD2, 0); /* reloads to 0, so the effect is visible */
  leon2_wr (R_TIMER_CTRL2, TC_LD | TC_RL | TC_EN);
  leon2_wr (R_IRQCTRL_IMR, 0x7ffe);

  advance_time (0x10000);

  CHECK (ext_irl[0] == 9); /* table 11: Timer 2 is interrupt 9 */
  CHECK ((leon2_rd (R_TIMER_CTRL2) & TC_EN) != 0);
  CHECK (leon2_rd (R_TIMER_TIMER2) == 0); /* reloaded from TIMER_RELOAD2 */
}

TEST_CASE_FIXTURE (leon2_regs_fixture,
		   "LEON2 (current behaviour) a disabled timer is not "
		   "decremented by a tick")
{
  leon2_wr (R_TIMER_RELOAD1, 7);
  leon2_wr (R_TIMER_CTRL1, TC_LD); /* loaded, not enabled */

  advance_time (1);

  CHECK (leon2_rd (R_TIMER_TIMER1) == 7);
}

TEST_CASE_FIXTURE (leon2_regs_fixture,
		   "LEON2 a tick that does not underflow the counter raises "
		   "no interrupt")
{
  leon2_wr (R_TIMER_RELOAD1, 9);
  leon2_wr (R_TIMER_CTRL1, TC_LD | TC_EN);
  leon2_wr (R_IRQCTRL_IMR, 0x7ffe);

  advance_time (0x10000); /* one tick: 9 -> 8, not -1 */

  CHECK (ext_irl[0] == 0);
  CHECK (leon2_rd (R_TIMER_TIMER1) == 8);
}

TEST_CASE_FIXTURE (leon2_regs_fixture,
		   "spec (7.4.2, figure 21) the scaler reload is masked to "
		   "16 bits by TIMER_SCLOAD")
{
  /* The manual gives the prescaler reload 10 bits; leon2.cc's own
     gpt_scaler_set masks to 0xffff (16 bits) instead -- current
     behaviour, not the documented field width.  */
  leon2_wr (R_TIMER_SCLOAD, 0x1abcd);
  CHECK (leon2_rd (R_TIMER_SCLOAD) == 0xabcd);
}

TEST_CASE_FIXTURE (
    leon2_regs_fixture,
    "LEON2 (current behaviour) TIMER_SCALER is not a write offset")
{
  /* apb_write's switch has no case for TIMER_SCALER (0x060), only
     TIMER_SCLOAD (0x064): a write there falls to the default and changes
     nothing.  boot_init below writes TIMER_SCALER first and TIMER_SCLOAD
     second, so its first write is silently dropped by this same gap.  */
  leon2_wr (R_TIMER_SCLOAD, 100);
  leon2_wr (R_TIMER_SCALER, 5); /* dropped */
  CHECK (leon2_rd (R_TIMER_SCLOAD) == 100);
}

TEST_CASE_FIXTURE (leon2_regs_fixture,
		   "LEON2 the scaler read counts down from TIMER_SCLOAD "
		   "while a timer runs it")
{
  leon2_wr (R_TIMER_SCLOAD, 100);
  leon2_wr (R_TIMER_RELOAD1, 0xffff);
  leon2_wr (R_TIMER_CTRL1, TC_LD | TC_RL | TC_EN); /* starts the gpt event */

  ebase.simtime = 40;
  CHECK (leon2_rd (R_TIMER_SCALER) == 60);
}

TEST_CASE_FIXTURE (leon2_regs_fixture,
		   "LEON2 (current behaviour) the cache control register "
		   "is not reset by leon2_reset")
{
  /* cache_ctrl is a plain file-local static with no reset case in
     leon2_reset, unlike every other register here: whatever an earlier
     case (or boot_init) left it at is still there.  This only checks
     that a write is masked and reads back, not any particular starting
     value.  */
  leon2_wr (R_CACHE_CTRL, 0xffffffffu);
  CHECK (leon2_rd (R_CACHE_CTRL) ==
	 0x1000f); /* masked, apb_write's & 0x1000f */
}

TEST_CASE_FIXTURE (leon2_regs_fixture,
		   "LEON2 (current behaviour) LEON2_CONFIG is a fixed "
		   "read-only value")
{
  CHECK (leon2_rd (R_LEON2_CONFIG) == 0x700310);
}

TEST_CASE_FIXTURE (leon2_regs_fixture,
		   "LEON2 a power-down write enters power-down mode")
{
  uint64 hold_before = sregs[0].hold;
  leon2_wr (R_POWER_DOWN, 0);
  CHECK (sregs[0].pwd_mode == 1);
  CHECK (sregs[0].hold > hold_before);
}

/* Every register of the APB window answers, so no access to it faults and
   none of them costs a waitstate, in contrast to the memory exception an
   address outside RAM, ROM and the window takes.  This holds for a
   register the decode knows and for one it does not.  */
TEST_CASE_FIXTURE (leon2_regs_fixture,
		   "LEON2 an APB access never faults and never waits")
{
  uint32 data = 0;
  int32 ws = 99;

  CHECK (leon2.memory_read (APB + R_LEON2_CONFIG, &data, &ws) == 0);
  CHECK (ws == 0);

  ws = 99;
  CHECK (leon2.memory_read (APB + 0x000, &data, &ws) == 0);
  CHECK (ws == 0);

  ws = 99;
  data = 0;
  CHECK (leon2.memory_write (APB + R_CACHE_CTRL, &data, SZ_WORD, &ws) == 0);
  CHECK (ws == 0);
}

TEST_CASE_FIXTURE (leon2_regs_fixture,
		   "LEON2 (current behaviour) an unimplemented register "
		   "reads as zero and is quiet by default, narrated at "
		   "verbosity above 1")
{
  CHECK (leon2_rd (0x000) == 0); /* no case in apb_read's switch */

  stdout_capture cap;
  leon2_wr (0x000, 0x1234); /* no case in apb_write's switch either */
  CHECK (cap.str ().empty ());

  sis_verbose = 2;
  stdout_capture cap2;
  CHECK (leon2_rd (0x000) == 0);
  leon2_wr (0x000, 0x1234);
  std::string out = cap2.str ();
  CHECK (out.find ("APB read") != std::string::npos);
  CHECK (out.find ("APB write") != std::string::npos);
}

/* ===================================================================== */
/* LEON2 UART.  All bundled in one TEST_CASE, in a fixed SUBCASE order,   */
/* the way tests/apbuart.cc bundles grlib.cc's shared apbuart: the first  */
/* SUBCASE is the only place leon2.cc's own porta is ever unopened, and   */
/* every later SUBCASE constructs a fresh leon2_uart_env, which reopens   */
/* it (safe: uart_port_open resets every flag before it (re)opens).       */
/* ===================================================================== */

TEST_CASE ("LEON2 UART")
{
  SUBCASE ("(current behaviour) before the port is ever opened, reads and "
	   "writes are quiet")
  {
    leon2_regs_fixture env; /* never calls leon2.init_sim: porta stays shut */
    leon2_wr (R_IRQCTRL_IMR, 0x7ffe);

    /* RXTX: aind < anum is false (both start at zero); with the port not
       open the read that would normally follow is skipped, so the stale
       (zero-initialised) queue byte comes back and nothing is raised.  */
    CHECK (leon2_rd (R_APBUART_RXTX) == 0);
    CHECK (ext_irl[0] == 0);

    /* STATUS: the same "not open" branch, so DR (bit 0) stays clear; bits
       1 and 2 are unconditional (7.5.6's TSRE/THRE-shaped bits, though the
       exact status layout is not itself in the excerpted 7.5.6 text this
       file has -- see the case below that cites it fully).  */
    CHECK (leon2_rd (R_APBUART_STATUS) == 0x6);

    /* A write still raises the interrupt (set_irq sits outside the
       "if (porta.open)" guard) but the byte is not queued anywhere.  */
    leon2_wr (R_APBUART_RXTX, 'q');
    CHECK (ext_irl[0] == 3); /* table 11: UART 1 is interrupt 3 */
  }

  SUBCASE ("spec (7.5.6, figure 25/26) STATUS reports data-ready and "
	   "the transmitter/receiver bits, RXTX carries the byte")
  {
    leon2_uart_env env;
    env.host_send ("Z", 1);

    uint32 status = leon2_rd (R_APBUART_STATUS);
    CHECK ((status & 0x1) != 0); /* DR: data ready */
    CHECK ((status & 0x6) == 0x6);

    /* A second STATUS read, with the byte still queued (aind < anum),
       takes the "already have one" branch instead of fetching again: DR
       stays set without a second host read.  */
    CHECK ((leon2_rd (R_APBUART_STATUS) & 0x1) != 0);

    CHECK (leon2_rd (R_APBUART_RXTX) == 'Z');
  }

  SUBCASE ("LEON2 (current behaviour) reading RXTX with one byte queued "
	   "does not raise the interrupt")
  {
    /* Fetching finds exactly one byte, so aind + 1 < anum is false on the
       fetch path itself: the "more remain" check on that side is never
       true here.  */
    leon2_uart_env env;
    leon2_wr (R_IRQCTRL_IMR, 0x7ffe);
    env.host_send ("Z", 1);

    CHECK (leon2_rd (R_APBUART_RXTX) == 'Z');
    CHECK (ext_irl[0] == 0);
  }

  SUBCASE ("LEON2 (current behaviour) reading RXTX with two bytes queued "
	   "raises the interrupt on the fetch itself")
  {
    /* Distinguishes the fetch path's own "more remain" check (aind + 1 <
       anum, right after aind is reset to 0) from the same check on the
       already-queued path: with exactly two bytes, the fetch path must
       itself detect one more remains.  */
    leon2_uart_env env;
    leon2_wr (R_IRQCTRL_IMR, 0x7ffe);
    env.host_send ("XY", 2);

    CHECK (leon2_rd (R_APBUART_RXTX) == 'X');
    CHECK (ext_irl[0] == 3);
  }

  SUBCASE ("LEON2 (current behaviour) reading RXTX walks a multi-byte "
	   "queue, raising the interrupt for every byte but the last")
  {
    /* Three bytes cover both branches of "aind < anum" (false on the
       first read, which does the fetch; true on the second and third)
       and both outcomes of "more remain" on each side of that branch.  */
    leon2_uart_env env;
    leon2_wr (R_IRQCTRL_IMR, 0x7ffe);
    env.host_send ("ABC", 3);

    CHECK (leon2_rd (R_APBUART_RXTX) == 'A'); /* fetch, 2 more remain: irq */
    CHECK (ext_irl[0] == 3);
    leon2_wr (R_IRQCTRL_ICR, 1u << 3);

    CHECK (leon2_rd (R_APBUART_RXTX) == 'B'); /* queued, 1 more remains: irq */
    CHECK (ext_irl[0] == 3);
    leon2_wr (R_IRQCTRL_ICR, 1u << 3);

    CHECK (leon2_rd (R_APBUART_RXTX) == 'C'); /* queued, none remain: no irq */
    CHECK (ext_irl[0] == 0);
  }

  SUBCASE ("LEON2 (current behaviour) reading STATUS with nothing waiting "
	   "does not raise the interrupt, a fresh batch does")
  {
    leon2_uart_env env;
    leon2_wr (R_IRQCTRL_IMR, 0x7ffe);

    CHECK ((leon2_rd (R_APBUART_STATUS) & 0x1) == 0);
    CHECK (ext_irl[0] == 0);

    env.host_send ("Q", 1);
    CHECK ((leon2_rd (R_APBUART_STATUS) & 0x1) != 0);
    CHECK (ext_irl[0] == 3);

    /* grlib_write_uart's STATUS case is empty: a write there is accepted
       and changes nothing observable.  */
    leon2_wr (R_APBUART_STATUS, 0xffffffffu);
    CHECK ((leon2_rd (R_APBUART_STATUS) & 0x6) == 0x6);
  }

  SUBCASE ("spec (7.5.1) a written byte is queued and reaches the host")
  {
    leon2_uart_env env;
    leon2_wr (R_IRQCTRL_IMR, 0x7ffe);

    leon2_wr (R_APBUART_RXTX, 'x');
    CHECK (ext_irl[0] == 3);

    char c;
    /* flush_uart runs on the periodic uart_intr event (armed by
       leon2.reset's uart_irq_start) and directly through sim_halt.  */
    leon2.sim_halt ();
    CHECK (env.host_recv (&c, 1) == 1);
    CHECK (c == 'x');
  }

  SUBCASE ("LEON2 (current behaviour) a full write buffer flushes to the "
	   "host immediately")
  {
    leon2_uart_env env;

    for (int i = 0; i < (int) UARTBUF; i++)
      leon2_wr (R_APBUART_RXTX, 'a');
    leon2_wr (R_APBUART_RXTX, 'b'); /* buffer was exactly full: flush first */

    leon2.sim_halt ();

    char buf[UARTBUF + 1];
    REQUIRE (env.host_recv (buf, sizeof (buf)) == (int) sizeof (buf));
    CHECK (buf[0] == 'a');
    CHECK (buf[UARTBUF - 1] == 'a');
    CHECK (buf[UARTBUF] == 'b');
  }

  SUBCASE ("LEON2 the periodic UART poll runs from the event queue and "
	   "reschedules itself")
  {
    leon2_uart_env env;

    leon2_wr (R_APBUART_RXTX, 'v');
    advance_time (3000); /* UART_FLUSH_TIME: runs uart_intr once */

    char c;
    CHECK (env.host_recv (&c, 1) == 1);
    CHECK (c == 'v');

    advance_time (6000); /* still rescheduling itself */
  }

  SUBCASE ("LEON2 exit_sim closes the port, stranding anything still "
	   "queued")
  {
    leon2_uart_env env;

    /* Queued while the port is still open, so it sits in wbufa unflushed.  */
    leon2_wr (R_APBUART_RXTX, 'y');

    leon2.exit_sim ();

    /* flush_uart's "wnuma && porta.open" now has data queued but a closed
       port: the loop does not run, and the byte is stranded rather than
       delivered late.  */
    leon2.sim_halt ();
    char c;
    CHECK (env.host_recv (&c, 1) == 0);

    /* A write after close is quiet too (matches the "before ever opened"
       SUBCASE's own porta.open == false path).  */
    leon2_wr (R_APBUART_RXTX, 'z');
    CHECK (env.host_recv (&c, 1) == 0);
  }

  SUBCASE ("LEON2 mem_init/port_init/gpt_init narrate when verbose")
  {
    leon2_uart_host_file host;
    int saved_verbose = sis_verbose;
    int saved_cputype = cputype;
    int saved_archtype = archtype;

    cputype = CPU_LEON2;
    archtype = CPU_SPARC;
    ms = &leon2;
    sis_verbose = 1;

    stdout_capture cap;
    leon2.init_sim ();
    std::string out = cap.str ();
    CHECK (out.find ("RAM start") != std::string::npos);   /* mem_init */
    CHECK (out.find ("GPT started") != std::string::npos); /* gpt_init */

    reset_all ();
    init_bpt (sregs);
    sis_verbose = saved_verbose;
    cputype = saved_cputype;
    archtype = saved_archtype;
  }
}

/* ===================================================================== */
/* LEON2 memory map                                                      */
/* ===================================================================== */

TEST_CASE_FIXTURE (leon2_regs_fixture, "LEON2 RAM read and write")
{
  uint32 data = 0x12345678;
  int32 ws;

  CHECK (leon2.memory_write (0x40001000, &data, SZ_WORD, &ws) == 0);
  data = 0;
  CHECK (leon2.memory_read (0x40001000, &data, &ws) == 0);
  CHECK (data == 0x12345678);
  CHECK (leon2.memory_iread (0x40001000, &data, &ws) == 0);
  CHECK (data == 0x12345678);
}

TEST_CASE_FIXTURE (leon2_regs_fixture, "LEON2 ROM read and write")
{
  uint32 data = 0xdeadbeef;
  int32 ws;

  CHECK (leon2.memory_write (0x100, &data, SZ_WORD, &ws) == 0);
  data = 0;
  CHECK (leon2.memory_read (0x100, &data, &ws) == 0);
  CHECK (data == 0xdeadbeef);
  CHECK (leon2.memory_iread (0x100, &data, &ws) == 0);
  CHECK (data == 0xdeadbeef);
}

TEST_CASE_FIXTURE (leon2_regs_fixture,
		   "LEON2 store_bytes writes a byte, a half-word and a "
		   "double word in host byte order")
{
  uint32 data;
  int32 ws;

  /* store_bytes XORs the index for a byte (^3) and a half-word (^2) on a
     little endian host (config.h's HOST_LITTLE_ENDIAN, true here per
     ./waf configure's own "Checking for endianness: little"), so the
     byte/half-word land at the XORed offset within ramb, not at waddr
     itself; the double word below has no such adjustment.  */
  data = 0x000000abu;
  CHECK (leon2.memory_write (0x40001100, &data, 0, &ws) == 0); /* byte */
  CHECK ((unsigned char) ramb[0x1100 ^ 3] == 0xab);

  data = 0x0000cdefu;
  CHECK (leon2.memory_write (0x40001200, &data, 1, &ws) == 0); /* half-word */
  CHECK (*(uint16 *) &ramb[0x1200 ^ 2] == 0xcdef);

  uint32 dbl[2] = { 0x11111111u, 0x22222222u };
  ws = 99;
  CHECK (leon2.memory_write (0x40001300, dbl, 3, &ws) == 0); /* double word */
  CHECK (memcmp (&ramb[0x1300], dbl, 8) == 0);

  /* leon2.cc models none of the memory configuration registers section 6
     gives waitstates in, and runs memory at none, so every size reports
     zero.  This is the simulator's contract, not the hardware's.  */
  CHECK (ws == 0);
  for (int32 sz = 0; sz < 4; sz++)
    {
      CAPTURE (sz);
      ws = 99;
      CHECK (leon2.memory_write (0x40001400, dbl, sz, &ws) == 0);
      CHECK (ws == 0);
    }

  /* The same three sizes on the ROM side of memory_write's own dispatch.  */
  CHECK (leon2.memory_write (0x200, &data, 0, &ws) == 0);
  CHECK (leon2.memory_write (0x204, &data, 1, &ws) == 0);
  CHECK (leon2.memory_write (0x208, dbl, 3, &ws) == 0);
}

TEST_CASE_FIXTURE (
    leon2_regs_fixture,
    "LEON2 an address outside RAM/ROM/APB is a memory exception")
{
  uint32 data;
  int32 ws;

  CHECK (leon2.memory_read (0x20000000, &data, &ws) == 1);
  CHECK (ws == 1);
  CHECK (leon2.memory_iread (0x20000000, &data, &ws) == 1);
  CHECK (leon2.memory_write (0x20000000, &data, SZ_WORD, &ws) == 1);

  /* An address at or above RAM_END is the other side of memory_iread's
     "addr >= RAM_START && addr < RAM_END": the first half of the
     condition is true here, unlike 0x20000000 above, which fails it
     already.  */
  CHECK (leon2.memory_iread (0x42000000, &data, &ws) == 1);

  /* Likewise for memory_read/memory_write's "addr >= APBSTART &&
     addr < APBEND": an address at or above APBEND but still short of
     ROM_END's wraparound (moot here, ROM_END is far below APBSTART) is
     the other side.  */
  CHECK (leon2.memory_read (APB + 0x100, &data, &ws) == 1);
  CHECK (leon2.memory_write (APB + 0x100, &data, SZ_WORD, &ws) == 1);

  sis_verbose = 1;
  stdout_capture cap_iread;
  leon2.memory_iread (0x20000000, &data, &ws);
  CHECK (cap_iread.str ().find ("Memory exception") != std::string::npos);

  stdout_capture cap;
  leon2.memory_read (0x20000000, &data, &ws);
  CHECK (cap.str ().find ("Memory exception") != std::string::npos);
}

TEST_CASE_FIXTURE (leon2_regs_fixture,
		   "LEON2 an APB byte/half-word write is a memory exception, "
		   "a word write reaches the register file")
{
  uint32 data = 5;
  int32 ws;

  CHECK (leon2.memory_write (APB + R_CACHE_CTRL, &data, 0, &ws) == 1);
  CHECK (leon2.memory_write (APB + R_CACHE_CTRL, &data, 1, &ws) == 1);
  CHECK (leon2.memory_write (APB + R_CACHE_CTRL, &data, SZ_WORD, &ws) == 0);
}

TEST_CASE_FIXTURE (
    leon2_regs_fixture,
    "LEON2 (current behaviour) get_mem_ptr and set_irq are absent from "
    "the board's memsys table")
{
  /* leon2.cc's own struct memsys initialiser lists 13 members in
     declaration order and stops at boot_init; get_mem_ptr and set_irq,
     the last two fields of struct memsys (sis.h), are left to aggregate
     initialisation's default of a null pointer.  leon2.cc does define a
     file-local get_mem_ptr, but only sis_memory_read/sis_memory_write
     call it directly, never through this table; nothing in the tree
     calls ms->get_mem_ptr or ms->set_irq for LEON2.  Unlike leon3.cc,
     gr740.cc and rv32.cc, which all wire both fields.  Not fixed here;
     see the final report.  */
  CHECK (leon2.get_mem_ptr == NULL);
  CHECK (leon2.set_irq == NULL);
}

TEST_CASE_FIXTURE (
    leon2_regs_fixture,
    "LEON2 (current behaviour) sis_memory_write reaches only RAM/ROM, "
    "unlike leon3.cc's own sis_memory_write it has no length-4 fallback "
    "to memory_write for anything else")
{
  char data[4] = { 1, 2, 3, 4 };

  CHECK (leon2.sis_memory_write (0x40001000, data, 4) == 4);
  CHECK (memcmp (&ramb[0x1000], data, 4) == 0);

  /* get_mem_ptr finds nothing outside RAM/ROM (the APB window is not
     reachable through get_mem_ptr at all): sis_memory_write's only other
     branch returns 0 unconditionally, so this is quiet even at length 4.
     cache_ctrl is never reset by leon2.reset, so this checks that the
     write leaves it exactly as it was rather than assuming a fixed
     starting value.  */
  uint32 before = leon2_rd (R_CACHE_CTRL);
  uint32 word = before ^ 0xffffffffu; /* whatever it is, write the opposite */
  CHECK (leon2.sis_memory_write (APB + R_CACHE_CTRL, (char *) &word, 4) == 0);
  CHECK (leon2_rd (R_CACHE_CTRL) == before); /* untouched */

  char one = 9;
  CHECK (leon2.sis_memory_write (0x20000000, &one, 1) == 0);
}

TEST_CASE_FIXTURE (leon2_regs_fixture,
		   "LEON2 sis_memory_read take the length-4 shortcut, "
		   "otherwise falls back to get_mem_ptr")
{
  char buf[4];

  uint32 word = 0x11223344;
  int32 ws;
  leon2.memory_write (0x40002000, &word, SZ_WORD, &ws);
  CHECK (leon2.sis_memory_read (0x40002000, buf, 4) == 4);
  CHECK (memcmp (buf, &word, 4) == 0);

  char one;
  CHECK (leon2.sis_memory_read (0x100, &one, 1) ==
	 1); /* ROM, via get_mem_ptr */
  CHECK (leon2.sis_memory_read (0x20000000, &one, 1) == 0); /* neither */
}

TEST_CASE_FIXTURE (leon2_regs_fixture, "LEON2 boot_init programs the timer "
				       "and the initial register file")
{
  leon2.boot_init ();

  CHECK (sregs[0].wim == 2);
  CHECK (sregs[0].psr == 0x000010e0u);
  CHECK (sregs[0].r[30] == 0x42000000u); /* RAM_END */
  CHECK (sregs[0].r[14] == sregs[0].r[30] - 96 * 4);
  CHECK (leon2_rd (R_CACHE_CTRL) == 0x01000f);

  /* TIMER_SCALER's write is dropped (see the dedicated case above), so
     only TIMER_SCLOAD's later write sets the reload.  boot_init writes
     TIMER_CTRL1 = 0x7 (LD | RL | EN), but timer_ctrl's own 0xb mask drops
     LD (see "the control register keeps bit 3..." above), so it reads
     back as RL | EN = 0x3.  */
  CHECK (leon2_rd (R_TIMER_SCLOAD) == ebase.freq - 1);
  CHECK (leon2_rd (R_TIMER_TIMER1) == 0xffffffffu);
  CHECK (leon2_rd (R_TIMER_CTRL1) == 0x3);
}

TEST_CASE_FIXTURE (leon2_regs_fixture, "LEON2 error_mode does nothing "
				       "observable")
{
  leon2.error_mode (0x1000);
  CHECK (true);
}

TEST_CASE_FIXTURE (leon2_regs_fixture,
		   "LEON2 init_stdio/restore_stdio reach the port's raw/"
		   "restore calls without crashing on an unopened port")
{
  /* uart_port_raw/uart_port_restore (uartport.cc, already graduated) are
     no-ops on a port whose ifd is never 0 -- this fixture never calls
     leon2.init_sim, so porta is exactly that.  This is only here for
     leon2.cc's own init_stdio/restore_stdio lines, which do nothing but
     forward to them.  */
  leon2.init_stdio ();
  leon2.restore_stdio ();
  CHECK (true);
}

/* ===================================================================== */
/* LEON3                                                                 */
/* ===================================================================== */

TEST_CASE_FIXTURE (leon3_fixture, "LEON3 RAM read and write")
{
  uint32 data = 0x12345678;
  int32 ws;

  CHECK (leon3.memory_write (LEON3_RAM_START + 0x1000, &data, SZ_WORD, &ws) ==
	 0);
  CHECK (ws == 0);
  data = 0;
  CHECK (leon3.memory_read (LEON3_RAM_START + 0x1000, &data, &ws) == 0);
  CHECK (data == 0x12345678);
  CHECK (ws == 0);
}

TEST_CASE_FIXTURE (leon3_fixture, "LEON3 ROM read and write, with the "
				  "documented read waitstate")
{
  uint32 data = 0xdeadbeef;
  int32 ws;

  CHECK (leon3.memory_write (0x100, &data, SZ_WORD, &ws) == 0);
  CHECK (ws == 0);
  data = 0;
  CHECK (leon3.memory_read (0x100, &data, &ws) == 0);
  CHECK (data == 0xdeadbeef);
  CHECK (ws == 2);
}

TEST_CASE_FIXTURE (
    leon3_fixture,
    "LEON3 outside RAM/ROM reaches grlib_read/grlib_write: a mounted "
    "slave answers, an address nothing claims is a memory exception")
{
  uint32 data = 0;
  int32 ws;
  int reads_before = fake_ahb_read_calls;
  int writes_before = fake_ahb_write_calls;

  CHECK (leon3.memory_read (FAKE_AHB_BASE + 4, &data, &ws) == 0);
  CHECK (data == (0x5eed0000u | 4u));
  CHECK (ws == 4);
  CHECK (fake_ahb_read_calls == reads_before + 1);

  data = 0x99;
  CHECK (leon3.memory_write (FAKE_AHB_BASE + 8, &data, SZ_WORD, &ws) == 0);
  CHECK (ws == 4);
  CHECK (fake_ahb_write_calls == writes_before + 1);

  /* An address inside neither RAM/ROM nor the fake slave's 1 MB window is
     a memory exception.  Quiet, ws stays 4 (set unconditionally before the
     verbose-gated printf branch) when not verbose...  */
  CHECK (leon3.memory_read (0x60000000, &data, &ws) == 1);
  CHECK (ws == 4);
  CHECK (leon3.memory_write (0x60000000, &data, SZ_WORD, &ws) == 1);
  CHECK (ws == 4);

  /* ...narrated, and ws downgraded to MEM_EX_WS (1), when verbose.  */
  sis_verbose = 1;
  stdout_capture cap;
  CHECK (leon3.memory_read (0x60000000, &data, &ws) == 1);
  CHECK (ws == 1);
  CHECK (cap.str ().find ("Memory exception") != std::string::npos);

  stdout_capture cap2;
  CHECK (leon3.memory_write (0x60000000, &data, SZ_WORD, &ws) == 1);
  CHECK (ws == 1);
  CHECK (cap2.str ().find ("Memory exception") != std::string::npos);

  /* Still verbose, but this time a successful access (mexc 0): the
     "sis_verbose && mexc" check evaluates both operands and comes out
     false, quiet either way.  */
  CHECK (leon3.memory_read (FAKE_AHB_BASE, &data, &ws) == 0);
  CHECK (leon3.memory_write (FAKE_AHB_BASE, &data, SZ_WORD, &ws) == 0);
}

TEST_CASE_FIXTURE (leon3_fixture, "LEON3 get_mem_ptr for RAM, ROM and "
				  "neither")
{
  char *ram_ptr = leon3.get_mem_ptr (LEON3_RAM_START + 0x1000, 4);
  char *rom_ptr = leon3.get_mem_ptr (0x100, 4);
  char *miss_ptr = leon3.get_mem_ptr (0x60000000, 4);

  CHECK ((void *) ram_ptr == (void *) &ramb[0x1000]);
  CHECK ((void *) rom_ptr == (void *) &romb[0x100]);
  CHECK (miss_ptr == NULL);
}

TEST_CASE_FIXTURE (
    leon3_fixture,
    "LEON3 sis_memory_write reaches RAM/ROM directly, and a 4 byte write "
    "outside them falls back to memory_write")
{
  char data[4] = { 5, 6, 7, 8 };

  CHECK (leon3.sis_memory_write (LEON3_RAM_START + 0x2000, data, 4) == 4);
  CHECK (memcmp (&ramb[0x2000], data, 4) == 0);

  int writes_before = fake_ahb_write_calls;
  uint32 word = 0x2a;
  CHECK (leon3.sis_memory_write (FAKE_AHB_BASE, (char *) &word, 4) == 0);
  CHECK (fake_ahb_write_calls == writes_before + 1);

  char one = 9;
  CHECK (leon3.sis_memory_write (0x60000000, &one, 1) == 0);
}

TEST_CASE_FIXTURE (leon3_fixture,
		   "LEON3 sis_memory_read takes the length-4 shortcut, "
		   "otherwise falls back to get_mem_ptr")
{
  char buf[4];

  uint32 word = 0x44332211;
  int32 ws;
  leon3.memory_write (LEON3_RAM_START + 0x3000, &word, SZ_WORD, &ws);
  CHECK (leon3.sis_memory_read (LEON3_RAM_START + 0x3000, buf, 4) == 4);
  CHECK (memcmp (buf, &word, 4) == 0);

  char one;
  CHECK (leon3.sis_memory_read (0x100, &one, 1) == 1);	    /* ROM */
  CHECK (leon3.sis_memory_read (0x60000000, &one, 1) == 0); /* neither */
}

TEST_CASE_FIXTURE (leon3_fixture,
		   "LEON3 boot_init programs every CPU's initial register "
		   "file")
{
  leon3.boot_init ();

  for (int i = 0; i < NCPU; i++)
    {
      CHECK (sregs[i].wim == 2);
      CHECK (sregs[i].psr == 0xF30010e0u);
      CHECK (sregs[i].r[30] == LEON3_RAM_END - (uint32) (i * 0x20000));
      CHECK (sregs[i].r[14] == sregs[i].r[30] - 96 * 4);
      CHECK (sregs[i].cache_ctrl == 0x81000fu);
      CHECK (sregs[i].r[2] == sregs[i].r[30]);
    }
}

TEST_CASE_FIXTURE (leon3_fixture, "LEON3 error_mode/sim_halt/reset/exit_sim "
				  "do nothing observable")
{
  /* error_mode's body is empty; sim_halt's is compiled out entirely
     without FAST_UART (not defined anywhere in this tree); reset just
     forwards to grlib_reset, already exercised by tests/grlibbus.cc's own
     delta-checked cases; exit_sim's apbuart_close_port is a safe no-op on
     a port that was never opened (uart_port_close's own "if (!port->open)
     return" -- and nothing in this file ever opens the shared apbuart
     port, since that only happens through leon3.init_sim, which this file
     deliberately never calls; see the file header).  */
  leon3.error_mode (0x2000);
  leon3.sim_halt ();
  leon3.reset ();
  leon3.exit_sim ();
  CHECK (true);
}

TEST_CASE_FIXTURE (leon3_fixture, "LEON3 set_irq forwards to grlib_set_irq")
{
  /* leon3's memsys set_irq field is grlib_set_irq itself (no leon3.cc code
     of its own); tests/irqmp.cc and tests/apbuart.cc already cover
     grlib_set_irq's own logic.  This only pins that the memsys table
     really points at it and that calling it through leon3 does not
     crash.  */
  leon3.set_irq (5);
  CHECK (true);
}
