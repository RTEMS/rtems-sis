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
 * GRLIB MEMSCRUB memory scrubber and AHB status register.
 *
 * The model covers what the GR740 memory scrubber driver exercises: the scrub
 * and regenerate runs over one or two address ranges, the run position, the
 * global and scrub scoped error counters with their interrupt thresholds, and
 * the task completed interrupt.
 *
 * Initialize mode writes the configured pattern across the range through the
 * emulated memory, so a test can start an Initialize run and read the pattern
 * back.
 *
 * Errors are held in a per-word EDAC fault map.  The map is filled by the
 * memory controller FT diagnostic registers (FTDA and FTDC, modelled in
 * grlib.cc), which corrupt the checkbits of a word, so that a test injects an
 * error the way it would on the hardware and then watches the scrubber find it.
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include "sis.h"
#include "grlib.h"

/* Register offsets inside the 1 M window of the core. */

#define MEMSCRUB_REGS_OFFSET 0x1000

#define MEMSCRUB_AHBS	 0x00
#define MEMSCRUB_AHBFAR	 0x04
#define MEMSCRUB_AHBERC	 0x08
#define MEMSCRUB_STAT	 0x10
#define MEMSCRUB_CONFIG	 0x14
#define MEMSCRUB_RANGEL	 0x18
#define MEMSCRUB_RANGEH	 0x1C
#define MEMSCRUB_POS	 0x20
#define MEMSCRUB_ETHRES	 0x24
#define MEMSCRUB_INIT	 0x28
#define MEMSCRUB_RANGEL2 0x2C
#define MEMSCRUB_RANGEH2 0x30

/*
 * SDRAM memory controller (MMCTRL) FT diagnostic registers.  They live at the
 * base of the same 1 M window as the scrubber, below MEMSCRUB_REGS_OFFSET.  The
 * driver tests inject EDAC errors through them the way they do on hardware.
 */
#define MMCTRL_MUXCFG 0x20
#define MMCTRL_FTDA   0x24
#define MMCTRL_FTDC   0x28
#define MMCTRL_FTDD   0x2C

#define MMCTRL_MUXCFG_EDEN 0x1

/* AHB status register bits. */

#define MEMSCRUB_AHBS_HMASTER (0xF << 3)
#define MEMSCRUB_AHBS_HWRITE  (1 << 7)
#define MEMSCRUB_AHBS_NE      (1 << 8)
#define MEMSCRUB_AHBS_CE      (1 << 9)
#define MEMSCRUB_AHBS_SBC     (1 << 10)
#define MEMSCRUB_AHBS_SEC     (1 << 11)
#define MEMSCRUB_AHBS_DONE    (1 << 13)
#define MEMSCRUB_AHBS_UECNT   (0xFF << 14)
#define MEMSCRUB_AHBS_CECNT   (0x3FF << 22)

#define MEMSCRUB_AHBS_UECNT_BIT 14
#define MEMSCRUB_AHBS_CECNT_BIT 22

/* AHB error configuration register bits. */

#define MEMSCRUB_AHBERC_UECTE  (1 << 0)
#define MEMSCRUB_AHBERC_CECTE  (1 << 1)
#define MEMSCRUB_AHBERC_UECNTT (0xFF << 14)
#define MEMSCRUB_AHBERC_CECNTT (0x3FF << 22)

#define MEMSCRUB_AHBERC_UECNTT_BIT 14
#define MEMSCRUB_AHBERC_CECNTT_BIT 22

/* Scrubber status register bits. */

#define MEMSCRUB_STAT_ACTIVE   (1 << 0)
#define MEMSCRUB_STAT_BURSTLEN (0xF << 1)
#define MEMSCRUB_STAT_DONE     (1 << 13)
#define MEMSCRUB_STAT_BLKCOUNT (0xFF << 14)
#define MEMSCRUB_STAT_RUNCOUNT (0x3FF << 22)

#define MEMSCRUB_STAT_BURSTLEN_BIT 1
#define MEMSCRUB_STAT_BLKCOUNT_BIT 14
#define MEMSCRUB_STAT_RUNCOUNT_BIT 22

/* Scrubber configuration register bits. */

#define MEMSCRUB_CONFIG_SCEN  (1 << 0)
#define MEMSCRUB_CONFIG_ES    (1 << 1)
#define MEMSCRUB_CONFIG_MODE  (3 << 2)
#define MEMSCRUB_CONFIG_LOOP  (1 << 4)
#define MEMSCRUB_CONFIG_SERA  (1 << 5)
#define MEMSCRUB_CONFIG_IRQD  (1 << 7)
#define MEMSCRUB_CONFIG_DELAY (0xFF << 8)

#define MEMSCRUB_CONFIG_MODE_BIT  2
#define MEMSCRUB_CONFIG_DELAY_BIT 8

#define MEMSCRUB_MODE_SCRUB 0
#define MEMSCRUB_MODE_REGEN 1
#define MEMSCRUB_MODE_INIT  2

/* Scrub thresholds register bits. */

#define MEMSCRUB_ETHRES_BECTE (1 << 0)
#define MEMSCRUB_ETHRES_RECTE (1 << 1)
#define MEMSCRUB_ETHRES_BECT  (0xFF << 14)
#define MEMSCRUB_ETHRES_RECT  (0x3FF << 22)

#define MEMSCRUB_ETHRES_BECT_BIT 14
#define MEMSCRUB_ETHRES_RECT_BIT 22

/* The burst and the count block are both 32 bytes on the GR740. */

#define MEMSCRUB_BURST_BYTES 32

/* Encoded burst length, the driver reads it back as 1 << 3 words. */

#define MEMSCRUB_BURSTLEN_CODE 3

/* Bursts processed per scheduled step, to keep the event queue short. */

#define MEMSCRUB_BURSTS_PER_STEP 32

/* Bus clocks a burst takes, before the programmed inter burst delay. */

#define MEMSCRUB_BURST_CLOCKS 16

#define MEMSCRUB_MAX_INJECTED 16

#define MEMSCRUB_CECNT_MAX 0x3FF
#define MEMSCRUB_UECNT_MAX 0xFF

/*
 * An EDAC fault is modelled per 64-bit codeword.  The GR740 Reed-Solomon code
 * silently corrects any corruption confined to the data or to the checkbits,
 * even a full data flip, and reports an uncorrectable error only when both the
 * data and the checkbits of a codeword are corrupted (confirmed on hardware).
 * So the entry tracks the two independently: the two 32-bit data halves and the
 * checkbits.  data_bad OR cb_bad is a correctable error, data_bad AND cb_bad is
 * an uncorrectable one.  The good data captured on a diagnostic read lets a
 * later diagnostic write that restores it clear the fault.
 */
struct memscrub_injected
{
  uint32 addr;
  int used;
  int data_bad[2];
  int cb_bad;
};

static struct memscrub_injected memscrub_injected[MEMSCRUB_MAX_INJECTED];

/* Good data captured on diagnostic reads, so a restoring write clears a fault. */

#define MEMSCRUB_GOOD_CACHE 8

struct memscrub_good
{
  uint32 word;
  uint32 value;
  int valid;
};

static struct memscrub_good memscrub_good[MEMSCRUB_GOOD_CACHE];

/* The MMCTRL FT diagnostic register state. */

static uint32 mmctrl_muxcfg;

static uint32 mmctrl_ftda;

/* The eight word (32 byte) pattern written by an Initialize run. */

static uint32 memscrub_init_pattern[8];

static int memscrub_init_index;

static uint32 memscrub_ahbs;
static uint32 memscrub_ahbfar;
static uint32 memscrub_ahberc;
static uint32 memscrub_stat;
static uint32 memscrub_config;
static uint32 memscrub_rangel;
static uint32 memscrub_rangeh;
static uint32 memscrub_pos;
static uint32 memscrub_ethres;
static uint32 memscrub_rangel2;
static uint32 memscrub_rangeh2;

static int memscrub_irq;

/* Non-zero while a run is scheduled, so a stop can drop a pending step. */

static int memscrub_running;

/* Set while the run sweeps the secondary range. */

static int memscrub_in_second_range;

static void memscrub_step (int32 arg);

static uint32
memscrub_field_get (uint32 value, uint32 mask, int shift)
{
  return (value & mask) >> shift;
}

static uint32
memscrub_get_mode (void)
{
  return memscrub_field_get (memscrub_config, MEMSCRUB_CONFIG_MODE,
			     MEMSCRUB_CONFIG_MODE_BIT);
}

/*
 * Raise the core interrupt.  Both the error and the task completed condition
 * use the single interrupt line of the core.
 */

static void
memscrub_set_irq (void)
{
  if (memscrub_irq > 0)
    grlib_set_irq (memscrub_irq);
}

/*
 * Increment a counter field, saturating at its maximum, and report whether the
 * increment crossed the given threshold.  The hardware raises an interrupt
 * only on the increment from the threshold to the threshold plus one.
 */

static int
memscrub_count_up (uint32 *reg, uint32 mask, int shift, uint32 max,
		   uint32 threshold, int enabled)
{
  uint32 count = memscrub_field_get (*reg, mask, shift);

  if (count >= max)
    return 0;

  count++;
  *reg = (*reg & ~mask) | (count << shift);

  return enabled && count == threshold + 1;
}

/* Record an error found by the scrubber at the given address. */

static void
memscrub_report_error (uint32 addr, int correctable)
{
  uint32 threshold;
  int enabled;
  int raise = 0;

  /*
   * The status and address registers are frozen while the New Error bit is
   * set, so the guest sees the first error until it acknowledges.
   */
  if ((memscrub_ahbs & MEMSCRUB_AHBS_NE) == 0)
    {
      memscrub_ahbfar = addr;
      memscrub_ahbs &= ~(MEMSCRUB_AHBS_HMASTER | MEMSCRUB_AHBS_HWRITE);
      memscrub_ahbs |= MEMSCRUB_AHBS_NE;

      if (correctable)
	memscrub_ahbs |= MEMSCRUB_AHBS_CE;
      else
	memscrub_ahbs &= ~MEMSCRUB_AHBS_CE;
    }

  if (correctable)
    {
      threshold = memscrub_field_get (memscrub_ahberc, MEMSCRUB_AHBERC_CECNTT,
				      MEMSCRUB_AHBERC_CECNTT_BIT);
      enabled = (memscrub_ahberc & MEMSCRUB_AHBERC_CECTE) != 0;
      raise |= memscrub_count_up (&memscrub_ahbs, MEMSCRUB_AHBS_CECNT,
				  MEMSCRUB_AHBS_CECNT_BIT, MEMSCRUB_CECNT_MAX,
				  threshold, enabled);

      /* The scrub scoped counters only advance while no error is pending. */
      threshold = memscrub_field_get (memscrub_ethres, MEMSCRUB_ETHRES_RECT,
				      MEMSCRUB_ETHRES_RECT_BIT);
      enabled = (memscrub_ethres & MEMSCRUB_ETHRES_RECTE) != 0;

      if (memscrub_count_up (&memscrub_stat, MEMSCRUB_STAT_RUNCOUNT,
			     MEMSCRUB_STAT_RUNCOUNT_BIT, MEMSCRUB_CECNT_MAX,
			     threshold, enabled))
	{
	  memscrub_ahbs |= MEMSCRUB_AHBS_NE | MEMSCRUB_AHBS_SEC;
	  raise = 1;
	}

      threshold = memscrub_field_get (memscrub_ethres, MEMSCRUB_ETHRES_BECT,
				      MEMSCRUB_ETHRES_BECT_BIT);
      enabled = (memscrub_ethres & MEMSCRUB_ETHRES_BECTE) != 0;

      if (memscrub_count_up (&memscrub_stat, MEMSCRUB_STAT_BLKCOUNT,
			     MEMSCRUB_STAT_BLKCOUNT_BIT, MEMSCRUB_UECNT_MAX,
			     threshold, enabled))
	{
	  memscrub_ahbs |= MEMSCRUB_AHBS_NE | MEMSCRUB_AHBS_SBC;
	  raise = 1;
	}
    }
  else
    {
      threshold = memscrub_field_get (memscrub_ahberc, MEMSCRUB_AHBERC_UECNTT,
				      MEMSCRUB_AHBERC_UECNTT_BIT);
      enabled = (memscrub_ahberc & MEMSCRUB_AHBERC_UECTE) != 0;
      raise |= memscrub_count_up (&memscrub_ahbs, MEMSCRUB_AHBS_UECNT,
				  MEMSCRUB_AHBS_UECNT_BIT, MEMSCRUB_UECNT_MAX,
				  threshold, enabled);
    }

  if (raise)
    memscrub_set_irq ();
}

/*
 * Process one burst.  A correctable error is corrected and leaves the
 * injection table, an uncorrectable one is left untouched in memory and in the
 * table.
 */

static void
memscrub_burst (uint32 addr)
{
  int i;

  /* With the EDAC disabled the controller does not check reads. */
  if ((mmctrl_muxcfg & MMCTRL_MUXCFG_EDEN) == 0)
    return;

  for (i = 0; i < MEMSCRUB_MAX_INJECTED; i++)
    {
      struct memscrub_injected *e = &memscrub_injected[i];
      int data_bad;

      if (!e->used)
	continue;

      if (e->addr < addr || e->addr >= addr + MEMSCRUB_BURST_BYTES)
	continue;

      data_bad = e->data_bad[0] || e->data_bad[1];

      if (data_bad && e->cb_bad)
	{
	  /* Uncorrectable: left untouched, so every pass finds it again. */
	  memscrub_report_error (e->addr, 0);
	}
      else if (data_bad || e->cb_bad)
	{
	  /* Correctable: corrected by the scrub and leaves the table. */
	  memscrub_report_error (e->addr, 1);
	  e->used = 0;
	}
    }
}

static uint32
memscrub_range_start (void)
{
  if (memscrub_in_second_range)
    return memscrub_rangel2 & ~(MEMSCRUB_BURST_BYTES - 1);

  return memscrub_rangel & ~(MEMSCRUB_BURST_BYTES - 1);
}

static uint32
memscrub_range_end (void)
{
  if (memscrub_in_second_range)
    return memscrub_rangeh2;

  return memscrub_rangeh;
}

/* Schedule the next step of an active run. */

static void
memscrub_schedule (void)
{
  uint32 delay = memscrub_field_get (memscrub_config, MEMSCRUB_CONFIG_DELAY,
				     MEMSCRUB_CONFIG_DELAY_BIT);
  uint64 clocks;

  clocks = (uint64) MEMSCRUB_BURSTS_PER_STEP *
	   (uint64) (MEMSCRUB_BURST_CLOCKS + delay);
  event (memscrub_step, 0, clocks);
}

/* Finish the run, or restart it when looping is requested. */

static void
memscrub_finish (void)
{
  if (memscrub_config & MEMSCRUB_CONFIG_LOOP)
    {
      memscrub_in_second_range = 0;
      memscrub_pos = memscrub_range_start ();
      memscrub_stat &= ~MEMSCRUB_STAT_RUNCOUNT;
      memscrub_schedule ();
      return;
    }

  memscrub_running = 0;
  memscrub_stat &= ~MEMSCRUB_STAT_ACTIVE;
  memscrub_stat |= MEMSCRUB_STAT_DONE;
  memscrub_config &= ~MEMSCRUB_CONFIG_SCEN;

  if (memscrub_config & MEMSCRUB_CONFIG_IRQD)
    memscrub_set_irq ();
}

static void
memscrub_step (int32 arg)
{
  int i;

  (void) arg;

  if (!memscrub_running)
    return;

  for (i = 0; i < MEMSCRUB_BURSTS_PER_STEP; i++)
    {
      if (memscrub_pos > memscrub_range_end ())
	{
	  /* Move on to the secondary range when dual range is enabled. */
	  if ((memscrub_config & MEMSCRUB_CONFIG_SERA) &&
	      !memscrub_in_second_range)
	    {
	      memscrub_in_second_range = 1;
	      memscrub_pos = memscrub_range_start ();
	      continue;
	    }

	  memscrub_finish ();
	  return;
	}

      /* The block scoped counter is scoped to one burst. */
      memscrub_stat &= ~MEMSCRUB_STAT_BLKCOUNT;
      memscrub_burst (memscrub_pos);
      memscrub_pos += MEMSCRUB_BURST_BYTES;
    }

  memscrub_schedule ();
}

/* Clear the EDAC fault of every word in the range, as a write of fresh data
   and checkbits would. */

static void
memscrub_edac_clear_range (uint32 lo, uint32 hi)
{
  int i;

  for (i = 0; i < MEMSCRUB_MAX_INJECTED; i++)
    if (memscrub_injected[i].used
	&& memscrub_injected[i].addr >= lo && memscrub_injected[i].addr <= hi)
      memscrub_injected[i].used = 0;
}

/* Write the initialization pattern across a range and clear its EDAC faults,
   modelling an Initialize run over one range. */

static void
memscrub_init_range (uint32 lo, uint32 hi)
{
  int32 ws;
  uint32 a;

  lo &= ~(MEMSCRUB_BURST_BYTES - 1);

  for (a = lo; a <= hi; a += 4)
    {
      uint32 w = memscrub_init_pattern[(a >> 2) & 7];

      ms->memory_write (a, &w, 2, &ws);
    }

  memscrub_edac_clear_range (lo, hi);
}

/* Start a run from the beginning of the primary range. */

static void
memscrub_start (void)
{
  uint32 mode = memscrub_get_mode ();

  /* Initialize mode writes the pattern across the range and completes at once. */
  if (mode == MEMSCRUB_MODE_INIT)
    {
      memscrub_init_range (memscrub_rangel, memscrub_rangeh);
      if (memscrub_config & MEMSCRUB_CONFIG_SERA)
	memscrub_init_range (memscrub_rangel2, memscrub_rangeh2);

      memscrub_stat &= ~MEMSCRUB_STAT_ACTIVE;
      memscrub_stat |= MEMSCRUB_STAT_DONE;
      memscrub_config &= ~MEMSCRUB_CONFIG_SCEN;

      if (memscrub_config & MEMSCRUB_CONFIG_IRQD)
	memscrub_set_irq ();

      return;
    }

  /* A scrub or regenerate run reuses the pattern buffer as scratch. */
  memset (memscrub_init_pattern, 0, sizeof (memscrub_init_pattern));

  memscrub_in_second_range = 0;
  memscrub_pos = memscrub_range_start ();
  memscrub_stat &=
      ~(MEMSCRUB_STAT_DONE | MEMSCRUB_STAT_RUNCOUNT | MEMSCRUB_STAT_BLKCOUNT);
  memscrub_stat |= MEMSCRUB_STAT_ACTIVE;
  memscrub_running = 1;
  memscrub_schedule ();
}

static void
memscrub_stop (void)
{
  memscrub_running = 0;
  memscrub_in_second_range = 0;
  memscrub_stat &= ~MEMSCRUB_STAT_ACTIVE;
  remove_event (memscrub_step, 0);
}

/* Find the fault entry for a codeword, creating one when asked. */

static struct memscrub_injected *
memscrub_edac_entry (uint32 word, int create)
{
  uint32 base = word & ~0x7u;
  int i, free = -1;

  for (i = 0; i < MEMSCRUB_MAX_INJECTED; i++)
    {
      if (memscrub_injected[i].used && memscrub_injected[i].addr == base)
	return &memscrub_injected[i];
      if (!memscrub_injected[i].used && free < 0)
	free = i;
    }

  if (!create)
    return NULL;

  if (free < 0)
    free = 0;

  memset (&memscrub_injected[free], 0, sizeof (memscrub_injected[free]));
  memscrub_injected[free].used = 1;
  memscrub_injected[free].addr = base;
  return &memscrub_injected[free];
}

/* Drop an entry once it carries no corruption again. */

static void
memscrub_edac_prune (struct memscrub_injected *e)
{
  if (e != NULL && !e->data_bad[0] && !e->data_bad[1] && !e->cb_bad)
    e->used = 0;
}

/* Capture good data seen on a diagnostic read, keyed by 32-bit word.  A read of
   a half already corrupted must not redefine its good value, so the good data
   stays the value seen before the corruption. */

static void
memscrub_edac_note_good (uint32 word, uint32 value)
{
  struct memscrub_injected *e = memscrub_edac_entry (word, 0);
  int half = (word >> 2) & 1;
  int i, slot = -1;

  if (e != NULL && e->data_bad[half])
    return;

  for (i = 0; i < MEMSCRUB_GOOD_CACHE; i++)
    {
      if (memscrub_good[i].valid && memscrub_good[i].word == word)
	{
	  slot = i;
	  break;
	}
      if (!memscrub_good[i].valid && slot < 0)
	slot = i;
    }

  if (slot < 0)
    slot = 0;

  memscrub_good[slot].word = word;
  memscrub_good[slot].value = value;
  memscrub_good[slot].valid = 1;
}

static int
memscrub_edac_good (uint32 word, uint32 *value)
{
  int i;

  for (i = 0; i < MEMSCRUB_GOOD_CACHE; i++)
    if (memscrub_good[i].valid && memscrub_good[i].word == word)
      {
	*value = memscrub_good[i].value;
	return 1;
      }

  return 0;
}

/* A diagnostic data write.  It corrupts the half when the value differs from
   the captured good data, and clears it when the value restores it.  This
   relies on the word having been read before it is corrupted, which an
   injection does naturally, so the good data is known.  A write to a word never
   read is treated as leaving it consistent. */

static void
memscrub_edac_data_write (uint32 word, uint32 value)
{
  int half = (word >> 2) & 1;
  uint32 good;
  struct memscrub_injected *e;

  if (memscrub_edac_good (word, &good) && value != good)
    {
      e = memscrub_edac_entry (word, 1);
      e->data_bad[half] = 1;
    }
  else
    {
      e = memscrub_edac_entry (word, 0);
      if (e != NULL)
	{
	  e->data_bad[half] = 0;
	  memscrub_edac_prune (e);
	}
    }
}

/* A diagnostic checkbit write.  Correct checkbits are modelled as zero. */

static void
memscrub_edac_checkbit_write (uint32 word, uint32 value)
{
  struct memscrub_injected *e;

  if (value != 0)
    {
      e = memscrub_edac_entry (word, 1);
      e->cb_bad = 1;
    }
  else
    {
      e = memscrub_edac_entry (word, 0);
      if (e != NULL)
	{
	  e->cb_bad = 0;
	  memscrub_edac_prune (e);
	}
    }
}

/* The MMCTRL FT diagnostic registers, decoded below MEMSCRUB_REGS_OFFSET. */

static int
mmctrl_read (uint32 addr, uint32 *data)
{
  int32 ws;
  uint32 res = 0;

  switch (addr)
    {
    case MMCTRL_MUXCFG:
      res = mmctrl_muxcfg;
      break;
    case MMCTRL_FTDA:
      res = mmctrl_ftda;
      break;
    case MMCTRL_FTDC:
      /* Correct checkbits are modelled as zero. */
      res = 0;
      break;
    case MMCTRL_FTDD:
      ms->memory_read (mmctrl_ftda, &res, &ws);
      memscrub_edac_note_good (mmctrl_ftda, res);
      break;
    default:
      res = 0;
      break;
    }

  *data = res;
  return 1;
}

static int
mmctrl_write (uint32 addr, uint32 *data)
{
  uint32 value = *data;
  int32 ws;

  switch (addr)
    {
    case MMCTRL_MUXCFG:
      mmctrl_muxcfg = value;
      break;
    case MMCTRL_FTDA:
      mmctrl_ftda = value & ~0x3u;
      break;
    case MMCTRL_FTDC:
      memscrub_edac_checkbit_write (mmctrl_ftda, value);
      break;
    case MMCTRL_FTDD:
      ms->memory_write (mmctrl_ftda, &value, 2, &ws);
      memscrub_edac_data_write (mmctrl_ftda, value);
      break;
    default:
      break;
    }

  return 1;
}

static int
memscrub_read (uint32 addr, uint32 *data)
{
  uint32 reg = addr - MEMSCRUB_REGS_OFFSET;

  if (addr < MEMSCRUB_REGS_OFFSET)
    return mmctrl_read (addr, data);

  switch (reg)
    {
    case MEMSCRUB_AHBS:
      /* The task completed bit is a read-only copy of the one in STAT. */
      *data = memscrub_ahbs & ~MEMSCRUB_AHBS_DONE;
      if (memscrub_stat & MEMSCRUB_STAT_DONE)
	*data |= MEMSCRUB_AHBS_DONE;
      break;
    case MEMSCRUB_AHBFAR:
      *data = memscrub_ahbfar;
      break;
    case MEMSCRUB_AHBERC:
      *data = memscrub_ahberc;
      break;
    case MEMSCRUB_STAT:
      *data = memscrub_stat |
	      (MEMSCRUB_BURSTLEN_CODE << MEMSCRUB_STAT_BURSTLEN_BIT);
      break;
    case MEMSCRUB_CONFIG:
      *data = memscrub_config;
      break;
    case MEMSCRUB_RANGEL:
      *data = memscrub_rangel;
      break;
    case MEMSCRUB_RANGEH:
      *data = memscrub_rangeh;
      break;
    case MEMSCRUB_POS:
      *data = memscrub_pos;
      break;
    case MEMSCRUB_ETHRES:
      *data = memscrub_ethres;
      break;
    case MEMSCRUB_RANGEL2:
      *data = memscrub_rangel2;
      break;
    case MEMSCRUB_RANGEH2:
      *data = memscrub_rangeh2;
      break;
    default:
      *data = 0;
      break;
    }

  return 1;
}

static int
memscrub_write (uint32 addr, uint32 *data, uint32 size)
{
  uint32 reg = addr - MEMSCRUB_REGS_OFFSET;
  uint32 value = *data;

  if (addr < MEMSCRUB_REGS_OFFSET)
    return mmctrl_write (addr, data);

  if (size != 2)
    return 1;

  switch (reg)
    {
    case MEMSCRUB_AHBS:
      /*
       * The guest clears the status bits and the counters it has consumed by
       * writing them back as zero.  The task completed bit lives in STAT.
       */
      memscrub_ahbs = value & ~MEMSCRUB_AHBS_DONE;
      break;
    case MEMSCRUB_AHBERC:
      memscrub_ahberc = value;
      break;
    case MEMSCRUB_STAT:
      /* The active bit and the burst length are read-only. */
      memscrub_stat =
	  (memscrub_stat & MEMSCRUB_STAT_ACTIVE) |
	  (value & ~(MEMSCRUB_STAT_ACTIVE | MEMSCRUB_STAT_BURSTLEN));
      break;
    case MEMSCRUB_CONFIG:
      memscrub_config = value;

      if ((value & MEMSCRUB_CONFIG_SCEN) == 0)
	memscrub_stop ();
      else if (!memscrub_running)
	memscrub_start ();

      break;
    case MEMSCRUB_RANGEL:
      memscrub_rangel = value;
      break;
    case MEMSCRUB_RANGEH:
      memscrub_rangeh = value;
      break;
    case MEMSCRUB_ETHRES:
      memscrub_ethres = value;
      break;
    case MEMSCRUB_INIT:
      /* The pattern buffer is filled by eight consecutive writes. */
      memscrub_init_pattern[memscrub_init_index] = value;
      memscrub_init_index = (memscrub_init_index + 1) % 8;
      break;
    case MEMSCRUB_RANGEL2:
      memscrub_rangel2 = value;
      break;
    case MEMSCRUB_RANGEH2:
      memscrub_rangeh2 = value;
      break;
    default:
      break;
    }

  return 1;
}

static void
memscrub_reset (void)
{
  memscrub_ahbs = 0;
  memscrub_ahbfar = 0;
  memscrub_stat = 0;
  memscrub_config = 0;
  memscrub_rangel = 0;
  memscrub_rangeh = 0;
  memscrub_pos = 0;
  memscrub_ethres = 0;
  memscrub_rangel2 = 0;
  memscrub_rangeh2 = 0;
  memscrub_running = 0;
  memscrub_in_second_range = 0;
  memscrub_init_index = 0;

  /*
   * Both thresholds are zero and enabled after reset, so the first error on
   * the bus raises an interrupt.
   */
  memscrub_ahberc = MEMSCRUB_AHBERC_UECTE | MEMSCRUB_AHBERC_CECTE;

  memset (memscrub_injected, 0, sizeof (memscrub_injected));
  memset (memscrub_init_pattern, 0, sizeof (memscrub_init_pattern));
}

static void
memscrub_add (int irq, uint32 addr, uint32 mask)
{
  memscrub_irq = irq;
  grlib_ahbmpp_add (GRLIB_PP_ID (VENDOR_GAISLER, GAISLER_MEMSCRUB, 0, irq));
  grlib_ahbspp_add (GRLIB_PP_ID (VENDOR_GAISLER, GAISLER_MEMSCRUB, 0, irq),
		    GRLIB_PP_AHBADDR (addr, mask, 0, 0, 2), 0, 0, 0);
  if (sis_verbose)
    printf (" MEMSCRUB memory scrubber           0x%08x   irq %2d\n",
	    addr + MEMSCRUB_REGS_OFFSET, irq);
}

const struct grlib_ipcore memscrub = { NULL, memscrub_reset, memscrub_read,
				       memscrub_write, memscrub_add };
