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
 * GR1553B MIL-STD-1553B interface emulation.
 *
 * Only the subset exercised by the RTEMS/Zephyr example applications is
 * modelled.  The bus carries one emulated peer terminal whose behaviour is
 * selected from the engine the guest enables:
 *
 *   - guest enables BC: the peer is a remote terminal at address 4 which
 *     serves subaddress 2 with the contents of subaddress 3 incremented
 *     by one.
 *   - guest enables RT: the peer is a bus controller which repeatedly reads
 *     two words from subaddress 2 and writes them back to subaddress 3.
 *
 * Bus monitor mode is not implemented.
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include "sis.h"
#include "grlib.h"

/* Register offsets. */

#define GR1553_IRQ     0x00
#define GR1553_IMASK   0x04
#define GR1553_HWCFG   0x10
#define GR1553_BCSTAT  0x40
#define GR1553_BCCTRL  0x44
#define GR1553_BCBD    0x48
#define GR1553_BCABD   0x4C
#define GR1553_BCTIMER 0x50
#define GR1553_BCWAKE  0x54
#define GR1553_BCIRQPT 0x58
#define GR1553_BCBUSMK 0x5C
#define GR1553_BCSLOT  0x68
#define GR1553_BCASLOT 0x6C
#define GR1553_RTSTAT  0x80
#define GR1553_RTCFG   0x84
#define GR1553_RTSTAT2 0x88
#define GR1553_RTSTATW 0x8C
#define GR1553_RTSYNC  0x90
#define GR1553_RTTAB   0x94
#define GR1553_RTMCCTL 0x98
#define GR1553_RTTTAG  0xA4
#define GR1553_RTEVSZ  0xAC
#define GR1553_RTEVLOG 0xB0
#define GR1553_RTEVIRQ 0xB4
#define GR1553_BMSTAT  0xC0

/* Interrupt flags, shared by the IRQ and IMASK registers. */

#define GR1553_IRQ_BCEV (1 << 0)
#define GR1553_IRQ_RTEV (1 << 8)

/* Access keys guarding the BC and RT control registers. */

#define GR1553_BCKEY 0x1552
#define GR1553_RTKEY 0x1553

/* BC action register.  Suspend is not emulated. */

#define GR1553_BCACT_SCSRT (1 << 0)
#define GR1553_BCACT_SCSTP (1 << 2)

/* Capability bits reported to the driver. */

#define GR1553_BCSUP (1u << 31)
#define GR1553_RTSUP (1u << 31)

/* RT config register. */

#define GR1553_RTCFG_RTEN   (1 << 0)
#define GR1553_RTCFG_RTADDR (0x1F << 1)

/* BC descriptor word 0. */

#define GR1553_BD_TYPE	 0x80000000 /* 0: transfer, 1: branch or end */
#define GR1553_BD_COND	 0x02000000 /* branch descriptor when BD_TYPE set */
#define GR1553_BD_IRQEN	 0x04000000 /* branch: interrupt if condition met */
#define GR1553_BD_IRQN	 0x08000000 /* transfer: interrupt after transfer */
#define GR1553_BD_TIME	 0x0000FFFF /* timeslot in units of 4 us */
#define GR1553_BD_CONDOK 0x000000FF /* condition code, 0xFF: always */

/* BC descriptor word 1. */

#define GR1553_TR_DUMMY 0x80000000

/* Size of one descriptor in bytes. */

#define GR1553_BD_SIZE 16

/* The IRQ log is a ring of 16 word entries. */

#define GR1553_IRQLOG_SIZE 64

/* Address of the emulated peer terminal. */

#define GR1553_PEER_RT_ADDRESS 4

/* Subaddresses used by the emulated peer terminals. */

#define GR1553_PEER_SUB_TO_BC 2
#define GR1553_PEER_SUB_TO_RT 3

/* The ECSS Time Message subaddress, broadcast only.  Table 6-1. */
#define GR1553_PEER_SUB_BCAST 29

/* Period of the emulated bus controller, in microseconds. */

#define GR1553_PEER_BC_PERIOD 50000

int gr1553_irq;

static uint32 gr1553_regs[0x100 / 4];

/* Data held by the emulated remote terminal, one buffer per subaddress. */

static uint16 peer_rt_data[32][32];

/* Scheduled state of the emulated bus controller. */

static int peer_bc_running;

/* Scheduled state of the guest bus controller. */

static int bc_running;

static void bc_step (int32 arg);
static void peer_bc_step (int32 arg);

/* Convert microseconds to clock cycles at the emulated CPU frequency. */

static uint64
gr1553_us_to_clocks (uint32 us)
{
  return (uint64) ((double) us * (double) ebase.freq);
}

/* Read one 32-bit word from emulated memory. */

static uint32
gr1553_read32 (uint32 addr)
{
  uint32 data = 0;
  int32 ws;

  ms->memory_read (addr, &data, &ws);
  return data;
}

/* Write one 32-bit word to emulated memory. */

static void
gr1553_write32 (uint32 addr, uint32 data)
{
  int32 ws;

  ms->memory_write (addr, &data, 2, &ws);
}

/* Read one 16-bit data word from emulated memory. */

static uint16
gr1553_read16 (uint32 addr)
{
  uint32 data = gr1553_read32 (addr & ~3);

  if (addr & 2)
    return data & 0xFFFF;
  return (data >> 16) & 0xFFFF;
}

/* Write one 16-bit data word to emulated memory. */

static void
gr1553_write16 (uint32 addr, uint16 value)
{
  uint32 data = gr1553_read32 (addr & ~3);

  if (addr & 2)
    data = (data & 0xFFFF0000) | value;
  else
    data = (data & 0x0000FFFF) | ((uint32) value << 16);
  gr1553_write32 (addr & ~3, data);
}

/* Raise an interrupt if the guest has unmasked it. */

static void
gr1553_irq_set (uint32 flag)
{
  gr1553_regs[GR1553_IRQ / 4] |= flag;
  if (gr1553_regs[GR1553_IMASK / 4] & flag)
    grlib_set_irq (gr1553_irq);
}

/* Append a descriptor address to the BC interrupt log and advance the log
   pointer, wrapping inside the ring. */

static void
bc_irq_log (uint32 bd)
{
  uint32 pos = gr1553_regs[GR1553_BCIRQPT / 4];

  gr1553_write32 (pos, bd);
  pos = (pos & ~(GR1553_IRQLOG_SIZE - 1)) |
	((pos + 4) & (GR1553_IRQLOG_SIZE - 1));
  gr1553_regs[GR1553_BCIRQPT / 4] = pos;
  gr1553_irq_set (GR1553_IRQ_BCEV);
}

/* Perform one bus transfer against the emulated remote terminal.  WORD1 is
   descriptor word 1 and DPTR the descriptor data pointer.  Returns 0 on
   success or 1 if the addressed terminal did not respond. */

static int
bc_transfer (uint32 word1, uint32 dptr)
{
  uint32 rtaddr = (word1 >> 11) & 0x1F;
  uint32 tr = (word1 >> 10) & 1;
  uint32 subaddr = (word1 >> 5) & 0x1F;
  uint32 wc = word1 & 0x1F;
  uint32 i;

  if (wc == 0)
    wc = 32;

  /* Nothing answers for any address other than the emulated terminal, so the
     bus controller sees a no-response transfer. */
  if (rtaddr != GR1553_PEER_RT_ADDRESS)
    return 1;

  if (tr == 0)
    {
      /* BC to RT: copy the data into the terminal. */
      for (i = 0; i < wc; i++)
	peer_rt_data[subaddr][i] = gr1553_read16 (dptr + i * 2);
    }
  else
    {
      /* RT to BC: the terminal serves subaddress 2 with the contents of
	 subaddress 3 incremented by one. */
      for (i = 0; i < wc; i++)
	{
	  uint16 value;

	  if (subaddr == GR1553_PEER_SUB_TO_BC)
	    value = peer_rt_data[GR1553_PEER_SUB_TO_RT][i] + 1;
	  else
	    value = peer_rt_data[subaddr][i];
	  gr1553_write16 (dptr + i * 2, value);
	}
    }

  return 0;
}

/* Execute the descriptor the schedule currently points at and arm the next
   step. */

static void
bc_step (int32 arg)
{
  uint32 bd = gr1553_regs[GR1553_BCSLOT / 4];
  uint32 word0, word1, next;
  uint32 us = 0;

  if (!bc_running)
    return;

  word0 = gr1553_read32 (bd);

  if (word0 & GR1553_BD_TYPE)
    {
      if (!(word0 & GR1553_BD_COND))
	{
	  /* End of list, the schedule stops here. */
	  bc_running = 0;
	  gr1553_regs[GR1553_BCSTAT / 4] &= ~0x7;
	  return;
	}

      if (word0 & GR1553_BD_IRQEN)
	bc_irq_log (bd);

      if ((word0 & GR1553_BD_CONDOK) == GR1553_BD_CONDOK)
	next = gr1553_read32 (bd + 4);
      else
	next = bd + GR1553_BD_SIZE;

      /* Branch descriptors consume no bus time. */
      us = 1;
    }
  else
    {
      word1 = gr1553_read32 (bd + 4);

      if (!(word1 & GR1553_TR_DUMMY))
	{
	  int nores = bc_transfer (word1, gr1553_read32 (bd + 8));

	  /* Result word transfer status, table 292: 0 success, 1 the RT did
	     not respond. */
	  gr1553_write32 (bd + 12, nores ? 1 : 0);
	}

      /* A transfer descriptor can request an interrupt after the transfer,
	 which is how the driver marks the end of a Communication Frame.  The
	 setting is in effect for dummy transfers too. */
      if (word0 & GR1553_BD_IRQN)
	bc_irq_log (bd);

      /* The timeslot field counts 4 us units. */
      us = (word0 & GR1553_BD_TIME) * 4;
      if (us == 0)
	us = 1;
      next = bd + GR1553_BD_SIZE;
    }

  gr1553_regs[GR1553_BCSLOT / 4] = next;
  event (bc_step, 0, gr1553_us_to_clocks (us));
}

/* Subaddress table entry layout, four words per subaddress. */

#define RT_SA_CTRL  0x00
#define RT_SA_TXPTR 0x04
#define RT_SA_RXPTR 0x08
#define RT_SA_SIZE  16

/* Subaddress control word. */

#define RT_SA_BCRXE (1 << 16)
#define RT_SA_RXEN  (1 << 15)
#define RT_SA_RXIRQ (1 << 13)
#define RT_SA_TXEN  (1 << 7)
#define RT_SA_TXIRQ (1 << 5)

/* Descriptor layout, four words per descriptor. */

#define RT_BD_CTRL 0x00
#define RT_BD_DPTR 0x04
#define RT_BD_NEXT 0x08

#define RT_BD_IRQEN (1 << 30)
#define RT_BD_DONE  (1u << 31)

/* A next pointer of 3 marks the end of a descriptor list. */

#define RT_BD_EOL 0x3

/* Event log entry. */

#define RT_EV_IRQ     (1u << 31)
#define RT_EV_TYPE_TX 0
#define RT_EV_TYPE_RX 1

/* Append an entry to the RT event log and raise an interrupt if the
   descriptor asked for one. */

static void
rt_event_log (uint32 type, uint32 samc, uint32 sz, int irqen)
{
  uint32 pos = gr1553_regs[GR1553_RTEVLOG / 4];
  uint32 mask = gr1553_regs[GR1553_RTEVSZ / 4];
  uint32 entry = ((type & 0x3) << 29) | ((samc & 0x1F) << 24) |
		 ((sz & 0x3F) << 3);

  if (irqen)
    {
      entry |= RT_EV_IRQ;
      /* The driver walks the log from the first entry that raised an
	 interrupt, so only move that mark when no interrupt is pending.  */
      if (!(gr1553_regs[GR1553_IRQ / 4] & GR1553_IRQ_RTEV))
	gr1553_regs[GR1553_RTEVIRQ / 4] = pos;
    }

  gr1553_write32 (pos, entry);

  /* The size register holds the inverted size mask, so the low bits select
     the offset inside the log and the high bits hold its base.  */
  pos = (pos & mask) | ((pos + 4) & ~mask);
  gr1553_regs[GR1553_RTEVLOG / 4] = pos;

  if (irqen)
    gr1553_irq_set (GR1553_IRQ_RTEV);
}

/* Move WC data words between the descriptor list of subaddress SUBADDR and
   BUF.  RX selects the receive list and the direction of the copy.  BCAST
   marks a transfer broadcast to RT address 31 rather than addressed to this
   terminal, which a receive subaddress accepts only with BCRXE set.  A
   broadcast transmit is not a valid command and is ignored. */

static void
rt_transfer (uint32 subaddr, int rx, uint16 *buf, uint32 wc, int bcast)
{
  uint32 satab = gr1553_regs[GR1553_RTTAB / 4] & ~0x1FF;
  uint32 sa = satab + subaddr * RT_SA_SIZE;
  uint32 ctrl = gr1553_read32 (sa + RT_SA_CTRL);
  uint32 ptroff = rx ? RT_SA_RXPTR : RT_SA_TXPTR;
  uint32 bd, bdctrl, dptr, next;
  uint32 i;
  int irqen;

  if (rx && !(ctrl & RT_SA_RXEN))
    return;
  if (!rx && !(ctrl & RT_SA_TXEN))
    return;
  if (bcast && !rx)
    return;
  if (bcast && !(ctrl & RT_SA_BCRXE))
    return;

  bd = gr1553_read32 (sa + ptroff);
  if (bd == 0 || bd == RT_BD_EOL)
    return;

  bdctrl = gr1553_read32 (bd + RT_BD_CTRL);
  dptr = gr1553_read32 (bd + RT_BD_DPTR);
  next = gr1553_read32 (bd + RT_BD_NEXT);

  for (i = 0; i < wc; i++)
    {
      if (rx)
	gr1553_write16 (dptr + i * 2, buf[i]);
      else
	buf[i] = gr1553_read16 (dptr + i * 2);
    }

  gr1553_write32 (bd + RT_BD_CTRL, bdctrl | RT_BD_DONE | wc);

  if (next != RT_BD_EOL)
    gr1553_write32 (sa + ptroff, next);

  /* The interrupt can be requested per descriptor or, as the driver does,
     through the receive or transmit interrupt enable in the subaddress
     control word. */
  irqen = (bdctrl & RT_BD_IRQEN) != 0;
  if (rx && (ctrl & RT_SA_RXIRQ))
    irqen = 1;
  if (!rx && (ctrl & RT_SA_TXIRQ))
    irqen = 1;

  rt_event_log (rx ? RT_EV_TYPE_RX : RT_EV_TYPE_TX, subaddr, wc, irqen);
}

/* Map a mode code number to its RT mode code control register field shift,
   table 326, non-broadcast fields.  Returns -1 when the mode code has no
   control field. */

static int
rt_mode_code_shift (uint32 mc)
{
  switch (mc)
    {
    case 0:  return 16; /* Dynamic bus control. */
    case 1:  return 0;  /* Synchronize. */
    case 3:  return 18; /* Initiate self test. */
    case 4:
    case 5:  return 8;  /* Transmitter shutdown. */
    case 6:
    case 7:  return 22; /* Inhibit terminal flag. */
    case 8:  return 26; /* Reset remote terminal. */
    case 16: return 12; /* Transmit vector word. */
    case 17: return 4;  /* Synchronize with data. */
    case 19: return 14; /* Transmit BIT word. */
    default: return -1;
    }
}

/* Deliver a mode code to the guest remote terminal.  Logs a mode-code event
   when the mode code is enabled for logging in the mode code control register,
   and raises an interrupt when it is enabled for interrupt.  BCAST selects the
   broadcast field of the pair, which the transmit mode codes do not have.

   Returns 1 when the terminal accepts the mode code and 0 when the mode code
   control register marks it illegal.  An illegal mode code is answered with
   the message error bit set and no action taken, so the caller has to know
   whether it was acted on. */

static int
rt_mode_code (uint32 mc, int bcast, uint16 data)
{
  uint32 mcc = gr1553_regs[GR1553_RTMCCTL / 4];
  int shift = rt_mode_code_shift (mc);
  uint32 field;

  if (shift < 0)
    return 0;

  if (bcast)
    {
      /* Dynamic bus control, transmit vector word and transmit BIT word have
         no broadcast field, table 326, and broadcast is illegal for them. */
      if (mc == 0 || mc == 16 || mc == 19)
	return 0;
      shift += 2;
    }

  field = (mcc >> shift) & 0x3;
  if (field == 0)
    return 0; /* Illegal. */

  /* Table 512.  The sync register latches the data word of the last legal
     synchronize with data word mode command, and the RT timer with it.  This
     is the Communication Frame number of ECSS-E-ST-50-13C clause 8.3.1.2a. */
  if (mc == 17)
    gr1553_regs[GR1553_RTSYNC / 4] =
      (gr1553_regs[GR1553_RTTTAG / 4] << 16) | data;

  if (field < 2)
    return 1; /* Legal, but not logged. */

  /* Type 2 is a mode code in the event log. */
  rt_event_log (2, mc, 0, field == 3);
  return 1;
}

/* Drive the emulated bus controller against the guest remote terminal.  Two
   words are read from subaddress 2 and written back to subaddress 3, then a
   synchronize mode code is sent. */

/* Communication Frame number the emulated bus controller distributes with the
   synchronize with data word mode command, incremented once per frame. */

static uint16 peer_frame_number;

static void
peer_bc_step (int32 arg)
{
  uint16 buf[2] = { 0, 0 };
  uint32 rtaddr;

  if (!peer_bc_running)
    return;

  /* The emulated bus controller only addresses the demo terminal, so a
     terminal configured with any other address stays silent.  */
  rtaddr = (gr1553_regs[GR1553_RTCFG / 4] & GR1553_RTCFG_RTADDR) >> 1;
  if (rtaddr == GR1553_PEER_RT_ADDRESS)
    {
      /* Establish frame synchronization before running the frame.  A terminal
         which rejects the synchronize with data word mode code as illegal has
         no frame reference, so the frame is not run against it.  This is what
         makes a terminal that builds the mode code control register from zero
         visible: the mode code is legal at reset and stays legal unless the
         driver overwrites the field. */
      if (!rt_mode_code (17, 0, peer_frame_number))
	{
	  event (peer_bc_step, 0, gr1553_us_to_clocks (GR1553_PEER_BC_PERIOD));
	  return;
	}

      rt_transfer (GR1553_PEER_SUB_TO_BC, 0, buf, 2, 0);
      rt_transfer (GR1553_PEER_SUB_TO_RT, 1, buf, 2, 0);

      /* The ECSS Time Message is broadcast to subaddress 29, clause 8.2.1.1d
         and table 6-1, so it reaches a receive subaddress only with BCRXE. */
      rt_transfer (GR1553_PEER_SUB_BCAST, 1, buf, 2, 1);

      rt_mode_code (1, 0, 0); /* Synchronize, communication frame synchronization. */
      peer_frame_number++;
    }

  event (peer_bc_step, 0, gr1553_us_to_clocks (GR1553_PEER_BC_PERIOD));
}

static void
gr1553_reset (void)
{
  memset (gr1553_regs, 0, sizeof (gr1553_regs));
  memset (peer_rt_data, 0, sizeof (peer_rt_data));
  gr1553_regs[GR1553_BCSTAT / 4] = GR1553_BCSUP;
  gr1553_regs[GR1553_RTSTAT / 4] = GR1553_RTSUP;

  /* Table 326.  Synchronize, synchronize with data word and transmitter
     shutdown are legal at reset, each in its own and its broadcast form.
     Zero here would make every mode code illegal, which no real part does and
     which hides a driver that builds the register from zero. */
  gr1553_regs[GR1553_RTMCCTL / 4] = 0x00000555;

  /* Table 321.  The sync and bus reset output enables reset to 1. */
  gr1553_regs[GR1553_RTCFG / 4] = 0x0000E000;
  bc_running = 0;
  peer_bc_running = 0;
  peer_frame_number = 1;
}

static int
gr1553_read (uint32 addr, uint32 *data)
{
  *data = gr1553_regs[(addr & 0xFF) >> 2];
  return 1;
}

static int
gr1553_write (uint32 addr, uint32 *data, uint32 sz)
{
  uint32 off = addr & 0xFF;
  uint32 value = *data;

  switch (off)
    {
    case GR1553_IRQ:
      /* Write one to clear. */
      gr1553_regs[GR1553_IRQ / 4] &= ~value;
      break;

    case GR1553_BCSTAT:
    case GR1553_RTSTAT:
      /* Read only. */
      break;

    case GR1553_BCCTRL:
      if ((value >> 16) != GR1553_BCKEY)
	break;
      if (value & GR1553_BCACT_SCSTP)
	{
	  bc_running = 0;
	  gr1553_regs[GR1553_BCSTAT / 4] &= ~0x7;
	}
      if (value & GR1553_BCACT_SCSRT)
	{
	  gr1553_regs[GR1553_BCSLOT / 4] = gr1553_regs[GR1553_BCBD / 4];
	  gr1553_regs[GR1553_BCSTAT / 4] =
	      (gr1553_regs[GR1553_BCSTAT / 4] & ~0x7) | 1;
	  if (!bc_running)
	    {
	      bc_running = 1;
	      event (bc_step, 0, gr1553_us_to_clocks (1));
	    }
	}
      break;

    case GR1553_RTCFG:
      if ((value >> 16) != GR1553_RTKEY)
	break;
      gr1553_regs[GR1553_RTCFG / 4] = value & 0xFFFF;
      if (value & GR1553_RTCFG_RTEN)
	{
	  gr1553_regs[GR1553_RTSTAT / 4] |= 1;
	  if (!peer_bc_running)
	    {
	      peer_bc_running = 1;
	      event (peer_bc_step, 0,
		     gr1553_us_to_clocks (GR1553_PEER_BC_PERIOD));
	    }
	}
      else
	{
	  gr1553_regs[GR1553_RTSTAT / 4] &= ~1u;
	  peer_bc_running = 0;
	}
      break;

    default:
      gr1553_regs[off >> 2] = value;
      break;
    }

  return 1;
}

static void
gr1553_add (int irq, uint32 addr, uint32 mask)
{
  gr1553_irq = irq;
  grlib_apbpp_add (GRLIB_PP_ID (VENDOR_GAISLER, GAISLER_GR1553B, 0, irq),
		   GRLIB_PP_APBADDR (addr, mask));
  if (sis_verbose)
    printf (" GR1553B MIL-STD-1553B              0x%08x   irq %2d\n", addr,
	    irq);
}

const struct grlib_ipcore gr1553b = { NULL, gr1553_reset, gr1553_read,
				      gr1553_write, gr1553_add };
