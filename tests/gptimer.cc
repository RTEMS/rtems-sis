/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Tests for the GPTIMER general purpose timer unit in grlib.cc.

   ref/grlib-ip-core-manual-scoped.md, chapter "455-464: GPTIMER", only
   covers three things: the Scaler Value Register (40.3.1), the Scaler
   Reload Value Register (40.3.2), and that reset exists in either a
   synchronous or an asynchronous flavour (40.5.1).  The timer counter,
   reload and control registers, the per-timer interrupt, and the
   scheduling of the underflow event are not in the scoped manual at all.
   Cases that assert on the scaler are marked "spec" and cite the section.
   Cases that assert on the rest pin grlib.cc's current behaviour instead
   of a written requirement, and say so in their name and comment.

   A case drives the core through its own register file with no board, the
   way tests/irqmp.cc does.  Interrupt cases also drive the irqmp core
   directly, the same way, to read the level out of ext_irl.  */

#include "doctest.h"
#include "support.h"

#include "config.h"
#include "grlibcore.h"

namespace
{

/* Register offsets, from grlib.cc's GPTIMER_* macros and table 464/465 of
   the manual for the first two.  */
const uint32 SCALER = 0x00;
const uint32 SCLOAD = 0x04;
const uint32 CONFIG = 0x08;
const uint32 TIMER1 = 0x10;
const uint32 RELOAD1 = 0x14;
const uint32 CTRL1 = 0x18;
const uint32 TIMER2 = 0x20;
const uint32 RELOAD2 = 0x24;
const uint32 CTRL2 = 0x28;

/* Control register bits, read back off grlib.cc's gpt_ctrl_write: not in
   the scoped manual, transcribed from the code under test.  */
const uint32 EN = 1;
const uint32 RS = 2;
const uint32 LD = 4;
const uint32 IE = 8;
const uint32 IP = 0x10;

/* The irqmp register this file drives directly, from tests/irqmp.cc.  */
const uint32 IMASK = 0x40;

/* The interrupt line GPTIMER is wired to in this fixture.  Chosen away
   from both 0 (irqmp treats interrupt 0 as nonexistent) and from the
   timer count, so timer 1 and timer 2 land on different, unambiguous
   levels.  */
const int GPT_IRQ = 5;

void
irqmp_write (uint32 offset, uint32 value)
{
  irqmp.write (offset, &value, 2);
}

struct gptimer_fixture : sis_tests::grlib_core_fixture
{
  gptimer_fixture () : sis_tests::grlib_core_fixture (&gptimer)
  {
    /* core->add only records the plug&play id and the irq line GPTIMER
       reports itself on; it does not touch the AHB/APB bus registration
       arrays the setup instructions rule out, so it is safe to call from
       a fixture that runs once per case.  Without it gpt_irq stays at
       whatever a previous test file left it.  */
    core->add (GPT_IRQ, 0, 0);

    /* irqmp is a second core reached directly, exactly as
       grlib_core_fixture reaches gptimer, so an interrupt case can read
       the result off ext_irl.  */
    irqmp.init ();
    irqmp.reset ();
    irqmp_write (IMASK, 0xfffe);

    for (int i = 0; i < NCPU; i++)
      ext_irl[i] = 0;
  }

  ~gptimer_fixture ()
  {
    for (int i = 0; i < NCPU; i++)
      ext_irl[i] = 0;
  }
};

}

TEST_CASE_FIXTURE (gptimer_fixture,
		   "GPTIMER spec: the scaler resets to all ones")
{
  /* Table 464 (40.3.1) and table 465 (40.3.2) both give "all 1" as the
     reset default of the 16 bit value field.  */
  CHECK (read (SCALER) == 0xffff);
  CHECK (read (SCLOAD) == 0xffff);
}

TEST_CASE_FIXTURE (gptimer_fixture,
		   "GPTIMER spec: unused scaler bits always read as zero")
{
  /* 40.3.1 and 40.3.2: "Any unused most significant bits are reserved.
     Always reads as '000...0'."  Bits 31:16 are outside the 16 bit field
     of either register.  */
  write (SCLOAD, 0xffff1234);
  CHECK (read (SCLOAD) == 0x1234);
  CHECK ((read (SCALER) & 0xffff0000) == 0);
}

TEST_CASE_FIXTURE (
    gptimer_fixture,
    "GPTIMER spec: a write to the reload register also sets the scaler")
{
  /* 40.3.1: "This value will also be set by writes to the Scaler reload
     value register."  No time has passed since the write, so the scaler
     has not yet counted down from it.  */
  write (SCLOAD, 0x2222);
  CHECK (read (SCALER) == 0x2222);
}

TEST_CASE_FIXTURE (
    gptimer_fixture,
    "GPTIMER pin: timer 1 wraps to all ones on underflow, not to zero")
{
  /* Not in the scoped manual.  grlib.cc's gpt_intr sets gpt_counter[i] to
     -1 on underflow.  A scaler of 0 gives a one clock tick, so a counter
     of 2 underflows after 3 ticks.  */
  write (SCLOAD, 0);
  write (RELOAD1, 0);
  write (TIMER1, 2);
  write (CTRL1, EN);

  run (3);
  CHECK (read (TIMER1) == 0xffffffff);
}

TEST_CASE_FIXTURE (
    gptimer_fixture,
    "GPTIMER pin: the restart bit reloads the counter on underflow")
{
  /* Not in the scoped manual.  RS (bit 1) makes gpt_intr copy the reload
     register into the counter instead of leaving it wrapped.  */
  write (SCLOAD, 0);
  write (RELOAD1, 5);
  write (TIMER1, 1);
  write (CTRL1, EN | RS);

  run (2);
  CHECK (read (TIMER1) == 5);
}

TEST_CASE_FIXTURE (gptimer_fixture,
		   "GPTIMER pin: the load bit reloads the counter immediately")
{
  /* Not in the scoped manual.  LD (bit 2) is an action bit: gpt_ctrl_write
     copies the reload register into the counter on the write itself, even
     with the timer disabled, and is not stored back into the control
     register.  */
  write (RELOAD1, 42);
  write (TIMER1, 99);
  write (CTRL1, LD);

  CHECK (read (TIMER1) == 42);
  CHECK (read (CTRL1) == 0);
}

TEST_CASE_FIXTURE (gptimer_fixture,
		   "GPTIMER pin: timer 1 raises its own interrupt line")
{
  /* Not in the scoped manual.  With IE (bit 3) set, gpt_intr sets the IP
     status bit and calls grlib_set_irq on gpt_irq plus the timer index,
     which is the path every GRLIB core takes to reach ext_irl.  */
  write (SCLOAD, 0);
  write (RELOAD1, 0);
  write (TIMER1, 0);
  write (CTRL1, EN | IE);

  run (1);
  CHECK ((read (CTRL1) & IP) != 0);
  CHECK (ext_irl[0] == GPT_IRQ);
}

TEST_CASE_FIXTURE (gptimer_fixture,
		   "GPTIMER pin: timer 2 raises a different line than timer 1")
{
  /* Not in the scoped manual.  gpt_intr raises gpt_irq + i, so the two
     timers in this unit are distinguishable interrupt sources.  */
  write (SCLOAD, 0);
  write (RELOAD2, 0);
  write (TIMER2, 0);
  write (CTRL2, EN | IE);

  run (1);
  CHECK ((read (CTRL2) & IP) != 0);
  CHECK (ext_irl[0] == GPT_IRQ + 1);
}

TEST_CASE_FIXTURE (
    gptimer_fixture,
    "GPTIMER pin: a disabled timer neither counts down nor interrupts")
{
  /* Not in the scoped manual.  Without EN (bit 0) gpt_add_intr never
     arms an event, so the counter read back is the raw value written and
     no interrupt appears no matter how long the case runs.  */
  write (SCLOAD, 0);
  write (RELOAD1, 0);
  write (TIMER1, 0);
  write (CTRL1, IE);

  run (10);
  CHECK (read (TIMER1) == 0);
  CHECK (ext_irl[0] == 0);
}

TEST_CASE_FIXTURE (
    gptimer_fixture,
    "GPTIMER pin: changing the scaler reschedules every running timer")
{
  /* Not in the scoped manual.  gpt_scaler_set cancels every pending
     underflow with remove_event(gpt_intr, -1) and rearms every timer with
     gpt_add_intr_all, so a scaler change mid flight retimes an already
     running timer instead of leaving its old schedule in place.  Timer 1
     is armed for a period 10 tick; changing the scaler to a period 2 tick
     after 5 ticks have passed must make it underflow 2 ticks later, at
     tick 7, not at the original tick 10.  */
  write (SCLOAD, 9);
  write (RELOAD1, 0);
  write (TIMER1, 0);
  write (CTRL1, EN | IE);

  run (5);
  CHECK ((read (CTRL1) & IP) == 0);

  write (SCLOAD, 1);
  run (7);
  CHECK ((read (CTRL1) & IP) != 0);
}

TEST_CASE_FIXTURE (
    gptimer_fixture,
    "GPTIMER pin: rewriting one timer's counter leaves the other's "
    "schedule alone")
{
  /* Not in the scoped manual.  Writing TIMER1 only calls
     remove_event(gpt_intr, 0), the single-argument form, so timer 2's
     already scheduled underflow is untouched.  */
  write (SCLOAD, 0);
  write (RELOAD1, 0);
  write (TIMER1, 0);
  write (CTRL1, EN | IE);
  write (RELOAD2, 0);
  write (TIMER2, 0);
  write (CTRL2, EN | IE);

  /* Both timers were armed for a one tick underflow.  Rearm timer 1 for a
     later one before either fires.  */
  write (TIMER1, 4);

  run (1);
  CHECK ((read (CTRL1) & IP) == 0);
  CHECK ((read (CTRL2) & IP) != 0);
}

TEST_CASE_FIXTURE (gptimer_fixture,
		   "GPTIMER pin: the config register reports two timers")
{
  /* Not in the scoped manual: chapter 40 has no 40.3.3 in the excerpt
     that ships in ref/.  grlib.cc hard codes two timers per unit (macro
     NGPTIMERS) and packs the irq line GPTIMER was added on into the same
     word.  */
  CHECK ((read (CONFIG) & 0x7) == 2);
  CHECK (((read (CONFIG) >> 3) & 0x1f) == GPT_IRQ);
}

TEST_CASE_FIXTURE (
    gptimer_fixture,
    "GPTIMER pin: writing the interrupt pending bit back clears it")
{
  /* Not in the scoped manual.  gpt_ctrl_write clears IP whenever bit 4 of
     the written value is set, which is the normal write-1-to-clear
     pattern a driver uses by writing back what it just read.  */
  write (SCLOAD, 0);
  write (RELOAD1, 0);
  write (TIMER1, 0);
  write (CTRL1, EN | IE);

  run (1);
  REQUIRE ((read (CTRL1) & IP) != 0);

  write (CTRL1, read (CTRL1));
  CHECK ((read (CTRL1) & IP) == 0);
}

TEST_CASE_FIXTURE (gptimer_fixture,
		   "GPTIMER disabling a timer cancels its underflow")
{
  /* A write which clears the enable bit stops the timer, so the callback
     scheduled for the old deadline has to go with it.  Left queued it
     still arrives and wraps the counter, which would let a stopped timer
     change its own value.  */
  write (SCLOAD, 0);
  write (RELOAD1, 0);
  write (TIMER1, 3);
  write (CTRL1, EN);

  write (CTRL1, 0);
  CHECK (read (TIMER1) == 3);

  run (4);
  CHECK (read (TIMER1) == 3);
  CHECK (ext_irl[0] == 0);

  /* Enabling it again schedules a fresh underflow from where it stood.  */
  write (CTRL1, EN);
  run (8);
  CHECK (read (TIMER1) != 3);
}

TEST_CASE_FIXTURE (gptimer_fixture,
		   "GPTIMER pin: writing the same scaler value is a no-op")
{
  /* Not in the scoped manual.  gpt_scaler_set only cancels and reschedules
     every timer's underflow when the masked value actually differs from
     the current one.  gpt_add_intr computes the next deadline from the
     raw counter register, not from how much of the count has already
     elapsed, so a reschedule that runs anyway would restart the timer
     from its full period instead of leaving the original deadline alone.
     Arm a 10 tick underflow, let 5 of them pass, then write the scaler's
     current value back: the timer must still underflow 5 ticks later, at
     tick 10, not 10 ticks later at tick 15.  */
  write (SCLOAD, 0);
  write (RELOAD1, 0);
  write (TIMER1, 9);
  write (CTRL1, EN | IE);

  run (5);
  CHECK ((read (CTRL1) & IP) == 0);

  write (SCLOAD, 0);
  run (5);
  CHECK ((read (CTRL1) & IP) != 0);
}

TEST_CASE_FIXTURE (gptimer_fixture,
		   "GPTIMER the scaler value register is writable")
{
  /* Table 464 (40.3.1) marks the Scaler Value Register "rw", and table 465
     says a write to the reload register sets the scaler too.  This model
     keeps one value for both, so either address sets it.  */
  write (SCALER, 0x1234);
  CHECK (read (SCALER) == 0x1234);

  write (SCLOAD, 0x0abc);
  CHECK (read (SCALER) == 0x0abc);

  /* The scaler is sixteen bits wide, so the value above them is dropped.  */
  write (SCALER, 0xdead1234);
  CHECK (read (SCALER) == 0x1234);
}

TEST_CASE_FIXTURE (gptimer_fixture,
		   "GPTIMER pin: an unmapped offset reads and writes as zero")
{
  /* Not in the scoped manual.  gpt_read's default case returns 0 and
     gpt_write has no default case at all, so an offset outside the
     register file is silently accepted and changes nothing observable.  */
  const uint32 UNMAPPED = 0x30;

  write (UNMAPPED, 0xdeadbeef);
  CHECK (read (UNMAPPED) == 0);
}
