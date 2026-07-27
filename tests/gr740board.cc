/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Tests for the GR740 board file, gr740.cc.

   The board is a LEON4 quad core on GRLIB's shared AHB/APB bus.  Its
   memsys entry points are driven here directly, one board callback per
   case, with no board registration at all: gr740.init_sim registers a
   bridge and ten cores on grlib.cc's process wide bus and calls
   grlib_init, which claims addresses other test files already own, fills
   an APB bridge towards its sixteen core cap and schedules peripheral
   events into the shared event queue.  Calling it from this binary hung
   the suite under some random orderings before, so it is not called and
   the four arcs of init_sim (the irqmp_extirq default and the AHB master
   loop) stay uncovered.  Everything else gr740.cc does needs nothing
   init_sim provides.

   The one thing the memory dispatch needs from the bus is a slave to
   answer outside RAM and ROM.  This file registers a single private fake
   AHB slave for that, at 0x58000000, an address no other test file and no
   emulated board uses, following the seam tests/leon.cc opened for
   leon3.cc.

   Specification citations are to the GR740 User's Manual GR740-UM-DS-2-10
   (also at /opt/eb-docs/SoC/Gaisler/GR740), section 2.3 "Memory map",
   Table 7 "AMBA memory map, as seen from processors", and to doc/gr740.md
   "Memory interface", which records the subset of that map SIS emulates.
   Cases marked "current behaviour" pin what the code does rather than a
   documented interface.  */

#include "doctest.h"
#include "support.h"

#include "config.h"
#include "sis.h"
#include "grlib.h"

#include <cstring>
#include <string>

using sis_tests::stdout_capture;

namespace
{

/* Table 7: the L2 cache area 0x00000000-0x7FFFFFFF covers the SDRAM, and
   the FTMCTRL PROM area is 0xC0000000-0xCFFFFFFF.  doc/gr740.md maps the
   64 MB of RAM and the first 16 MB of the PROM area that SIS emulates.  */
const uint32 GR740_RAM_START = 0x00000000;
const uint32 GR740_RAM_END = 0x04000000;
const uint32 GR740_ROM_START = 0xC0000000;
const uint32 GR740_ROM_SIZE = 0x01000000;
const uint32 GR740_ROM_END = GR740_ROM_START + GR740_ROM_SIZE;

/* Table 7 calls 0xD0000000-0xDFFFFFFF a memory mapped I/O area.  SIS
   models nothing there (doc/gr740.md lists every area it does model), so
   it is a memory exception and serves as the "no slave claims it"
   address.  It also sits above ROM_END, which is what makes the ROM
   decode's upper bound observable.  */
const uint32 GR740_UNMAPPED = 0xD0000000;

/* Word access, the size code grlib_store_bytes takes for a full word.  */
const int32 SZ_WORD = 2;

/* A private fake AHB slave.  0x58000000 is claimed by no other test file
   (tests/leon.cc uses 0x50000000, tests/grlibbus.cc 0x70000000-0x72000000,
   tests/grlibcores.cc the APB window at 0x80000200/0x80000300) and by no
   emulated board, and it lies below the GR740 ROM window, which is what
   makes the lower bound of the ROM decode observable.  Registered once for
   the whole binary, since grlib.cc's registration arrays only ever
   grow.  */
const uint32 FAKE_AHB_BASE = 0x58000000;

int fake_ahb_read_calls;
int fake_ahb_write_calls;
int fake_ahb_reset_calls;

void
gr740_fake_reset (void)
{
  fake_ahb_reset_calls++;
}

int
gr740_fake_read (uint32 addr, uint32 *data)
{
  fake_ahb_read_calls++;
  *data = 0x74000000u | (addr & 0xffff);
  return 1;
}

int
gr740_fake_write (uint32 addr, uint32 *data, uint32 size)
{
  (void) addr;
  (void) data;
  (void) size;
  fake_ahb_write_calls++;
  return 1;
}

void
gr740_fake_add (int irq, uint32 addr, uint32 mask)
{
  (void) irq;
  (void) addr;
  (void) mask;
}

/* grlib_ahbs_add records start/end/mask only for a core with a non-NULL
   add, so the fake needs one to be reachable at all.  init stays NULL,
   which grlib_init skips; reset only bumps a counter, which is what makes
   the board's reset observable.  */
const struct grlib_ipcore fake_ahb_slave = { NULL, gr740_fake_reset,
					     gr740_fake_read, gr740_fake_write,
					     gr740_fake_add };

void
ensure_fake_ahb_registered ()
{
  static bool done = false;

  if (done)
    return;
  done = true;

  grlib_ahbs_add (&fake_ahb_slave, 0, FAKE_AHB_BASE, 0xfff);
}

/* Per-case setup: points ms and arch at the GR740's SPARC configuration,
   clears the event queue and the register file through reset_all, and
   restores every global it touched.  arch is set explicitly because
   grlib_store_bytes swaps a sub-word address by arch->bswap.  */
struct gr740_fixture
{
  int saved_verbose;
  int saved_cputype;
  int saved_archtype;
  const struct cpu_arch *saved_arch;
  const struct memsys *saved_ms;

  gr740_fixture ()
      : saved_verbose (sis_verbose), saved_cputype (cputype),
	saved_archtype (archtype), saved_arch (arch), saved_ms (ms)
  {
    ensure_fake_ahb_registered ();

    cputype = CPU_LEON4;
    archtype = CPU_SPARC;
    arch = &sparc32;
    ms = &gr740;
    ebase.freq = 14;
    ebase.simtime = 0;
    ebase.simstart = 0;
    reset_all ();
    init_bpt (sregs);
    sis_verbose = 0;
  }

  ~gr740_fixture ()
  {
    sis_verbose = saved_verbose;
    cputype = saved_cputype;
    archtype = saved_archtype;
    arch = saved_arch;
    ms = saved_ms;
  }
};

} /* namespace */

TEST_CASE_FIXTURE (gr740_fixture, "GR740 RAM is read and written with no "
				  "waitstate")
{
  /* Table 7: the processor sees SDRAM through the L2 cache area starting
     at 0x00000000; doc/gr740.md maps 64 MB of it.  */
  uint32 data = 0x74010203;
  int32 ws = -1;

  CHECK (gr740.memory_write (GR740_RAM_START + 0x4000, &data, SZ_WORD, &ws) ==
	 0);
  CHECK (ws == 0);
  CHECK (memcmp (&ramb[0x4000], &data, 4) == 0);

  data = 0;
  ws = -1;
  CHECK (gr740.memory_read (GR740_RAM_START + 0x4000, &data, &ws) == 0);
  CHECK (data == 0x74010203u);
  CHECK (ws == 0);

  /* The last word of the emulated RAM still decodes as RAM.  */
  data = 0x74040506;
  CHECK (gr740.memory_write (GR740_RAM_END - 4, &data, SZ_WORD, &ws) == 0);
  data = 0;
  CHECK (gr740.memory_read (GR740_RAM_END - 4, &data, &ws) == 0);
  CHECK (data == 0x74040506u);
}

TEST_CASE_FIXTURE (gr740_fixture,
		   "GR740 a ROM write is masked into the 16 MB ROM array")
{
  /* Table 7 puts the PROM area at 0xC0000000; doc/gr740.md emulates
     0xC0000000-0xC1000000, and the array behind it is exactly that 16 MB
     wide and indexed from the start of the window, the way the read path
     and get_mem_ptr index it.  A store that keeps the bus address indexes
     3 GB past the array instead, so this case pins that a write and the
     read of the same address land on the same byte, at the bottom, in the
     middle and at the top of the window.  */
  uint32 data;
  int32 ws;

  data = 0xc7000001;
  CHECK (gr740.memory_write (GR740_ROM_START, &data, SZ_WORD, &ws) == 0);
  CHECK (ws == 0);
  CHECK (memcmp (&romb[0], &data, 4) == 0);

  data = 0xc7000002;
  CHECK (gr740.memory_write (GR740_ROM_START + 0x800, &data, SZ_WORD, &ws) ==
	 0);
  CHECK (memcmp (&romb[0x800], &data, 4) == 0);

  data = 0xc7000003;
  CHECK (gr740.memory_write (GR740_ROM_END - 4, &data, SZ_WORD, &ws) == 0);
  CHECK (memcmp (&romb[GR740_ROM_SIZE - 4], &data, 4) == 0);

  data = 0;
  CHECK (gr740.memory_read (GR740_ROM_START, &data, &ws) == 0);
  CHECK (data == 0xc7000001u);
  CHECK (gr740.memory_read (GR740_ROM_START + 0x800, &data, &ws) == 0);
  CHECK (data == 0xc7000002u);
  CHECK (gr740.memory_read (GR740_ROM_END - 4, &data, &ws) == 0);
  CHECK (data == 0xc7000003u);

  /* get_mem_ptr masks the same way, so the byte pointer a peripheral gets
     for a ROM address is the byte the store wrote.  */
  CHECK (memcmp (gr740.get_mem_ptr (GR740_ROM_START + 0x800, 4), &romb[0x800],
		 4) == 0);
}

TEST_CASE_FIXTURE (gr740_fixture, "GR740 (current behaviour) a ROM read "
				  "costs two waitstates")
{
  /* The PROM is the slow area of Table 7's map.  SIS charges a fixed two
     waitstates for a ROM read and none for a ROM write.  */
  uint32 data = 0xc7005a5a;
  int32 ws = -1;

  CHECK (gr740.memory_write (GR740_ROM_START + 0x900, &data, SZ_WORD, &ws) ==
	 0);
  CHECK (ws == 0);

  ws = -1;
  CHECK (gr740.memory_read (GR740_ROM_START + 0x900, &data, &ws) == 0);
  CHECK (ws == 2);

  /* memory_iread is the same entry point as memory_read on this board.  */
  ws = -1;
  CHECK (gr740.memory_iread (GR740_ROM_START + 0x900, &data, &ws) == 0);
  CHECK (data == 0xc7005a5au);
  CHECK (ws == 2);
}

TEST_CASE_FIXTURE (gr740_fixture,
		   "GR740 an address outside RAM and ROM goes to the bus, "
		   "and an unclaimed one is a memory exception")
{
  uint32 data = 0;
  int32 ws = -1;
  int reads_before = fake_ahb_read_calls;
  int writes_before = fake_ahb_write_calls;

  /* A mounted slave answers, and a bus access costs four waitstates.  */
  CHECK (gr740.memory_read (FAKE_AHB_BASE + 4, &data, &ws) == 0);
  CHECK (data == (0x74000000u | 4u));
  CHECK (ws == 4);
  CHECK (fake_ahb_read_calls == reads_before + 1);

  data = 0x74aa0011;
  CHECK (gr740.memory_write (FAKE_AHB_BASE + 8, &data, SZ_WORD, &ws) == 0);
  CHECK (ws == 4);
  CHECK (fake_ahb_write_calls == writes_before + 1);

  /* "Access to non-existing memory will result in a memory exception
     trap" (doc/gr740.md, Memory interface).  Quiet when not verbose, and
     ws stays at the bus cost.  */
  CHECK (gr740.memory_read (GR740_UNMAPPED, &data, &ws) == 1);
  CHECK (ws == 4);
  CHECK (gr740.memory_write (GR740_UNMAPPED, &data, SZ_WORD, &ws) == 1);
  CHECK (ws == 4);

  /* Narrated when verbose, and the waitstate count drops to the memory
     exception cost of one.  */
  sis_verbose = 1;
  {
    stdout_capture cap;
    CHECK (gr740.memory_read (GR740_UNMAPPED, &data, &ws) == 1);
    CHECK (ws == 1);
    CHECK (cap.str ().find ("Memory exception at d0000000") !=
	   std::string::npos);
  }
  {
    stdout_capture cap;
    CHECK (gr740.memory_write (GR740_UNMAPPED, &data, SZ_WORD, &ws) == 1);
    CHECK (ws == 1);
    CHECK (cap.str ().find ("Memory exception at d0000000") !=
	   std::string::npos);
  }

  /* Still verbose, but a bus access that succeeds stays quiet.  */
  {
    stdout_capture cap;
    CHECK (gr740.memory_read (FAKE_AHB_BASE, &data, &ws) == 0);
    CHECK (gr740.memory_write (FAKE_AHB_BASE, &data, SZ_WORD, &ws) == 0);
    CHECK (cap.str ().empty ());
  }
}

TEST_CASE_FIXTURE (gr740_fixture, "GR740 get_mem_ptr answers for RAM and "
				  "ROM and refuses everything else")
{
  CHECK ((void *) gr740.get_mem_ptr (GR740_RAM_START + 0x4000, 4) ==
	 (void *) &ramb[0x4000]);
  CHECK ((void *) gr740.get_mem_ptr (GR740_ROM_START + 0x800, 4) ==
	 (void *) &romb[0x800]);

  /* An offset in the upper half of the window maps there too, so the whole
     16 MB is reachable and not a folded copy of the lower half.  */
  CHECK ((void *) gr740.get_mem_ptr (GR740_ROM_START + 0xc00800, 4) ==
	 (void *) &romb[0xc00800]);
  CHECK ((void *) gr740.get_mem_ptr (GR740_RAM_START + 0x3c00800, 4) ==
	 (void *) &ramb[0x3c00800]);

  /* Below the ROM window, above it, and a block that would run off the end
     of RAM all give nothing back.  */
  char *below_rom = gr740.get_mem_ptr (FAKE_AHB_BASE, 4);
  char *above_rom = gr740.get_mem_ptr (GR740_UNMAPPED, 4);
  char *off_ram_end = gr740.get_mem_ptr (GR740_RAM_END - 2, 4);
  char *off_rom_end = gr740.get_mem_ptr (GR740_ROM_END - 2, 4);

  CHECK (below_rom == NULL);
  CHECK (above_rom == NULL);
  CHECK (off_ram_end == NULL);
  CHECK (off_rom_end == NULL);
}

TEST_CASE_FIXTURE (gr740_fixture,
		   "GR740 sis_memory_write writes memory directly and falls "
		   "back to the bus for a word")
{
  char data[4] = { 1, 2, 3, 4 };

  CHECK (gr740.sis_memory_write (GR740_RAM_START + 0x4400, data, 4) == 4);
  CHECK (memcmp (&ramb[0x4400], data, 4) == 0);

  CHECK (gr740.sis_memory_write (GR740_ROM_START + 0xa00, data, 4) == 4);
  CHECK (memcmp (&romb[0xa00], data, 4) == 0);

  /* Outside memory a four byte write goes through memory_write onto the
     bus, and reports nothing written.  */
  int writes_before = fake_ahb_write_calls;
  uint32 word = 0x7400beef;
  CHECK (gr740.sis_memory_write (FAKE_AHB_BASE, (char *) &word, 4) == 0);
  CHECK (fake_ahb_write_calls == writes_before + 1);

  /* Outside memory and not a word: nothing happens at all.  */
  char one = 0x5a;
  CHECK (gr740.sis_memory_write (GR740_UNMAPPED, &one, 1) == 0);
  CHECK (fake_ahb_write_calls == writes_before + 1);
}

TEST_CASE_FIXTURE (gr740_fixture,
		   "GR740 sis_memory_read takes the word shortcut and "
		   "otherwise copies from memory")
{
  uint32 word = 0x74334455;
  int32 ws;
  char buf[4];

  gr740.memory_write (GR740_RAM_START + 0x4800, &word, SZ_WORD, &ws);
  CHECK (gr740.sis_memory_read (GR740_RAM_START + 0x4800, buf, 4) == 4);
  CHECK (memcmp (buf, &word, 4) == 0);

  /* The shortcut is what makes a four byte read of an address with no
     memory behind it work at all: it goes onto the bus, where the mounted
     slave answers.  */
  uint32 bus = 0;
  CHECK (gr740.sis_memory_read (FAKE_AHB_BASE + 0x20, (char *) &bus, 4) == 4);
  CHECK (bus == (0x74000000u | 0x20u));

  /* A length of one comes out of the byte pointer, for RAM and for ROM.  */
  char one = 0;
  romb[0xb00] = 0x37;
  CHECK (gr740.sis_memory_read (GR740_ROM_START + 0xb00, &one, 1) == 1);
  CHECK (one == 0x37);

  /* An address with no memory behind it reads nothing.  */
  CHECK (gr740.sis_memory_read (GR740_UNMAPPED, &one, 1) == 0);
}

TEST_CASE_FIXTURE (gr740_fixture,
		   "GR740 boot_init sets up every core's register file")
{
  /* GR740-UM-DS-2-10 section 6, LEON4 reset table: the processor starts in
     supervisor mode with traps enabled.  The PSR value below is impl 0xF,
     ver 3, EF set, S, PS and ET set and CWP 0; the window invalid mask
     starts at window 1.  Each core gets its own stack near the top of RAM,
     0x20000 apart, and r[2] carries the same stack pointer for a RISC-V
     core sharing this board file.  */
  gr740.boot_init ();

  for (int i = 0; i < NCPU; i++)
    {
      CHECK (sregs[i].wim == 2);
      CHECK (sregs[i].psr == 0xF30010e0u);
      CHECK (sregs[i].r[30] == GR740_RAM_END - (uint32) (i * 0x20000));
      CHECK (sregs[i].r[14] == sregs[i].r[30] - 96 * 4);
      CHECK (sregs[i].cache_ctrl == 0x81000fu);
      CHECK (sregs[i].r[2] == sregs[i].r[30]);
    }
}

TEST_CASE_FIXTURE (gr740_fixture,
		   "GR740 reset, error_mode, sim_halt and exit_sim")
{
  /* reset forwards to grlib_reset, which resets every registered core, the
     private fake among them; error_mode has no body on this board, since
     the GR740 has no error mode register file of its own to update;
     sim_halt only flushes the UART when FAST_UART is configured, which
     this tree never defines; exit_sim closes the shared APBUART port, and
     uart_port_close clears its open flag before closing, so this is safe
     whether or not another case has opened it.  */
  int resets_before = fake_ahb_reset_calls;

  gr740.reset ();
  CHECK (fake_ahb_reset_calls == resets_before + 1);
  gr740.error_mode (GR740_ROM_START);
  gr740.sim_halt ();
  gr740.exit_sim ();
  CHECK (true);
}

TEST_CASE_FIXTURE (gr740_fixture, "GR740 set_irq is grlib_set_irq itself")
{
  /* The board contributes no interrupt code of its own: the memsys entry
     points straight at grlib.cc's, which tests/irqmp.cc covers.  This only
     pins the table entry.  */
  CHECK ((void *) gr740.set_irq == (void *) grlib_set_irq);
}
