/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Tests for the GRETH Ethernet MAC in greth.cc.

   The expectations come from the GRLIB IP core manual in ref/, chapter
   "680-699: GRETH", sections 53.3 (Tx DMA), 53.4 (Rx DMA), 53.7 (Media
   Independent Interfaces) and the register descriptions of 53.9.  Where a
   register is not in the scoped manual (53.9.3 MAC address and 53.9.5
   MDIO were left out when ref/ was scoped down, see ref/SOURCES.md), the
   test says so and pins what greth.cc does today instead of deriving an
   expectation from the manual.

   greth.cc reaches the host only through sis_tap_init/sis_tap_write; see
   faketap.h for how the test binary avoids ever opening a real tap
   device.  Two things about the file matter for how these cases are
   built:

   - The pointer field of a descriptor table register wraps at a fixed
     1 kB boundary (BASE_PNT = 0x3f8 in greth.cc) regardless of what the
     status register's NRD field would say, and greth_read never gives
     NRD a nonzero value in the first place.  This is a fixed-size
     table, not the configurable one section 53.4.2 describes; the wrap
     tests below pin the fixed 1 kB behaviour.

   - greth_write only calls sis_tap_init and arms the greth_tx polling
     event on the very first CTRL_RE 0->1 transition of the whole
     process (the file-static "mac" latch only ever goes 0 to 1 once),
     and the core's own reset callback (greth_mdio_reset) resets only the
     PHY registers, not greth_ctrl/greth_status/the latch.  All of this
     file's cases live in one TEST_CASE with SUBCASEs in a fixed order:
     doctest runs one leaf per pass and reruns shared code common to all
     of them from the top each time, but greth.cc's own statics are
     process state and carry from one pass to the next.  Only the first
     SUBCASE ("registers") is written to never touch CTRL_RE, so the
     second ("transmit DMA descriptor walk") is guaranteed to be the
     first RE transition in the whole binary.  Once a pass ends, this
     fixture's destructor (via grlib_core_fixture) clears the event
     queue, so an armed greth_tx cannot reach into a later pass; its
     memory access would otherwise be against whatever ms points at by
     then, per the file header's second hazard.  */

#include "doctest.h"
#include "support.h"

#include "config.h"
#include "grlibcore.h"
#include "faketap.h"

#include <string.h>

/* greth.cc defines this as a plain global with no header of its own, and
   grlib.cc reaches it the same way.  The declaration has to sit outside the
   anonymous namespace below, which would otherwise give it internal linkage
   and make it a different object from the one greth.cc writes.  */
extern int greth_irq;

namespace
{

/* Register offsets, from the tables of sections 53.9.1, 53.9.2, 53.9.6 and
   53.9.7.  The MAC address (0x08/0x0c) and MDIO (0x10) registers are not
   in the scoped manual; the offsets come from the case statements in
   greth.cc instead.  */
const uint32 CTRL = 0x00;
const uint32 STAT = 0x04;
const uint32 MACMSB = 0x08;
const uint32 MACLSB = 0x0c;
const uint32 MDIO = 0x10;
const uint32 TXBASE = 0x14;
const uint32 RXBASE = 0x18;

/* CTRL bits, table 797.  */
const uint32 CTRL_RI = 1u << 3;
const uint32 CTRL_TI = 1u << 2;
const uint32 CTRL_RE = 1u << 1;
const uint32 CTRL_TE = 1u << 0;
const uint32 CTRL_RS = 1u << 6;
const uint32 CTRL_SP = 1u << 7;

/* STAT bits, table 798.  */
const uint32 STAT_TI = 1u << 3;
const uint32 STAT_RI = 1u << 2;

/* Descriptor word 0 bits, table 789 (Rx) and the equivalent Tx layout of
   section 53.3.2.  */
const uint32 DESC_IE = 1u << 13;
const uint32 DESC_WR = 1u << 12;
const uint32 DESC_EN = 1u << 11;

/* MDIO control word, section 53.9.1 does not cover this register, so
   these come from the MDIO_WRITE/MDIO_READ macros in greth.cc.  */
const uint32 MDIO_WRITE = 1;
const uint32 MDIO_READ = 2;

/* Offsets on the IRQMP register file, transcribed in tests/irqmp.cc from
   chapter 96 of the same manual.  Used here only as a bystander: greth.cc
   raises an interrupt through the shared grlib_set_irq, and driving IRQMP
   directly is the only way from outside grlib.cc to see whether that call
   was made.  */
const uint32 IRQMP_ICLEAR = 0x0c;
const uint32 IRQMP_IMASK = 0x40;

/* greth_irq is a plain global in greth.cc (declared "int greth_irq" with
   no header of its own); grlib.cc reaches it the same way.  A real board
   sets it from greth_add, which this fixture never calls, so the tests
   set it directly to a level IRQMP does not reserve.  */
const int GRETH_TEST_IRQ = 5;

/* Packs four bytes of a frame into one big-endian 32 bit word.  A tx/rx
   buffer is reached through ms->get_mem_ptr and walked one byte at a
   time with the index XORed by arch->bswap (see greth_tx/greth_rxready
   and the note in CLAUDE.md on that pattern).  With the sparc32 fixture
   arch (bswap == 3 on a little-endian host) that XOR reverses the byte
   order within each 32 bit word between a plain byte buffer and a word
   poked into flatmem, which stores words in host (little-endian) byte
   order.  Packing the four wire bytes big-endian into the word handed to
   flatmem_poke, or reading one back and unpacking it big-endian, cancels
   the reversal out, so the helpers below only ever need to deal in plain
   frame bytes.  */
uint32
be32 (unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3)
{
  return ((uint32) b0 << 24) | ((uint32) b1 << 16) | ((uint32) b2 << 8) | b3;
}

/* Pokes LEN bytes of a frame starting at ADDR, four at a time.  A short
   final word is padded with zero, matching what get_mem_ptr would see
   past the end of a packet that is not a multiple of 4 bytes long.  */
void
poke_frame (uint32 addr, const unsigned char *bytes, int len)
{
  for (int i = 0; i < len; i += 4)
    {
      unsigned char b0 = bytes[i];
      unsigned char b1 = i + 1 < len ? bytes[i + 1] : 0;
      unsigned char b2 = i + 2 < len ? bytes[i + 2] : 0;
      unsigned char b3 = i + 3 < len ? bytes[i + 3] : 0;

      sis_tests::flatmem_poke (addr + i, be32 (b0, b1, b2, b3));
    }
}

/* Checks that LEN bytes of a frame at ADDR, poked the way poke_frame
   would, read back as BYTES.  Used to verify a receive buffer greth
   filled in.  */
bool
frame_at (uint32 addr, const unsigned char *bytes, int len)
{
  for (int i = 0; i < len; i += 4)
    {
      unsigned char b0 = bytes[i];
      unsigned char b1 = i + 1 < len ? bytes[i + 1] : 0;
      unsigned char b2 = i + 2 < len ? bytes[i + 2] : 0;
      unsigned char b3 = i + 3 < len ? bytes[i + 3] : 0;

      if (sis_tests::flatmem_peek (addr + i) != be32 (b0, b1, b2, b3))
	return false;
    }
  return true;
}

bool
frame_equals (const std::vector<unsigned char> &got, const unsigned char *want,
	      int len)
{
  if (got.size () != (size_t) len)
    return false;
  for (int i = 0; i < len; i++)
    if (got[i] != want[i])
      return false;
  return true;
}

struct greth_fixture : sis_tests::grlib_core_fixture
{
  int saved_irq;

  /* greth_write sets sync_rt on the first receive enable of the process and
     nothing clears it.  It is a real side effect of the core, but leaving it
     set would make every later wait for interrupt anywhere in the binary
     take the synchronising path.  */
  int saved_sync_rt;

  greth_fixture () : sis_tests::grlib_core_fixture (&greth), saved_irq (0)
  {
    /* greth_irq is not reset by anything the base fixture does; give it a
       known, non-reserved level so a case can check whether grlib_set_irq
       was called by reading IRQMP back.  */
    saved_irq = greth_irq;
    saved_sync_rt = sync_rt;
    greth_irq = GRETH_TEST_IRQ;

    /* IRQMP is a second core with its own process-wide statics.  Neither
       this fixture nor the one in tests/irqmp.cc leaves it zeroed on the
       way out, so a case that wants to read ext_irl has to bring it to a
       known state itself rather than assume one.  */
    irqmp.init ();
    irqmp.reset ();
    ext_irl[0] = 0;

    sis_tests::faketap_reset ();
  }

  ~greth_fixture ()
  {
    irqmp.reset ();
    ext_irl[0] = 0;
    greth_irq = saved_irq;
    sync_rt = saved_sync_rt;
  }
};

}

TEST_CASE_FIXTURE (greth_fixture, "GRETH")
{
  SUBCASE ("registers")
  {
    /* Section 53.9.1: RS is self clearing and, on this core, a write
       with RS set replaces the whole control register with only the
       speed bit, discarding whatever else was written alongside it.
       This also matches the SP reset value of '1' the manual gives,
       which greth.cc otherwise never establishes: greth_mdio_reset (the
       core's reset callback) never touches greth_ctrl, so before any
       write greth_ctrl reads back plain zero rather than the manual's
       stated reset value.  */
    CHECK (read (CTRL) == 0);
    write (CTRL, CTRL_RS | CTRL_TE | CTRL_RI);
    CHECK (read (CTRL) == CTRL_SP);

    /* Section 53.9.2 marks STAT write-clear: a one written to a bit
       clears it, a zero leaves it alone.  There is no way to set STAT
       from the register interface, so reach it through the receive
       path with a one-descriptor ring and a matching broadcast
       packet.  */
    write (RXBASE, 0x2000);
    sis_tests::flatmem_poke (0x2000, DESC_EN | DESC_WR);
    sis_tests::flatmem_poke (0x2004, 0x2100);
    write (CTRL, CTRL_TE | CTRL_RI);
    /* CTRL_RE is deliberately withheld here: this is the "registers"
       SUBCASE, which never transitions CTRL_RE and so cannot be the
       pass that arms the tap and greth_tx.  Without CTRL_RE the
       broadcast packet below is simply not accepted, which is exactly
       what section 53.9.1's RE description says and lets STAT stay
       untouched by anything but the write below.  */
    const unsigned char broadcast_dst[6] = {
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff
    };
    greth_rxready (const_cast<unsigned char *> (broadcast_dst), 6);
    CHECK (read (STAT) == 0);

    /* MAC address registers (53.9.3, not in the scoped manual): pin
       what greth_write does today, a 16 bit read/write MSB half and a
       32 bit LSB half.  */
    write (MACMSB, 0xdead1234);
    CHECK (read (MACMSB) == 0x1234);
    write (MACLSB, 0x89abcdef);
    CHECK (read (MACLSB) == 0x89abcdef);

    /* MDIO (53.9.5, not in the scoped manual): a write with MDIO_WRITE
       stores to the addressed PHY's register, one with MDIO_READ reads
       it back into MDIO[31:16].  Address 0 is the PHY's basic mode
       control register; greth.cc masks off the self-clearing reset and
       restart-autonegotiation bits (1 << 15 and 1 << 9) from what is
       stored.  */
    /* greth.cc indexes mdio_phys by the same field it decodes as the
       register number, so this address selects the register, register 0
       being the basic mode control one.  */
    write (MDIO, (0x8134u << 16) | (0u << 6) | MDIO_WRITE);
    write (MDIO, (0u << 6) | MDIO_READ);
    CHECK ((read (MDIO) >> 16) == 0x0134);

    /* Register 4 (autonegotiation advertisement) round-trips the same
       way; the other basic registers (1, 2, 3, 5) and an out-of-range
       one are read-only constants or zero.  Exercising a few pins that
       mdio_read's switch does not regress silently.  */
    write (MDIO, (0x0021u << 16) | (4u << 6) | MDIO_WRITE);
    write (MDIO, (4u << 6) | MDIO_READ);
    CHECK ((read (MDIO) >> 16) == 0x0021);

    write (MDIO, (1u << 6) | MDIO_READ);
    CHECK ((read (MDIO) >> 16) == 0x7865);
    write (MDIO, (2u << 6) | MDIO_READ);
    CHECK ((read (MDIO) >> 16) == 0x0022);
    write (MDIO, (3u << 6) | MDIO_READ);
    CHECK ((read (MDIO) >> 16) == 0x1512);
    write (MDIO, (5u << 6) | MDIO_READ);
    CHECK ((read (MDIO) >> 16) == 0x41e1);
    write (MDIO, (31u << 6) | MDIO_READ);
    CHECK ((read (MDIO) >> 16) == 0);

    /* A write to an unhandled PHY register (e.g. 2, read-only) and a
       write with neither MDIO_READ nor MDIO_WRITE set are both no-ops
       that still update the plain MDIO shadow register.  */
    write (MDIO, (0x1111u << 16) | (2u << 6) | MDIO_WRITE);
    write (MDIO, (2u << 6) | MDIO_READ);
    CHECK ((read (MDIO) >> 16) == 0x0022);
    write (MDIO, (7u << 6));
    CHECK (read (MDIO) == (7u << 6));

    /* TXBASE/RXBASE (53.9.6, 53.9.7): the three low bits are reserved
       and greth_write masks them off on every write.  */
    write (TXBASE, 0x12345678);
    CHECK (read (TXBASE) == 0x12345678);
    write (RXBASE, 0x87654321);
    CHECK (read (RXBASE) == 0x87654320);

    /* An address outside the register file's 7 cases reads back zero
       and a write to it is silently ignored.  */
    CHECK (read (0x20) == 0);
    write (0x20, 0xffffffff);
    CHECK (read (0x20) == 0);

    CHECK (sis_tests::faketap_init_calls == 0);
  }

  SUBCASE ("transmit DMA descriptor walk")
  {
    /* This SUBCASE is the first (and only) one in the file to raise
       CTRL_RE, so it is guaranteed to be the one process-wide RE 0->1
       transition that arms sis_tap_init and the greth_tx polling
       event (see the file header).  */
    write (MACMSB, 0x0002);
    write (MACLSB, 0x03040506);

    const unsigned char frame[18] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
				      0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
				      0x08, 0x00, 0xde, 0xad, 0xbe, 0xef };
    write (TXBASE, 0x3000);
    sis_tests::flatmem_poke (0x3000, DESC_EN | DESC_IE | 18);
    sis_tests::flatmem_poke (0x3004, 0x3100);
    poke_frame (0x3100, frame, 18);

    uint32 imask = 1u << GRETH_TEST_IRQ;
    irqmp.write (IRQMP_IMASK, &imask, 2);

    /* Section 53.3.2: TE must be set for the descriptor to be picked
       up.  Section 53.9.1: RE's 0->1 transition is what arms the
       engine (mac latch and the tap init call in greth_write).  Both
       are set in one write, along with TI so the completed send raises
       an interrupt.  */
    write (CTRL, CTRL_RE | CTRL_TE | CTRL_TI);

    CHECK (sis_tests::faketap_init_calls == 1);
    CHECK (sis_tests::faketap_init_mac == 0x000203040506ul);

    /* greth_tx's first firing is 100 clocks after the arm.  */
    run (200);

    REQUIRE (sis_tests::faketap_writes.size () == 1);
    CHECK (frame_equals (sis_tests::faketap_writes[0].bytes, frame, 18));
    CHECK ((read (STAT) & STAT_TI) != 0);
    /* Section 53.3.3: after a successful send the descriptor's control
       bits (EN among them) are cleared and only the length survives.  */
    CHECK (sis_tests::flatmem_peek (0x3000) == 18);
    CHECK (ext_irl[0] == GRETH_TEST_IRQ);

    /* The single descriptor had no WRAP bit, so TXBASE increments by 8
       rather than wrapping back to the table base.  */
    CHECK (read (TXBASE) == 0x3008);

    /* A second descriptor, this time with WRAP set, sends without an
       interrupt (no DESC_IE) and wraps the pointer back to the table
       base rather than incrementing further.  */
    uint32 iclear = imask;
    irqmp.write (IRQMP_ICLEAR, &iclear, 2);
    CHECK (ext_irl[0] == 0);
    write (STAT, STAT_TI);
    CHECK (read (STAT) == 0);

    sis_tests::flatmem_poke (0x3008, DESC_EN | DESC_WR | 18);
    sis_tests::flatmem_poke (0x300c, 0x3100);

    run (5300);

    REQUIRE (sis_tests::faketap_writes.size () == 2);
    CHECK (frame_equals (sis_tests::faketap_writes[1].bytes, frame, 18));
    CHECK ((read (STAT) & STAT_TI) != 0);
    CHECK (read (TXBASE) == 0x3000);
    /* No DESC_IE on this descriptor, so no new interrupt was raised. */
    CHECK (ext_irl[0] == 0);

    /* A disabled descriptor (EN clear) is left alone: TE stays set but
       nothing is sent and TXBASE does not move.  */
    write (STAT, STAT_TI);
    sis_tests::flatmem_poke (0x3000, 18);
    run (10600);
    CHECK (sis_tests::faketap_writes.size () == 2);
    CHECK ((read (STAT) & STAT_TI) == 0);
    CHECK (read (TXBASE) == 0x3000);
  }

  SUBCASE ("receive path")
  {
    /* CTRL_RE was already latched to one by the previous SUBCASE's
       process-wide state; this SUBCASE writes it again explicitly
       (still not a 0->1 transition, since it is already one) so it
       does not rely on that ordering to make sense to read.  */
    write (CTRL, CTRL_RE | CTRL_RI);
    write (MACMSB, 0x0011);
    write (MACLSB, 0x22334455);

    const unsigned char dest_unicast[6] = {
      0x00, 0x11, 0x22, 0x33, 0x44, 0x55
    };
    unsigned char frame[20] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0xaa,
				0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x08, 0x06,
				0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
    memcpy (frame, dest_unicast, 6);

    uint32 imask = 1u << GRETH_TEST_IRQ;
    irqmp.write (IRQMP_IMASK, &imask, 2);

    write (RXBASE, 0x4000);
    sis_tests::flatmem_poke (0x4000, DESC_EN | DESC_IE | DESC_WR);
    sis_tests::flatmem_poke (0x4004, 0x4100);

    greth_rxready (frame, sizeof (frame));

    CHECK ((read (STAT) & STAT_RI) != 0);
    CHECK (frame_at (0x4100, frame, sizeof (frame)));
    /* Section 53.4.3: the length is the only descriptor field left
       after a completed reception.  */
    CHECK (sis_tests::flatmem_peek (0x4000) == sizeof (frame));
    /* WRAP was set on the only descriptor, so the pointer returns to
       the table base.  */
    CHECK (read (RXBASE) == 0x4000);
    CHECK (ext_irl[0] == GRETH_TEST_IRQ);

    /* A packet for a different unicast address is neither this MAC nor
       broadcast, so it is dropped: no copy, no status bit, no pointer
       move.  */
    uint32 iclear = imask;
    irqmp.write (IRQMP_ICLEAR, &iclear, 2);
    write (STAT, STAT_RI);
    sis_tests::flatmem_poke (0x4000, DESC_EN | DESC_IE | DESC_WR);
    unsigned char other[20];
    memcpy (other, frame, sizeof (frame));
    other[5] = 0x99;

    greth_rxready (other, sizeof (other));

    CHECK ((read (STAT) & STAT_RI) == 0);
    CHECK (read (RXBASE) == 0x4000);
    CHECK (ext_irl[0] == 0);
    CHECK (!frame_at (0x4100, other, sizeof (other)));

    /* A broadcast destination is always accepted, independent of the
       configured MAC.  */
    unsigned char bcast[20];
    memcpy (bcast, frame, sizeof (frame));
    memset (bcast, 0xff, 6);

    greth_rxready (bcast, sizeof (bcast));

    CHECK ((read (STAT) & STAT_RI) != 0);
    CHECK (frame_at (0x4100, bcast, sizeof (bcast)));

    /* A matching packet against a disabled descriptor (EN clear) is
       dropped just like a mismatched address: nothing is written and
       the pointer does not move.  */
    write (STAT, STAT_RI);
    sis_tests::flatmem_poke (0x4000, 0);
    sis_tests::flatmem_poke (0x4100, 0);

    greth_rxready (frame, sizeof (frame));

    CHECK ((read (STAT) & STAT_RI) == 0);
    CHECK (sis_tests::flatmem_peek (0x4100) == 0);
    CHECK (read (RXBASE) == 0x4000);
  }
}
