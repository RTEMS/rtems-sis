/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* A flat memory for driving a CPU core without a board.  See cpumem.h.  */

#include "cpumem.h"

#include "sis.h"

#include <string.h>

namespace
{

char flatmem_bytes[sis_tests::FLATMEM_SIZE];
int flatmem_mexc;
uint32 flatmem_ro = ~0u;

bool
inside (uint32 addr, uint32 size)
{
  return addr + size <= sis_tests::FLATMEM_SIZE;
}

void
noop ()
{
}

void
noop_error_mode (uint32 pc)
{
  (void) pc;
}

int
flat_read (uint32 addr, uint32 *data, int32 *ws)
{
  if (!inside (addr, 4))
    {
      flatmem_mexc++;
      *ws = 1;
      return 1;
    }
  memcpy (data, &flatmem_bytes[addr & ~3u], 4);
  *ws = 0;
  return 0;
}

int
flat_write (uint32 addr, uint32 *data, int32 sz, int32 *ws)
{
  /* sz is the two-bit store size: 0 byte, 1 half-word, 2 word, 3 double.  */
  uint32 size = sz == 3 ? 8 : 1u << sz;
  uint32 waddr = addr & ~3u;

  if (!inside (addr, size))
    {
      flatmem_mexc++;
      *ws = 1;
      return 1;
    }

  if ((addr & ~3u) == flatmem_ro)
    {
      flatmem_mexc++;
      *ws = 1;
      return 1;
    }

  *ws = 0;
  if (sz == 0)
    {
      waddr = addr;
#ifdef HOST_LITTLE_ENDIAN
      waddr ^= 3;
#endif
      flatmem_bytes[waddr] = (char) (*data & 0x0ff);
    }
  else if (sz == 1)
    {
      waddr = addr & ~1u;
#ifdef HOST_LITTLE_ENDIAN
      waddr ^= 2;
#endif
      uint16 half = (uint16) (*data & 0x0ffff);
      memcpy (&flatmem_bytes[waddr], &half, 2);
    }
  else if (sz == 2)
    memcpy (&flatmem_bytes[waddr], data, 4);
  else
    memcpy (&flatmem_bytes[waddr], data, 8);

  return 0;
}

int
flat_sis_write (uint32 addr, const char *data, uint32 length)
{
  if (!inside (addr, length))
    return 0;
  memcpy (&flatmem_bytes[addr], data, length);
  return (int) length;
}

int
flat_sis_read (uint32 addr, char *data, uint32 length)
{
  if (!inside (addr, length))
    return 0;
  memcpy (data, &flatmem_bytes[addr], length);
  return (int) length;
}

char *
flat_mem_ptr (uint32 addr, uint32 size)
{
  if (!inside (addr, size))
    return NULL;
  return &flatmem_bytes[addr];
}

void
flat_set_irq (int32 level)
{
  (void) level;
}

} /* namespace */

namespace sis_tests
{

const struct memsys flatmem = { noop, /* init_sim */
				noop, /* reset */
				noop_error_mode,
				noop,	   /* sim_halt */
				noop,	   /* exit_sim */
				noop,	   /* init_stdio */
				noop,	   /* restore_stdio */
				flat_read, /* memory_iread */
				flat_read,
				flat_write,
				flat_sis_write,
				flat_sis_read,
				noop, /* boot_init */
				flat_mem_ptr,
				flat_set_irq };

void
flatmem_clear ()
{
  memset (flatmem_bytes, 0, sizeof flatmem_bytes);
  flatmem_mexc = 0;
  flatmem_ro = ~0u;
}

void
flatmem_fail_write (uint32 addr)
{
  flatmem_ro = addr & ~3u;
}

void
flatmem_poke (uint32 addr, uint32 value)
{
  memcpy (&flatmem_bytes[addr & ~3u], &value, 4);
}

uint32
flatmem_peek (uint32 addr)
{
  uint32 value;

  memcpy (&value, &flatmem_bytes[addr & ~3u], 4);
  return value;
}

int
flatmem_faults ()
{
  return flatmem_mexc;
}

} /* namespace sis_tests */
