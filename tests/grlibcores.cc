/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Tests for three small GRLIB cores in grlib.cc: the L2 cache controller
   (l2c), the LEON3 Debug Support Unit (dsu), and the AHB/APB bridge
   (apbmst / apbmst2).

   The expectations for l2c and dsu come from the GRLIB IP core manual in
   ref/: chapter "1582-1606: L2C" sections 98.4.1, 98.4.2 and 98.4.16 for
   l2c, and chapter "267-282: DSU3" section 32.6.5 for the dsu time tag
   counter.  Where grlib.cc implements a register the manual documents but
   does not model any state for, the case says so and pins the current
   fallback rather than a specified value.

   The APBCTRL chapter ("115-119: APBCTRL") in the scoped manual has no
   register table at all, only a VHDL component declaration and an
   instantiation example (16.9, 16.10).  The plug&play area layout the
   bridge tests below rely on (0xFF000, 8 bytes per entry) is therefore
   taken from grlib.cc itself, not from ref/, and is pinned rather than
   verified against a specification.  */

#include "doctest.h"
#include "support.h"

#include "config.h"
#include "grlibcore.h"

/* greth.cc defines this as a plain global with no header of its own, and
   grlib.cc's greth_add reaches it the same way; see tests/greth.cc.  The
   declaration has to sit outside the anonymous namespace below, which
   would otherwise give it internal linkage and make it a different object
   from the one greth_add writes.  */
extern int greth_irq;

namespace
{

/* ------------------- L2C ----------------------- */

/* Register offsets from section 98's subsection numbers.  */
const uint32 L2C_CTRL = 0x00;	/* 98.4.1 */
const uint32 L2C_STATUS = 0x04; /* 98.4.2 */
const uint32 L2C_ACCC = 0x3c;	/* 98.4.16 */
const uint32 L2C_ACC = 0x10;	/* 98.4.5, not implemented by grlib.cc */

struct l2c_fixture : sis_tests::grlib_core_fixture
{
  l2c_fixture () : sis_tests::grlib_core_fixture (&l2c) {}
};

/* ------------------- DSU ----------------------- */

const uint32 DSU_DTTC = 0x08; /* 32.6.5 */
const uint32 DSU_CTRL = 0x00; /* Not implemented by grlib.cc */

struct dsu_fixture : sis_tests::grlib_core_fixture
{
  int saved_cpu;

  dsu_fixture () : sis_tests::grlib_core_fixture (&dsu), saved_cpu (cpu)
  {
    /* dsu_read answers with sregs[cpu].simtime, where cpu is the active
       debug cpu a case selects independently of ncpu.  Pin it to the one
       CPU the fixture sets up.  */
    cpu = 0;
  }

  ~dsu_fixture () { cpu = saved_cpu; }
};

/* ------------------- APBMST / APBMST2 ----------------------- */

struct apbmst_fixture : sis_tests::grlib_core_fixture
{
  apbmst_fixture () : sis_tests::grlib_core_fixture (&apbmst) {}
};

/* A minimal core mounted on the bridge to exercise dispatch and the
   plug&play area.  grlib_apb_add's own core list is bounded (16 entries,
   checked) unlike grlib_ahbs_add/grlib_ahbm_add, and this file calls it
   a handful of times total (once per test that needs a mounted core),
   so the bound is never in reach.  Its device id (0x100) is not one of
   the GAISLER_* ids grlib.h defines, so a plug&play scan can never
   confuse it with a real core someone adds to grlib.h later.  */

uint32 fake_last_write_addr;
uint32 fake_last_write_data;
int fake_write_count;

int
fake_read (uint32 addr, uint32 *data)
{
  *data = 0xa5a50000 | addr;
  return 1;
}

int
fake_write (uint32 addr, uint32 *data, uint32 size)
{
  (void) size;
  fake_last_write_addr = addr;
  fake_last_write_data = *data;
  fake_write_count++;
  return 1;
}

void
fake_reset (void)
{
  fake_last_write_addr = 0;
  fake_last_write_data = 0;
  fake_write_count = 0;
}

void
fake_add (int irq, uint32 addr, uint32 mask)
{
  (void) irq;
  grlib_apbpp_add (GRLIB_PP_ID (VENDOR_GAISLER, 0x100, 0, 0),
		   GRLIB_PP_APBADDR (addr, mask));
}

const struct grlib_ipcore fake_core = { NULL, fake_reset, fake_read,
					fake_write, fake_add };

/* A second minimal core, with init set and reset/read/write left NULL: the
   opposite combination from fake_core above.  apbbus_init/apbbus_reset/
   apbbus_read/apbbus_write each guard their call to a core's callback
   behind a null check, and fake_core alone only ever shows one side of
   each check (init always NULL, reset/read/write always set).  Mounting
   this alongside fake_core exercises the other side without disturbing
   the offsets fake_core's own cases rely on, since it goes in a distinct
   256 byte window.  Device id 0x101, also not a real GAISLER_* id.  */

int fake2_init_calls;

void
fake2_init (void)
{
  fake2_init_calls++;
}

void
fake2_add (int irq, uint32 addr, uint32 mask)
{
  (void) irq;
  grlib_apbpp_add (GRLIB_PP_ID (VENDOR_GAISLER, 0x101, 0, 0),
		   GRLIB_PP_APBADDR (addr, mask));
}

const struct grlib_ipcore fake_core2 = { fake2_init, NULL, NULL, NULL,
					 fake2_add };

}

/* -------------------------------------------------------------------- */
/* L2C                                                                   */
/* -------------------------------------------------------------------- */

TEST_CASE_FIXTURE (l2c_fixture, "L2C the core has no write path")
{
  /* The dispatch table's write slot is NULL: every L2C register the core
     answers is read-only, there is no register state a store could
     change.  */
  CHECK (l2c.write == NULL);
}

TEST_CASE_FIXTURE (l2c_fixture,
		   "L2C the status register reports a fixed configuration")
{
  /* Section 98.4.2 gives bits 1:0 a "WAY" field, "11" meaning 4-way, and
     bit 22 a memory protection flag.  grlib.cc answers a single
     hardwired value rather than modelling configuration state, so pin
     both the raw value and what the two documented fields decode to.  */
  uint32 status = read (L2C_STATUS);

  CHECK (status == 0x00502803);
  CHECK ((status & 0x3) == 0x3);	 /* WAY = "11", 4-way */
  CHECK (((status >> 22) & 0x1) == 0x1); /* MP = implemented */
}

TEST_CASE_FIXTURE (
    l2c_fixture, "L2C the access control register reports 128WF and SPLIT set")
{
  /* Section 98.4.16: bit 4 is the 128-bit write line fetch enable, bit 1
     is the SPLIT response enable.  The comment in grlib.cc names the same
     two bits, so this pins the code to what it claims to implement.  */
  uint32 accc = read (L2C_ACCC);

  CHECK (accc == 0x00000012);
  CHECK (((accc >> 4) & 0x1) == 0x1); /* 128WF */
  CHECK ((accc & 0x2) == 0x2);	      /* SPLIT */
}

TEST_CASE_FIXTURE (l2c_fixture, "L2C an unimplemented register reads as zero")
{
  /* The control register (98.4.1) and the access counter (98.4.5) are
     both in the manual, but grlib.cc's switch has no case for either: it
     falls through to a zero default.  This is not a specified reset
     value, it pins the current fallback for registers the core does not
     model.  */
  CHECK (read (L2C_CTRL) == 0);
  CHECK (read (L2C_ACC) == 0);
}

/* -------------------------------------------------------------------- */
/* DSU                                                                   */
/* -------------------------------------------------------------------- */

TEST_CASE_FIXTURE (
    dsu_fixture,
    "DSU the time tag counter tracks the active cpu's simulated time")
{
  /* Section 32.6.5: the trace buffer time tag counter increments each
     clock the processor runs.  grlib.cc approximates this with the
     active debug cpu's own simulated time (sregs[cpu].simtime), which the
     fixture leaves at zero after reset_all's init_regs.  advance_time
     (the fixture's run()) only moves the shared event-queue clock
     ebase.simtime, not any per-cpu simtime, so this case drives the
     counter directly the way run_sim would once instructions execute.  */
  CHECK (read (DSU_DTTC) == 0);

  sregs[cpu].simtime = 12345;
  CHECK (read (DSU_DTTC) == 12345);

  sregs[cpu].simtime = 0;
  CHECK (read (DSU_DTTC) == 0);
}

TEST_CASE_FIXTURE (dsu_fixture,
		   "DSU the time tag counter truncates to 32 bits")
{
  /* dsu_read assigns a uint64 simtime into a uint32 *data with no
     masking.  The comment above the case says software is expected to
     know how many bits are implemented and mask accordingly, so this
     pins the natural truncation rather than any documented bit width.  */
  sregs[cpu].simtime = 0x100000001ULL;
  CHECK (read (DSU_DTTC) == 1);
}

TEST_CASE_FIXTURE (dsu_fixture,
		   "DSU a write does not change the time tag counter")
{
  /* dsu_write exists (unlike l2c's NULL) but its body is empty: every
     write it accepts is silently discarded.  */
  sregs[cpu].simtime = 42;
  write (DSU_DTTC, 0xffffffff);
  CHECK (read (DSU_DTTC) == 42);
}

TEST_CASE_FIXTURE (dsu_fixture, "DSU an unimplemented register reads as zero")
{
  /* Only the time tag counter (32.6.5) is in scope here; every other
     register the DSU3 chapter documents falls to grlib.cc's default
     case.  Pinning the fallback, not a specified value.  */
  CHECK (read (DSU_CTRL) == 0);
}

/* -------------------------------------------------------------------- */
/* APBMST / APBMST2                                                      */
/* -------------------------------------------------------------------- */

/* This case must run before any other APBMST case in this file: it needs
   apbbus[0]'s plug&play area (ppindex) untouched so its own entry lands
   at the very first slot, offset 0, letting it pin the exact 0xFF000
   boundary rather than merely finding its entry somewhere in the area.
   Nothing outside this file calls grlib_apbpp_add/grlib_apb_add, and
   doctest runs TEST_CASE_FIXTURE cases in source order, so this being
   textually first is enough.  */
TEST_CASE_FIXTURE (apbmst_fixture,
		   "APBMST the plug-and-play area starts exactly at 0xFF000")
{
  apbmst.add (0, 0x80000000, 0xfff);

  /* Just below the plug&play area, and not inside the fake core's window
     (nothing is mounted yet), is a hole: apbbus_read leaves *data at the
     zero the fixture's read() pre-set.  */
  CHECK (read (0xff000 - 4) == 0);

  /* grlib_apbpp_add is the primitive grlib_apb_add's core->add callback
     reaches.  Its index is shared by every case in the binary and only
     ever grows, so this case cannot assume where its own entry lands.
     The index wraps to zero after 32 entries, so drive it round until the
     entry sits at the start of the area, which is what makes the offsets
     below exact whatever ran before.  */
  uint32 id = GRLIB_PP_ID (VENDOR_GAISLER, 0x101, 0, 0);
  uint32 addr_word = GRLIB_PP_APBADDR (0x80000200, 0xfff);
  int idx;

  do
    idx = grlib_apbpp_add (id, addr_word);
  while (idx != 2);

  REQUIRE (idx == 2);
  CHECK (read (0xff000) == id);
  CHECK (read (0xff000 + 4) == addr_word);
}

TEST_CASE_FIXTURE (apbmst_fixture, "APBMST forwards accesses inside a mounted "
				   "core's window and drops the rest")
{
  /* apbmst.add sets this bridge's own 1 M AHB window (base/mask), the
     step a board normally takes through grlib_ahbs_add.  Calling add
     directly here reaches the same apbbus_add the macro-generated
     wrapper calls, without touching grlib_ahbs_add's unbounded AHB slave
     list.  */
  apbmst.add (0, 0x80000000, 0xfff);

  /* Mount the fake core at the same relative offset leon3.cc uses for
     irqmp (APBSTART + 0x200), giving it the 256-byte window [0x200,
     0x300).  */
  grlib_apb_add (&fake_core, 0, 0x80000200, 0xfff);

  /* apbbus_write forwards addr & bc->mask, the offset relative to the
     mounted core's own 256-byte window, the same convention every real
     core's register switch (e.g. irqmp's addr & 0xff) expects.  */
  write (0x200, 0x11111111);
  CHECK (fake_last_write_addr == 0x00);
  CHECK (fake_last_write_data == 0x11111111);

  /* The last word inside the window still reaches the core... */
  write (0x2fc, 0x22222222);
  CHECK (fake_last_write_addr == 0xfc);

  /* ...but the first word past it does not: apbbus_write's range check
     is addr < end, not <=.  */
  fake_write_count = 0;
  write (0x300, 0x33333333);
  CHECK (fake_write_count == 0);

  /* Nor does the word just below the window. */
  write (0x1fc, 0x44444444);
  CHECK (fake_write_count == 0);

  CHECK (read (0x204) == (0xa5a50000u | 0x04));

  /* An address in the hole between the mounted core and the plug&play
     area (0xFF000) matches nothing: apbbus_read leaves *data untouched,
     which the fixture's read() pre-set to zero.  */
  CHECK (read (0x500) == 0);
}

TEST_CASE_FIXTURE (apbmst_fixture,
		   "APBMST plug-and-play area reports a mounted core")
{
  apbmst.add (0, 0x80000000, 0xfff);
  grlib_apb_add (&fake_core, 0, 0x80000200, 0xfff);

  uint32 id = GRLIB_PP_ID (VENDOR_GAISLER, 0x100, 0, 0);
  uint32 pp_addr = GRLIB_PP_APBADDR (0x80000200, 0xfff);

  /* grlib_apbpp_add's index is shared by every core ever mounted on this
     bridge across the whole test binary, so this does not assume the
     fake core landed at any particular slot: it scans the 32-entry,
     8-byte-per-entry area (0xFF000-0xFF0FC) for the id this test just
     registered.  */
  int found = -1;
  for (uint32 off = 0; off < 0x100; off += 8)
    if (read (0xff000 + off) == id)
      {
	found = (int) off;
	break;
      }

  REQUIRE (found >= 0);
  CHECK (read (0xff000 + (uint32) found + 4) == pp_addr);
}

TEST_CASE_FIXTURE (apbmst_fixture,
		   "APBMST2 dispatches on its own bridge, not APBMST's")
{
  /* struct grlib_ipcore carries no instance pointer, so apbmst and
     apbmst2 exist only because GRLIB_APBMST(name, n) generates a
     distinct set of wrappers per bridge index n.  This is what actually
     proves the macro keeps the two apart, rather than both wrappers
     quietly sharing bridge 0.  */
  apbmst.add (0, 0x80000000, 0xfff);
  apbmst2.add (0, 0x80100000, 0xfff);

  grlib_apb_add (&fake_core, 0, 0x80000200, 0xfff);

  /* Same relative offset, but on apbmst2's bridge nothing is mounted
     there: a hole leaves *data untouched, so start from zero the way
     the fixture's read() helper does. */
  uint32 data = 0;
  apbmst2.read (0x200, &data);
  CHECK (data == 0);

  data = 0x12345678;
  apbmst2.write (0x200, &data, 2);
  CHECK (fake_write_count == 0);

  apbmst2.init ();
  apbmst2.reset ();
}

/* -------------------------------------------------------------------- */
/* GRETH / L2C / LEON3 add wrappers                                     */
/* -------------------------------------------------------------------- */

/* greth_add, l2c_add and leon3_add (grlib.cc) are thin wrappers around a
   plug&play registration plus a verbose-only identification banner; none
   of that is part of any core's own register file, so nothing here comes
   from ref/.  Reusing l2c as the fixture's core is arbitrary: none of
   these cases touch it, and l2c has no init/reset of its own to run.  */
struct wrapper_add_fixture : sis_tests::grlib_core_fixture
{
  int saved_greth_irq;

  wrapper_add_fixture ()
      : sis_tests::grlib_core_fixture (&l2c), saved_greth_irq (greth_irq)
  {
  }

  ~wrapper_add_fixture () { greth_irq = saved_greth_irq; }
};

TEST_CASE_FIXTURE (wrapper_add_fixture,
		   "GRETH/L2C/LEON3 add wrappers are silent by default")
{
  sis_tests::stdout_capture cap;

  greth.add (3, 0x80000000, 0xfff);
  l2c.add (0, 0x80000000, 0xfff);
  leon3s.add (0, 0, 0);

  CHECK (cap.str ().empty ());
}

TEST_CASE_FIXTURE (
    wrapper_add_fixture,
    "GRETH/L2C/LEON3 add wrappers print an identification banner when verbose")
{
  sis_verbose = 1;

  {
    sis_tests::stdout_capture cap;

    greth.add (3, 0x80000000, 0xfff);

    CHECK (cap.str ().find ("GRETH") != std::string::npos);
  }
  /* greth_add's one piece of actual state: the irq a board picked is what
     grlib_set_irq (reached from greth.cc) later raises.  */
  CHECK (greth_irq == 3);

  {
    sis_tests::stdout_capture cap;

    l2c.add (0, 0x80000000, 0xfff);

    CHECK (cap.str ().find ("Level 2 Cache") != std::string::npos);
  }

  {
    sis_tests::stdout_capture cap;

    leon3s.add (0, 0, 0);

    CHECK (cap.str ().find ("LEON3") != std::string::npos);
  }
}

/* -------------------------------------------------------------------- */
/* APBMST dispatch to a core with no init/reset/read/write              */
/* -------------------------------------------------------------------- */

TEST_CASE_FIXTURE (
    apbmst_fixture,
    "APBMST init/reset/read/write skip a mounted core's null callbacks")
{
  apbmst.add (0, 0x80000000, 0xfff);

  /* A distinct 256 byte window from fake_core's, so this does not disturb
     the offsets the fake_core cases above rely on.  */
  int init_calls_before = fake2_init_calls;
  grlib_apb_add (&fake_core2, 0, 0x80000300, 0xfff);

  apbmst.init ();
  CHECK (fake2_init_calls == init_calls_before + 1);

  /* fake_core2.reset is NULL; apbmst.reset () also walks fake_core (mounted
     by earlier cases in this file, if any ran first) or, if this is the
     first case to mount anything, just fake_core2.  Either way this must
     not crash on the NULL reset.  */
  apbmst.reset ();

  /* fake_core2.read is NULL: apbbus_read marks the address matched (so the
     0xFF000 plug&play fallback below it is not reached) but leaves *data
     untouched.  */
  uint32 data = 0xdeadbeef;
  apbmst.read (0x300, &data);
  CHECK (data == 0xdeadbeef);

  /* fake_core2.write is NULL: apbbus_write must not crash reaching for a
     write callback that is not there.  */
  data = 0x12345678;
  apbmst.write (0x300, &data, 2);

  /* An address below every mounted core's window (fake_core starts at
     0x200) fails the range check's own first comparison rather than its
     second: every other case in this file only ever probes an address
     past a mounted core's end, never before its start.  */
  data = 0x55555555;
  apbmst.read (0x100, &data);
  CHECK (data == 0x55555555);
}

TEST_CASE_FIXTURE (apbmst_fixture,
		   "APBMST verbose diagnostics for the bridge add and the "
		   "plug-and-play read fallback")
{
  /* Neither of these printf calls is part of the APBCTRL chapter (see the
     file header): it has no register table at all.  Both pin today's
     diagnostic output.  */
  {
    sis_tests::stdout_capture cap;

    apbmst.add (0, 0x80000000, 0xfff);

    CHECK (cap.str ().empty ());
  }

  {
    sis_verbose = 1;
    sis_tests::stdout_capture cap;

    apbmst.add (0, 0x80000000, 0xfff);

    CHECK (cap.str ().find ("AHB/APB Bridge") != std::string::npos);
    sis_verbose = 0;
  }

  {
    sis_tests::stdout_capture cap;

    uint32 data = 0;
    apbmst.read (0xff000, &data);

    CHECK (cap.str ().empty ());
  }

  {
    sis_verbose = 2;
    sis_tests::stdout_capture cap;

    uint32 data = 0;
    apbmst.read (0xff000, &data);

    CHECK (cap.str ().find ("APB PP read a:") != std::string::npos);
    sis_verbose = 0;
  }
}
