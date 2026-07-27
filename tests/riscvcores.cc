/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Tests for the RISC-V CLINT and PLIC cores in grlib.cc.

   Both cores are driven through their own register file with no board, the
   way tests/irqmp.cc drives IRQMP.  Unlike IRQMP, clint and plic answer
   riscv.cc's per-hart mip/mie/mstatus rather than the shared IRQMP registers,
   so the fixtures set arch to &riscv and read sregs[] and ext_irl[] the same
   way tests/riscv.cc's instruction cases do.

   rv32.cc places the CLINT at 0x02000000 and the PLIC at 0x0C000000, which
   is the SiFive FU540-C000's own map (ref/sifive-fu540-c000-manual.md,
   table 35).  That manual's chapter 9 (table 36) gives the CLINT register
   layout and chapter 10 (table 37) the PLIC layout, and both match
   grlib.cc's offsets, so cases that rely on an offset cite the table
   directly instead of pinning it as an implementation detail.

   The *semantics* on top of that layout, mtime/mtimecmp/msip and PLIC
   claim/complete, come from two places that agree with each other: the
   RISC-V privileged architecture (v1.9 draft, sections 3.1.13 "Machine
   Interrupt Registers (mip and mie)" and 3.1.14 "Machine Timer Registers
   (mtime and mtimecmp)", and chapter 7, the Platform-Level Interrupt
   Controller; also ref/riscv-current-privileged-isa-scoped.adoc's "Machine
   Interrupt (mip and mie) Registers" and "Machine Timer (mtime and
   mtimecmp) Registers" headings) and the SiFive manual's own restatement of
   the same rules for its concrete implementation (sections 9.2 "MSIP
   Registers", 9.3 "Timer Registers", 10.3 "Interrupt Priorities" through
   10.8 "Interrupt Completion").  A case cites whichever said it most
   plainly, and both where both apply.

   grlib.cc's plic comment says its functionality is "simplified... for
   now": priority and threshold registers are stored but never consulted by
   plic_check_irq.  Against the generic PLIC chapter that reads like an
   acknowledged simplification, but the SiFive manual is concrete enough to
   turn part of it into an outright conformance gap; see the "priority 0"
   case below and the final report.

   plic has no init or reset, so its pending, enable, threshold and priority
   state are static file-local arrays in grlib.cc that outlive any one test
   case.  The fixture drains them through the claim/complete cycle of
   sections 7.10/7.11 (10.7/10.8 in the SiFive manual) before and after
   every case, the only channel the struct grlib_ipcore interface offers to
   reach them.

   PLIC's pending bits are set by ns16550's interrupt line, the only trigger
   grlib.cc wires to plic_irq.  A case drives ns16550's register file
   directly to raise a chosen interrupt ID, the same way the board's UART
   would.  */

#include "doctest.h"
#include "support.h"

#include "config.h"
#include "riscv.h"

#include "grlibcore.h"

using sis_tests::stdout_capture;

namespace
{

/* ---------------------------- CLINT ---------------------------------- */

/* Offsets, matching both grlib.cc's CLINT_* macros and table 36 of the
   SiFive manual (relative to the CLINT's own base, which rv32.cc sets to
   0x02000000): msip for hart N at 4*N, mtimecmp for hart N at 0x4000+8*N,
   mtime at 0xbff8.  */
const uint32 CLINT_MSIP0 = 0x0000;
const uint32 CLINT_MSIP1 = 0x0004;
const uint32 CLINT_MTIMECMP0_LO = 0x4000;
const uint32 CLINT_MTIMECMP0_HI = 0x4004;
const uint32 CLINT_MTIMECMP1_LO = 0x4008;
const uint32 CLINT_MTIMECMP1_HI = 0x400c;
const uint32 CLINT_MTIME_LO = 0xbff8;
const uint32 CLINT_MTIME_HI = 0xbffc;
const uint32 CLINT_END = 0x10000;

struct clint_fixture : sis_tests::grlib_core_fixture
{
  uint64 saved_mtimecmp[NCPU];

  clint_fixture () : sis_tests::grlib_core_fixture (&clint)
  {
    arch = &riscv;

    /* init_regs zeroes mip, mie, mstatus, simtime and ext_irl for every
       hart, and the base fixture's reset_all already ran it, so those need
       no saving here.  mtimecmp is the one CLINT field init_regs leaves
       alone, so it is the one a case here can leak into the next.  */
    for (int i = 0; i < NCPU; i++)
      {
	saved_mtimecmp[i] = sregs[i].mtimecmp;
	sregs[i].mtimecmp = 0;
      }
  }

  ~clint_fixture ()
  {
    for (int i = 0; i < NCPU; i++)
      sregs[i].mtimecmp = saved_mtimecmp[i];
  }

  /* Arms the machine timer interrupt path so a pending MTIP bit is visible
     in ext_irl, mirroring what riscv.cc's rv32_check_lirq requires: global
     interrupts on and the local timer enable bit set (v1.9 draft 3.1.13).  */
  void
  enable_mtie (int cpu)
  {
    sregs[cpu].mstatus |= MSTATUS_MIE;
    sregs[cpu].mie |= MIE_MTIE;
  }
};

}

TEST_CASE_FIXTURE (clint_fixture, "CLINT add logs the address when verbose")
{
  /* Line 1511's branch needs both a silent and a verbose call to reach
     full coverage; the fixture leaves sis_verbose at 0.  */
  clint.add (7, 0xff000000, 0xfff);

  sis_verbose = 1;
  stdout_capture cap;
  clint.add (7, 0xff000000, 0xfff);
  std::string text = cap.str ();

  CHECK (text.find ("CLINT") != std::string::npos);
  CHECK (text.find ("0xff000000") != std::string::npos);
}

TEST_CASE_FIXTURE (
    clint_fixture,
    "CLINT mtime is at offset 0xbff8/0xbffc (table 36, 9.1/9.3)")
{
  /* Table 36 places the 8-byte mtime register at 0xbff8, split as a low and
     a high word for RV32 (9.3, and norm:mtime_sz).  grlib.cc reads it out
     of the shared engine clock ebase.simtime.  */
  ebase.simtime = 0x1122334455667788ULL;

  CHECK (read (CLINT_MTIME_LO) == 0x55667788u);
  CHECK (read (CLINT_MTIME_HI) == 0x11223344u);
}

TEST_CASE_FIXTURE (clint_fixture,
		   "CLINT mtimecmp is at offset 0x4000 (table 36, 9.1)")
{
  /* Table 36: "mtimecmp for hart 0" at offset 0x4000, 9.3: a 64-bit
     per-hart timer compare register.  */
  write (CLINT_MTIMECMP0_LO, 0xcafef00d);
  write (CLINT_MTIMECMP0_HI, 0x00000001);

  CHECK (sregs[0].mtimecmp == 0x00000001cafef00dULL);
  CHECK (read (CLINT_MTIMECMP0_LO) == 0xcafef00du);
  CHECK (read (CLINT_MTIMECMP0_HI) == 0x00000001u);
}

TEST_CASE_FIXTURE (clint_fixture, "CLINT a 32-bit mtimecmp write only changes "
				  "that half (norm:mtimecmp_rv32_wr)")
{
  sregs[0].mtimecmp = 0x1111111122222222ULL;

  write (CLINT_MTIMECMP0_LO, 0x33333333);
  CHECK (sregs[0].mtimecmp == 0x1111111133333333ULL);

  write (CLINT_MTIMECMP0_HI, 0x44444444);
  CHECK (sregs[0].mtimecmp == 0x4444444433333333ULL);
}

TEST_CASE_FIXTURE (
    clint_fixture,
    "CLINT a timer interrupt is pending once mtime reaches mtimecmp")
{
  /* norm:mtime_intr_pending / v1.9 draft 3.1.14: "A machine timer interrupt
     becomes pending whenever mtime contains a value greater than or equal
     to mtimecmp."  */
  sregs[0].simtime = 100;

  write (CLINT_MTIMECMP0_LO, 50);
  write (CLINT_MTIMECMP0_HI, 0);

  CHECK ((sregs[0].mip & MIP_MTIP) != 0);
}

TEST_CASE_FIXTURE (
    clint_fixture,
    "CLINT writing mtimecmp past mtime clears the pending interrupt")
{
  /* norm:mtime_intr_pending: "The interrupt remains posted until mtimecmp
     becomes greater than mtime (typically as a result of writing
     mtimecmp)."  */
  sregs[0].simtime = 100;
  write (CLINT_MTIMECMP0_LO, 50);
  write (CLINT_MTIMECMP0_HI, 0);
  REQUIRE ((sregs[0].mip & MIP_MTIP) != 0);

  write (CLINT_MTIMECMP0_LO, 1000000);
  write (CLINT_MTIMECMP0_HI, 0);

  CHECK ((sregs[0].mip & MIP_MTIP) == 0);
}

TEST_CASE_FIXTURE (
    clint_fixture,
    "CLINT a future mtimecmp arms a timer event that reaches the CPU")
{
  /* norm:mtime_intr_taken / v1.9 3.1.14: "The interrupt will only be taken
     if interrupts are enabled and the MTIE bit is set."  */
  enable_mtie (0);
  sregs[0].simtime = 0;
  ebase.simtime = 0;

  write (CLINT_MTIMECMP0_LO, 100);
  write (CLINT_MTIMECMP0_HI, 0);
  CHECK ((sregs[0].mip & MIP_MTIP) == 0);
  CHECK (ext_irl[0] == 0);

  run (100);

  CHECK ((sregs[0].mip & MIP_MTIP) != 0);
  CHECK (ext_irl[0] != 0);
}

TEST_CASE_FIXTURE (
    clint_fixture,
    "CLINT rewriting a future mtimecmp cancels the earlier timer event")
{
  /* Exercises clint_write's remove_event call: without it, the interrupt
     armed by the first write would still fire at t=50 even though the
     second write asked for t=200.  */
  enable_mtie (0);
  sregs[0].simtime = 0;
  ebase.simtime = 0;

  write (CLINT_MTIMECMP0_LO, 50);
  write (CLINT_MTIMECMP0_HI, 0);

  write (CLINT_MTIMECMP0_LO, 200);
  write (CLINT_MTIMECMP0_HI, 0);

  run (50);
  CHECK ((sregs[0].mip & MIP_MTIP) == 0);

  run (150);
  CHECK ((sregs[0].mip & MIP_MTIP) != 0);
}

TEST_CASE_FIXTURE (clint_fixture,
		   "CLINT mtimecmp is stationed 8 bytes apart per hart "
		   "(table 36: 0x4000, 0x4008, ...)")
{
  int saved_ncpu = ncpu;
  ncpu = 2;

  write (CLINT_MTIMECMP0_LO, 0x10);
  write (CLINT_MTIMECMP0_HI, 0);
  write (CLINT_MTIMECMP1_LO, 0x20);
  write (CLINT_MTIMECMP1_HI, 0);

  CHECK (sregs[0].mtimecmp == 0x10u);
  CHECK (sregs[1].mtimecmp == 0x20u);
  CHECK (read (CLINT_MTIMECMP0_LO) == 0x10u);
  CHECK (read (CLINT_MTIMECMP1_LO) == 0x20u);

  ncpu = saved_ncpu;
}

TEST_CASE_FIXTURE (clint_fixture,
		   "CLINT msip is per hart at offset 0x0/0x4 (table 36, 9.2)")
{
  int saved_ncpu = ncpu;
  ncpu = 2;

  write (CLINT_MSIP0, 1);

  CHECK ((sregs[0].mip & MIP_MSIP) != 0);
  CHECK ((sregs[1].mip & MIP_MSIP) == 0);

  write (CLINT_MSIP1, 1);
  CHECK ((sregs[1].mip & MIP_MSIP) != 0);

  write (CLINT_MSIP0, 0);
  CHECK ((sregs[0].mip & MIP_MSIP) == 0);
  CHECK ((sregs[1].mip & MIP_MSIP) != 0);

  ncpu = saved_ncpu;
}

TEST_CASE_FIXTURE (
    clint_fixture,
    "CLINT (suspected defect) reading msip does not reflect the pending bit")
{
  /* Both documents agree on the encoding: norm:msip_enc and the SiFive
     manual's 9.2 both say the memory-mapped msip register is 32 bits wide
     with the upper 31 bits tied to 0 and "the least significant bit ...
     reflected in the MSIP bit of the mip CSR" (9.2).  clint_write sets
     MIP_MSIP (0x008, CSR bit 3) correctly on a write, but clint_read's
     msip branch shifts the mip word right by 4 instead of 3
     (grlib.cc:1546), so it always reports 0 regardless of the pending
     state.  This case pins that reading, not the spec.  See the final
     report for the exact line.  */
  write (CLINT_MSIP0, 1);
  REQUIRE ((sregs[0].mip & MIP_MSIP) != 0);

  CHECK (read (CLINT_MSIP0) == 0u);
}

TEST_CASE_FIXTURE (clint_fixture,
		   "CLINT writes outside the register window are ignored")
{
  sregs[0].mtimecmp = 0x42;

  write (CLINT_END, 0xffffffff);

  CHECK (sregs[0].mtimecmp == 0x42u);
}

TEST_CASE_FIXTURE (
    clint_fixture,
    "CLINT (suspected defect) writing mtime's low word corrupts a "
    "hart's mtimecmp instead")
{
  /* Table 36 gives mtime its own 8-byte register at 0xbff8, distinct from
     any hart's mtimecmp, with attribute RW (9.3 confirms mtime is
     read-write).  clint_write's range check for mtimecmp is
     "(addr >= CLINT_TIMECMP) && (addr <= CLINT_TIMEBASE)" (grlib.cc:1568),
     an inclusive upper bound that wrongly folds address 0xbff8 itself into
     the mtimecmp range.  A write meant for mtime's low word instead lands
     on cpuid = (0xbff8 >> 3) % NCPU, hart 3 here, and mtime is left
     unmodified (it is not backed by any writable state).  This case pins
     that reading; see the final report.  */
  sregs[3].mtimecmp = 0;

  write (CLINT_MTIME_LO, 0x12345678);

  CHECK ((sregs[3].mtimecmp & 0xffffffffu) == 0x12345678u);
}

TEST_CASE_FIXTURE (
    clint_fixture,
    "CLINT (suspected defect) mtime's high word cannot be written at all")
{
  /* Table 36 / 9.3 describe mtime as one 8-byte RW register, so its high
     word at 0xbffc should be as writable as its low word.  0xbffc is past
     CLINT_TIMEBASE, so it fails clint_write's mtimecmp range the same way
     0xc000 does, and falls through every branch untouched: there is no
     path in grlib.cc that stores a write to mtime.  */
  sregs[3].mtimecmp = 0x42;

  write (CLINT_MTIME_HI, 0xffffffff);

  CHECK (sregs[3].mtimecmp == 0x42u);
  CHECK (read (CLINT_MTIME_HI) == 0u);
}

TEST_CASE_FIXTURE (clint_fixture, "CLINT reading an unmapped offset is zero")
{
  CHECK (read (CLINT_END) == 0u);
}

/* ----------------------------- PLIC ----------------------------------- */

namespace
{

/* Register offsets, matching both grlib.cc's PLIC_* macros and table 37 of
   the SiFive manual (relative to the PLIC's own base, 0x0C000000): source
   priority at 4*ID (10.3), the pending array at 0x1000 (10.4), hart 0's
   M-Mode enables at 0x2000 with each further hart 0x80 apart (10.5), and
   hart 0's threshold/claim-complete pair at 0x200000/0x200004 with each
   further hart 0x1000 apart (10.6/10.7/10.8).  grlib.cc's plic models one
   context per hart, matching only the M-Mode row of each hart's block; the
   FU540-C000 itself has separate M-Mode and S-Mode contexts, which
   grlib.cc's board never needs since RTEMS on griscv runs bare machine
   mode.  */
const uint32 PLIC_PRIO = 0x0000;
const uint32 PLIC_IPEND0 = 0x1000;
const uint32 PLIC_IENA0 = 0x2000;
const uint32 PLIC_IENA_STRIDE = 0x80;
const uint32 PLIC_THRES0 = 0x200000;
const uint32 PLIC_THRES_STRIDE = 0x1000;
const uint32 PLIC_CLAIM0 = 0x200004;

struct plic_fixture : sis_tests::grlib_core_fixture
{
  plic_fixture () : sis_tests::grlib_core_fixture (&plic)
  {
    arch = &riscv;
    ns16550.add (3, 0, 0);
    ns16550.reset ();
    drain ();
  }

  ~plic_fixture ()
  {
    drain ();
    ns16550.reset ();
  }

  /* plic has no init and no reset (const struct grlib_ipcore plic = { NULL,
     NULL, ... }), so plic_prio/plic_ie/plic_ip/plic_thres/plic_claim are
     static file-local arrays in grlib.cc that live for the whole process,
     shared with whatever else drives the same plic core (tests/ns16550.cc's
     cases raise interrupts through the same ns16550_write -> plic_irq path
     and leave bits in plic_ip[0] set when they are done).  The only way to
     zero them back through the struct grlib_ipcore interface is the
     claim/complete cycle of sections 7.10 and 7.11: open every source's
     enable and threshold, claim and complete until a claim returns 0
     (7.10: "The PLIC core will return an ID of zero, if there were no
     pending interrupts"), then close the enables and priorities again.
     Run this before and after every case so one case can never see what an
     earlier one left pending.

     plic_claim[hart] only ever gets a value inside plic_check_irq
     (grlib.cc:1625-1637), which only runs from plic_irq or from a
     completion write (the "irq completion" arm of plic_write).  A claim
     read hands out whatever is already in plic_claim[hart] and then zeroes
     it (grlib.cc:1662-1664); it does not compute anything itself.  So a
     bare read here, with nothing having forced a completion first, returns
     the stale 0 an earlier claim already read and left behind, the loop
     exits on its first try, and any bits some other test's raise left in
     plic_ip[0] survive the drain.  A completion write forces the
     recompute; do one before the first read as well as after every
     nonzero one.  */
  void
  drain ()
  {
    for (int h = 0; h < NCPU; h++)
      {
	uint32 thres = PLIC_THRES0 + h * PLIC_THRES_STRIDE;
	uint32 claim = PLIC_CLAIM0 + h * PLIC_THRES_STRIDE;
	uint32 ena = PLIC_IENA0 + h * PLIC_IENA_STRIDE;

	write (ena, 0xffffffff);
	write (thres, 0);
	write (claim, 0); /* completion, forces the first recompute */
	for (int tries = 0; tries < 40; tries++)
	  {
	    uint32 id = read (claim);
	    if (id == 0)
	      break;
	    write (claim, id); /* completion, section 7.11 */
	  }
	write (ena, 0);
	write (ena + 4, 0); /* second enable word, sources 32-53 (table 43) */
      }

    for (int i = 0; i < 64; i++)
      write (PLIC_PRIO + 4 * i, 0);

    for (int i = 0; i < NCPU; i++)
      {
	sregs[i].mip = 0;
	ext_irl[i] = 0;
      }

    /* A future change that breaks the drain should fail loudly here
       rather than as a mystifying assertion in some unrelated case.  CHECK,
       not REQUIRE: this runs from the destructor too, where throwing would
       be unsafe.  */
    CHECK (read (PLIC_IPEND0) == 0u);
    CHECK (read (PLIC_IPEND0 + 4) == 0u);
  }

  /* Raises an interrupt request the way the ns16550 UART does: grlib.cc
     wires ns16550_write to call plic_irq once both the transmit interrupt
     enable and the modem control "out2" bit are set.  This is the only
     trigger grlib.cc connects to plic_irq, so it is how a case gets a bit
     into plic_ip without reaching into the core's static state directly.  */
  void
  raise (int irq)
  {
    ns16550.reset ();
    ns16550.add (irq, 0, 0);

    uint32 ie = 0x2;
    uint32 mcr = 0x8;
    ns16550.write (0x04, &ie, 2);
    ns16550.write (0x10, &mcr, 2);
  }
};

}

TEST_CASE_FIXTURE (plic_fixture, "PLIC add logs the address when verbose")
{
  plic.add (9, 0xfd000000, 0xfff);

  sis_verbose = 1;
  stdout_capture cap;
  plic.add (9, 0xfd000000, 0xfff);
  std::string text = cap.str ();

  CHECK (text.find ("PLIC") != std::string::npos);
  CHECK (text.find ("0xfd000000") != std::string::npos);
}

TEST_CASE_FIXTURE (
    plic_fixture,
    "PLIC interrupt ID 0 means no interrupt is pending (7.5, 10.7)")
{
  /* v1.9 draft 7.10 and the SiFive manual's 10.7 say the same thing: a
     claim "returns the id of the highest pending interrupt", or zero if
     there is none.  */
  CHECK (read (PLIC_CLAIM0) == 0u);
}

TEST_CASE_FIXTURE (
    plic_fixture,
    "PLIC an enabled source is claimed and its pending bit is cleared "
    "(7.10, 10.7)")
{
  write (PLIC_IENA0, 1u << 7);

  raise (7);

  CHECK (read (PLIC_IPEND0) == (1u << 7));
  CHECK (read (PLIC_CLAIM0) == 7u);

  /* "A successful claim also atomically clears the corresponding pending
     bit" (10.7), and a second claim with nothing newly pending returns 0.  */
  CHECK ((read (PLIC_IPEND0) & (1u << 7)) == 0u);
  CHECK (read (PLIC_CLAIM0) == 0u);
}

TEST_CASE_FIXTURE (
    plic_fixture,
    "PLIC a source disabled at the target raises no notification (7.7, 10.5)")
{
  /* 7.7: "The target will not receive interrupts from sources that are
     disabled."  IE was opened for drain and is closed again there, so this
     case starts with everything disabled.  */
  raise (9);

  CHECK ((read (PLIC_IPEND0) & (1u << 9)) != 0u);
  CHECK (read (PLIC_CLAIM0) == 0u);
  CHECK ((sregs[0].mip & MIP_MEIP) == 0);

  /* Enabling and completing re-evaluates the still-pending source (7.9,
     7.11/10.8) and the notification appears.  */
  write (PLIC_IENA0, 1u << 9);
  write (PLIC_CLAIM0, 0);

  CHECK (read (PLIC_CLAIM0) == 9u);
}

TEST_CASE_FIXTURE (plic_fixture,
		   "PLIC an interrupt notification sets MEIP for the target "
		   "(7.9, 3.1.13)")
{
  /* v1.9 draft 3.1.13: "The MEIP ... bits ... are set and cleared by a
     platform-specific interrupt controller, such as the standard
     platform-level interrupt controller."  10.7 cross-references the same
     bit as "the MEIP bit in its mip register".  */
  write (PLIC_IENA0, 1u << 4);

  raise (4);

  CHECK ((sregs[0].mip & MIP_MEIP) != 0);
}

TEST_CASE_FIXTURE (plic_fixture,
		   "PLIC priority and threshold registers are read/write "
		   "storage (10.3, 10.6)")
{
  /* Table 39 and table 44 describe both as ordinary WARL registers: a
     value written is read back.  */
  write (PLIC_PRIO + 4 * 4, 3);
  CHECK (read (PLIC_PRIO + 4 * 4) == 3u);

  write (PLIC_THRES0, 7);
  CHECK (read (PLIC_THRES0) == 7u);
}

TEST_CASE_FIXTURE (
    plic_fixture,
    "PLIC (suspected defect) a source at priority 0 still raises a "
    "notification")
{
  /* Section 10.3: "A priority value of 0 is reserved to mean 'never
     interrupt' and effectively disables the interrupt."  (The generic v1.9
     draft says the same in 7.6, without giving a concrete value to test
     against, since it leaves the priority width platform specific;
     table 39 nails it down to a real 0.)  The fixture's drain leaves every
     source's priority register at that reserved 0, and this case never
     changes it.  plic_check_irq (grlib.cc:1625-1637) only consults
     plic_ie and plic_ip; it never reads plic_prio at all, so a source at
     priority 0 is delivered exactly like any other enabled source.  This
     case pins that reading; see the final report.  */
  write (PLIC_IENA0, 1u << 4);
  REQUIRE (read (PLIC_PRIO + 4 * 4) == 0u);

  raise (4);

  CHECK (read (PLIC_CLAIM0) == 4u);
}

TEST_CASE_FIXTURE (
    plic_fixture,
    "PLIC (suspected defect) a claim picks the highest ID, not the smallest")
{
  /* Section 10.3 states the tie-break rule outright: "Ties between global
     interrupts of the same priority are broken by the Interrupt ID;
     interrupts with the lowest ID have the highest effective priority."
     (v1.9 draft 7.5 gives the same rule in more general terms: "Smaller
     values of interrupt ID take precedence over larger values of interrupt
     ID.")  grlib.cc never assigns a priority other than the reserved 0
     (see the previous case), so every enabled, pending source ties, and
     the lowest ID should win.  plic_check_irq's claim loop
     (grlib.cc:1630-1634) instead scans IDs from 1 upward and keeps
     overwriting plic_claim without breaking, so it lands on the highest
     set bit.  This case pins that reading; see the final report.  */
  write (PLIC_IENA0, (1u << 2) | (1u << 5));

  raise (2);
  raise (5);

  CHECK (read (PLIC_CLAIM0) == 5u);
}

TEST_CASE_FIXTURE (plic_fixture,
		   "PLIC per-hart enables are independent (10.5)")
{
  int saved_ncpu = ncpu;
  ncpu = 2;

  write (PLIC_IENA0 + 1 * PLIC_IENA_STRIDE, 1u << 6);

  raise (6);

  CHECK (read (PLIC_CLAIM0) == 0u);
  CHECK (read (PLIC_CLAIM0 + 1 * PLIC_THRES_STRIDE) == 6u);
  CHECK ((sregs[0].mip & MIP_MEIP) == 0);
  CHECK ((sregs[1].mip & MIP_MEIP) != 0);

  ncpu = saved_ncpu;
}

TEST_CASE_FIXTURE (
    plic_fixture,
    "PLIC the enable and pending arrays have a second word for sources "
    "32-53 (10.4, 10.5)")
{
  /* Table 43 gives each hart a second 32-bit enable word, for the sources
     the first word's 32 bits don't reach.  It is ordinary read/write
     storage.  */
  write (PLIC_IENA0 + 4, 0xffffffff);
  CHECK (read (PLIC_IENA0 + 4) == 0xffffffffu);
  write (PLIC_IENA0 + 4, 0);

  /* Table 41 gives the matching second pending word.  grlib.cc's plic_irq
     (grlib.cc:1640-1649) only ever sets plic_ip[0], the first word, so no
     source ever reaches word 2 and it reads zero here; this only exercises
     grlib.cc's read path for it, not the sources.  */
  CHECK (read (PLIC_IPEND0 + 4) == 0u);
}

TEST_CASE_FIXTURE (plic_fixture, "PLIC reading an unmapped offset is zero")
{
  /* plic_read has no final else; an address that reaches none of its
     ranges leaves *data at the read() helper's zero-initialised value.
     Every address below PLIC_IPEND0 matches the priority branch, so there
     is no unmapped low address; this only demonstrates the helper's
     initial value, consistent with tests/irqmp.cc's case of the same
     name.  */
  CHECK (read (PLIC_PRIO) == 0u);
}
