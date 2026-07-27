/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Tests for the GR740 SpaceWire router and its GRSPW2 AMBA ports in
   grspw.cc.

   grspw.cc's own header states the scope: only what the RTEMS/Zephyr
   example applications exercise is modelled.  The SpaceWire cable ports
   accept configuration and report a link state but carry no traffic (no
   link layer at all), and the RMAP target of the AMBA ports is not
   emulated.  These tests drive only what the file implements and do not
   pin behaviour it deliberately leaves out.

   Two documentation sources are cited below, both under /opt/eb-docs:

   - The GR740 register chapter, chapter 13 "SpaceWire router" of
     GR740-UM-DS-2-10.pdf, split into chunks under
     SoC/Gaisler/GR740/text/datasheet/13-SpaceWire-router-*.md.  Cited as
     "GR740-UM-DS chunk NN, pN" plus a table number.  The corpus carries
     revision 2-10; ref/ in this tree only has 2-9, so the corpus wins
     whenever the two disagree (none found while writing this file).
   - The GRSPW2 IP core chapter (chapter 78) of grip.pdf, indexed under
     IP/SpaceWire/Gaisler/GRSPW2/text/datasheet/78-*.md.  This is the
     document grspw.cc's own comments call "GRIP chapter 78".  Cited as
     "GRIP chunk NN, pN" plus a table number.

   One transcription artefact in the GR740 chunk worth recording: chunk 03
   (13-SpaceWire-router-03-AMBA-interface.md) heads table 169
   (RTR.AMBAINTTO1) with the same "0xAC" address as table 168
   (RTR.AMBAINTTO0), and heads both table 170 (RTR.AMBAINTMSK0) and table
   171 (RTR.AMBAINTMSK1, chunk 04) with "0xB0".  GRIP's independently
   produced register table (chunk 04, table 1392) gives the AMBA port's
   INTTO/INTTOEXT/TICKMASK/AUTOACK a clean 0xAC/0xB0/0xB4/0xB8 spacing,
   which is also what grspw.cc's SPW_INTMSK (0xB4) and SPW_INTMSKX (0xB8)
   macros use.  This looks like a copy-paste heading error in the GR740
   chunk rather than a real hardware difference; this file follows GRIP
   and grspw.cc.  grspw.cc does not implement the ISR/ISR-change timers at
   all (INTTO/INTTOEXT, GR740-UM-DS tables 168/169, GRIP tables
   1407/1408), consistent with "only what the example applications
   exercise".

   Another point worth recording for physical/path addressing (table
   175's note, GR740-UM-DS chunk 05): on real hardware RTR.RTACTRL.EN and
   RTR.RTACTRL.HD are hardware constants of 1 for physical addresses
   (1-31), and RTR.RTPMAP is consulted for group-adaptive/redundant
   routing when explicitly programmed away from its default.  grspw.cc's
   spw_route does not model RTPMAP/RTACTRL for addresses below 32 at all:
   any first byte 1-31 is routed straight to the port with that number
   and the header byte is always stripped (the "target = addr; strip =
   1;" branch), which is exactly the default/common case and leaves the
   redundancy override out.  This matches the header's stated scope
   ("only what the example applications exercise") rather than being a
   defect, so it is pinned as current behaviour below and not treated as
   a bug.

   A fixture here drives five cores at once (the router plus all four
   GRSPW2 AMBA ports) with no board, the way tests/grlibcore.h drives one.
   grlib_core_fixture itself only holds a single core pointer, so this
   file keeps its own fixture rather than reusing it, following the same
   save/restore and flatmem/reset_all discipline.  struct grlib_ipcore has
   no instance pointer (see CLAUDE.md), so the four AMBA ports are the
   four instantiations the SPW_CORE macro in grspw.cc generates
   (spwpkt0..spwpkt3); their per-instance state (struct spw_core in
   grspw.cc) is a plain array indexed by the compile-time n each macro
   expansion bakes in, reset by each instance's own reset callback.  The
   router's rtr_regs and the per-port spw[] array are process-wide statics
   with no owner, so this fixture resets every core it touches on both
   ends, leaving nothing for a file that runs before or after it in the
   same process.  */

#include "doctest.h"
#include "support.h"

#include "config.h"
#include "grlibcore.h"

#include <string.h>

using sis_tests::stdout_capture;

namespace
{

/* Delay grspw.cc arms a TX DMA event with (SPW_XFER_DELAY in grspw.cc,
   private to that file) plus margin, and the largest packet the
   emulation carries (SPW_MAX_PKT, also private). */
const uint64 XFER_MARGIN = 600;
const uint32 MAX_PKT = 2048;

/* spw_read_bytes/spw_write_bytes reach a packet header/data/receive
   buffer through ptr[((addr & 3) + i) ^ arch->bswap] (grspw.cc), the same
   convention tests/greth.cc's poke_frame/frame_at document: with the
   sparc32 fixture arch (bswap == 3 on a little endian host) that XOR
   reverses the byte order within each 32-bit word between a plain byte
   buffer and a word poked into flatmem, which stores words in host
   (little endian) byte order.  Packing bytes big-endian into the word
   handed to flatmem_poke, or reading one back and unpacking it
   big-endian, cancels the reversal out.  Every packet buffer in this file
   is at most 4 bytes, so one word suffices; unused trailing bytes are
   padded with zero. */
uint32
be32 (unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3)
{
  return ((uint32) b0 << 24) | ((uint32) b1 << 16) | ((uint32) b2 << 8) | b3;
}

void
poke_pkt (uint32 addr, unsigned char b0, unsigned char b1 = 0,
	  unsigned char b2 = 0, unsigned char b3 = 0)
{
  sis_tests::flatmem_poke (addr, be32 (b0, b1, b2, b3));
}

unsigned char
byte_at (uint32 addr, uint32 offset = 0)
{
  uint32 word = sis_tests::flatmem_peek (addr);
  return (unsigned char) ((word >> (24 - 8 * offset)) & 0xFFu);
}

/* ---------------------- Router registers ---------------------------

   Offsets from GR740-UM-DS chunk 05 (13-SpaceWire-router-05-Registers.md,
   pp188-204), table 174 "GRSPWROUTER registers".  */

const uint32 RTR_RTPMAP = 0x004;      /* table 174/175, physical 1-12 */
const uint32 RTR_RTPMAP_LOG = 0x080;  /* table 174/175, logical 32-255 */
const uint32 RTR_RTACTRL = 0x404;     /* table 174/176, physical 1-12 */
const uint32 RTR_RTACTRL_LOG = 0x480; /* table 174/176, logical 32-255 */
const uint32 RTR_PCTRLCFG = 0x800;    /* table 174/177, port 0 */
const uint32 RTR_PCTRL = 0x804;	      /* table 174/178, port 1 */
const uint32 RTR_PSTS = 0x884;	      /* table 174/180, port 1 */
const uint32 RTR_PCTRL2 = 0x984;      /* table 174/183, port 1 */
const uint32 RTR_RTRCFG = 0xA00;      /* table 174/184 */
const uint32 RTR_TC = 0xA04;	      /* table 174/185 */
const uint32 RTR_VER = 0xA08;	      /* table 174/186 */
const uint32 RTR_ISR0 = 0xA28;	      /* table 174/194 */
const uint32 RTR_ISR1 = 0xA2C;	      /* table 174/195 */
const uint32 RTR_CAP = 0xA44;	      /* table 174/200 */

uint32
rtr_pctrl (int port)
{
  return RTR_PCTRLCFG + 4 * (uint32) port;
}
uint32
rtr_pctrl2 (int port)
{
  return 0x980 + 4 * (uint32) port;
}
uint32
rtr_psts (int port)
{
  return 0x880 + 4 * (uint32) port;
}

/* Port numbering, GR740-UM-DS chunk 01 (13-SpaceWire-router-01-Overview
   -13-1.md): port 0 is the configuration port, 1-8 the SpaceWire cable
   ports, 9-12 the AMBA ports (matching grspw.cc's SPW_NPORTS_SPW/
   SPW_AMBA_PORT0).  */
const int PORT_CABLE1 = 1;
const int PORT_AMBA0 = 9;
const int PORT_AMBA1 = 10;

/* RTR.PCTRL bits, table 178. */
const uint32 PCTRL_IC = 1u << 15;
const uint32 PCTRL_ET = 1u << 14;
const uint32 PCTRL_TE = 1u << 5;

/* RTR.PCTRL2 bits, table 183. */
const uint32 PCTRL2_AT = 1u << 12;
const uint32 PCTRL2_AR = 1u << 11;
const uint32 PCTRL2_IT = 1u << 10;
const uint32 PCTRL2_IR = 1u << 9;

/* RTR.PSTS bits, table 180. */
const uint32 PSTS_PT_BIT = 30;
const uint32 PSTS_PT_AMBA = 1u;
const uint32 PSTS_LS_BIT = 12;
const uint32 PSTS_LS_RUN = 5u;
const uint32 PSTS_IA = 1u << 4; /* write-clear, "Invalid address" */

/* RTR.RTACTRL bits, table 176. */
const uint32 RTACTRL_HD = 1u << 0;
const uint32 RTACTRL_EN = 1u << 2;

/* RTR.RTRCFG bits, table 184. */
const uint32 RTRCFG_PP = 1u << 0;  /* r, constant 1 */
const uint32 RTRCFG_TA = 1u << 1;  /* r, constant 1 */
const uint32 RTRCFG_ME = 1u << 2;  /* wc */
const uint32 RTRCFG_TF = 1u << 3;  /* rw */
const uint32 RTRCFG_IE = 1u << 8;  /* rw */
const uint32 RTRCFG_PE = 1u << 14; /* r, constant 1 */
const uint32 RTRCFG_SR = 1u << 15; /* r, constant 1 */
const uint32 RTRCFG_SP = 8u << 27; /* r, constant 8 SpaceWire ports */
const uint32 RTRCFG_AP = 4u << 22; /* r, constant 4 AMBA ports */

/* RTR.TC bits, table 185. */
const uint32 TC_TC = 0x3F;
const uint32 TC_CF = 0xC0;
const uint32 TC_EN = 1u << 8;
const uint32 TC_RE = 1u << 9;

/* RTR.CAP bits, table 200. */
const uint32 CAP_SD = 1u << 10;
const uint32 CAP_ID = 1u << 11;
const uint32 CAP_AX = 1u << 13;

/* --------------------- GRSPW2 AMBA port registers --------------------

   Offsets from GRIP chunk 04 (78-GRSPW2-SpaceWire-codec-with-AHB-host-
   04-Registers.md, pp1179-1196), table 1392, cross-checked against the
   GR740-specific RTR.AMBA* names of GR740-UM-DS chunk 03
   (13-SpaceWire-router-03-AMBA-interface.md).  */

const uint32 SPW_CTRL = 0x00;	  /* GRIP table 1392/1393 */
const uint32 SPW_STATUS = 0x04;	  /* GRIP table 1392/1394 */
const uint32 SPW_NODEADDR = 0x08; /* GRIP table 1392/1395 (DEFADDR) */
const uint32 SPW_TC = 0x14;	  /* GRIP table 1392/1398 */
const uint32 SPW_DMA0 = 0x20;	  /* GRIP table 1392, channel 1 base */
const uint32 SPW_INTCTRL = 0xA0;  /* GRIP table 1392/1404 */
const uint32 SPW_INTRX = 0xA4;	  /* GRIP table 1392/1405 */
const uint32 SPW_ICACK = 0xA8;	  /* GRIP table 1392/1406 */
const uint32 SPW_INTMSK = 0xB4;	  /* GRIP table 1392 TICKMASK, see header */
const uint32 SPW_INTMSKX = 0xB8;  /* GRIP table 1392 AUTOACK, see header */

uint32
spw_dma (int ch)
{
  return SPW_DMA0 + 32 * (uint32) ch;
}
const uint32 SPW_DMA_CTRL = 0x00;
const uint32 SPW_DMA_RXMAX = 0x04;
const uint32 SPW_DMA_TXDESC = 0x08;
const uint32 SPW_DMA_RXDESC = 0x0C;
const uint32 SPW_DMA_ADDR = 0x10;

/* CTRL bits, GRIP table 1393. */
const uint32 CTRL_RA = 1u << 31; /* r, RMAP available */
const uint32 CTRL_RX = 1u << 30; /* r, unaligned rx available */
const uint32 CTRL_RC = 1u << 29; /* r, RMAP CRC available */
const uint32 CTRL_DI = 1u << 24; /* r, interrupt distribution available */
const uint32 CTRL_IE = 1u << 3;
const uint32 CTRL_TQ = 1u << 8;
const uint32 CTRL_TI = 1u << 4;
const uint32 CTRL_PM = 1u << 5;

/* STATUS bits, GRIP table 1394. */
const uint32 STATUS_LS_BIT = 21;
const uint32 STATUS_LS_RUN = 5u;
const uint32 STATUS_TO = 1u << 0;

/* NODEADDR (DEFADDR) fields, GRIP table 1395. */
const uint32 ADDR_MASK_BIT = 8;

/* TC fields, GRIP table 1398. */
const uint32 SPWTC_TIMECNT = 0x3F;

/* DMACTRL bits, GRIP table 1399. */
const uint32 DMACTRL_TE = 1u << 0;
const uint32 DMACTRL_RE = 1u << 1;
const uint32 DMACTRL_TI = 1u << 2;
const uint32 DMACTRL_RI = 1u << 3;
const uint32 DMACTRL_PS = 1u << 5;
const uint32 DMACTRL_PR = 1u << 6;
const uint32 DMACTRL_TA = 1u << 7;
const uint32 DMACTRL_RA = 1u << 8;
const uint32 DMACTRL_EN = 1u << 13;

/* INTCTRL bits, GRIP table 1404. */
const uint32 INTCTRL_TXINT = 0x3F;
const uint32 INTCTRL_II = 1u << 6;
const uint32 INTCTRL_ID = 1u << 7;
const uint32 INTCTRL_AA = 1u << 15;
const uint32 INTCTRL_IQ = 1u << 18;
const uint32 INTCTRL_AQ = 1u << 19;
const uint32 INT_ACK = 0x20;

/* Tx descriptor; layout transcribed from grspw.cc's SPW_TXBD_* macros,
   which cite no table directly. */
const uint32 TXBD_EN = 1u << 12;
const uint32 TXBD_WR = 1u << 13;
const uint32 TXBD_IE = 1u << 14;

/* Rx descriptor; layout transcribed from grspw.cc's SPW_RXBD_* macros. */
const uint32 RXBD_EN = 1u << 25;
const uint32 RXBD_WR = 1u << 26;
const uint32 RXBD_IE = 1u << 27;
const uint32 RXBD_TR = 1u << 31;

const uint32 IRQMP_ICLEAR = 0x0c;
const uint32 IRQMP_IMASK = 0x40;

/* Distinct, non-reserved test IRQ lines for the four AMBA ports, the way
   tests/greth.cc picks GRETH_TEST_IRQ. */
const int SPW_IRQ0 = 4;
const int SPW_IRQ1 = 5;
const int SPW_IRQ2 = 6;
const int SPW_IRQ3 = 7;

}

/* grspw_irq[] is a plain global array grspw.cc defines with no header of
   its own (like greth_irq in greth.cc), reached the same way. */
extern int grspw_irq[4];

namespace
{

struct grspw_fixture
{
  int saved_verbose;
  int saved_ncpu;
  const struct memsys *saved_ms;
  const struct cpu_arch *saved_arch;
  float32 saved_freq;
  int saved_irq[4];

  grspw_fixture ()
      : saved_verbose (sis_verbose), saved_ncpu (ncpu), saved_ms (ms),
	saved_arch (arch), saved_freq (ebase.freq)
  {
    ms = &sis_tests::flatmem;
    arch = &sparc32;
    sis_verbose = 0;
    ncpu = 1;
    ebase.freq = 1;
    ebase.simtime = 0;
    ebase.simstart = 0;
    sis_tests::flatmem_clear ();

    reset_all ();

    spwrouter.reset ();
    spwpkt0.reset ();
    spwpkt1.reset ();
    spwpkt2.reset ();
    spwpkt3.reset ();

    for (int i = 0; i < 4; i++)
      saved_irq[i] = grspw_irq[i];
    grspw_irq[0] = SPW_IRQ0;
    grspw_irq[1] = SPW_IRQ1;
    grspw_irq[2] = SPW_IRQ2;
    grspw_irq[3] = SPW_IRQ3;

    /* IRQMP is a second core with its own process-wide statics, reached
       directly the way tests/irqmp.cc and tests/greth.cc do. */
    irqmp.init ();
    irqmp.reset ();
    for (int i = 0; i < NCPU; i++)
      ext_irl[i] = 0;
  }

  ~grspw_fixture ()
  {
    reset_all ();

    irqmp.reset ();
    for (int i = 0; i < NCPU; i++)
      ext_irl[i] = 0;

    for (int i = 0; i < 4; i++)
      grspw_irq[i] = saved_irq[i];

    ncpu = saved_ncpu;
    sis_verbose = saved_verbose;
    ms = saved_ms;
    arch = saved_arch;
    ebase.freq = saved_freq;
  }

  static uint32
  rtr_read (uint32 offset)
  {
    uint32 data = 0;
    spwrouter.read (offset, &data);
    return data;
  }

  static void
  rtr_write (uint32 offset, uint32 value)
  {
    spwrouter.write (offset, &value, 2);
  }

  static uint32
  port_read (const struct grlib_ipcore &core, uint32 offset)
  {
    uint32 data = 0;
    core.read (offset, &data);
    return data;
  }

  static void
  port_write (const struct grlib_ipcore &core, uint32 offset, uint32 value)
  {
    core.write (offset, &value, 2);
  }

  static void
  run (uint64 clocks)
  {
    advance_time (ebase.simtime + clocks);
  }

  static void
  unmask_irq (int line)
  {
    /* Read-modify-write: several cases unmask more than one line on the
       same IRQMP, and IRQMP_IMASK is a plain register, not accumulated
       by the core, so a second plain write would stomp the first. */
    uint32 imask = 0;
    irqmp.read (IRQMP_IMASK, &imask);
    imask |= 1u << line;
    irqmp.write (IRQMP_IMASK, &imask, 2);
  }

  static void
  clear_irq (int line)
  {
    uint32 iclear = 1u << line;
    irqmp.write (IRQMP_ICLEAR, &iclear, 2);
  }
};

}

/* -------------------------------------------------------------------- */
/* Router registers                                                      */
/* -------------------------------------------------------------------- */

TEST_CASE_FIXTURE (grspw_fixture, "SpaceWire router registers")
{
  SUBCASE ("reset values")
  {
    /* GR740-UM-DS chunk 05, table 184: SP/AP are constants 8 and 4, SR/PE/
       TA/PP are constant one. */
    CHECK (rtr_read (RTR_RTRCFG) == (RTRCFG_SP | RTRCFG_AP | RTRCFG_SR |
				     RTRCFG_PE | RTRCFG_TA | RTRCFG_PP));

    /* Table 185: time-codes are globally enabled out of reset. */
    CHECK (rtr_read (RTR_TC) == TC_EN);

    /* Table 178: RTR.PCTRL.TE resets to one on every port from 1 to 12;
       grspw.cc's reset loop starts at port 1, so the configuration
       port's slot at offset 0 (table 177's narrower layout) is left at
       plain zero. */
    CHECK ((rtr_read (rtr_pctrl (0)) & PCTRL_TE) == 0);
    CHECK ((rtr_read (rtr_pctrl (PORT_CABLE1)) & PCTRL_TE) != 0);
    CHECK ((rtr_read (rtr_pctrl (12)) & PCTRL_TE) != 0);

    /* Table 183: the four distributed interrupt direction bits reset to
       one on every port, including the configuration port. */
    uint32 dirs = PCTRL2_IR | PCTRL2_IT | PCTRL2_AR | PCTRL2_AT;
    CHECK ((rtr_read (rtr_pctrl2 (0)) & dirs) == dirs);
    CHECK ((rtr_read (rtr_pctrl2 (PORT_CABLE1)) & dirs) == dirs);

    /* Table 186: version 1.2.0. */
    CHECK (rtr_read (RTR_VER) == ((1u << 24) | (2u << 16) | 1u));

    /* Table 200: SpaceWire-D, distributed interrupt and auxiliary
       time-code support are all advertised. */
    CHECK (rtr_read (RTR_CAP) == (CAP_SD | CAP_ID | CAP_AX));
  }

  SUBCASE ("RTRCFG: read-only fields, ME write-clear, the rest read/write")
  {
    uint32 before = rtr_read (RTR_RTRCFG);

    rtr_write (RTR_RTRCFG, 0xFFFFFFFF);
    uint32 after = rtr_read (RTR_RTRCFG);

    CHECK ((after & (RTRCFG_SP | RTRCFG_AP | RTRCFG_SR | RTRCFG_PE |
		     RTRCFG_TA | RTRCFG_PP)) ==
	   (before & (RTRCFG_SP | RTRCFG_AP | RTRCFG_SR | RTRCFG_PE |
		      RTRCFG_TA | RTRCFG_PP)));
    CHECK ((after & RTRCFG_ME) == 0);
    CHECK ((after & RTRCFG_IE) != 0);
    CHECK ((after & RTRCFG_TF) != 0);

    /* A write with ME clear leaves ME (still 0 here) untouched rather
       than forcing it to 0.  Writing every other bit as 0, including
       the read-only constants, must not actually clear them: this is
       the direction the write-as-1 check above cannot tell apart from
       a field that quietly became writable, since every constant here
       already reset to 1. */
    rtr_write (RTR_RTRCFG, 0);
    CHECK ((rtr_read (RTR_RTRCFG) & RTRCFG_ME) == 0);
    CHECK ((rtr_read (RTR_RTRCFG) & (RTRCFG_SP | RTRCFG_AP | RTRCFG_SR |
				     RTRCFG_PE | RTRCFG_TA | RTRCFG_PP)) ==
	   (before & (RTRCFG_SP | RTRCFG_AP | RTRCFG_SR | RTRCFG_PE |
		      RTRCFG_TA | RTRCFG_PP)));
  }

  SUBCASE ("VER: only the low instance-ID byte is writable")
  {
    uint32 before = rtr_read (RTR_VER);

    rtr_write (RTR_VER, 0xAABBCCDD);

    CHECK (rtr_read (RTR_VER) == ((before & 0xFFFFFF00) | 0xDD));
  }

  SUBCASE ("TC: counter/flags read-only, EN fully read/write, RE "
	   "self-clears and resets counter and flags")
  {
    /* TC and CF only ever keep their old value or get reset by RE; they
       never take a bit from a plain write.  EN, unlike them, is a plain
       read/write field that a write fully replaces (not just ORs in),
       so writing without EN set clears it even though TC/CF stay 0. */
    rtr_write (RTR_TC, TC_TC | TC_CF);
    CHECK (rtr_read (RTR_TC) == 0);

    rtr_write (RTR_TC, TC_EN);
    CHECK (rtr_read (RTR_TC) == TC_EN);

    rtr_write (RTR_TC, TC_EN | TC_RE);
    CHECK (rtr_read (RTR_TC) == TC_EN);
  }

  SUBCASE ("CAP is entirely read-only")
  {
    rtr_write (RTR_CAP, 0);
    CHECK (rtr_read (RTR_CAP) == (CAP_SD | CAP_ID | CAP_AX));
  }

  SUBCASE ("ISR0/ISR1 are write-clear diagnostics registers")
  {
    rtr_write (RTR_ISR0, 0xFFFFFFFF);
    CHECK (rtr_read (RTR_ISR0) == 0);
    rtr_write (RTR_ISR1, 0xFFFFFFFF);
    CHECK (rtr_read (RTR_ISR1) == 0);
  }

  SUBCASE ("PSTS: port type and link-running are synthesised on read, "
	   "and the sticky bits are write-clear")
  {
    /* Table 180: PT is 0 for a SpaceWire cable port (1-8) and 1 for an
       AMBA port (9-12); grspw.cc reports a cable port as always
       link-running even though no link layer is emulated. */
    uint32 cable = rtr_read (rtr_psts (PORT_CABLE1));
    CHECK (((cable >> PSTS_PT_BIT) & 1) == 0);
    CHECK (((cable >> PSTS_LS_BIT) & 7) == PSTS_LS_RUN);

    uint32 amba = rtr_read (rtr_psts (PORT_AMBA0));
    CHECK (((amba >> PSTS_PT_BIT) & PSTS_PT_AMBA) == PSTS_PT_AMBA);
    /* grspw.cc only synthesises the link-running bit for cable ports 1-8;
       an AMBA port's status word is plain storage apart from PT. */
    CHECK (((amba >> PSTS_LS_BIT) & 7) == 0);

    rtr_write (rtr_psts (PORT_AMBA0), PSTS_IA);
    CHECK ((rtr_read (rtr_psts (PORT_AMBA0)) & PSTS_IA) == 0);

    /* A port number in the PSTS range that is neither a cable port
       (1-8) nor an AMBA port (9-12): physical addresses 13-31 do not
       exist (table 174's note), so neither synthesis applies and the
       word is plain storage. */
    rtr_write (rtr_psts (20), 0);
    CHECK (((rtr_read (rtr_psts (20)) >> PSTS_PT_BIT) & 3) == 0);
    CHECK (((rtr_read (rtr_psts (20)) >> PSTS_LS_BIT) & 7) == 0);

    /* spwrouter_read does not mask a sub-word address off before
       computing the synthesised port number (only the read of rtr_regs
       itself is word-indexed, by integer division): an address one byte
       into the port 1 status word still lands inside the ">RTR_PSTS"
       range check but computes port 0, which is neither a cable port
       (1-8) nor an AMBA port (9-12) either, the same as the port 20
       case above.  Nothing in this tree issues such an unaligned
       access; this pins spwrouter_read's current behaviour for it
       rather than a documented requirement. */
    uint32 data = 0;
    spwrouter.read (rtr_psts (0) + 1, &data);
    CHECK (((data >> PSTS_PT_BIT) & 3) == 0);
    CHECK (((data >> PSTS_LS_BIT) & 7) == 0);
  }

  SUBCASE ("The routing table registers are plain storage")
  {
    rtr_write (RTR_RTPMAP, 0x1234);
    CHECK (rtr_read (RTR_RTPMAP) == 0x1234);
    rtr_write (RTR_RTACTRL, RTACTRL_EN | RTACTRL_HD);
    CHECK (rtr_read (RTR_RTACTRL) == (RTACTRL_EN | RTACTRL_HD));
  }

  SUBCASE ("add reports the mapped address only when verbose")
  {
    {
      stdout_capture cap;
      spwrouter.add (SPW_IRQ0, 0xFF900000, 0xfff);
      CHECK (cap.str ().empty ());
    }
    {
      sis_verbose = 2;
      stdout_capture cap;
      spwrouter.add (SPW_IRQ0, 0xFF900000, 0xfff);
      CHECK (cap.str ().find ("SpaceWire router") != std::string::npos);
      sis_verbose = 0;
    }
  }
}

/* -------------------------------------------------------------------- */
/* GRSPW2 AMBA port registers                                            */
/* -------------------------------------------------------------------- */

TEST_CASE_FIXTURE (grspw_fixture, "GRSPW2 AMBA port registers")
{
  SUBCASE ("reset values")
  {
    /* grspw.cc's own comment cites "the GR740 user manual, GRSPW2
       control register" for the capability constants it reports
       (RA/RX/RC/DI, GRIP table 1393, and NCH = 3 for four channels).
       The RMAP target is explicitly not emulated (grspw.cc's reset
       comment), so RMAP-related bits are left out of what is asserted
       here. */
    uint32 ctrl = port_read (spwpkt0, SPW_CTRL);
    CHECK ((ctrl & CTRL_RA) != 0);
    CHECK ((ctrl & CTRL_RX) != 0);
    CHECK ((ctrl & CTRL_RC) != 0);
    CHECK ((ctrl & CTRL_DI) != 0);
    CHECK (((ctrl >> 27) & 0x3) == 3); /* NCH_MAX: four channels */

    CHECK (((port_read (spwpkt0, SPW_STATUS) >> STATUS_LS_BIT) & 7) ==
	   STATUS_LS_RUN);

    /* GRIP table 1395: reset default address is 254, mask 0. */
    CHECK (port_read (spwpkt0, SPW_NODEADDR) == 254);

    /* Each of the four SPW_CORE instances resets its own struct
       spw_core, not a shared one: writing port 0 must not leak into
       port 1. */
    port_write (spwpkt0, SPW_NODEADDR, 0x1234);
    CHECK (port_read (spwpkt1, SPW_NODEADDR) == 254);
  }

  SUBCASE ("CTRL: capability fields read-only, TI increments TC and "
	   "sends a time-code")
  {
    uint32 caps = port_read (spwpkt0, SPW_CTRL) & 0xFF000000;

    /* CTRL_TI is masked off this round-trip write: a set TI bit is not
       just a stored field, it triggers a tick (covered next), so
       leaving it out here keeps this check to the plain capability
       masking. */
    port_write (spwpkt0, SPW_CTRL, 0xFFFFFFFF & ~CTRL_TI);
    CHECK ((port_read (spwpkt0, SPW_CTRL) & 0xFF000000) == caps);
    CHECK ((port_read (spwpkt0, SPW_TC) & SPWTC_TIMECNT) == 0);

    port_write (spwpkt0, SPW_CTRL, CTRL_TI);
    CHECK ((port_read (spwpkt0, SPW_TC) & SPWTC_TIMECNT) == 1);
    CHECK ((port_read (spwpkt0, SPW_CTRL) & CTRL_TI) == 0);

    /* With RTR.PCTRL.TE set (reset default) on both the sending and a
       receiving AMBA port, and the router's own RTR.TC.EN set (reset
       default), the new value is forwarded to the other AMBA ports
       (section 13.2.17) and each sets its own tick-out status and, since
       CTRL.TQ and CTRL.IE are set here, raises an interrupt (section
       13.4.3.1). */
    port_write (spwpkt1, SPW_CTRL, CTRL_TQ | CTRL_IE);
    unmask_irq (SPW_IRQ1);

    port_write (spwpkt0, SPW_CTRL, CTRL_TI);

    CHECK ((port_read (spwpkt1, SPW_TC) & SPWTC_TIMECNT) == 2);
    CHECK ((port_read (spwpkt1, SPW_STATUS) & STATUS_TO) != 0);
    CHECK (ext_irl[0] == SPW_IRQ1);
    clear_irq (SPW_IRQ1);

    /* Without CTRL.TQ the tick-out status is still set but no interrupt
       is raised. */
    port_write (spwpkt1, SPW_STATUS, STATUS_TO);
    port_write (spwpkt1, SPW_CTRL, CTRL_IE);
    port_write (spwpkt0, SPW_CTRL, CTRL_TI);
    CHECK ((port_read (spwpkt1, SPW_STATUS) & STATUS_TO) != 0);
    CHECK (ext_irl[0] == 0);

    /* Disabling PCTRL.TE on the router's slot for the sending port stops
       the time-code from ever being distributed: the router's own
       RTR.TC does not move. */
    rtr_write (rtr_pctrl (PORT_AMBA0), 0);
    uint32 rtr_tc_before = rtr_read (RTR_TC);
    port_write (spwpkt0, SPW_CTRL, CTRL_TI);
    CHECK (rtr_read (RTR_TC) == rtr_tc_before);
  }

  SUBCASE ("CTRL.TI with RTR.PCTRL.ET set only forwards a value that "
	   "continues the router's own counter")
  {
    /* Section 13.2.17 / grspw.cc's spw_tc_distribute comment: with
       RTR.PCTRL.ET clear (the default) the router discards what it
       receives and forwards its own incremented counter instead
       (already pinned above).  With ET set, the received value updates
       the router counter but is forwarded only when it equals the
       router's old counter plus one, modulo 64. */
    rtr_write (rtr_pctrl (PORT_AMBA0), PCTRL_TE | PCTRL_ET);
    port_write (spwpkt1, SPW_CTRL, CTRL_TQ | CTRL_IE);
    unmask_irq (SPW_IRQ1);

    /* spwpkt0's own TC starts at 0 and CTRL_TI increments it to 1 before
       sending, which is exactly the router's old counter (0) plus one:
       this value is forwarded. */
    port_write (spwpkt0, SPW_CTRL, CTRL_TI);
    CHECK ((rtr_read (RTR_TC) & TC_TC) == 1);
    CHECK ((port_read (spwpkt1, SPW_TC) & SPWTC_TIMECNT) == 1);
    clear_irq (SPW_IRQ1);

    /* GRIP table 1398: TC can be written directly, and the written
       value is not itself transmitted since CTRL_TI increments it
       before sending.  Jump spwpkt0's own counter far ahead so its next
       CTRL_TI sends a value that is not the router's old value plus
       one; the router still records the value (its counter still
       updates to it) but does not forward it, so spwpkt1 sees no new
       time-code and no interrupt. */
    port_write (spwpkt0, SPW_TC, 40);
    port_write (spwpkt0, SPW_CTRL, CTRL_TI);
    CHECK ((rtr_read (RTR_TC) & TC_TC) == 41);
    CHECK ((port_read (spwpkt1, SPW_TC) & SPWTC_TIMECNT) == 1);
    CHECK (ext_irl[0] == 0);
  }

  SUBCASE ("STATUS is write-clear except the read-only link state field")
  {
    uint32 ls = port_read (spwpkt0, SPW_STATUS) & (0x7u << STATUS_LS_BIT);

    port_write (spwpkt0, SPW_STATUS, 0xFFFFFFFF);

    CHECK ((port_read (spwpkt0, SPW_STATUS) & (0x7u << STATUS_LS_BIT)) == ls);
    CHECK ((port_read (spwpkt0, SPW_STATUS) & STATUS_TO) == 0);
  }

  SUBCASE ("INTCTRL: ID sticky/write-clear, II self-clears, sending a "
	   "code the router does not carry anywhere sets ID")
  {
    /* No AMBA port other than the sender has PCTRL.IC set by default
       (only PCTRL.TE resets to one, per table 178), and RTR.RTRCFG.IE
       itself resets to 0 and RTRCFG.TF resets to 0, so a code sent here
       is discarded by spw_dint_distribute's first check (which, with
       RTRCFG.TF clear, treats it as a time-code instead) and never
       "sent" in the interrupt sense, so ID is set. */
    port_write (spwpkt0, SPW_INTMSK, 0xFFFFFFFF);
    port_write (spwpkt0, SPW_INTCTRL, INTCTRL_II | 5u);

    CHECK ((port_read (spwpkt0, SPW_INTCTRL) & INTCTRL_ID) != 0);
    CHECK ((port_read (spwpkt0, SPW_INTCTRL) & INTCTRL_II) == 0);
    CHECK ((port_read (spwpkt0, SPW_INTCTRL) & INTCTRL_TXINT) == 5u);

    /* ID is write-clear and, unlike every other field here, is held out
       of a plain overwrite: writing without an explicit 1 in ID's bit
       position must not clear it. */
    port_write (spwpkt0, SPW_INTCTRL, 5u);
    CHECK ((port_read (spwpkt0, SPW_INTCTRL) & INTCTRL_ID) != 0);
    port_write (spwpkt0, SPW_INTCTRL, INTCTRL_ID);
    CHECK ((port_read (spwpkt0, SPW_INTCTRL) & INTCTRL_ID) == 0);
  }

  SUBCASE ("INTRX/ICACK are write-clear")
  {
    port_write (spwpkt0, SPW_INTRX, 0xFFFFFFFF);
    CHECK (port_read (spwpkt0, SPW_INTRX) == 0);
    port_write (spwpkt0, SPW_ICACK, 0xFFFFFFFF);
    CHECK (port_read (spwpkt0, SPW_ICACK) == 0);
  }

  SUBCASE ("A DMA control register write masks the write-one-to-clear "
	   "status bits and leaves other fields a plain store")
  {
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL,
		DMACTRL_PS | DMACTRL_PR | DMACTRL_TA | DMACTRL_RA);
    CHECK ((port_read (spwpkt0, spw_dma (0) + SPW_DMA_CTRL) &
	    (DMACTRL_PS | DMACTRL_PR | DMACTRL_TA | DMACTRL_RA)) == 0);

    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_RE);
    CHECK ((port_read (spwpkt0, spw_dma (0) + SPW_DMA_CTRL) & DMACTRL_RE) !=
	   0);
  }

  SUBCASE ("Registers outside the switch's special cases are a plain "
	   "store, including the other three DMA channel words")
  {
    port_write (spwpkt0, SPW_NODEADDR, 0x0201);
    CHECK (port_read (spwpkt0, SPW_NODEADDR) == 0x0201);

    port_write (spwpkt0, spw_dma (2) + SPW_DMA_RXMAX, 512);
    CHECK (port_read (spwpkt0, spw_dma (2) + SPW_DMA_RXMAX) == 512);
    port_write (spwpkt0, spw_dma (2) + SPW_DMA_TXDESC, 0x2000);
    CHECK (port_read (spwpkt0, spw_dma (2) + SPW_DMA_TXDESC) == 0x2000);
    port_write (spwpkt0, spw_dma (2) + SPW_DMA_RXDESC, 0x3000);
    CHECK (port_read (spwpkt0, spw_dma (2) + SPW_DMA_RXDESC) == 0x3000);
    port_write (spwpkt0, spw_dma (2) + SPW_DMA_ADDR, 0x0102);
    CHECK (port_read (spwpkt0, spw_dma (2) + SPW_DMA_ADDR) == 0x0102);

    /* An address past every case (SPW_REGS is 0x100) still round trips
       through the same default branch. */
    port_write (spwpkt0, 0xF0, 0xdeadbeef);
    CHECK (port_read (spwpkt0, 0xF0) == 0xdeadbeef);
  }

  SUBCASE ("add reports the mapped address and interrupt only when "
	   "verbose")
  {
    {
      stdout_capture cap;
      spwpkt0.add (SPW_IRQ0, 0x80100000, 0xfff);
      CHECK (cap.str ().empty ());
    }
    {
      sis_verbose = 2;
      stdout_capture cap;
      spwpkt0.add (SPW_IRQ0, 0x80100000, 0xfff);
      CHECK (cap.str ().find ("GRSPW2 SpaceWire port") != std::string::npos);
      sis_verbose = 0;
    }
    CHECK (grspw_irq[0] == SPW_IRQ0);

    /* struct grlib_ipcore has no instance pointer (CLAUDE.md), so each of
       the other three AMBA ports is its own SPW_CORE macro expansion
       with its own add function; exercise the other three the same way
       so the whole macro body is covered for every instance, not just
       spwpkt0's. */
    spwpkt1.add (SPW_IRQ1, 0x80200000, 0xfff);
    spwpkt2.add (SPW_IRQ2, 0x80300000, 0xfff);
    spwpkt3.add (SPW_IRQ3, 0x80400000, 0xfff);
    CHECK (grspw_irq[1] == SPW_IRQ1);
    CHECK (grspw_irq[2] == SPW_IRQ2);
    CHECK (grspw_irq[3] == SPW_IRQ3);

    /* Each instance's own sis_verbose branch, not just spwpkt0's. */
    sis_verbose = 2;
    {
      stdout_capture cap;
      spwpkt1.add (SPW_IRQ1, 0x80200000, 0xfff);
      spwpkt2.add (SPW_IRQ2, 0x80300000, 0xfff);
      spwpkt3.add (SPW_IRQ3, 0x80400000, 0xfff);
      CHECK (cap.str ().find ("GRSPW2 SpaceWire port") != std::string::npos);
    }
    sis_verbose = 0;
  }

  SUBCASE ("Time-code tick-out interrupt needs both CTRL.TQ and CTRL.IE, "
	   "and distributed interrupt delivery needs both its own wake "
	   "condition and CTRL.IE")
  {
    /* spw_tc_receive: TQ set, IE clear does not raise an interrupt (the
       opposite combination, TQ clear/IE set, is pinned in the CTRL/TI
       SUBCASE above); the tick-out status bit is still set either way. */
    port_write (spwpkt1, SPW_CTRL, CTRL_TQ);
    rtr_write (rtr_pctrl (PORT_AMBA0), PCTRL_TE);
    unmask_irq (SPW_IRQ1);
    port_write (spwpkt0, SPW_CTRL, CTRL_TI);
    CHECK ((port_read (spwpkt1, SPW_STATUS) & STATUS_TO) != 0);
    CHECK (ext_irl[0] == 0);

    /* spw_dint_receive: wake (INTCTRL.IQ here) set, CTRL.IE clear does
       not raise an interrupt either, even though the code is still
       recorded in INTRX. */
    rtr_write (RTR_RTRCFG, RTRCFG_IE);
    rtr_write (rtr_pctrl (PORT_AMBA0), PCTRL_TE | PCTRL_IC);
    rtr_write (rtr_pctrl (PORT_AMBA1), PCTRL_TE | PCTRL_IC);
    uint32 dirs = PCTRL2_IR | PCTRL2_IT | PCTRL2_AR | PCTRL2_AT;
    rtr_write (rtr_pctrl2 (PORT_AMBA0), dirs);
    rtr_write (rtr_pctrl2 (PORT_AMBA1), dirs);
    port_write (spwpkt1, SPW_INTMSK, 0xFFFFFFFF);
    port_write (spwpkt1, SPW_INTCTRL, INTCTRL_IQ);
    /* spwpkt1's own CTRL.IE is deliberately left clear here. */
    port_write (spwpkt0, SPW_INTCTRL, INTCTRL_II | 5u);
    CHECK ((port_read (spwpkt1, SPW_INTRX) & (1u << 5)) != 0);
    CHECK (ext_irl[0] == 0);
  }
}

/* -------------------------------------------------------------------- */
/* Transmit DMA walk, routing and receive delivery                       */
/* -------------------------------------------------------------------- */

TEST_CASE_FIXTURE (grspw_fixture, "GRSPW2 transmit DMA walk and routing")
{
  SUBCASE ("TE gates the whole walk: no descriptor is read while TE is "
	   "clear")
  {
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC, 0x1000);
    sis_tests::flatmem_poke (0x1000, TXBD_EN | 4);
    /* DMACTRL.TE is never set, so the event this write would otherwise
       arm is never scheduled; running time forward changes nothing. */
    run (XFER_MARGIN);
    CHECK (sis_tests::flatmem_peek (0x1000) == (TXBD_EN | 4));
  }

  SUBCASE ("A disabled descriptor stops the walk on its first iteration")
  {
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC, 0x1000);
    sis_tests::flatmem_poke (0x1000, 4); /* EN clear */
    sis_tests::flatmem_poke (0x1004, 0x1100);

    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_TE);
    run (XFER_MARGIN);

    CHECK ((port_read (spwpkt0, spw_dma (0) + SPW_DMA_CTRL) & DMACTRL_PS) ==
	   0);
    CHECK (port_read (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC) == 0x1000);
  }

  SUBCASE ("Path addressing: header+data are read, the descriptor is "
	   "released before routing, WR wraps the pointer while its "
	   "absence increments it, and the receiver's descriptor is "
	   "released with the length and its own interrupt")
  {
    /* addr 1-31 is path addressing (see the file header): the leading
       byte is always the output port number and is always stripped. */
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC, 0x1000);
    sis_tests::flatmem_poke (0x1000, TXBD_EN | TXBD_IE | 2 /* hlen */);
    sis_tests::flatmem_poke (0x1004, 0x1100); /* header address */
    sis_tests::flatmem_poke (0x1008, 3 /* dlen */);
    sis_tests::flatmem_poke (0x100C, 0x1200); /* data address */

    poke_pkt (0x1100, PORT_AMBA1, 0x01);
    poke_pkt (0x1200, 0xAA, 0xBB, 0xCC);

    /* Path addressing strips only the leading port byte, so spw_receive
       sees 0x01 as the leading (address) byte of what is left; give
       spwpkt1 that as its default address so its one receive-enabled
       channel accepts the packet (address filtering itself is covered
       separately below). */
    port_write (spwpkt1, SPW_NODEADDR, 0x01);
    port_write (spwpkt1, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_RE | DMACTRL_RI);
    port_write (spwpkt1, spw_dma (0) + SPW_DMA_RXDESC, 0x2000);
    sis_tests::flatmem_poke (0x2000, RXBD_EN | RXBD_IE);
    sis_tests::flatmem_poke (0x2004, 0x2100);

    unmask_irq (SPW_IRQ0);
    unmask_irq (SPW_IRQ1);
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_TE | DMACTRL_TI);

    run (XFER_MARGIN);

    /* The descriptor is released and the pointer advances by 16
       (SPW_TXBD_SIZE) since WR was not set; PS and the transmit
       interrupt are both raised (DMACTRL.TI and TXBD_IE).  Routing to
       the receiver happens synchronously inside the same spw_dma_run
       firing, so both the transmit interrupt (SPW_IRQ0) and the receive
       interrupt (SPW_IRQ1) are pending together by the time run()
       returns; IRQMP reports the numerically higher pending level
       (irqmp's priority order in grlib.cc's chk_irq), so SPW_IRQ1 is
       what ext_irl shows first. */
    CHECK ((sis_tests::flatmem_peek (0x1000) & TXBD_EN) == 0);
    CHECK (port_read (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC) == 0x1010);
    CHECK ((port_read (spwpkt0, spw_dma (0) + SPW_DMA_CTRL) & DMACTRL_PS) !=
	   0);
    CHECK (ext_irl[0] == SPW_IRQ1);

    /* Path addressing always strips the header byte, so the receiving
       port only sees the 4 bytes after it (0x01, 0xAA, 0xBB, 0xCC), all
       landing in the one word at the receive buffer's base; the receive
       descriptor is released with the length and RI/RXBD_IE raise the
       receive interrupt. */
    CHECK (byte_at (0x2100, 0) == 0x01);
    CHECK (byte_at (0x2100, 1) == 0xAA);
    CHECK ((sis_tests::flatmem_peek (0x2000) & 0x01FFFFFF) == 4);
    clear_irq (SPW_IRQ1);
    CHECK (ext_irl[0] == SPW_IRQ0);
    clear_irq (SPW_IRQ0);

    /* WR set this time: the pointer wraps back to the table base
       (SPW_BD_TABLE_MASK) instead of continuing to increment. */
    sis_tests::flatmem_poke (0x1010, TXBD_EN | TXBD_WR | 2);
    sis_tests::flatmem_poke (0x1014, 0x1100);
    sis_tests::flatmem_poke (0x1018, 0);
    sis_tests::flatmem_poke (0x101C, 0);
    sis_tests::flatmem_poke (0x2004, 0x2100);
    sis_tests::flatmem_poke (0x2000, RXBD_EN);

    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_TE);
    run (XFER_MARGIN);

    CHECK (port_read (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC) == 0x1000);
  }

  SUBCASE ("hlen+dlen exceeding the maximum packet size truncates dlen")
  {
    /* hlen is 1 byte (just the routing address), stripped entirely by
       path addressing, so spw_receive's leading byte is the first data
       byte, left at zero by flatmem_clear; match spwpkt1's default
       address to it. */
    port_write (spwpkt1, SPW_NODEADDR, 0);
    port_write (spwpkt1, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_RE);
    port_write (spwpkt1, spw_dma (0) + SPW_DMA_RXDESC, 0x2000);
    sis_tests::flatmem_poke (0x2000, RXBD_EN);
    sis_tests::flatmem_poke (0x2004, 0x2100);

    port_write (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC, 0x1000);
    /* hlen 1 (just the address byte) plus an oversized dlen. */
    sis_tests::flatmem_poke (0x1000, TXBD_EN | 1);
    sis_tests::flatmem_poke (0x1004, 0x1100);
    sis_tests::flatmem_poke (0x1008, MAX_PKT);
    sis_tests::flatmem_poke (0x100C, 0x1200);
    poke_pkt (0x1100, PORT_AMBA1);

    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_TE);
    run (XFER_MARGIN);

    /* hlen + dlen is clamped to MAX_PKT total (1 header byte plus
       MAX_PKT - 1 data bytes), and path addressing then strips that one
       header byte before delivery, so the length spw_receive records is
       one less again. */
    CHECK ((sis_tests::flatmem_peek (0x2000) & 0x01FFFFFF) == MAX_PKT - 1);
  }

  SUBCASE ("A one-byte path-addressed packet strips to a zero-length "
	   "packet, which spw_receive discards")
  {
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC, 0x1000);
    sis_tests::flatmem_poke (0x1000, TXBD_EN | 1);
    sis_tests::flatmem_poke (0x1004, 0x1100);
    sis_tests::flatmem_poke (0x1008, 0);
    sis_tests::flatmem_poke (0x100C, 0);
    /* The single byte in this packet is exactly the target port
       number, leaving nothing behind it once stripped. */
    poke_pkt (0x1100, PORT_AMBA0);

    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_RE);
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_RXDESC, 0x2000);
    sis_tests::flatmem_poke (0x2000, RXBD_EN);

    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_TE | DMACTRL_RE);
    run (XFER_MARGIN);

    /* Nothing was received: the zero-length packet was discarded before
       any channel was picked. */
    CHECK ((sis_tests::flatmem_peek (0x2000) & RXBD_EN) != 0);
  }

  SUBCASE ("Routing: addr 0 is dropped, path addressing to a cable port "
	   "has nowhere to be delivered, and logical addressing is gated "
	   "by RTACTRL.EN and needs a set RTPMAP port bit")
  {
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC, 0x1000);
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_TE);

    /* addr == 0: spw_route's own guard. */
    sis_tests::flatmem_poke (0x1000, TXBD_EN | 1);
    sis_tests::flatmem_poke (0x1004, 0x1100);
    sis_tests::flatmem_poke (0x1008, 0);
    sis_tests::flatmem_poke (0x100C, 0);
    poke_pkt (0x1100, 0);
    run (XFER_MARGIN);
    CHECK ((port_read (spwpkt0, spw_dma (0) + SPW_DMA_CTRL) & DMACTRL_PS) !=
	   0);

    /* Path addressing to a cable port (1-8): the router has no core
       behind a cable port to receive it, so the bound check after
       routing drops it.  The transmit pointer advanced past the first
       descriptor above, so put this one back at the table base. */
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_PS);
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC, 0x1000);
    sis_tests::flatmem_poke (0x1000, TXBD_EN | 1);
    poke_pkt (0x1100, PORT_CABLE1);
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_TE);
    run (XFER_MARGIN);
    CHECK ((port_read (spwpkt0, spw_dma (0) + SPW_DMA_CTRL) & DMACTRL_PS) !=
	   0);

    /* Path addressing to a physical address between the cable ports and
       the AMBA ports (13-31): table 174's note says physical addresses
       13-31 do not exist, and this is still path addressing (any first
       byte below 32), so target is 20 directly, which the bound check's
       upper half rejects (as opposed to the cable port case above,
       rejected by the lower half). */
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_PS);
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC, 0x1000);
    sis_tests::flatmem_poke (0x1000, TXBD_EN | 1);
    poke_pkt (0x1100, 20);
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_TE);
    run (XFER_MARGIN);
    CHECK ((port_read (spwpkt0, spw_dma (0) + SPW_DMA_CTRL) & DMACTRL_PS) !=
	   0);

    /* A logical address (>= 32) whose RTACTRL.EN is clear: dropped by
       "!(actrl & RTACTRL_EN)".  RTPMAP for this address is left set so
       the only thing under test is RTACTRL.EN. */
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_PS);
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC, 0x1000);
    rtr_write (RTR_RTPMAP_LOG + 4 * (33 - 32), 1u << PORT_AMBA1);
    rtr_write (RTR_RTACTRL_LOG + 4 * (33 - 32), 0);
    sis_tests::flatmem_poke (0x1000, TXBD_EN | 1);
    poke_pkt (0x1100, 33);
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_TE);
    run (XFER_MARGIN);
    CHECK ((port_read (spwpkt0, spw_dma (0) + SPW_DMA_CTRL) & DMACTRL_PS) !=
	   0);

    /* RTACTRL.EN set but RTPMAP has no port bit at all: the port-bit
       search never finds a target, so the packet is still dropped
       (target stays out of range). */
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_PS);
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC, 0x1000);
    rtr_write (RTR_RTPMAP_LOG + 4 * (34 - 32), 0);
    rtr_write (RTR_RTACTRL_LOG + 4 * (34 - 32), RTACTRL_EN);
    sis_tests::flatmem_poke (0x1000, TXBD_EN | 1);
    poke_pkt (0x1100, 34);
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_TE);
    run (XFER_MARGIN);
    CHECK ((port_read (spwpkt0, spw_dma (0) + SPW_DMA_CTRL) & DMACTRL_PS) !=
	   0);

    /* A working logical route with RTACTRL.HD clear: the header byte is
       kept (group adaptive routing style), so the receiver sees the
       address byte as the first byte of the packet; match spwpkt1's
       default address to it. */
    port_write (spwpkt1, SPW_NODEADDR, 35);
    port_write (spwpkt1, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_RE);
    port_write (spwpkt1, spw_dma (0) + SPW_DMA_RXDESC, 0x2000);
    sis_tests::flatmem_poke (0x2000, RXBD_EN);
    sis_tests::flatmem_poke (0x2004, 0x2100);

    rtr_write (RTR_RTPMAP_LOG + 4 * (35 - 32), 1u << PORT_AMBA1);
    rtr_write (RTR_RTACTRL_LOG + 4 * (35 - 32), RTACTRL_EN);
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_PS);
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC, 0x1000);
    sis_tests::flatmem_poke (0x1000, TXBD_EN | 1);
    poke_pkt (0x1100, 35);
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_TE);
    run (XFER_MARGIN);

    CHECK ((sis_tests::flatmem_peek (0x2000) & 0x01FFFFFF) == 1);
    CHECK (byte_at (0x2100, 0) == 35);
  }

  SUBCASE ("Channel selection: address filtering, promiscuous mode and "
	   "no channel accepting the packet")
  {
    /* Two channels on the receiving port: channel 0 filters on its own
       DMAADDR pair, channel 1 shares the default NODEADDR pair.  Both
       receive-enabled, per GRIP section 78.6.3 / 78.4.4.1 (address
       comparison and channel selection) as summarised in grspw.cc's
       spw_pick_channel comment: the enabled channel with the lowest
       number whose filter matches wins. */
    port_write (spwpkt1, spw_dma (0) + SPW_DMA_ADDR, 0x0042 | (0u << 8));
    port_write (spwpkt1, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_RE | DMACTRL_EN);
    port_write (spwpkt1, spw_dma (0) + SPW_DMA_RXDESC, 0x2000);
    sis_tests::flatmem_poke (0x2000, RXBD_EN);
    sis_tests::flatmem_poke (0x2004, 0x2100);

    port_write (spwpkt1, SPW_NODEADDR, 0x0099);
    port_write (spwpkt1, spw_dma (1) + SPW_DMA_CTRL, DMACTRL_RE);
    port_write (spwpkt1, spw_dma (1) + SPW_DMA_RXDESC, 0x3000);
    sis_tests::flatmem_poke (0x3000, RXBD_EN);
    sis_tests::flatmem_poke (0x3004, 0x3100);

    port_write (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC, 0x1000);
    /* hlen 2: [port, address].  Path addressing strips only the leading
       port byte, so spw_receive's leading byte is the address that
       follows it. */
    sis_tests::flatmem_poke (0x1004, 0x1100);
    sis_tests::flatmem_poke (0x1008, 0);
    sis_tests::flatmem_poke (0x100C, 0);

    /* A packet addressed to channel 0's own address (0x42) is taken by
       channel 0, not channel 1. */
    sis_tests::flatmem_poke (0x1000, TXBD_EN | 2);
    poke_pkt (0x1100, PORT_AMBA1, 0x42);
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_TE);
    run (XFER_MARGIN);
    CHECK ((sis_tests::flatmem_peek (0x2000) & RXBD_EN) == 0);
    CHECK ((sis_tests::flatmem_peek (0x3000) & RXBD_EN) != 0);

    /* A packet addressed to the shared default address (0x99) is taken
       by channel 1, since channel 0's own filter does not match.  Both
       the transmit pointer and channel 0's own receive pointer advanced
       past the descriptor consumed above (neither had WR set), so put
       both back at their table base each time from here on. */
    sis_tests::flatmem_poke (0x2000, RXBD_EN);
    sis_tests::flatmem_poke (0x1000, TXBD_EN | 2);
    poke_pkt (0x1100, PORT_AMBA1, 0x99);
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC, 0x1000);
    port_write (spwpkt1, spw_dma (0) + SPW_DMA_RXDESC, 0x2000);
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_TE);
    run (XFER_MARGIN);
    CHECK ((sis_tests::flatmem_peek (0x3000) & RXBD_EN) == 0);

    /* Neither channel's address matches and promiscuous mode is off: the
       packet is dropped, no descriptor moves. */
    sis_tests::flatmem_poke (0x2000, RXBD_EN);
    sis_tests::flatmem_poke (0x3000, RXBD_EN);
    sis_tests::flatmem_poke (0x1000, TXBD_EN | 2);
    poke_pkt (0x1100, PORT_AMBA1, 0xAB);
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC, 0x1000);
    port_write (spwpkt1, spw_dma (0) + SPW_DMA_RXDESC, 0x2000);
    port_write (spwpkt1, spw_dma (1) + SPW_DMA_RXDESC, 0x3000);
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_TE);
    run (XFER_MARGIN);
    CHECK ((sis_tests::flatmem_peek (0x2000) & RXBD_EN) != 0);
    CHECK ((sis_tests::flatmem_peek (0x3000) & RXBD_EN) != 0);

    /* Promiscuous mode: the first enabled channel (channel 0) takes the
       packet regardless of address, GRIP section 78.6.10. */
    port_write (spwpkt1, SPW_CTRL, CTRL_PM);
    sis_tests::flatmem_poke (0x1000, TXBD_EN | 2);
    poke_pkt (0x1100, PORT_AMBA1, 0xAB);
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC, 0x1000);
    port_write (spwpkt1, spw_dma (0) + SPW_DMA_RXDESC, 0x2000);
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_TE);
    run (XFER_MARGIN);
    CHECK ((sis_tests::flatmem_peek (0x2000) & RXBD_EN) == 0);
  }

  SUBCASE ("A packet longer than DMARXMAX is truncated on receive")
  {
    /* hlen is 1 (just the routing address), stripped entirely by path
       addressing, so spw_receive's leading byte is the first data byte
       (0x11 below); match spwpkt1's default address to it. */
    port_write (spwpkt1, SPW_NODEADDR, 0x11);
    port_write (spwpkt1, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_RE);
    port_write (spwpkt1, spw_dma (0) + SPW_DMA_RXMAX, 2);
    port_write (spwpkt1, spw_dma (0) + SPW_DMA_RXDESC, 0x2000);
    sis_tests::flatmem_poke (0x2000, RXBD_EN);
    sis_tests::flatmem_poke (0x2004, 0x2100);

    port_write (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC, 0x1000);
    sis_tests::flatmem_poke (0x1000, TXBD_EN | 1);
    sis_tests::flatmem_poke (0x1004, 0x1100);
    sis_tests::flatmem_poke (0x1008, 4);
    sis_tests::flatmem_poke (0x100C, 0x1200);
    poke_pkt (0x1100, PORT_AMBA1);
    poke_pkt (0x1200, 0x11, 0x22, 0x33, 0x44);

    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_TE);
    run (XFER_MARGIN);

    CHECK ((sis_tests::flatmem_peek (0x2000) & 0x01FFFFFF) == 2);
    CHECK ((sis_tests::flatmem_peek (0x2000) & RXBD_TR) != 0);

    /* A DMARXMAX set but not exceeded: the length is delivered as-is
       and RXBD_TR is not raised.  Raise DMARXMAX above the packet's own
       length (still 4, the same descriptor as above) instead of
       shrinking the packet; the receive pointer advanced past the
       consumed descriptor (no WR), so put it back and re-enable it. */
    port_write (spwpkt1, spw_dma (0) + SPW_DMA_RXMAX, 10);
    port_write (spwpkt1, spw_dma (0) + SPW_DMA_RXDESC, 0x2000);
    sis_tests::flatmem_poke (0x2000, RXBD_EN);
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC, 0x1000);
    sis_tests::flatmem_poke (0x1000, TXBD_EN | 1);
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_TE);
    run (XFER_MARGIN);

    CHECK ((sis_tests::flatmem_peek (0x2000) & 0x01FFFFFF) == 4);
    CHECK ((sis_tests::flatmem_peek (0x2000) & RXBD_TR) == 0);

    /* Boundary: DMARXMAX equal to the packet length is not "exceeded"
       either (only len > rxmax truncates, not len >= rxmax). */
    port_write (spwpkt1, spw_dma (0) + SPW_DMA_RXMAX, 4);
    port_write (spwpkt1, spw_dma (0) + SPW_DMA_RXDESC, 0x2000);
    sis_tests::flatmem_poke (0x2000, RXBD_EN);
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC, 0x1000);
    sis_tests::flatmem_poke (0x1000, TXBD_EN | 1);
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_TE);
    run (XFER_MARGIN);

    CHECK ((sis_tests::flatmem_peek (0x2000) & 0x01FFFFFF) == 4);
    CHECK ((sis_tests::flatmem_peek (0x2000) & RXBD_TR) == 0);
  }

  SUBCASE ("A receive descriptor with WR set wraps the pointer back to "
	   "the table base")
  {
    port_write (spwpkt1, SPW_NODEADDR, 0);
    port_write (spwpkt1, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_RE);
    port_write (spwpkt1, spw_dma (0) + SPW_DMA_RXDESC, 0x2000);
    sis_tests::flatmem_poke (0x2000, RXBD_EN | RXBD_WR);
    sis_tests::flatmem_poke (0x2004, 0x2100);

    /* hlen 2: [port, address 0], so the packet is not stripped down to
       zero length (which spw_receive would just discard, never reaching
       any descriptor at all) and 0 matches spwpkt1's default address
       set above. */
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC, 0x1000);
    sis_tests::flatmem_poke (0x1000, TXBD_EN | 2);
    sis_tests::flatmem_poke (0x1004, 0x1100);
    sis_tests::flatmem_poke (0x1008, 0);
    sis_tests::flatmem_poke (0x100C, 0);
    poke_pkt (0x1100, PORT_AMBA1, 0);

    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_TE);
    run (XFER_MARGIN);

    /* WR set: the pointer wraps to the table base (masked by
       SPW_BD_TABLE_MASK) instead of incrementing to 0x2008. */
    CHECK (port_read (spwpkt1, spw_dma (0) + SPW_DMA_RXDESC) == 0x2000);
  }

  SUBCASE ("RXBD_IE without DMACTRL.RI raises no interrupt, and "
	   "TXBD_IE without DMACTRL.TI raises no interrupt")
  {
    /* Receive side: the descriptor asks for an interrupt but the
       channel's own RI is left clear. */
    port_write (spwpkt1, SPW_NODEADDR, 0);
    port_write (spwpkt1, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_RE);
    port_write (spwpkt1, spw_dma (0) + SPW_DMA_RXDESC, 0x2000);
    sis_tests::flatmem_poke (0x2000, RXBD_EN | RXBD_IE);
    sis_tests::flatmem_poke (0x2004, 0x2100);
    unmask_irq (SPW_IRQ1);

    /* hlen 2: [port, address 0], so the packet is not stripped down to
       zero length (which spw_receive would just discard) and 0 matches
       spwpkt1's default address set above. */
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC, 0x1000);
    sis_tests::flatmem_poke (0x1000, TXBD_EN | 2);
    sis_tests::flatmem_poke (0x1004, 0x1100);
    sis_tests::flatmem_poke (0x1008, 0);
    sis_tests::flatmem_poke (0x100C, 0);
    poke_pkt (0x1100, PORT_AMBA1, 0);

    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_TE);
    run (XFER_MARGIN);

    CHECK ((sis_tests::flatmem_peek (0x2000) & 0x01FFFFFF) == 1);
    CHECK (ext_irl[0] == 0);

    /* Transmit side: the descriptor asks for an interrupt but the
       channel's own TI is left clear. */
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC, 0x1010);
    sis_tests::flatmem_poke (0x1010, TXBD_EN | TXBD_IE | 1);
    sis_tests::flatmem_poke (0x1014, 0x1100);
    sis_tests::flatmem_poke (0x1018, 0);
    sis_tests::flatmem_poke (0x101C, 0);

    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_TE);
    run (XFER_MARGIN);

    CHECK ((port_read (spwpkt0, spw_dma (0) + SPW_DMA_CTRL) & DMACTRL_PS) !=
	   0);
    CHECK (ext_irl[0] == 0);
  }

  SUBCASE ("A DMACTRL.TE cleared between arming and the event firing "
	   "leaves the descriptor untouched")
  {
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC, 0x1000);
    sis_tests::flatmem_poke (0x1000, TXBD_EN | 1);
    sis_tests::flatmem_poke (0x1004, 0x1100);
    sis_tests::flatmem_poke (0x1008, 0);
    sis_tests::flatmem_poke (0x100C, 0);
    poke_pkt (0x1100, PORT_AMBA1);

    /* Arm the event with TE set, then clear TE again before it fires:
       spw_dma_run's own TE check (not just the write that scheduled it)
       is what decides whether the walk happens. */
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_TE);
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, 0);
    run (XFER_MARGIN);

    CHECK ((sis_tests::flatmem_peek (0x1000) & TXBD_EN) != 0);
    CHECK ((port_read (spwpkt0, spw_dma (0) + SPW_DMA_CTRL) & DMACTRL_PS) ==
	   0);
  }

  SUBCASE ("A packet addressed to a disabled receive descriptor is "
	   "dropped without moving the pointer")
  {
    port_write (spwpkt1, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_RE);
    port_write (spwpkt1, spw_dma (0) + SPW_DMA_RXDESC, 0x2000);
    sis_tests::flatmem_poke (0x2000, 0); /* EN clear */

    port_write (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC, 0x1000);
    /* hlen 2: [port, address].  The address (254) matches spwpkt1's
       default (reset value), so the packet reaches the channel and its
       disabled descriptor rather than being dropped earlier by address
       filtering. */
    sis_tests::flatmem_poke (0x1000, TXBD_EN | 2);
    sis_tests::flatmem_poke (0x1004, 0x1100);
    sis_tests::flatmem_poke (0x1008, 0);
    sis_tests::flatmem_poke (0x100C, 0);
    poke_pkt (0x1100, PORT_AMBA1, 254);

    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_TE);
    run (XFER_MARGIN);

    CHECK (port_read (spwpkt1, spw_dma (0) + SPW_DMA_RXDESC) == 0x2000);
  }

  SUBCASE ("The TX walk stops at SPW_TXBD_NR descriptors even when every "
	   "one of them is still enabled")
  {
    /* 64 consecutive, always-enabled, zero-length descriptors: the walk
       exhausts the loop bound instead of ever finding a disabled one, so
       this exercises the for loop's own exit condition rather than the
       "not enabled" break. */
    const int n = 64;
    uint32 base = 0x8000;

    for (int i = 0; i < n; i++)
      {
	uint32 bd = base + 16u * (uint32) i;

	sis_tests::flatmem_poke (bd, TXBD_EN);
	sis_tests::flatmem_poke (bd + 4, 0);
	sis_tests::flatmem_poke (bd + 8, 0);
	sis_tests::flatmem_poke (bd + 12, 0);
      }

    port_write (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC, base);
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_TE);
    run (XFER_MARGIN);

    CHECK (port_read (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC) ==
	   base + 16u * (uint32) n);
    CHECK ((port_read (spwpkt0, spw_dma (0) + SPW_DMA_CTRL) & DMACTRL_PS) !=
	   0);
  }

  SUBCASE ("A header or data buffer past the end of memory is read as "
	   "zero, and a receive buffer past the end of memory is silently "
	   "dropped")
  {
    /* spw_read_bytes/spw_write_bytes fall back when get_mem_ptr returns
       NULL (outside the flat window, FLATMEM_SIZE bytes).  Put the
       header just past the window so the transmit side reads zero bytes
       instead of touching memory. */
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC, 0x1000);
    sis_tests::flatmem_poke (0x1000, TXBD_EN | 1);
    sis_tests::flatmem_poke (0x1004, sis_tests::FLATMEM_SIZE - 4);
    sis_tests::flatmem_poke (0x1008, 0);
    sis_tests::flatmem_poke (0x100C, 0);
    /* The last word of the window: get_mem_ptr (addr, len + 8) with
       len == 1 needs 9 bytes from FLATMEM_SIZE - 4, which does not fit,
       so spw_read_bytes zero-fills instead of reading. */
    sis_tests::flatmem_poke (sis_tests::FLATMEM_SIZE - 4, 0xFFFFFFFF);

    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_TE);
    run (XFER_MARGIN);

    /* The zero-filled header byte (0) is not a valid path/logical
       address (0 is spw_route's own "drop" case), so nothing is routed;
       the important part for coverage is that this did not crash and
       the descriptor was still consumed. */
    CHECK ((sis_tests::flatmem_peek (0x1000) & TXBD_EN) == 0);

    /* Now the receive side: a receive buffer address past the window
       makes spw_write_bytes's NULL branch drop the copy silently, while
       the descriptor is still written back as if the copy had
       succeeded. */
    port_write (spwpkt1, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_RE);
    port_write (spwpkt1, spw_dma (0) + SPW_DMA_RXDESC, 0x2000);
    sis_tests::flatmem_poke (0x2000, RXBD_EN);
    sis_tests::flatmem_poke (0x2004, sis_tests::FLATMEM_SIZE - 4);

    /* hlen 2: [port, address 254], so the packet is not stripped down to
       zero length and reaches spw_write_bytes with something to copy;
       254 matches spwpkt1's untouched default address. */
    port_write (spwpkt0, spw_dma (0) + SPW_DMA_TXDESC, 0x1010);
    sis_tests::flatmem_poke (0x1010, TXBD_EN | 2);
    sis_tests::flatmem_poke (0x1014, 0x1100);
    sis_tests::flatmem_poke (0x1018, 0);
    sis_tests::flatmem_poke (0x101C, 0);
    poke_pkt (0x1100, PORT_AMBA1, 254);

    port_write (spwpkt0, spw_dma (0) + SPW_DMA_CTRL, DMACTRL_TE);
    run (XFER_MARGIN);

    CHECK ((sis_tests::flatmem_peek (0x2000) & 0x01FFFFFF) == 1);
  }
}

/* -------------------------------------------------------------------- */
/* Distributed interrupt code distribution                               */
/* -------------------------------------------------------------------- */

TEST_CASE_FIXTURE (grspw_fixture, "Distributed interrupt code distribution")
{
  SUBCASE ("With RTRCFG.IE clear, RTRCFG.TF selects between silent "
	   "discard and time-code handling")
  {
    /* RTRCFG.IE and RTRCFG.TF both reset to 0 (table 184's IE/TF are
       plain rw bits with reset value 0/0).  TF clear (the default): the
       code is handled as a time-code instead, per GR740-UM-DS's IE bit
       description in table 184 ("or handled as time-codes (if
       RTRCFG.TF = 0)").  spw_tc_distribute needs the router's TC.EN
       (reset default) and the source AMBA port's own PCTRL.TE (reset
       default) to actually move anything. */
    port_write (spwpkt1, SPW_CTRL, CTRL_TQ | CTRL_IE);
    unmask_irq (SPW_IRQ1);
    port_write (spwpkt0, SPW_INTCTRL, INTCTRL_II | 3u);
    /* Handled as a time-code: RTR.TC and spwpkt1's own TC both moved.
       spw_dint_distribute still reports "not sent" either way in this
       branch (the time-code handling is a side effect, not an
       alternate success return), so the sender's INTCTRL.ID is set
       regardless of RTRCFG.TF; that is pinned on the RTRCFG.TF-set case
       right below, and is exactly the same outcome here. */
    CHECK ((rtr_read (RTR_TC) & TC_TC) != 0);
    CHECK ((port_read (spwpkt1, SPW_TC) & SPWTC_TIMECNT) != 0);
    CHECK ((port_read (spwpkt0, SPW_INTCTRL) & INTCTRL_ID) != 0);
    clear_irq (SPW_IRQ1);

    /* RTRCFG.TF set: the code is silently discarded instead, and the
       sender's ID bit is set since spw_dint_distribute still reports
       "not sent". */
    rtr_write (RTR_RTRCFG, RTRCFG_TF);
    uint32 rtr_tc_before = rtr_read (RTR_TC);
    port_write (spwpkt0, SPW_INTCTRL, INTCTRL_II | 3u);
    CHECK (rtr_read (RTR_TC) == rtr_tc_before);
    CHECK ((port_read (spwpkt0, SPW_INTCTRL) & INTCTRL_ID) != 0);
  }

  SUBCASE ("With RTRCFG.IE set, distribution needs the source port's "
	   "PCTRL.IC, the matching PCTRL2 receive-direction bit and a "
	   "matching ISR bit, then reaches every other port whose own "
	   "PCTRL.IC and PCTRL2 transmit-direction bit allow it")
  {
    rtr_write (RTR_RTRCFG, RTRCFG_IE);

    /* Source port's own PCTRL.IC is clear (the reset default only sets
       PCTRL.TE): discarded, ID set. */
    port_write (spwpkt0, SPW_INTCTRL, INTCTRL_II | 3u);
    CHECK ((port_read (spwpkt0, SPW_INTCTRL) & INTCTRL_ID) != 0);

    /* Set IC on the source but clear PCTRL2.IR (the direction bit an
       interrupt code, as opposed to an acknowledgement, needs): still
       discarded. */
    rtr_write (rtr_pctrl (PORT_AMBA0), PCTRL_TE | PCTRL_IC);
    rtr_write (rtr_pctrl2 (PORT_AMBA0), PCTRL2_IT | PCTRL2_AR | PCTRL2_AT);
    port_write (spwpkt0, SPW_INTCTRL, INTCTRL_II | 3u);
    CHECK ((port_read (spwpkt0, SPW_INTCTRL) & INTCTRL_ID) != 0);

    /* Restore the source port's own receive direction, but with every
       other AMBA port still at its reset default (PCTRL.IC clear): the
       loop's "port has no IC" skip runs for all of them and nothing is
       sent. */
    rtr_write (rtr_pctrl2 (PORT_AMBA0),
	       PCTRL2_IR | PCTRL2_IT | PCTRL2_AR | PCTRL2_AT);
    port_write (spwpkt0, SPW_INTCTRL, INTCTRL_II | 3u);
    CHECK ((port_read (spwpkt0, SPW_INTCTRL) & INTCTRL_ID) != 0);

    /* Give spwpkt2's AMBA port PCTRL.IC but clear its PCTRL2.IT (the
       direction bit needed to transmit an interrupt code, as opposed to
       an acknowledgement, out of a port): the loop's "direction not
       enabled" skip is now the reason, distinct from "no IC" above. */
    rtr_write (rtr_pctrl (11), PCTRL_TE | PCTRL_IC); /* port 11 = spwpkt2 */
    rtr_write (rtr_pctrl2 (11), PCTRL2_IR | PCTRL2_AR | PCTRL2_AT);
    port_write (spwpkt0, SPW_INTCTRL, INTCTRL_II | 3u);
    CHECK ((port_read (spwpkt0, SPW_INTCTRL) & INTCTRL_ID) != 0);

    /* Finally give spwpkt1's AMBA port (port 10) both IC and IT: the
       code is now distributed, and spwpkt1's own INTMSK gates whether it
       actually records the code in INTRX (GR740-UM-DS tables 166/167 as
       cited in grspw.cc, and GRIP's TICKMASK).  ID is sticky and
       write-one-to-clear (already pinned in the register test above): a
       successful send only avoids setting it again, it does not clear
       what an earlier discarded send left, so clear it explicitly first
       to observe this send's own outcome. */
    rtr_write (rtr_pctrl (PORT_AMBA1), PCTRL_TE | PCTRL_IC);
    rtr_write (rtr_pctrl2 (PORT_AMBA1),
	       PCTRL2_IR | PCTRL2_IT | PCTRL2_AR | PCTRL2_AT);
    unmask_irq (SPW_IRQ1);
    port_write (spwpkt0, SPW_INTCTRL, INTCTRL_ID);

    /* INTMSK bit for interrupt number 3 is clear on spwpkt1: the code
       reaches the port but spw_dint_receive's own mask check drops it
       before touching INTRX. */
    port_write (spwpkt0, SPW_INTCTRL, INTCTRL_II | 3u);
    CHECK ((port_read (spwpkt0, SPW_INTCTRL) & INTCTRL_ID) == 0);
    CHECK ((port_read (spwpkt1, SPW_INTRX) & (1u << 3)) == 0);
    CHECK (ext_irl[0] == 0);

    /* Unmask it and enable the wake path (INTCTRL.IQ and CTRL.IE): the
       code is recorded and an interrupt raised.  The previous send
       already succeeded at the router level (INTMSK only gates the
       receiver's own recording, not the router's distribution), which
       left interrupt number 3's RTR.ISR0 bit set; requirement 3 of
       section 13.2.18.1 needs that bit clear for a fresh interrupt code
       with the same number, so acknowledge it first the way a real
       acknowledgement code would (table 194, write-one-to-clear). */
    rtr_write (RTR_ISR0, 1u << 3);
    port_write (spwpkt1, SPW_INTMSK, 1u << 3);
    port_write (spwpkt1, SPW_INTCTRL, INTCTRL_IQ);
    port_write (spwpkt1, SPW_CTRL, CTRL_IE);
    port_write (spwpkt0, SPW_INTCTRL, INTCTRL_ID);
    port_write (spwpkt0, SPW_INTCTRL, INTCTRL_II | 3u);
    CHECK ((port_read (spwpkt1, SPW_INTRX) & (1u << 3)) != 0);
    CHECK (ext_irl[0] == SPW_IRQ1);
    clear_irq (SPW_IRQ1);

    /* A second interrupt code with the same number is now discarded:
       the ISR bit for number 3 is set (an interrupt code needs it
       clear), so the sender's ID is set even though the transport path
       is otherwise identical. */
    port_write (spwpkt0, SPW_INTCTRL, INTCTRL_II | 3u);
    CHECK ((port_read (spwpkt0, SPW_INTCTRL) & INTCTRL_ID) != 0);
  }

  SUBCASE ("Acknowledgement codes: requirement 3 gates an ISR bit that "
	   "was never set, AA gates an unmatched acknowledgement at a "
	   "third port, a matched one needs no AA, and AQ/IQ separately "
	   "gate the wake-up interrupt")
  {
    rtr_write (RTR_RTRCFG, RTRCFG_IE);
    rtr_write (rtr_pctrl (PORT_AMBA0), PCTRL_TE | PCTRL_IC);
    rtr_write (rtr_pctrl (PORT_AMBA1), PCTRL_TE | PCTRL_IC);
    rtr_write (rtr_pctrl (11), PCTRL_TE | PCTRL_IC); /* port 11 = spwpkt2 */
    /* Both directions needed on every port: IR/IT to get the interrupt
       code out and back, AR/AT the same for the acknowledgement. */
    uint32 dirs = PCTRL2_IR | PCTRL2_IT | PCTRL2_AR | PCTRL2_AT;
    rtr_write (rtr_pctrl2 (PORT_AMBA0), dirs);
    rtr_write (rtr_pctrl2 (PORT_AMBA1), dirs);
    rtr_write (rtr_pctrl2 (11), dirs);
    port_write (spwpkt0, SPW_INTMSK, 0xFFFFFFFF);
    port_write (spwpkt1, SPW_INTMSK, 0xFFFFFFFF);
    port_write (spwpkt2, SPW_INTMSK, 0xFFFFFFFF);

    /* spwpkt1 sends an acknowledgement for interrupt 7 while its RTR.ISR0
       bit has never been set (nobody has sent interrupt code 7):
       requirement 3 of section 13.2.18.1 ("an acknowledgement code needs
       its ISR bit set") fails independently of INTCTRL.AA, so this is
       discarded, spwpkt1's own ID is set, and spwpkt0's ICACK is
       untouched. */
    port_write (spwpkt1, SPW_INTCTRL, INTCTRL_II | INT_ACK | 7u);
    CHECK ((port_read (spwpkt1, SPW_INTCTRL) & INTCTRL_ID) != 0);
    CHECK (port_read (spwpkt0, SPW_ICACK) == 0);

    /* spwpkt0 sends interrupt code 7 (not an acknowledgement): this sets
       RTR.ISR0's bit 7 and spwpkt0's own dint_sent bit 7, and is
       delivered to both spwpkt1 and spwpkt2 (INTRX, not exercised
       further here). */
    port_write (spwpkt0, SPW_INTCTRL, INTCTRL_II | 7u);

    /* spwpkt2 now acknowledges interrupt 7: requirement 3 passes since
       the ISR bit is set, so the router delivers the acknowledgement to
       every other eligible port.  spwpkt0 accepts it unconditionally,
       since its own dint_sent bit 7 matches (a code it sent itself).
       spwpkt1 never sent interrupt code 7, so its dint_sent bit 7 is
       clear: without spwpkt1's own INTCTRL.AA ("handle all
       acknowledgement codes"), spw_dint_receive drops the record for
       spwpkt1 specifically, even though the send overall succeeded (no
       ID on the sender, spwpkt2, and spwpkt0 did record it). */
    port_write (spwpkt2, SPW_INTCTRL, INTCTRL_II | INT_ACK | 7u);
    CHECK ((port_read (spwpkt2, SPW_INTCTRL) & INTCTRL_ID) == 0);
    CHECK ((port_read (spwpkt0, SPW_ICACK) & (1u << 7)) != 0);
    CHECK ((port_read (spwpkt1, SPW_ICACK) & (1u << 7)) == 0);

    /* With spwpkt1's own INTCTRL.AA set, the identical acknowledgement
       is accepted at spwpkt1 too, even though it still does not match
       anything spwpkt1 itself sent.  RTR.ISR0 bit 7 is cleared by this
       acknowledgement (spw_dint_distribute clears it on a sent ack), so
       resend the underlying interrupt code first to satisfy requirement
       3 again. */
    port_write (spwpkt1, SPW_INTCTRL, INTCTRL_AA);
    port_write (spwpkt0, SPW_INTCTRL, INTCTRL_II | 7u);
    port_write (spwpkt2, SPW_INTCTRL, INTCTRL_II | INT_ACK | 7u);
    CHECK ((port_read (spwpkt1, SPW_ICACK) & (1u << 7)) != 0);
    /* AQ is clear on spwpkt1 and CTRL.IE was never set on it: no
       interrupt, even though the record was made. */
    CHECK (ext_irl[0] == 0);

    /* Send interrupt 9 from spwpkt0 (setting its own dint_sent bit),
       then acknowledge it from spwpkt1 without AA: this time the
       acknowledgement is accepted at spwpkt0 because it matches a code
       spwpkt0 itself sent, and AQ plus CTRL.IE raise spwpkt0's
       interrupt. */
    port_write (spwpkt0, SPW_INTCTRL, INTCTRL_II | 9u);
    port_write (spwpkt1, SPW_INTCTRL, 0); /* clear AA */
    port_write (spwpkt0, SPW_INTCTRL, INTCTRL_AQ);
    port_write (spwpkt0, SPW_CTRL, CTRL_IE);
    unmask_irq (SPW_IRQ0);

    port_write (spwpkt1, SPW_INTCTRL, INTCTRL_II | INT_ACK | 9u);

    CHECK ((port_read (spwpkt0, SPW_ICACK) & (1u << 9)) != 0);
    CHECK (ext_irl[0] == SPW_IRQ0);
  }

  SUBCASE ("A distributed interrupt code still counts as delivered to an "
	   "eligible cable port, which has no core behind it to receive")
  {
    /* Section 13.2.18.1: the code is sent to every other port whose IC
       and direction bits allow it.  A SpaceWire cable port can be one of
       those ports, but grspw.cc's spw_dint_distribute only calls
       spw_dint_receive for port >= SPW_AMBA_PORT0 (an AMBA port); a
       cable port still counts toward "sent" (and toward the ISR bit
       update) even though nothing is actually delivered to it, matching
       the header comment "still counts as having accepted the code". */
    rtr_write (RTR_RTRCFG, RTRCFG_IE);
    rtr_write (rtr_pctrl (PORT_AMBA0), PCTRL_TE | PCTRL_IC);
    rtr_write (rtr_pctrl (PORT_CABLE1), PCTRL_IC);
    uint32 dirs = PCTRL2_IR | PCTRL2_IT | PCTRL2_AR | PCTRL2_AT;
    rtr_write (rtr_pctrl2 (PORT_AMBA0), dirs);
    rtr_write (rtr_pctrl2 (PORT_CABLE1), dirs);

    port_write (spwpkt0, SPW_INTCTRL, INTCTRL_II | 11u);

    CHECK ((port_read (spwpkt0, SPW_INTCTRL) & INTCTRL_ID) == 0);
    CHECK ((rtr_read (RTR_ISR0) & (1u << 11)) != 0);
  }

  SUBCASE ("Time-code distribution: RTR.TC.EN globally disabled, and an "
	   "intermediate port without PCTRL.TE does not receive the "
	   "forwarded value")
  {
    /* RTR.TC.EN clear: the router discards every time-code, even one
       that would otherwise be forwarded (spw_tc_distribute's own guard,
       distinct from a single port's PCTRL.TE). */
    rtr_write (RTR_TC, 0);
    port_write (spwpkt1, SPW_CTRL, CTRL_TQ | CTRL_IE);
    unmask_irq (SPW_IRQ1);
    port_write (spwpkt0, SPW_CTRL, CTRL_TI);
    CHECK ((port_read (spwpkt1, SPW_TC) & SPWTC_TIMECNT) == 0);
    CHECK (ext_irl[0] == 0);

    /* RTR.TC.EN back on, but spwpkt2's own PCTRL.TE is left clear: with
       three AMBA ports enabled for time-codes at the router but one of
       them (spwpkt2) not, that one port does not receive what the other
       does. */
    rtr_write (RTR_TC, TC_EN);
    rtr_write (rtr_pctrl (11), 0); /* port 11 = spwpkt2, TE left clear */
    port_write (spwpkt0, SPW_CTRL, CTRL_TI);
    CHECK ((port_read (spwpkt1, SPW_TC) & SPWTC_TIMECNT) != 0);
    CHECK ((port_read (spwpkt2, SPW_TC) & SPWTC_TIMECNT) == 0);
  }
}
