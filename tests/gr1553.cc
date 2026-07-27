/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Tests for the GR1553B MIL-STD-1553B interface in gr1553.cc.

   The expectations come from the device documentation corpus at
   /opt/eb-docs/SoC/Gaisler/GR740, chapter 16 (MIL-STD-1553B / AS15531
   Interface), source GR740-UM-DS-2-10.pdf:

     - text/datasheet/16-MIL-STD-1553B-AS15531-Interface-01-Overview.md
       (pages 248-261): sections 16.1-16.5, tables 289-299 (BC transfer
       descriptors and results, RT mode codes, RT event log and
       subaddress/descriptor layout).
     - text/datasheet/16-MIL-STD-1553B-AS15531-Interface-02-Registers.md
       (pages 262-271): table 304-330 (register map and layout).

   The corpus carries GR740-UM-DS-2-10; ref/ under the repo has 2-9 and
   silently drops the tables this file's comments cite (292, 321, 326),
   which is why gr1553.cc was previously thought unspecified. Where the two
   disagree the corpus wins; no disagreement was found.

   gr1553.cc's own header states the scope: only the subset the RTEMS/Zephyr
   example applications exercise is modelled, with a peer terminal invented
   for a single simulated node to talk to. Concretely:

     - Bus monitor mode (16.6) is not implemented at all: BMSTAT is a plain
       register, nothing else in the file reaches it.
     - The BC schedule ignores the asynchronous list (16.4.4), the bus swap
       / retry / suspend machinery (SUSE/SUSN/RETMD/NRET/STBUS), external
       trigger (WTRIG) and the richer AND/OR condition-code evaluation of
       table 294 (RT2CC/RTCC/STCC against the last transfer's actual status
       bits): a branch descriptor's condition is only ever "always taken"
       (CONDOK == 0xFF) or "never taken". Cases below pin gr1553.cc's own
       simplified encoding rather than table 294's full semantics, and say
       so.
     - The BC transfer result word (table 292) only ever gets TFRST written
       (0 success, 1 no response); RTST/RT2ST/RETCNT stay zero because the
       emulated peer never fails or retries.
     - The emulated peer (BC when the guest is RT, RT at address 4 when the
       guest is BC) is SIS's own invention, not part of chapter 16. Cases
       that exercise it say so and pin gr1553.cc's current behaviour rather
       than a documented interface.

   rt_transfer, rt_mode_code and rt_event_log are file-static and reached
   only through peer_bc_step, which calls rt_transfer with exactly three
   (rx, bcast) pairs: (0,0), (1,0), (1,1) -- never (0,1) -- and rt_mode_code
   with exactly two mode codes, 17 and 1, neither broadcast. The guards for
   the combinations that cannot occur (a broadcast transmit, a mode code
   with no control field, the broadcast fields of table 326) were dead code
   and are now asserts, so what is left is reachable from these cases.  */

#include "doctest.h"
#include "support.h"

#include "config.h"
#include "grlibcore.h"

using sis_tests::stdout_capture;

/* gr1553.cc defines this as a plain global with no header of its own
   (mirrors greth_irq in greth.cc/tests/greth.cc); grlib.cc reaches it the
   same way.  */
extern int gr1553_irq;

namespace
{

/* Register offsets, table 304-306 (pages 262-263) and table 331 (page
   269).  gr1553.cc keeps its own copies as file-local macros, transcribed
   here rather than shared with it.  */
const uint32 IRQ = 0x00;
const uint32 IMASK = 0x04;
const uint32 HWCFG = 0x10;
const uint32 BCSTAT = 0x40;
const uint32 BCCTRL = 0x44;
const uint32 BCBD = 0x48;
const uint32 BCTIMER = 0x50;
const uint32 BCIRQPT = 0x58;
const uint32 BCSLOT = 0x68;
const uint32 RTSTAT = 0x80;
const uint32 RTCFG = 0x84;
const uint32 RTSYNC = 0x90;
const uint32 RTTAB = 0x94;
const uint32 RTMCCTL = 0x98;
const uint32 RTEVSZ = 0xAC;
const uint32 RTEVLOG = 0xB0;
const uint32 RTEVIRQ = 0xB4;
const uint32 BMSTAT = 0xC0;

/* IRQ/IMASK flags, table 308/309 (page 263).  */
const uint32 IRQ_BCEV = 1u << 0;
const uint32 IRQ_RTEV = 1u << 8;

/* Access keys, table 312 (BCA, page 264) and table 321 (RTC, page 266).  */
const uint32 BCKEY = 0x1552;
const uint32 RTKEY = 0x1553;

/* BC Action register, table 312 (page 264).  */
const uint32 BCA_SCSTP = 1u << 2;
const uint32 BCA_SCSRT = 1u << 0;

/* BC Status and Config register, table 311 (page 264).  */
const uint32 BCSUP = 1u << 31;

/* RT Status register, table 320 (page 266).  */
const uint32 RTSUP = 1u << 31;

/* RT Config register, table 321 (page 266).  */
const uint32 RTCFG_RTEN = 1u << 0;
const uint32 RTCFG_RTADDR_SHIFT = 1;

/* BC transfer descriptor word 0, table 290 (page 253).  */
const uint32 BD_TYPE = 1u << 31;
const uint32 BD_IRQN = 1u << 27;

/* gr1553.cc's own simplified branch descriptor encoding: BD_COND (bit 25,
   table 294's ACT field in the real core) instead marks "this is a branch,
   not the end-of-list marker" and IRQEN (bit 26, table 294's IRQC) is the
   log-on-branch flag.  See the file header above.  */
const uint32 BD_COND = 1u << 25;
const uint32 BD_IRQEN = 1u << 26;
const uint32 BD_CONDOK = 0xFF;

/* BC transfer descriptor word 1, table 291 (page 253).  */
const uint32 TR_DUMMY = 1u << 31;

uint32
bc_word1 (uint32 rtaddr, uint32 tr, uint32 subaddr, uint32 wc)
{
  return ((rtaddr & 0x1F) << 11) | ((tr & 1) << 10) | ((subaddr & 0x1F) << 5) |
	 (wc & 0x1F);
}

/* RT subaddress table control word, table 298 (page 258).  */
const uint32 SA_BCRXE = 1u << 16;
const uint32 SA_RXEN = 1u << 15;
const uint32 SA_RXIRQ = 1u << 13;
const uint32 SA_TXEN = 1u << 7;
const uint32 SA_TXIRQ = 1u << 5;

/* RT descriptor control/status word, table 300 (page 259).  */
const uint32 BD_RT_IRQEN = 1u << 30;
const uint32 BD_EOL = 0x3;

/* RT descriptor layout, table 299 (page 259).  */
const uint32 RT_BD_CTRL_OFFSET = 0x00;
const uint32 RT_BD_DPTR_OFFSET = 0x04;
const uint32 RT_BD_NEXT_OFFSET = 0x08;

/* Emulated peer addressing, gr1553.cc's own invention (see file header). */
const uint32 PEER_RT_ADDRESS = 4;
const uint32 PEER_SUB_TO_BC = 2;
const uint32 PEER_SUB_TO_RT = 3;
const uint32 PEER_SUB_BCAST = 29;
const uint64 PEER_BC_PERIOD = 50000;

const int GR1553_TEST_IRQ = 6;

/* Poke/peek a 16-bit data word the way gr1553_read16/gr1553_write16 do:
   addr & 2 selects the low half of the containing word, addr & ~2 the high
   half.  flatmem's word accessors are a plain memcpy with no byte
   reordering (see tests/cpumem.cc), matching gr1553_read32/write32's own
   sz==2 ms->memory_read/write calls, so this only needs to reproduce the
   half selection.  */
void
poke16 (uint32 addr, uint16 value)
{
  uint32 base = addr & ~3u;
  uint32 word = sis_tests::flatmem_peek (base);

  if (addr & 2)
    word = (word & 0xFFFF0000u) | value;
  else
    word = (word & 0x0000FFFFu) | ((uint32) value << 16);
  sis_tests::flatmem_poke (base, word);
}

uint16
peek16 (uint32 addr)
{
  uint32 word = sis_tests::flatmem_peek (addr & ~3u);

  if (addr & 2)
    return (uint16) (word & 0xFFFF);
  return (uint16) ((word >> 16) & 0xFFFF);
}

struct gr1553_fixture : sis_tests::grlib_core_fixture
{
  int saved_irq;

  gr1553_fixture () : sis_tests::grlib_core_fixture (&gr1553b), saved_irq (0)
  {
    /* gr1553_irq is not reset by anything the base fixture does; give it a
       known, non-reserved level so a case can check whether
       grlib_set_irq was called by reading IRQMP back (same pattern as
       tests/greth.cc's greth_irq).  */
    saved_irq = gr1553_irq;
    gr1553_irq = GR1553_TEST_IRQ;

    /* IRQMP is a second core with its own process-wide statics; neither
       this fixture nor tests/irqmp.cc leaves it zeroed on the way out.  */
    irqmp.init ();
    irqmp.reset ();
    ext_irl[0] = 0;
  }

  ~gr1553_fixture ()
  {
    irqmp.reset ();
    ext_irl[0] = 0;
    gr1553_irq = saved_irq;
  }

  void
  unmask_irq ()
  {
    uint32 imask = 1u << GR1553_TEST_IRQ;
    irqmp.write (0x40, &imask, 2); /* IMASK. */
  }
};

}

TEST_CASE_FIXTURE (gr1553_fixture,
		   "GR1553 registers reset to their documented defaults")
{
  /* Table 311 (p264) and table 320 (p266): BCSUP/RTSUP read '1' whenever
     the core supports the mode, unconditionally on this core.  */
  CHECK (read (BCSTAT) == BCSUP);
  CHECK (read (RTSTAT) == RTSUP);

  /* Table 326 (p267-268): Synchronize (S, bits 1:0), Synchronize broadcast
     (SB, 3:2), Synchronize with data word (SD, 5:4) and its broadcast (SDB,
     7:6), and Transmitter shutdown (TS, 9:8) and its broadcast (TSB, 11:10)
     reset to '01' (legal, not logged); every other mode code resets to '00'
     (illegal).  gr1553_reset (gr1553.cc:578-582) sets this to 0x00000555,
     which is exactly that pattern.  */
  CHECK (read (RTMCCTL) == 0x00000555);

  /* Table 321 (p266): SYS (bit 15), SYDS (bit 14) and BRS (bit 13) reset to
     '1', RTADDR (bits 5:1) resets to 0b11111 (address 31), and RTEIS (bit
     6) and RTEN (bit 0) reset to '0'.  */
  CHECK (read (RTCFG) == 0x0000E03E);
  CHECK (((read (RTCFG) >> RTCFG_RTADDR_SHIFT) & 0x1F) == 31);

  /* BMSTAT (table 331, p269): bus monitor mode is not implemented (see file
     header); the register is a plain zeroed word like any other unhandled
     offset, not the documented BMSUP/KEYEN pattern.  */
  CHECK (read (BMSTAT) == 0);

  /* Table 328 (p268): RTELM (RT Event log size mask) resets to 0xFFFFFFFC,
     a one-entry ring.  */
  CHECK (read (RTEVSZ) == 0xFFFFFFFC);
}

TEST_CASE_FIXTURE (
    gr1553_fixture,
    "GR1553 BCCTRL and RTCFG ignore a write with the wrong safety key")
{
  /* Table 312 (p264): BCA writes are ignored unless BCKEY (bits 31:16) is
     exactly 0x1552.  */
  write (BCCTRL, ((BCKEY ^ 1) << 16) | BCA_SCSRT);
  CHECK ((read (BCSTAT) & 0x7) == 0);

  /* Table 321 (p266): RTC writes are ignored unless RTKEY is exactly
     0x1553.  */
  write (RTCFG, ((RTKEY ^ 1) << 16) | RTCFG_RTEN);
  CHECK ((read (RTSTAT) & 1) == 0);
  CHECK (read (RTCFG) == 0x0000E03E);
}

TEST_CASE_FIXTURE (gr1553_fixture,
		   "GR1553 BCSTAT and RTSTAT reject a direct write")
{
  /* gr1553_write's BCSTAT/RTSTAT case (gr1553.cc:615-618) is a documented
     no-op: table 311 and table 320 mark both registers read-only.  */
  write (BCSTAT, 0xffffffff);
  CHECK (read (BCSTAT) == BCSUP);

  write (RTSTAT, 0xffffffff);
  CHECK (read (RTSTAT) == RTSUP);
}

TEST_CASE_FIXTURE (gr1553_fixture,
		   "GR1553 an unmatched offset round-trips through the "
		   "plain register file")
{
  /* Every offset gr1553_write's switch does not name falls into the
     default case (gr1553.cc:662-664), a plain read/write register with no
     masking.  HWCFG (table 310, p263) and BCTIMER (table 315, p265) are
     both nominally special (read-only on real hardware) but gr1553.cc does
     not special-case them, so a write round-trips exactly like any other
     unhandled offset -- pinning gr1553.cc's current, unqualified pass
     through rather than table 310/315's read-only semantics.  */
  write (HWCFG, 0x12345678);
  CHECK (read (HWCFG) == 0x12345678);

  write (BCTIMER, 0xdeadbeef);
  CHECK (read (BCTIMER) == 0xdeadbeef);

  write (IMASK, 0x00030103);
  CHECK (read (IMASK) == 0x00030103);
}

TEST_CASE_FIXTURE (gr1553_fixture,
		   "GR1553 the IRQ register is write-one-to-clear and gated "
		   "by IMASK")
{
  /* gr1553_irq_set (gr1553.cc:209-215) always sets the IRQ bit but only
     raises grlib_set_irq when the same bit is set in IMASK; reach it
     through a completed BC transfer descriptor with IRQN set (table 290,
     bit 27, "always interrupts after transfer").  */
  const uint32 bd = 0x100;

  write (BCBD, bd);
  sis_tests::flatmem_poke (bd, BD_IRQN);      /* Not type/branch: transfer. */
  sis_tests::flatmem_poke (bd + 4, TR_DUMMY); /* Dummy: no RT needed. */

  write (BCCTRL, (BCKEY << 16) | BCA_SCSRT);
  run (10);

  /* IMASK still zero: the bit is pending but not forwarded.  */
  CHECK ((read (IRQ) & IRQ_BCEV) != 0);
  CHECK (ext_irl[0] == 0);

  /* Table 309 (p263): IRQE bit 0, BCEVE.  Also unmask the same line in
     IRQMP (gr1553_irq's target), the second, independent gate that every
     GRLIB core's interrupt has to pass (see tests/irqmp.cc).  */
  write (IMASK, IRQ_BCEV);
  unmask_irq ();

  /* Nothing re-raises the event on its own; writing IMASK does not, only a
     fresh gr1553_irq_set call does (matches how greth.cc's IMASK gating
     is only observed through a fresh event too).  Force a second one via
     another IRQN descriptor.  */
  sis_tests::flatmem_poke (bd, BD_IRQN);
  write (BCBD, bd);
  write (BCCTRL, (BCKEY << 16) | BCA_SCSRT);
  run (10);

  CHECK ((read (IRQ) & IRQ_BCEV) != 0);
  CHECK (ext_irl[0] == GR1553_TEST_IRQ);

  /* Table 308 (p263): "write back '1' to acknowledge".  Writing a zero
     changes nothing, matching the plain AND-NOT gr1553_write performs.  */
  write (IRQ, 0);
  CHECK ((read (IRQ) & IRQ_BCEV) != 0);

  write (IRQ, IRQ_BCEV);
  CHECK ((read (IRQ) & IRQ_BCEV) == 0);
}

TEST_CASE_FIXTURE (
    gr1553_fixture,
    "GR1553 a BC transfer descriptor moves data to and from the peer RT")
{
  /* Table 289 (p252): transfer descriptor at 0x100, 16 bytes.  Word 0 per
     table 290, word 1 per table 291, data pointer at 0x08.  */
  const uint32 bd0 = 0x100;
  const uint32 buf0 = 0x1000;
  const uint32 bd1 = 0x110;
  const uint32 buf1 = 0x1010;
  const uint32 bd_end = 0x120;

  write (BCBD, bd0);

  /* Descriptor 0: BC-to-RT (TR=0), RT address 4 (the emulated peer),
     subaddress 3, word count 2, no interrupt, slot time 0 (so
     GR1553_BD_TIME*4 == 0 and bc_step falls back to us=1, gr1553.cc:335-336).
     */
  sis_tests::flatmem_poke (bd0, 0);
  sis_tests::flatmem_poke (bd0 + 4,
			   bc_word1 (PEER_RT_ADDRESS, 0, PEER_SUB_TO_RT, 2));
  sis_tests::flatmem_poke (bd0 + 8, buf0);
  poke16 (buf0, 5);
  poke16 (buf0 + 2, 41);

  /* Descriptor 1: RT-to-BC (TR=1) from subaddress 2, which the emulated
     peer serves from subaddress 3 incremented by one (gr1553.cc:261-272,
     the file header's documented peer behaviour).  Slot time 10 units of
     4 us (table 290 bits 15:0), so us = 40.  */
  sis_tests::flatmem_poke (bd1, 10);
  sis_tests::flatmem_poke (bd1 + 4,
			   bc_word1 (PEER_RT_ADDRESS, 1, PEER_SUB_TO_BC, 2));
  sis_tests::flatmem_poke (bd1 + 8, buf1);

  /* End of list: type bit set, COND clear (gr1553.cc's own "end of list"
     encoding, see file header).  */
  sis_tests::flatmem_poke (bd_end, BD_TYPE);

  write (BCCTRL, (BCKEY << 16) | BCA_SCSRT);
  CHECK ((read (BCSTAT) & 0x7) == 1);

  run (100);

  /* Table 292 (p254): TFRST 000 = success.  */
  CHECK (sis_tests::flatmem_peek (bd0 + 12) == 0);
  CHECK (peek16 (buf1) == 6);
  CHECK (peek16 (buf1 + 2) == 42);
  CHECK (sis_tests::flatmem_peek (bd1 + 12) == 0);

  /* The end-of-list descriptor stops the schedule (gr1553.cc:297-300).  */
  CHECK ((read (BCSTAT) & 0x7) == 0);
  CHECK (read (BCSLOT) == bd_end);
}

TEST_CASE_FIXTURE (
    gr1553_fixture,
    "GR1553 a BC transfer to an unaddressed RT reports no response")
{
  /* No terminal answers any address but the emulated peer's (4), so the
     bus controller sees table 292's TFRST 001 (gr1553.cc:250-251, only the
     "0 success / 1 no response" pair the emulated peer can produce).  */
  const uint32 bd = 0x100;

  write (BCBD, bd);
  sis_tests::flatmem_poke (bd, 0);
  sis_tests::flatmem_poke (bd + 4, bc_word1 (7, 0, PEER_SUB_TO_RT, 2));
  sis_tests::flatmem_poke (bd + 8, 0x1000);
  sis_tests::flatmem_poke (bd + 0x10, BD_TYPE);

  write (BCCTRL, (BCKEY << 16) | BCA_SCSRT);
  run (10);

  CHECK (sis_tests::flatmem_peek (bd + 12) == 1);
}

TEST_CASE_FIXTURE (
    gr1553_fixture,
    "GR1553 a zero word count transfers all 32 words, and a dummy "
    "transfer generates no bus traffic")
{
  /* Table 291 WCMC field, table 293: "Word count (0 for 32)"
     (gr1553.cc:245-246).  */
  const uint32 bd0 = 0x100;
  const uint32 buf0 = 0x1000; /* 32 words, 64 bytes. */
  const uint32 bd1 = 0x110;
  const uint32 bd2 = 0x120;
  const uint32 buf2 = 0x2000;
  const uint32 bd_end = 0x130;

  write (BCBD, bd0);
  sis_tests::flatmem_poke (bd0, 0);
  sis_tests::flatmem_poke (bd0 + 4,
			   bc_word1 (PEER_RT_ADDRESS, 0, PEER_SUB_TO_RT, 0));
  sis_tests::flatmem_poke (bd0 + 8, buf0);
  for (uint32 i = 0; i < 32; i++)
    poke16 (buf0 + i * 2, (uint16) (100 + i));

  /* Descriptor 1: a dummy transfer (table 291 bit 31, DUM).  "No bus
     traffic is generated and the transfer succeeds immediately"
     (gr1553.cc:318-325): bc_transfer is not called at all, so the result
     word this test pre-seeds with a sentinel is left untouched, and IRQN
     still logs (comment gr1553.cc:327-329, "in effect for dummy transfers
     too").  */
  sis_tests::flatmem_poke (bd1, BD_IRQN);
  sis_tests::flatmem_poke (bd1 + 4, TR_DUMMY);
  sis_tests::flatmem_poke (bd1 + 12, 0xdeadbeef);

  /* Descriptor 2: RT-to-BC (TR=1) reads subaddress 3 straight back
     (bc_transfer's "else" branch, gr1553.cc:269-270, since the requested
     subaddress is not GR1553_PEER_SUB_TO_BC), which is the only way from
     outside the file to observe that descriptor 0's 32-word write actually
     reached the peer's per-subaddress storage.  */
  sis_tests::flatmem_poke (bd2, 0);
  sis_tests::flatmem_poke (bd2 + 4,
			   bc_word1 (PEER_RT_ADDRESS, 1, PEER_SUB_TO_RT, 2));
  sis_tests::flatmem_poke (bd2 + 8, buf2);

  sis_tests::flatmem_poke (bd_end, BD_TYPE);

  write (BCCTRL, (BCKEY << 16) | BCA_SCSRT);
  run (30);

  /* The peer's subaddress 3 buffer now holds words 0..31, written via
     bc_transfer's tr==0 loop (gr1553.cc:256-257) for i in [0,32).  */
  CHECK (sis_tests::flatmem_peek (bd0 + 12) == 0);
  CHECK (sis_tests::flatmem_peek (bd1 + 12) == 0xdeadbeef);
  CHECK ((read (IRQ) & IRQ_BCEV) != 0);
  CHECK (peek16 (buf2) == 100);
  CHECK (peek16 (buf2 + 2) == 101);
  CHECK (sis_tests::flatmem_peek (bd2 + 12) == 0);
}

TEST_CASE_FIXTURE (
    gr1553_fixture,
    "GR1553 a branch descriptor with an always-true condition jumps")
{
  /* gr1553.cc's own encoding of table 294 (see file header): BD_TYPE set
     and BD_COND set marks a branch (rather than the end-of-list marker),
     and CONDOK == 0xFF (table 294's suggested "constant true condition",
     p255) makes bc_step take the jump address at bd+4 (gr1553.cc:306-307)
     instead of falling through to bd+16.  No IRQEN here, so bc_irq_log is
     not called (gr1553.cc:303-304 skipped).  */
  const uint32 bd = 0x100;
  const uint32 jump_target = 0x200;

  write (BCBD, bd);
  sis_tests::flatmem_poke (bd, BD_TYPE | BD_COND | BD_CONDOK);
  sis_tests::flatmem_poke (bd + 4, jump_target);
  sis_tests::flatmem_poke (jump_target, BD_TYPE); /* End of list. */

  write (BCCTRL, (BCKEY << 16) | BCA_SCSRT);
  run (10);

  CHECK (read (BCSLOT) == jump_target);
  CHECK ((read (BCSTAT) & 0x7) == 0);
  CHECK ((read (IRQ) & IRQ_BCEV) == 0);
}

TEST_CASE_FIXTURE (
    gr1553_fixture,
    "GR1553 a branch descriptor with a false condition falls through and "
    "can log an interrupt, wrapping the BC IRQ ring at its 64-byte "
    "boundary")
{
  /* CONDOK != 0xFF is gr1553.cc's "never taken" condition (gr1553.cc:306,
     307-308): the schedule falls through to bd+16 instead of jumping.
     IRQEN set here does call bc_irq_log (gr1553.cc:303-304).  */
  const uint32 bd = 0x100;
  const uint32 bd_next = 0x110;
  const uint32 ring = 0x400; /* 64-byte aligned, table 316 (p265). */

  write (BCIRQPT, ring + 60); /* One entry from the ring's wrap point. */
  write (BCBD, bd);
  sis_tests::flatmem_poke (bd, BD_TYPE | BD_COND | BD_IRQEN);
  sis_tests::flatmem_poke (bd + 4, 0xdeadbeef); /* Unused: not taken. */
  sis_tests::flatmem_poke (bd_next, BD_TYPE);	/* End of list. */

  write (BCCTRL, (BCKEY << 16) | BCA_SCSRT);
  run (10);

  CHECK (read (BCSLOT) == bd_next);
  /* bc_irq_log (gr1553.cc:220-230) wrote the branch descriptor's own
     address into the ring, then wrapped the ring position (60+4=64 -> 0,
     the ring's own boundary) back to its base.  */
  CHECK (sis_tests::flatmem_peek (ring + 60) == bd);
  CHECK (read (BCIRQPT) == ring);
  CHECK ((read (IRQ) & IRQ_BCEV) != 0);
}

TEST_CASE_FIXTURE (
    gr1553_fixture,
    "GR1553 stopping the BC schedule leaves an already queued step inert")
{
  /* bc_step's very first check (gr1553.cc:288-289, "if (!bc_running)
     return") is the guard against a step that was queued before the
     schedule was stopped.  Set up two descriptors, let the first one fire
     and arm the second, stop the schedule before the second fires, and
     confirm the second is never touched.  */
  const uint32 bd0 = 0x100;
  const uint32 bd1 = 0x110;
  const uint32 buf1 = 0x1000;

  write (BCBD, bd0);
  /* Descriptor 0: dummy transfer, 400 us slot time (table 290 STIME,
     4 us units), so the second step is not due until well after this
     test's SCSTP write.  */
  sis_tests::flatmem_poke (bd0, 100);
  sis_tests::flatmem_poke (bd0 + 4, TR_DUMMY);

  /* Descriptor 1: a real transfer with a sentinel result word.  If the
     guard were missing, this would be processed and the sentinel
     overwritten with the TFRST success code (0).  */
  sis_tests::flatmem_poke (bd1, 0);
  sis_tests::flatmem_poke (bd1 + 4,
			   bc_word1 (PEER_RT_ADDRESS, 0, PEER_SUB_TO_RT, 1));
  sis_tests::flatmem_poke (bd1 + 8, buf1);
  sis_tests::flatmem_poke (bd1 + 12, 0xcafebabe);

  write (BCCTRL, (BCKEY << 16) | BCA_SCSRT);
  run (1); /* Let the first step fire and arm the second, 400 us out. */

  CHECK (read (BCSLOT) == bd1);

  write (BCCTRL, (BCKEY << 16) | BCA_SCSTP);
  CHECK ((read (BCSTAT) & 0x7) == 0);

  run (500); /* Past when the queued second step would have fired. */

  CHECK (sis_tests::flatmem_peek (bd1 + 12) == 0xcafebabe);
  CHECK (read (BCSLOT) == bd1);
  CHECK ((read (BCSTAT) & 0x7) == 0);
}

TEST_CASE_FIXTURE (
    gr1553_fixture,
    "GR1553 starting an already running BC schedule does not re-arm the "
    "step timer")
{
  /* gr1553_write's BCCTRL/SCSRT case only calls event() when bc_running
     was not already set (gr1553.cc:633-638).  If a second SCSRT write
     queued a second step, both would still be due at +1 us: the first to
     fire would advance BCSLOT to the next descriptor exactly as a correct
     single step does, so a second event landing right behind it would
     process that *next* descriptor too, in the same instant, rather than
     doing nothing.  Four identical IRQN descriptors in a row make that
     extra processing visible regardless of which of them it lands on: a
     lone step logs exactly one IRQ ring entry, an extra one logs a
     second.  */
  const uint32 bd = 0x100;
  const uint32 ring = 0x400;

  write (BCIRQPT, ring);
  write (BCBD, bd);
  for (uint32 i = 0; i < 4; i++)
    {
      sis_tests::flatmem_poke (bd + i * 16, 100 | BD_IRQN);
      sis_tests::flatmem_poke (bd + i * 16 + 4, TR_DUMMY);
    }

  write (BCCTRL, (BCKEY << 16) | BCA_SCSRT);
  write (BCCTRL, (BCKEY << 16) | BCA_SCSRT); /* Already running: no-op. */

  run (2); /* Past the single step due at +1 us. */

  CHECK (read (BCIRQPT) == ring + 4);
}

TEST_CASE_FIXTURE (gr1553_fixture,
		   "GR1553 add announces the controller only when verbose")
{
  /* gr1553_add (gr1553.cc:670-679) is the board registration hook, not
     part of chapter 16; the fixture never calls it since the core is
     driven directly with no bus.  The only observable effect from outside
     is the verbose trace.  */
  sis_verbose = 0;
  {
    stdout_capture quiet;
    core->add (0, 0x80000f00, 0xfff);
    CHECK (quiet.str ().empty ());
  }

  sis_verbose = 1;
  stdout_capture cap;

  core->add (0, 0x80000f00, 0xfff);

  CHECK (cap.str ().find ("GR1553B") != std::string::npos);
}

TEST_CASE_FIXTURE (
    gr1553_fixture,
    "GR1553 RTCFG enables the emulated peer bus controller, gated by its "
    "own key and started only once")
{
  /* gr1553_write's RTCFG case (gr1553.cc:641-660): RTEN's 0->1 edge starts
     peer_bc_step once (gr1553.cc:648-653); writing RTEN again while already
     running must not queue a second one, exactly like BCCTRL/SCSRT above.
     A wrong-keyed write earlier in this test already established the key
     gate; this focuses on the enable/disable and idempotency.

     A second, extraneous event would land at the same simulated instant
     as the first and run a second full frame back to back with it (the
     RT address matches and RTMCCTL's reset default leaves mode code 17
     legal, so nothing else gates it): peer_frame_number, and so RTSYNC,
     would then already read 2 after what should be a single period,
     instead of 1.  */
  write (RTTAB, 0x1000);
  write (RTCFG,
	 (RTKEY << 16) | (PEER_RT_ADDRESS << RTCFG_RTADDR_SHIFT) | RTCFG_RTEN);
  CHECK ((read (RTSTAT) & 1) != 0);

  write (RTCFG, (RTKEY << 16) | (PEER_RT_ADDRESS << RTCFG_RTADDR_SHIFT) |
		    RTCFG_RTEN); /* Already running: no-op. */

  run (PEER_BC_PERIOD + 100);

  CHECK (read (RTSYNC) == 1);

  /* Disabling clears RTSTAT's RUN bit and the running flag, so the event
     queued above for the *next* period must find peer_bc_running false
     when it eventually fires (the same top-of-function guard as bc_step,
     gr1553.cc:535-536) and do nothing further.  */
  write (RTCFG, (RTKEY << 16)); /* RTEN clear. */
  CHECK ((read (RTSTAT) & 1) == 0);

  run (PEER_BC_PERIOD);

  CHECK (read (RTSYNC) == 1);
  CHECK (sis_tests::flatmem_faults () == 0);
}

TEST_CASE_FIXTURE (
    gr1553_fixture,
    "GR1553 the emulated peer only drives the RT address it knows")
{
  /* peer_bc_step (gr1553.cc:537-541): "a terminal configured with any
     other address stays silent".  This is gr1553.cc's own peer, not
     chapter 16 (see file header): the RT config is set to an address
     other than the peer's hardcoded 4, so the whole frame body
     (gr1553.cc:543-565) is skipped, and the sync register -- which only
     the mode 17 call inside that body would set -- stays zero.  */
  write (RTTAB, 0x1000);
  write (RTCFG, (RTKEY << 16) | (5u << RTCFG_RTADDR_SHIFT) | RTCFG_RTEN);

  run (60000);

  CHECK (read (RTSYNC) == 0);
}

TEST_CASE_FIXTURE (
    gr1553_fixture,
    "GR1553 the emulated peer waits for the synchronize-with-data mode "
    "code to become legal before running a frame")
{
  /* peer_bc_step gates every frame on rt_mode_code(17, ...) (gr1553.cc:
     568-572): "A terminal which rejects the synchronize with data word
     mode code as illegal has no frame reference, so the frame is not run
     against it."  Zeroing RTMCCTL makes every mode code illegal (field
     00, table 326), including mode code 17's SD field (bits 5:4), so
     rt_mode_code's mc==17 shift of 4 (gr1553.cc:499) is applied but its
     "field == 0" guard (gr1553.cc:502-503) then refuses it.  */
  write (RTTAB, 0x1000);
  write (RTMCCTL, 0);
  write (RTCFG,
	 (RTKEY << 16) | (PEER_RT_ADDRESS << RTCFG_RTADDR_SHIFT) | RTCFG_RTEN);

  run (2 * PEER_BC_PERIOD + 100);

  /* No frame ever ran: the sync register (only mode code 17 touches it)
     and RTSTAT's ACT bit both stay at their reset value, and the schedule
     kept retrying rather than stopping (peer_bc_running still causes a
     fresh event every period, which this run duration crossed twice with
     no crash).  */
  CHECK (read (RTSYNC) == 0);
  CHECK ((read (RTSTAT) & (1u << 3)) == 0);
}

TEST_CASE_FIXTURE (
    gr1553_fixture,
    "GR1553 the emulated peer relays subaddress 2 to subaddress 3 and to "
    "the broadcast subaddress, logging and interrupting as configured")
{
  /* Full happy path through peer_bc_step (gr1553.cc:529-568) once RTEN is
     set and the guest RT is configured to answer address 4.  Subaddress
     layout per table 297 (p267): satab + 16*N.  */
  const uint32 satab = 0x1000;
  const uint32 sa_to_bc = satab + PEER_SUB_TO_BC * 16; /* Tx: guest->peer.*/
  const uint32 sa_to_rt = satab + PEER_SUB_TO_RT * 16; /* Rx: peer->guest.*/
  const uint32 sa_bcast = satab + PEER_SUB_BCAST * 16; /* Rx, broadcast. */

  const uint32 tx_bd = 0x2000;
  const uint32 rx_bd = 0x2010;
  const uint32 bcast_bd = 0x2020;

  const uint32 tx_buf = 0x3000;
  const uint32 rx_buf = 0x3010;
  const uint32 bcast_buf = 0x3020;

  const uint32 evlog = 0x5000; /* 16-byte aligned, 4-entry ring. */

  write (RTTAB, satab);

  /* Table 298 (p258): TXEN (bit 7) for subaddress 2, RXEN (bit 15) for
     subaddress 3 and RXEN|BCRXE (bits 15,16) for the broadcast
     subaddress 29.  RXIRQ (bit 13) on subaddress 3 tests irqen coming
     from the subaddress control word (gr1553.cc:471-474) rather than the
     descriptor's own IRQEN.  */
  sis_tests::flatmem_poke (sa_to_bc + 0x00, SA_TXEN);
  sis_tests::flatmem_poke (sa_to_bc + 0x04,
			   tx_bd); /* Tx descriptor pointer. */
  sis_tests::flatmem_poke (sa_to_rt + 0x00, SA_RXEN | SA_RXIRQ);
  sis_tests::flatmem_poke (sa_to_rt + 0x08,
			   rx_bd); /* Rx descriptor pointer. */
  sis_tests::flatmem_poke (sa_bcast + 0x00, SA_RXEN | SA_BCRXE);
  sis_tests::flatmem_poke (sa_bcast + 0x08, bcast_bd);

  /* Table 300 (p259): descriptor control/status word.  The Tx descriptor
     tests irqen coming from the descriptor's own IRQEN bit (gr1553.cc:470)
     instead of the subaddress control word, and RT_BD_EOL (0x3) as its
     next pointer, so the Tx pointer register is left untouched
     (gr1553.cc:464-465, "next != EOL" false).  The Rx descriptor points
     back at itself as "next", so the pointer register is written but
     ends up unchanged, exercising the "next != EOL" true branch.  */
  sis_tests::flatmem_poke (tx_bd + RT_BD_CTRL_OFFSET, BD_RT_IRQEN);
  sis_tests::flatmem_poke (tx_bd + RT_BD_DPTR_OFFSET, tx_buf);
  sis_tests::flatmem_poke (tx_bd + RT_BD_NEXT_OFFSET, BD_EOL);

  sis_tests::flatmem_poke (rx_bd + RT_BD_CTRL_OFFSET, 0);
  sis_tests::flatmem_poke (rx_bd + RT_BD_DPTR_OFFSET, rx_buf);
  sis_tests::flatmem_poke (rx_bd + RT_BD_NEXT_OFFSET, rx_bd);

  sis_tests::flatmem_poke (bcast_bd + RT_BD_CTRL_OFFSET, 0);
  sis_tests::flatmem_poke (bcast_bd + RT_BD_DPTR_OFFSET, bcast_buf);
  sis_tests::flatmem_poke (bcast_bd + RT_BD_NEXT_OFFSET, bcast_bd);

  poke16 (tx_buf, 11);
  poke16 (tx_buf + 2, 22);

  /* Table 326 (p267-268): keep the reset default (SD field legal, not
     logged) so the frame gate passes, but raise the Synchronize field (S,
     bits 1:0) to 0b11, legal+log+interrupt, so the unconditional
     rt_mode_code(1, ...) call at the end of the frame (gr1553.cc:563)
     both logs and interrupts.  */
  write (RTMCCTL, 0x00000557);

  write (RTEVLOG, evlog);
  write (RTEVSZ, 0xFFFFFFF0); /* 16-byte, 4-entry ring. */

  /* Table 309 (p263): RTEVE, bit 8.  Also unmask the same line in IRQMP,
     the second, independent gate every GRLIB core's interrupt has to pass
     (see tests/irqmp.cc).  */
  write (IMASK, IRQ_RTEV);
  unmask_irq ();

  write (RTCFG,
	 (RTKEY << 16) | (PEER_RT_ADDRESS << RTCFG_RTADDR_SHIFT) | RTCFG_RTEN);

  run (PEER_BC_PERIOD + 100);

  /* The peer relayed what it read from the Tx subaddress into the Rx and
     broadcast subaddresses (gr1553.cc:555-560, the file header's
     documented peer behaviour).  */
  CHECK (peek16 (rx_buf) == 11);
  CHECK (peek16 (rx_buf + 2) == 22);
  CHECK (peek16 (bcast_buf) == 11);
  CHECK (peek16 (bcast_buf + 2) == 22);

  /* Table 300 (p259): DV (bit 31) is "set to 1 by hardware after transfer",
     SZ (bits 8:3) is the transfer size counted in 16-bit words, and TRES
     (bits 2:0) is 000 for success.  Two words transferred, so SZ reads 2
     and the word reads DV | (2 << 3).  */
  CHECK (sis_tests::flatmem_peek (tx_bd + RT_BD_CTRL_OFFSET) ==
	 (BD_RT_IRQEN | 0x80000000u | (2u << 3)));
  CHECK (sis_tests::flatmem_peek (rx_bd + RT_BD_CTRL_OFFSET) ==
	 (0x80000000u | (2u << 3)));
  CHECK (sis_tests::flatmem_peek (bcast_bd + RT_BD_CTRL_OFFSET) ==
	 (0x80000000u | (2u << 3)));

  /* The Tx descriptor's EOL next pointer left the Tx pointer register
     alone; the Rx descriptor's self-pointing next left the Rx pointer
     register at the same, but now freshly written, address.  */
  CHECK (sis_tests::flatmem_peek (sa_to_bc + 0x04) == tx_bd);
  CHECK (sis_tests::flatmem_peek (sa_to_rt + 0x08) == rx_bd);

  /* Table 296 (p258) event log entries: IRQSRC(31) | TYPE(30:29) |
     SAMC(28:24) | SZ(8:3), TIMEL/BC left zero (gr1553.cc does not model
     the RT time tag counter tracking real time here) and TRES zero
     (success).  Four calls happened in program order: Tx (irqen from the
     descriptor), Rx (irqen from RXIRQ), broadcast (no irqen configured
     anywhere), then the Synchronize mode code (irqen from RTMCCTL's S
     field, field == 3).  The mode-17 call ahead of them logged nothing
     (field == 1, "legal, but not logged", gr1553.cc:512-513).  */
  CHECK (sis_tests::flatmem_peek (evlog + 0) ==
	 (0x80000000u | (0u << 29) | (PEER_SUB_TO_BC << 24) | (2u << 3)));
  CHECK (sis_tests::flatmem_peek (evlog + 4) ==
	 (0x80000000u | (1u << 29) | (PEER_SUB_TO_RT << 24) | (2u << 3)));
  CHECK (sis_tests::flatmem_peek (evlog + 8) ==
	 ((1u << 29) | (PEER_SUB_BCAST << 24) | (2u << 3)));
  CHECK (sis_tests::flatmem_peek (evlog + 12) ==
	 (0x80000000u | (2u << 29) | (1u << 24)));

  /* Only the first interrupt-raising log entry (the Tx one, at evlog+0)
     is recorded as the acknowledge target (gr1553.cc:398-401, "only move
     that mark when no interrupt is pending"); the Rx and Synchronize
     entries also raised the interrupt condition but found it already
     pending.  */
  CHECK (read (RTEVIRQ) == evlog);
  CHECK ((read (IRQ) & IRQ_RTEV) != 0);
  CHECK (ext_irl[0] == GR1553_TEST_IRQ);

  /* Mode code 17 (Synchronize with data word) latches the frame number
     into RTSYNC (table 324, p267; gr1553.cc:508-510) using the value
     peer_frame_number had *before* this frame's gr1553.cc:564 increment,
     i.e. the reset value of 1 (gr1553.cc:592). RTTTAG (table 327) is
     still zero, so only the low half is nonzero.  */
  CHECK (read (RTSYNC) == 1);

  /* A second period must see the incremented frame number, proving
     peer_frame_number persists and advances exactly once per period (not
     one of the double-scheduling failure modes the BCCTRL/RTCFG
     idempotency tests above guard against).  */
  run (PEER_BC_PERIOD);
  CHECK (read (RTSYNC) == 2);
}

TEST_CASE_FIXTURE (
    gr1553_fixture,
    "GR1553 rt_transfer gates on enable bits and rejects an unusable "
    "descriptor pointer")
{
  /* This test leaves subaddress 2 (Tx) unconfigured through its first
     periods (RXEN/TXEN clear), so every period's call against it hits
     rt_transfer's "!rx && !TXEN" early return (gr1553.cc:437-438) for
     free; it focuses on subaddress 3 (Rx) to walk through RXEN gating and
     both flavours of "no usable descriptor" (gr1553.cc:443-444): a null
     pointer and RT_BD_EOL.  */
  const uint32 satab = 0x1000;
  const uint32 sa_to_bc = satab + PEER_SUB_TO_BC * 16;
  const uint32 sa_to_rt = satab + PEER_SUB_TO_RT * 16;
  const uint32 sa_bcast = satab + PEER_SUB_BCAST * 16;
  const uint32 rx_bd = 0x2010;
  const uint32 rx_bd2 = 0x2050;
  const uint32 rx_buf = 0x3010;

  write (RTTAB, satab);
  write (RTCFG,
	 (RTKEY << 16) | (PEER_RT_ADDRESS << RTCFG_RTADDR_SHIFT) | RTCFG_RTEN);

  /* Period 1: RXEN clear, pointer left at its power-on zero.  Neither the
     "rx && !RXEN" return nor a null-pointer dereference can leave a
     trace, so this only has to not fault.  */
  run (PEER_BC_PERIOD + 100);
  CHECK (sis_tests::flatmem_faults () == 0);

  /* Period 2: RXEN set but the pointer is still the power-on zero.
     gr1553.cc:443, "bd == 0 || bd == RT_BD_EOL", the null-pointer half,
     distinct from period 3's EOL half below (RXEN clear alone would have
     returned first, at gr1553.cc:435-436, without ever reaching this
     check).  */
  sis_tests::flatmem_poke (sa_to_rt + 0x00, SA_RXEN);
  run (PEER_BC_PERIOD);
  CHECK (sis_tests::flatmem_faults () == 0);
  CHECK (sis_tests::flatmem_peek (sa_to_rt + 0x08) == 0);

  /* Period 3: pointer explicitly RT_BD_EOL, the other half of the same
     check.  */
  sis_tests::flatmem_poke (sa_to_rt + 0x08, BD_EOL);
  run (PEER_BC_PERIOD);
  CHECK (sis_tests::flatmem_faults () == 0);
  CHECK (sis_tests::flatmem_peek (sa_to_rt + 0x08) == BD_EOL);

  /* Period 4: a real descriptor whose "next" is a different address, so
     the Rx pointer register visibly advances (gr1553.cc:464-465, "next !=
     EOL" true) rather than the happy-path test's self-pointing one.  */
  sis_tests::flatmem_poke (rx_bd + RT_BD_CTRL_OFFSET, 0);
  sis_tests::flatmem_poke (rx_bd + RT_BD_DPTR_OFFSET, rx_buf);
  sis_tests::flatmem_poke (rx_bd + RT_BD_NEXT_OFFSET, rx_bd2);
  sis_tests::flatmem_poke (rx_bd2 + RT_BD_CTRL_OFFSET, 0);
  sis_tests::flatmem_poke (rx_bd2 + RT_BD_DPTR_OFFSET, rx_buf);
  sis_tests::flatmem_poke (rx_bd2 + RT_BD_NEXT_OFFSET, BD_EOL);
  sis_tests::flatmem_poke (sa_to_rt + 0x08, rx_bd);

  run (PEER_BC_PERIOD);

  CHECK (sis_tests::flatmem_peek (sa_to_rt + 0x08) == rx_bd2);

  /* Period 5: the broadcast subaddress with RXEN set but BCRXE clear.
     gr1553.cc:439, "bcast && !(ctrl & RT_SA_BCRXE)": a legal receive
     subaddress that still refuses a broadcast because it opted out.  A
     descriptor is present and holds a sentinel so a wrongly-processed
     transfer would be visible.  */
  const uint32 bcast_bd = 0x2100;
  const uint32 bcast_buf = 0x3100;

  sis_tests::flatmem_poke (sa_bcast + 0x00, SA_RXEN);
  sis_tests::flatmem_poke (sa_bcast + 0x08, bcast_bd);
  sis_tests::flatmem_poke (bcast_bd + RT_BD_CTRL_OFFSET, 0x12345678);
  sis_tests::flatmem_poke (bcast_bd + RT_BD_DPTR_OFFSET, bcast_buf);
  sis_tests::flatmem_poke (bcast_bd + RT_BD_NEXT_OFFSET, BD_EOL);
  poke16 (bcast_buf, 0xbeef);

  run (PEER_BC_PERIOD);

  CHECK (sis_tests::flatmem_peek (bcast_bd + RT_BD_CTRL_OFFSET) == 0x12345678);
  CHECK (peek16 (bcast_buf) == 0xbeef);

  /* Period 6: subaddress 2 (Tx) enabled with TXIRQ (gr1553.cc:473-474)
     instead of the happy-path test's descriptor IRQEN bit, so irqen comes
     from the subaddress control word's transmit side this time.  RTEVSZ
     keeps table 328's one-entry reset default, so the log position stays
     where RTEVLOG points; aim it at address 0 and this period's entry
     overwrites period 4's earlier, unrelated one there.  */
  sis_tests::flatmem_poke (sa_to_bc + 0x00, SA_TXEN | SA_TXIRQ);
  sis_tests::flatmem_poke (sa_to_bc + 0x04, rx_bd2); /* Reuse as Tx desc. */
  sis_tests::flatmem_poke (rx_bd2 + RT_BD_CTRL_OFFSET, 0);
  poke16 (rx_buf, 0x1234);
  write (RTEVLOG, 0);

  /* Subaddress 3 (Rx) is still enabled from period 4 above and would log
     its own, non-interrupting entry into the same log right after the Tx
     call in peer_bc_step's fixed order (gr1553.cc:555-556), overwriting
     the Tx entry this check is after.  Disable it so this period's only
     log call is the Tx one under test.  */
  sis_tests::flatmem_poke (sa_to_rt + 0x00, 0);

  run (PEER_BC_PERIOD);

  CHECK ((sis_tests::flatmem_peek (0) & 0x80000000u) != 0);
}
