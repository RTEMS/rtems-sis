/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * This file is part of SIS.
 *
 * SIS, SPARC instruction simulator. Copyright (C) 2026 Jiri Gaisler
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * GRSPW2 SpaceWire interface and SpaceWire router emulation.
 *
 * Only the subset exercised by the RTEMS/Zephyr example applications is
 * modelled.  Packets are moved between the AMBA ports of the router using
 * the routing table the guest programs.  The SpaceWire cable ports accept
 * configuration and report a link state, but carry no traffic, so no link
 * layer is emulated.
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include "sis.h"
#include "grlib.h"

/* Router geometry.  The GR740 router has eight cable ports and four AMBA
   ports.  Port 0 is the configuration port, ports 1 to 8 are cable ports and
   ports 9 to 12 are the AMBA ports of the four GRSPW cores.  */

#define SPW_NPORTS_SPW	8
#define SPW_NPORTS_AMBA 4
#define SPW_AMBA_PORT0	(SPW_NPORTS_SPW + 1)

/* Largest packet the emulation carries. */

#define SPW_MAX_PKT 2048

/* Router register offsets. */

#define RTR_PSETUP 0x004
#define RTR_ROUTES 0x404
#define RTR_PCTRL  0x800
#define RTR_PSTS   0x880
#define RTR_PCTRL2 0x980
#define RTR_CFGSTS 0xA00
#define RTR_TC	   0xA04
#define RTR_VER	   0xA08
#define RTR_ISR0   0xA28
#define RTR_ISR1   0xA2C
#define RTR_SIZE   0x2000

/* Port control register, one per port counting from port one, so the word at
   RTR_PCTRL itself belongs to the configuration port.  Only the two time-code
   bits are modelled; the other reset values of table 178 are not.  */

#define RTR_PCTRL_TE (1 << 5)
#define RTR_PCTRL_ET (1 << 14)

/* Port status register, one per port counting from port one.  The bits marked
   wc in table 180 are cleared by writing one and are unaffected by writing
   zero.  Every other bit is read only.  The word at RTR_PSTS itself is the
   configuration port status register of table 179, which has a different
   layout and is left as plain storage.  */

#define RTR_PSTS_WC 0x3C06001F

/* Port type field, a read only constant.  Zero marks a SpaceWire cable port
   and one marks an AMBA port.  */

#define RTR_PSTS_PT_BIT	 30
#define RTR_PSTS_PT_AMBA 1

/* Router time-code register, table 185. */

#define RTR_TC_TC 0x3F
#define RTR_TC_CF 0xC0
#define RTR_TC_EN (1 << 8)
#define RTR_TC_RE (1 << 9)

/* Capability register.  The interrupt code distribution and the auxiliary
   time-code interface are build options and the guest probes them here.  */

#define RTR_CAP	   0xA44
#define RTRCAP_SD  (1 << 10)
#define RTRCAP_ID  (1 << 11)
#define RTRCAP_AX  (1 << 13)

/* Router configuration and status register.  Read only are the port count
   fields in bits 31 to 22, the RESERVED bits 21 to 16, the static routing and
   plug-and-play enables at bits 15 and 14, the RESERVED bit 5, and the timers
   available and plug-and-play available constants at bits 1 and 0, table 184.
   RTRCFG.ME at bit 2 is cleared by writing one.

   SR, PE, TA and PP are constant one on the GR740, so they are part of the
   reset value.  A guest reads RTRCFG.TA to decide whether the port timers
   exist, so leaving it zero would report a router without timers.  */

#define RTRCFG_RO_MASK 0xFFFFC023
#define RTRCFG_WC_MASK 0x00000004
#define RTRCFG_SR      (1 << 15)
#define RTRCFG_PE      (1 << 14)
#define RTRCFG_TA      (1 << 1)
#define RTRCFG_PP      (1 << 0)

/* Routing table address control word. */

#define RTACTRL_HD 0x1
#define RTACTRL_EN 0x4

/* GRSPW register offsets. */

#define SPW_CTRL     0x00
#define SPW_STATUS   0x04
#define SPW_NODEADDR 0x08
#define SPW_TC	     0x14
#define SPW_DMA0     0x20
#define SPW_INTCTRL  0xA0
#define SPW_INTRX    0xA4
#define SPW_ICACK    0xA8
#define SPW_REGS     0x100

/* Number of DMA channels, and the register block of one of them.  The
   channels are 32 bytes apart.  */

#define SPW_NCH	       (SPW_CTRL_NCH_MAX + 1)
#define SPW_DMA(ch)    (SPW_DMA0 + 32 * (ch))

/* The default address pair in NODEADDR and the per channel pair in DMAADDR
   have the same layout: an address in bits 7 to 0 and a mask in bits 15 to 8.
   Both the received address and the configured address are anded with the
   inverse of the mask before the comparison, see GRIP chapter 78 table 1403.
   NODEADDR carries no mask on a first generation core, which is not modelled
   here.  */

#define SPW_ADDR_ADDR	  0x000000FF
#define SPW_ADDR_MASK	  0x0000FF00
#define SPW_ADDR_MASK_BIT 8

/* Time-code register.  The counter is six bits and the control flags are two,
   see GRIP chapter 78.  */

#define SPW_TC_TIMECNT 0x3F
#define SPW_TC_TCTRL   0xC0

/* GRSPW control register.  The capability fields in bits 31 to 24 are read
   only and hold the constants defined for the GR740. */

#define SPW_CTRL_NCH_BIT 27
#define SPW_CTRL_NCH_MAX 3
#define SPW_CTRL_TI	 (1 << 4)
#define SPW_CTRL_PM	 (1 << 5)
#define SPW_CTRL_RA	 (1 << 31)
#define SPW_CTRL_RX	 (1 << 30)
#define SPW_CTRL_RC	 (1 << 29)
#define SPW_CTRL_DI	 (1 << 24)

#define SPW_CTRL_IE	 (1 << 3)
#define SPW_CTRL_TQ	 (1 << 8)

/* Distributed interrupt code registers, GRIP chapter 78 table 1404.  TXINT
   carries the six bit code to send, whose top bit selects acknowledgement over
   interrupt, see D30.  II is the send trigger and reads back as zero.  ID is
   sticky and says the code was discarded.  IQ and AQ enable the interrupt for a
   received interrupt code and a received acknowledgement.  */

#define SPW_INTCTRL_TXINT 0x3F
#define SPW_INTCTRL_II	  (1 << 6)
#define SPW_INTCTRL_ID	  (1 << 7)
#define SPW_INTCTRL_AA	  (1 << 15)
#define SPW_INTCTRL_IQ	  (1 << 18)
#define SPW_INTCTRL_AQ	  (1 << 19)

/* Interrupt mask registers, GR740-UM-DS tables 170 and 171.  A received code
   sets a bit in INTRX or ICACK only when the corresponding mask bit is set,
   tables 166 and 167, so the mask gates the record and not only the
   interrupt.  */

#define SPW_INTMSK  0xB4
#define SPW_INTMSKX 0xB8

/* The top bit of the code selects acknowledgement, leaving five bits of
   interrupt number. */

#define SPW_INT_ACK 0x20
#define SPW_INT_NUM 0x1F

/* A distributed interrupt code is a control code whose control flags, bits 7
   and 6 of the code on the wire, are "10", GR740-UM-DS section 13.2.18.  They
   are what a time-code interpretation of the same code puts in RTR.TC.CF.  */

#define SPW_DINT_CF 0x80

/* Router distributed interrupt code enables.  RTR.RTRCFG.IE gates the whole
   facility and RTR.PCTRL.IC gates one port, GR740-UM-DS section 13.2.18.
   RTR.PCTRL2 gates the four directions separately: IR and IT for interrupt
   codes, AR and AT for acknowledgement codes.  */

#define RTRCFG_IE    (1 << 8)
#define RTRCFG_TF    (1 << 3)
#define RTR_PCTRL_IC (1 << 15)
#define RTR_PCTRL2_IR (1 << 9)
#define RTR_PCTRL2_IT (1 << 10)
#define RTR_PCTRL2_AR (1 << 11)
#define RTR_PCTRL2_AT (1 << 12)

/* GRSPW status register. */

#define SPW_STS_TO     (1 << 0)
#define SPW_STS_LS_BIT 21
#define SPW_STS_LS_RUN 5

/* GRSPW DMA channel control register. */

#define SPW_DMACTRL_TE (1 << 0)
#define SPW_DMACTRL_RE (1 << 1)
#define SPW_DMACTRL_TI (1 << 2)
#define SPW_DMACTRL_RI (1 << 3)
#define SPW_DMACTRL_PS (1 << 5)
#define SPW_DMACTRL_PR (1 << 6)
#define SPW_DMACTRL_TA (1 << 7)
#define SPW_DMACTRL_RA (1 << 8)
#define SPW_DMACTRL_EN (1 << 13)

/* Status bits of the channel control register, all write one to clear. */

#define SPW_DMACTRL_W1C                                                       \
  (SPW_DMACTRL_PS | SPW_DMACTRL_PR | SPW_DMACTRL_TA | SPW_DMACTRL_RA)

/* DMA channel register offsets, relative to the channel base. */

#define SPW_DMA_CTRL   0x00
#define SPW_DMA_RXMAX  0x04
#define SPW_DMA_TXDESC 0x08
#define SPW_DMA_RXDESC 0x0C
#define SPW_DMA_ADDR   0x10

/* Descriptor tables are 1 kbyte and aligned to their size. */

#define SPW_BD_TABLE_MASK 0x3FF

#define SPW_TXBD_SIZE 16
#define SPW_RXBD_SIZE 8

/* Number of descriptors that fit in one table. */

#define SPW_TXBD_NR 64

/* TX descriptor control word. */

#define SPW_TXBD_HLEN 0x000000FF
#define SPW_TXBD_EN   (1 << 12)
#define SPW_TXBD_WR   (1 << 13)
#define SPW_TXBD_IE   (1 << 14)

/* TX descriptor length word.  The upper byte holds flags. */

#define SPW_TXBD_DLEN 0x00FFFFFF

/* RX descriptor control word. */

#define SPW_RXBD_LEN 0x01FFFFFF
#define SPW_RXBD_EN  (1 << 25)
#define SPW_RXBD_WR  (1 << 26)
#define SPW_RXBD_IE  (1 << 27)
#define SPW_RXBD_EP  (1 << 28)
#define SPW_RXBD_HC  (1 << 29)
#define SPW_RXBD_DC  (1 << 30)
#define SPW_RXBD_TR  (1u << 31)

/* Reception status written by the hardware.  These are produced afresh for
   every packet, so they must not carry over from an earlier use of the
   descriptor.  */

#define SPW_RXBD_STATUS (SPW_RXBD_EP | SPW_RXBD_HC | SPW_RXBD_DC | SPW_RXBD_TR)

/* Delay between arming a transfer and the emulated completion. */

#define SPW_XFER_DELAY 500

int grspw_irq[SPW_NPORTS_AMBA];

static uint32 rtr_regs[RTR_SIZE / 4];

struct spw_core
{
  uint32 regs[SPW_REGS / 4];

  /* Interrupt numbers for which this port sent an interrupt code and has not
     seen the acknowledgement.  GR740-UM-DS table 167 records an incoming
     acknowledgement only for such a number, unless INTCTRL.AA is set.  */
  uint32 dint_sent;
};

static struct spw_core spw[SPW_NPORTS_AMBA];

/* Read one 32-bit word from emulated memory. */

static uint32
spw_read32 (uint32 addr)
{
  uint32 data = 0;
  int32 ws;

  ms->memory_read (addr, &data, &ws);
  return data;
}

/* Write one 32-bit word to emulated memory. */

static void
spw_write32 (uint32 addr, uint32 data)
{
  int32 ws;

  ms->memory_write (addr, &data, 2, &ws);
}

/* Copy LEN bytes out of emulated memory, honouring a host and target byte
   order difference. */

static void
spw_read_bytes (uint32 addr, unsigned char *buf, uint32 len)
{
  uint32 i;
  char *mem = ms->get_mem_ptr (addr & ~3, len + 8);

  if (mem == NULL)
    {
      memset (buf, 0, len);
      return;
    }

  for (i = 0; i < len; i++)
    buf[i] = mem[((addr & 3) + i) ^ arch->bswap];
}

/* Copy LEN bytes into emulated memory. */

static void
spw_write_bytes (uint32 addr, const unsigned char *buf, uint32 len)
{
  uint32 i;
  char *mem = ms->get_mem_ptr (addr & ~3, len + 8);

  if (mem == NULL)
    return;

  for (i = 0; i < len; i++)
    mem[((addr & 3) + i) ^ arch->bswap] = buf[i];
}

/* True when the leading address RXADDR of a packet passes the address filter
   of DMA channel CH.  A channel with DMACTRL.EN uses its own DMAADDR pair,
   otherwise it shares the default pair in NODEADDR, see GRIP chapter 78
   section 78.6.3.  */

static int
spw_addr_match (struct spw_core *core, int ch, uint32 rxaddr)
{
  uint32 ctrl = core->regs[(SPW_DMA (ch) + SPW_DMA_CTRL) / 4];
  uint32 pair, mask;

  if (ctrl & SPW_DMACTRL_EN)
    pair = core->regs[(SPW_DMA (ch) + SPW_DMA_ADDR) / 4];
  else
    pair = core->regs[SPW_NODEADDR / 4];

  mask = ~((pair & SPW_ADDR_MASK) >> SPW_ADDR_MASK_BIT) & SPW_ADDR_ADDR;

  return (rxaddr & mask) == (pair & SPW_ADDR_ADDR & mask);
}

/* Pick the DMA channel of AMBA port INDEX that takes a packet whose leading
   address is RXADDR, or -1 when no channel accepts it.

   In promiscuous mode the first enabled channel takes everything regardless
   of the address, see GRIP chapter 78 section 78.6.10.  Otherwise the enabled
   channel with the lowest number whose filter matches takes the packet, see
   section 78.6.3.  */

static int
spw_pick_channel (struct spw_core *core, uint32 rxaddr)
{
  int promisc = (core->regs[SPW_CTRL / 4] & SPW_CTRL_PM) != 0;
  int ch;

  for (ch = 0; ch < SPW_NCH; ch++)
    {
      if (!(core->regs[(SPW_DMA (ch) + SPW_DMA_CTRL) / 4] & SPW_DMACTRL_RE))
	continue;

      if (promisc || spw_addr_match (core, ch, rxaddr))
	return ch;
    }

  return -1;
}

/* Deliver a packet into the receive descriptor ring of AMBA port INDEX. */

static void
spw_receive (int index, const unsigned char *data, uint32 len)
{
  struct spw_core *core = &spw[index];
  uint32 base, ctrl, bd;
  uint32 bdctrl, addr, rxmax, status = 0;
  int ch;

  /* The leading character of the packet is the address the core matches
     against its filters, and it stays in the packet.  */
  if (len == 0)
    return;

  ch = spw_pick_channel (core, data[0]);
  if (ch < 0)
    return;

  base = SPW_DMA (ch) / 4;
  ctrl = core->regs[base + SPW_DMA_CTRL / 4];
  bd = core->regs[base + SPW_DMA_RXDESC / 4];

  bdctrl = spw_read32 (bd);
  if (!(bdctrl & SPW_RXBD_EN))
    return;

  /* A packet longer than the configured maximum is cut short and reported
     as truncated.  */
  rxmax = core->regs[base + SPW_DMA_RXMAX / 4];
  if (rxmax != 0 && len > rxmax)
    {
      len = rxmax;
      status |= SPW_RXBD_TR;
    }

  addr = spw_read32 (bd + 4);
  spw_write_bytes (addr, data, len);

  /* Report the length and status, and hand the descriptor back. */
  spw_write32 (bd, (bdctrl & ~(SPW_RXBD_EN | SPW_RXBD_LEN | SPW_RXBD_STATUS)) |
		       len | status);

  if (bdctrl & SPW_RXBD_WR)
    bd &= ~SPW_BD_TABLE_MASK;
  else
    bd += SPW_RXBD_SIZE;
  core->regs[base + SPW_DMA_RXDESC / 4] = bd;

  core->regs[base + SPW_DMA_CTRL / 4] |= SPW_DMACTRL_PR;

  if ((bdctrl & SPW_RXBD_IE) && (ctrl & SPW_DMACTRL_RI))
    grlib_set_irq (grspw_irq[index]);
}

/* Route one packet.  The leading byte of DATA is the destination address. */

static void
spw_route (unsigned char *data, uint32 len)
{
  uint32 addr, actrl, pmap;
  int target = -1;
  int strip = 0;
  int i;

  if (len == 0)
    return;

  addr = data[0];
  if (addr == 0)
    return;

  if (addr < 32)
    {
      /* Path addressing: the leading byte selects the port and is always
	 removed before the packet leaves the router.  */
      target = addr;
      strip = 1;
    }
  else
    {
      actrl = rtr_regs[(RTR_ROUTES / 4) + addr - 1];
      pmap = rtr_regs[(RTR_PSETUP / 4) + addr - 1];

      if (!(actrl & RTACTRL_EN))
	return;

      for (i = 1; i < 32; i++)
	if (pmap & (1u << i))
	  {
	    target = i;
	    break;
	  }

      strip = (actrl & RTACTRL_HD) != 0;
    }

  if (target < SPW_AMBA_PORT0 || target >= SPW_AMBA_PORT0 + SPW_NPORTS_AMBA)
    return;

  if (strip)
    {
      data++;
      len--;
    }

  spw_receive (target - SPW_AMBA_PORT0, data, len);
}

/* Walk the transmit descriptor ring of one AMBA port, emit every enabled
   descriptor into the router and hand the descriptors back. */

static void
spw_dma_run (int32 arg)
{
  int index = arg & 0xFF;
  int ch = (arg >> 8) & 0xFF;
  struct spw_core *core = &spw[index];
  uint32 base = SPW_DMA (ch) / 4;
  unsigned char pkt[SPW_MAX_PKT];
  int sent = 0;
  int irq = 0;
  int i;

  if (!(core->regs[base + SPW_DMA_CTRL / 4] & SPW_DMACTRL_TE))
    return;

  /* The ring holds at most SPW_TXBD_NR descriptors.  Each pass disables the
     descriptor it handled, so the walk terminates, but bound it anyway to
     keep a corrupt ring from hanging the simulator.  */
  for (i = 0; i < SPW_TXBD_NR; i++)
    {
      uint32 bd = core->regs[base + SPW_DMA_TXDESC / 4];
      uint32 bdctrl = spw_read32 (bd);
      uint32 hlen, dlen, haddr, daddr;

      if (!(bdctrl & SPW_TXBD_EN))
	break;

      hlen = bdctrl & SPW_TXBD_HLEN;
      haddr = spw_read32 (bd + 4);
      dlen = spw_read32 (bd + 8) & SPW_TXBD_DLEN;
      daddr = spw_read32 (bd + 12);

      if (hlen + dlen > SPW_MAX_PKT)
	dlen = SPW_MAX_PKT - hlen;

      if (hlen)
	spw_read_bytes (haddr, pkt, hlen);
      if (dlen)
	spw_read_bytes (daddr, pkt + hlen, dlen);

      /* Release the descriptor before routing: the receiving port may be
	 this same core.  */
      spw_write32 (bd, bdctrl & ~SPW_TXBD_EN);

      if (bdctrl & SPW_TXBD_WR)
	bd &= ~SPW_BD_TABLE_MASK;
      else
	bd += SPW_TXBD_SIZE;
      core->regs[base + SPW_DMA_TXDESC / 4] = bd;

      spw_route (pkt, hlen + dlen);

      sent = 1;
      if (bdctrl & SPW_TXBD_IE)
	irq = 1;
    }

  if (sent)
    core->regs[base + SPW_DMA_CTRL / 4] |= SPW_DMACTRL_PS;

  if (irq && (core->regs[base + SPW_DMA_CTRL / 4] & SPW_DMACTRL_TI))
    grlib_set_irq (grspw_irq[index]);
}

/* Deliver a time-code to AMBA port INDEX.  The value is stored in the port's
   time-code register and the tick out status bit is raised, see the GR740 user
   manual section 13.4.3.1.  */

static void
spw_tc_receive (int index, uint32 tc)
{
  struct spw_core *core = &spw[index];
  uint32 ctrl = core->regs[SPW_CTRL / 4];

  core->regs[SPW_TC / 4] = tc & (SPW_TC_TIMECNT | SPW_TC_TCTRL);
  core->regs[SPW_STATUS / 4] |= SPW_STS_TO;

  /* CTRL.TQ enables the tick out interrupt and CTRL.IE gates every event that
     is individually maskable, GRIP chapter 78 table 1393.  */
  if ((ctrl & SPW_CTRL_TQ) && (ctrl & SPW_CTRL_IE))
    grlib_set_irq (grspw_irq[index]);
}

static void spw_tc_distribute (int srcport, uint32 tc);

/* Deliver a distributed interrupt code to AMBA port INDEX.  An interrupt code
   sets a bit in INTRX and an acknowledgement sets one in ICACK, and each has
   its own interrupt enable, GRIP chapter 78 table 1404.

   Two conditions gate the record itself rather than only the interrupt.  The
   corresponding bit of the mask register must be set, GR740-UM-DS tables 166
   and 167.  And for an acknowledgement in the interrupt with acknowledgement
   mode, table 167 requires either INTCTRL.AA, "handle all interrupt
   acknowledgement codes", or that this port sent the matching interrupt code.
   Without AA a port therefore never records an acknowledgement of an interrupt
   somebody else raised.  */

static void
spw_dint_receive (int index, uint32 code)
{
  struct spw_core *core = &spw[index];
  uint32 ctrl = core->regs[SPW_CTRL / 4];
  uint32 intctrl = core->regs[SPW_INTCTRL / 4];
  uint32 num = code & SPW_INT_NUM;
  int wake;

  if (!(core->regs[SPW_INTMSK / 4] & (1u << num)))
    return;

  if (code & SPW_INT_ACK)
    {
      if (!(intctrl & SPW_INTCTRL_AA) && !(core->dint_sent & (1u << num)))
	return;

      core->dint_sent &= ~(1u << num);
      core->regs[SPW_ICACK / 4] |= 1u << num;
      wake = (intctrl & SPW_INTCTRL_AQ) != 0;
    }
  else
    {
      core->regs[SPW_INTRX / 4] |= 1u << num;
      wake = (intctrl & SPW_INTCTRL_IQ) != 0;
    }

  if (wake && (ctrl & SPW_CTRL_IE))
    grlib_set_irq (grspw_irq[index]);
}

/* Distribute an interrupt code that arrived from router port SRCPORT.

   GR740-UM-DS section 13.2.18.1 lists what must hold for the code to be
   distributed at all.  The router wide RTR.RTRCFG.IE must be set, and so must
   the RTR.PCTRL.IC of the port the code arrived on.  The direction bits of
   RTR.PCTRL2 must allow it: IR to receive an interrupt code and AR to receive
   an acknowledgement.  And the ISR bit for the interrupt number decides
   whether the code says anything new: an interrupt code is discarded when the
   bit is already set, because the previous code with that number has not been
   acknowledged and its ISR timer has not expired, and an acknowledgement code
   is discarded when the bit is clear, because there is nothing to acknowledge.

   The code is then sent out of every other port whose IC is set and whose
   RTR.PCTRL2.IT or AT allows that direction.

   Returns non-zero when the code left the source port, so the sender can raise
   the discard bit when it did not.  */

static int
spw_dint_distribute (int srcport, uint32 code)
{
  uint32 num = code & SPW_INT_NUM;
  uint32 isr = rtr_regs[RTR_ISR0 / 4];
  int ack = (code & SPW_INT_ACK) != 0;
  int port;
  int sent = 0;

  if (!(rtr_regs[RTR_CFGSTS / 4] & RTRCFG_IE))
    {
      /* With the facility off, table 184 bit 8 gives two behaviours and
	 RTR.RTRCFG.TF picks between them.  Set, the code is silently
	 discarded.  Clear, it is not discarded at all: it is handled as a
	 time-code, control flags and all, so the interrupt number lands in the
	 router time counter and can be forwarded onwards as a tick.  A guest
	 that clears RTRCFG.IE without also setting RTRCFG.TF gets the second,
	 which is why the driver sets both together.  */
      if (!(rtr_regs[RTR_CFGSTS / 4] & RTRCFG_TF))
	spw_tc_distribute (srcport, code | SPW_DINT_CF);

      return 0;
    }

  if (!(rtr_regs[(RTR_PCTRL / 4) + srcport] & RTR_PCTRL_IC))
    return 0;

  if (!(rtr_regs[(RTR_PCTRL2 / 4) + srcport]
	& (ack ? RTR_PCTRL2_AR : RTR_PCTRL2_IR)))
    return 0;

  /* Requirement 3 of section 13.2.18.1.  An interrupt code needs its ISR bit
     clear and an acknowledgement code needs it set.  */
  if (ack != ((isr & (1u << num)) != 0))
    return 0;

  for (port = 1; port < SPW_AMBA_PORT0 + SPW_NPORTS_AMBA; port++)
    {
      if (port == srcport)
	continue;
      if (!(rtr_regs[(RTR_PCTRL / 4) + port] & RTR_PCTRL_IC))
	continue;
      if (!(rtr_regs[(RTR_PCTRL2 / 4) + port]
	    & (ack ? RTR_PCTRL2_AT : RTR_PCTRL2_IT)))
	continue;

      sent = 1;

      /* Only the AMBA ports have a core behind them to deliver to.  A cable
	 port carries no traffic in this model, but it still counts as having
	 accepted the code.  */
      if (port >= SPW_AMBA_PORT0)
	spw_dint_receive (port - SPW_AMBA_PORT0, code);
    }

  /* The ISR bit follows the code that was distributed: an interrupt sets it
     and an acknowledgement clears it.  There is no ISR timer in this model, so
     an interrupt that is never acknowledged stays recorded and a second code
     with the same number is discarded until the guest acknowledges it.  */
  if (sent)
    {
      if (ack)
	rtr_regs[RTR_ISR0 / 4] &= ~(1u << num);
      else
	rtr_regs[RTR_ISR0 / 4] |= 1u << num;
    }

  return sent;
}

/* Distribute a time-code that arrived from router port SRCPORT, see the GR740
   user manual section 13.2.17.

   Time-codes are enabled globally by RTR.TC.EN and per port by RTR.PCTRL.TE.
   A port whose TE is clear discards what it receives and is never sent to.

   An AMBA port whose RTR.PCTRL.ET is clear is a special case of table 178: the
   router throws the received value away, increments its own counter and
   forwards that.  With ET set the received value updates the router counter,
   but is only forwarded when it is the old value plus one, modulo 64.  */

static void
spw_tc_distribute (int srcport, uint32 tc)
{
  uint32 rtrtc = rtr_regs[RTR_TC / 4];
  uint32 srcctrl = rtr_regs[(RTR_PCTRL / 4) + srcport];
  uint32 value, flags;
  int forward = 1;
  int port;

  if (!(rtrtc & RTR_TC_EN) || !(srcctrl & RTR_PCTRL_TE))
    return;

  if (srcctrl & RTR_PCTRL_ET)
    {
      value = tc & RTR_TC_TC;
      flags = tc & RTR_TC_CF;
      forward = value == ((rtrtc + 1) & RTR_TC_TC);
    }
  else
    {
      value = (rtrtc + 1) & RTR_TC_TC;
      flags = rtrtc & RTR_TC_CF;
    }

  /* Every incoming time-code updates the router register, whether or not it
     is forwarded.  */
  rtr_regs[RTR_TC / 4] = (rtrtc & ~(RTR_TC_TC | RTR_TC_CF)) | value | flags;

  if (!forward)
    return;

  /* The code goes to every other port that has time-codes enabled, but not
     back out of the port it arrived on.  The cable ports carry no traffic in
     this model, so only the AMBA ports can take delivery.  */
  for (port = 1; port < SPW_AMBA_PORT0 + SPW_NPORTS_AMBA; port++)
    {
      if (port == srcport)
	continue;

      if (!(rtr_regs[(RTR_PCTRL / 4) + port] & RTR_PCTRL_TE))
	continue;

      if (port >= SPW_AMBA_PORT0)
	spw_tc_receive (port - SPW_AMBA_PORT0, value | flags);
    }
}

/* ------------------- SpaceWire router -----------------------*/

static void
spwrouter_reset (void)
{
  int port;

  memset (rtr_regs, 0, sizeof (rtr_regs));

  rtr_regs[RTR_CFGSTS / 4] = (SPW_NPORTS_SPW << 27) | (SPW_NPORTS_AMBA << 22)
			     | RTRCFG_SR | RTRCFG_PE | RTRCFG_TA | RTRCFG_PP;

  /* Time-codes are enabled out of reset, table 185, and so is the per port
     enable of every port, table 178.  A guest which never touches either still
     distributes time, which is what the hardware does.  */
  rtr_regs[RTR_TC / 4] = RTR_TC_EN;

  for (port = 1; port < SPW_AMBA_PORT0 + SPW_NPORTS_AMBA; port++)
    rtr_regs[(RTR_PCTRL / 4) + port] = RTR_PCTRL_TE;

  /* The four distributed interrupt direction bits of RTR.PCTRL2 all reset to
     one, table 183, so a guest that only sets RTR.PCTRL.IC gets the traffic it
     expects.  The configuration port is included because the loop below writes
     from port zero.  */
  for (port = 0; port < SPW_AMBA_PORT0 + SPW_NPORTS_AMBA; port++)
    rtr_regs[(RTR_PCTRL2 / 4) + port] =
	RTR_PCTRL2_IR | RTR_PCTRL2_IT | RTR_PCTRL2_AR | RTR_PCTRL2_AT;

  /* Version 1.2.0, see the GR740 user manual, router version register.  The
     instance identifier depends on bootstrap signals on real hardware.  Report
     one so that a guest can tell the router apart from an unprogrammed one. */
  rtr_regs[RTR_VER / 4] = (1 << 24) | (2 << 16) | 1;

  /* The capability register is read only and reports what the router was
     built with.  Interrupt code distribution, SpaceWire-D and the auxiliary
     time-code interface are all present on the GR740, and a guest reads
     RTR.CAP.ID to decide whether the distributed interrupts exist at all.  */
  rtr_regs[RTR_CAP / 4] = RTRCAP_SD | RTRCAP_ID | RTRCAP_AX;
}

static int
spwrouter_read (uint32 addr, uint32 *data)
{
  uint32 off = addr & (RTR_SIZE - 1);

  *data = rtr_regs[off >> 2];

  /* The cable ports report a running link so that the guest sees a plausible
     status, even though they carry no traffic.  The port type field is a read
     only constant, zero for a cable port and one for an AMBA port, table 180. */
  if (off > RTR_PSTS && off < RTR_PSTS + 32 * 4)
    {
      uint32 port = (off - RTR_PSTS) >> 2;

      if (port >= 1 && port <= SPW_NPORTS_SPW)
	*data |= SPW_STS_LS_RUN << 12;

      if (port >= SPW_AMBA_PORT0 && port < SPW_AMBA_PORT0 + SPW_NPORTS_AMBA)
	*data |= RTR_PSTS_PT_AMBA << RTR_PSTS_PT_BIT;
    }

  return 1;
}

static int
spwrouter_write (uint32 addr, uint32 *data, uint32 sz)
{
  uint32 off = addr & (RTR_SIZE - 1);
  uint32 value = *data;

  switch (off)
    {
    case RTR_CFGSTS:
      {
	/* The port counts, the two constant enables and the two availability
	   constants are read only.  RTRCFG.ME is cleared by writing one and is
	   unaffected by writing zero, so it is held out of the ordinary
	   read-write part.  */
	uint32 old = rtr_regs[RTR_CFGSTS / 4];
	uint32 rw = ~(RTRCFG_RO_MASK | RTRCFG_WC_MASK);

	rtr_regs[RTR_CFGSTS / 4] = (old & (RTRCFG_RO_MASK | RTRCFG_WC_MASK)
				    & ~(value & RTRCFG_WC_MASK))
				   | (value & rw);
      }
      break;

    case RTR_VER:
      /* Only the instance identifier is writable. */
      rtr_regs[RTR_VER / 4] =
	  (rtr_regs[RTR_VER / 4] & 0xFFFFFF00) | (value & 0xFF);
      break;

    case RTR_TC:
      /* The counter and the control flags are read only, and RE clears them
	 and reads back as zero, table 185.  */
      rtr_regs[RTR_TC / 4] =
	  (rtr_regs[RTR_TC / 4] & (RTR_TC_TC | RTR_TC_CF)) | (value & RTR_TC_EN);

      if (value & RTR_TC_RE)
	rtr_regs[RTR_TC / 4] &= ~(RTR_TC_TC | RTR_TC_CF);
      break;

    case RTR_CAP:
      /* Read only. */
      break;

    case RTR_ISR0:
    case RTR_ISR1:
      /* The interrupt code distribution ISR registers are write one to clear,
	 tables 194 and 195.  A guest uses them for diagnostics and FDIR.  */
      rtr_regs[off >> 2] &= ~value;
      break;

    default:
      if (off > RTR_PSTS && off < RTR_PSTS + 32 * 4)
	{
	  /* Writing one clears a sticky bit, writing zero leaves it alone.
	     The read only bits keep their value.  */
	  rtr_regs[off >> 2] &= ~(value & RTR_PSTS_WC);
	}
      else
	{
	  rtr_regs[off >> 2] = value;
	}
      break;
    }

  return 1;
}

static void
spwrouter_add (int irq, uint32 addr, uint32 mask)
{
  grlib_ahbspp_add (GRLIB_PP_ID (VENDOR_GAISLER, GAISLER_SPWROUTER, 0, irq),
		    GRLIB_PP_AHBADDR (addr, mask, 0, 0, 2), 0, 0, 0);
  if (sis_verbose)
    printf (" SpaceWire router                   0x%08x\n", addr);
}

const struct grlib_ipcore spwrouter = { NULL, spwrouter_reset, spwrouter_read,
					spwrouter_write, spwrouter_add };

/* ------------------- GRSPW2 AMBA ports -----------------------*/

static void
spw_core_reset (int index)
{
  struct spw_core *core = &spw[index];

  memset (core->regs, 0, sizeof (core->regs));

  /* The capability fields are constants on the GR740, see the GR740 user
     manual, GRSPW2 control register.  RMAP, RMAP CRC, unaligned receive and
     distributed interrupts are advertised, the port count field reads zero for
     one port, and the DMA channel count field reads three for four channels.

     The fields report what the hardware reports.  The emulation is narrower:
     the RMAP target is not emulated.  All four DMA channels receive, filter on
     their address pair and transmit.  A guest which leaves RMAP disabled sees
     no difference. */
  core->regs[SPW_CTRL / 4] = SPW_CTRL_RA | SPW_CTRL_RX | SPW_CTRL_RC |
			     (SPW_CTRL_NCH_MAX << SPW_CTRL_NCH_BIT) |
			     SPW_CTRL_DI;

  /* The AMBA ports have no link state machine of their own, but the driver
     prints the field, so report a running link. */
  core->regs[SPW_STATUS / 4] = SPW_STS_LS_RUN << SPW_STS_LS_BIT;

  /* The default address pair does not reset to zero: the mask resets to 0x00
     and the address to 254, see the GR740 user manual, AMBA port default
     address register.  A DMA channel without DMACTRL.EN filters on this pair,
     so a zero here would silently give every such channel address zero.  */
  core->regs[SPW_NODEADDR / 4] = 0xFE;

  core->dint_sent = 0;
}

static int
spw_core_read (int index, uint32 addr, uint32 *data)
{
  *data = spw[index].regs[(addr & (SPW_REGS - 1)) >> 2];
  return 1;
}

static int
spw_core_write (int index, uint32 addr, uint32 *data, uint32 sz)
{
  struct spw_core *core = &spw[index];
  uint32 off = addr & (SPW_REGS - 1);
  uint32 value = *data;

  switch (off)
    {
    case SPW_CTRL:
      /* The capability fields are read only. */
      core->regs[SPW_CTRL / 4] =
	  (core->regs[SPW_CTRL / 4] & 0xFF000000) | (value & 0x00FFFFFF);

      /* A tick in increments the six bit time counter and sends a time-code
	 carrying the new value and the current control flags, see the GR740
	 user manual section 13.4.3.1.  The bit stays high until the code has
	 been sent, and the emulated send is immediate, so it never reads back
	 as set.  */
      if (value & SPW_CTRL_TI)
	{
	  uint32 tc = core->regs[SPW_TC / 4];

	  tc = (tc & ~SPW_TC_TIMECNT) | ((tc + 1) & SPW_TC_TIMECNT);
	  core->regs[SPW_TC / 4] = tc;
	  core->regs[SPW_CTRL / 4] &= ~SPW_CTRL_TI;

	  spw_tc_distribute (SPW_AMBA_PORT0 + index,
			     tc & (SPW_TC_TIMECNT | SPW_TC_TCTRL));
	}
      return 1;

    case SPW_STATUS:
      /* Write one to clear, the link state field is read only. */
      core->regs[SPW_STATUS / 4] &= ~(value & ~(0x7u << SPW_STS_LS_BIT));
      return 1;

    case SPW_INTCTRL:
      /* ID is sticky and write one to clear.  II is the send trigger and
	 reads back as zero, since the emulated send is immediate.  */
      core->regs[SPW_INTCTRL / 4] =
	  (value & ~(SPW_INTCTRL_II | SPW_INTCTRL_ID))
	  | (core->regs[SPW_INTCTRL / 4] & SPW_INTCTRL_ID & ~value);

      if (value & SPW_INTCTRL_II)
	{
	  uint32 code = value & SPW_INTCTRL_TXINT;

	  /* A code the router did not carry anywhere is discarded, which is
	     what ID reports.  */
	  if (!spw_dint_distribute (SPW_AMBA_PORT0 + index, code))
	    core->regs[SPW_INTCTRL / 4] |= SPW_INTCTRL_ID;
	  else if (!(code & SPW_INT_ACK))
	    core->dint_sent |= 1u << (code & SPW_INT_NUM);
	}
      return 1;

    case SPW_INTRX:
    case SPW_ICACK:
      /* The received code bitmasks are write one to clear. */
      core->regs[off >> 2] &= ~value;
      return 1;

    default:
      break;
    }

  if (off >= SPW_DMA0 && off < SPW_DMA (SPW_NCH)
      && (off - SPW_DMA0) % 32 == SPW_DMA_CTRL)
    {
      uint32 old = core->regs[off >> 2];

      /* The status bits are write one to clear, so a bit written as zero
	 keeps its value.  The driver relies on this when it re-arms the
	 interrupts of a channel with a read-modify-write.  */
      core->regs[off >> 2] =
	  (value & ~SPW_DMACTRL_W1C) | (old & SPW_DMACTRL_W1C & ~value);

      /* Every channel transmits.  The channel travels in the event argument
	 alongside the core index, since event () carries one word.

	 The Zephyr driver fixes its transmit channel at zero, so it never
	 reaches this with a non-zero channel and no test covers that path.
	 The consumer is grspw_pkt.c in RTEMS, which keeps a transmit semaphore
	 per DMA channel.  */
      if (value & SPW_DMACTRL_TE)
	{
	  int ch = (off - SPW_DMA0) / 32;

	  event (spw_dma_run, index | (ch << 8), SPW_XFER_DELAY);
	}

      return 1;
    }

  core->regs[off >> 2] = value;
  return 1;
}

#define SPW_CORE(n)                                                           \
  static void spw##n##_reset (void) { spw_core_reset (n); }                   \
  static int spw##n##_read (uint32 addr, uint32 *data)                        \
  {                                                                           \
    return spw_core_read (n, addr, data);                                     \
  }                                                                           \
  static int spw##n##_write (uint32 addr, uint32 *data, uint32 sz)            \
  {                                                                           \
    return spw_core_write (n, addr, data, sz);                                \
  }                                                                           \
  static void spw##n##_add (int irq, uint32 addr, uint32 mask)                \
  {                                                                           \
    grspw_irq[n] = irq;                                                       \
    grlib_apbpp_add (GRLIB_PP_ID (VENDOR_GAISLER, GAISLER_SPW2_DMA, 0, irq),  \
		     GRLIB_PP_APBADDR (addr, mask));                          \
    if (sis_verbose)                                                          \
      printf (" GRSPW2 SpaceWire port %d            0x%08x   irq %2d\n", n,   \
	      addr, irq);                                                     \
  }                                                                           \
  const struct grlib_ipcore spwpkt##n = { NULL, spw##n##_reset,               \
					  spw##n##_read, spw##n##_write,      \
					  spw##n##_add };

SPW_CORE (0)
SPW_CORE (1)
SPW_CORE (2)
SPW_CORE (3)
