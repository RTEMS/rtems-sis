/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Tests for the generic RV32 board file, rv32.cc.

   The board is a RISC-V RV32 system with a CLINT, a PLIC and an NS16550
   UART, the address map QEMU's "virt" machine uses and the one rv32.dts
   describes to the target.  Its memsys entry points are driven here
   directly, with no board registration at all: rv32.init_sim registers six
   AHB slaves on grlib.cc's process wide bus, among them an SDRAM
   controller at 0x80000000, the window tests/grlibcores.cc and leon3.cc
   already own, and then calls grlib_init, which schedules peripheral
   events into the shared event queue.  Calling it from this binary hung
   the suite under some random orderings before, so it is not called and
   the two arcs of init_sim's AHB master loop stay uncovered.  Everything
   else rv32.cc does needs nothing init_sim provides.

   The one thing the memory dispatch needs from the bus is a slave to
   answer outside RAM and ROM.  This file registers a single private fake
   AHB slave for that, at 0x5A000000, an address no other test file and no
   emulated board uses, following the seam tests/leon.cc opened for
   leon3.cc.

   Specification citations are to doc/riscv.md ("RISC-V emulation", the
   CLINT system address map), to rv32.dts, the device tree SIS itself hands
   the target, and to the RISC-V supervisor boot convention the board's
   register setup follows.  Cases marked "current behaviour" pin what the
   code does rather than a documented interface.  */

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

/* doc/riscv.md: ROM 0x20000000-0x21000000 (16 MB) and RAM
   0x80000000-0x84000000 (64 MB).  rv32.dts agrees: "flash@20000000" and
   "memory@80000000".  */
const uint32 RV32_RAM_START = 0x80000000;
const uint32 RV32_RAM_END = 0x84000000;
const uint32 RV32_ROM_START = 0x20000000;
const uint32 RV32_ROM_SIZE = 0x01000000;
const uint32 RV32_ROM_END = RV32_ROM_START + RV32_ROM_SIZE;

/* The DTB sits in the last 64 KB of ROM, at 0x20FF0000 (doc/riscv.md),
   and boot_init hands its address to each core.  */
const uint32 RV32_DTB = RV32_ROM_END - 0x10000;

/* Below every area of the map in doc/riscv.md, whose lowest entry is the
   CLINT at 0x02000000.  Nothing decodes here, and it sits below ROM_START,
   which is what makes the lower bound of the ROM decode observable.  */
const uint32 RV32_UNMAPPED_LOW = 0x01000000;

/* Above the ROM window, below the RAM window, and claimed by no slave.  */
const uint32 RV32_UNMAPPED_HIGH = 0x5B000000;

/* Above the RAM window, the top area of the map, where doc/riscv.md lists
   nothing.  This is what makes the upper bound of the RAM decode
   observable.  */
const uint32 RV32_ABOVE_RAM = 0xA0000000;

/* Word access, the size code grlib_store_bytes takes for a full word.  */
const int32 SZ_WORD = 2;

/* A private fake AHB slave.  0x5A000000 is claimed by no other test file
   (tests/leon.cc uses 0x50000000, tests/grlibbus.cc 0x70000000-0x72000000,
   tests/grlibcores.cc the APB window at 0x80000200/0x80000300) and by no
   emulated board.  Registered once for the whole binary, since grlib.cc's
   registration arrays only ever grow.  */
const uint32 FAKE_AHB_BASE = 0x5A000000;

int fake_ahb_read_calls;
int fake_ahb_write_calls;
int fake_ahb_reset_calls;

void
rv32_fake_reset (void)
{
  fake_ahb_reset_calls++;
}

int
rv32_fake_read (uint32 addr, uint32 *data)
{
  fake_ahb_read_calls++;
  *data = 0x32000000u | (addr & 0xffff);
  return 1;
}

int
rv32_fake_write (uint32 addr, uint32 *data, uint32 size)
{
  (void) addr;
  (void) data;
  (void) size;
  fake_ahb_write_calls++;
  return 1;
}

void
rv32_fake_add (int irq, uint32 addr, uint32 mask)
{
  (void) irq;
  (void) addr;
  (void) mask;
}

/* grlib_ahbs_add records start/end/mask only for a core with a non-NULL
   add, so the fake needs one to be reachable at all.  init stays NULL,
   which grlib_init skips; reset only bumps a counter, which is what makes
   the board's reset observable.  */
const struct grlib_ipcore fake_ahb_slave = { NULL, rv32_fake_reset,
					     rv32_fake_read, rv32_fake_write,
					     rv32_fake_add };

void
ensure_fake_ahb_registered ()
{
  static bool done = false;

  if (done)
    return;
  done = true;

  grlib_ahbs_add (&fake_ahb_slave, 0, FAKE_AHB_BASE, 0xfff);
}

/* Per-case setup: points ms and arch at the RV32 configuration, clears the
   event queue and the register file through reset_all, and restores every
   global it touched.  arch is set explicitly because grlib_store_bytes
   swaps a sub-word address by arch->bswap.  ncpu drives boot_init's loop
   and is restored too.  */
struct rv32_fixture
{
  int saved_verbose;
  int saved_cputype;
  int saved_archtype;
  int saved_ncpu;
  const struct cpu_arch *saved_arch;
  const struct memsys *saved_ms;

  rv32_fixture ()
      : saved_verbose (sis_verbose), saved_cputype (cputype),
	saved_archtype (archtype), saved_ncpu (ncpu), saved_arch (arch),
	saved_ms (ms)
  {
    ensure_fake_ahb_registered ();

    cputype = CPU_RISCV;
    archtype = CPU_RISCV;
    arch = &riscv;
    ms = &rv32;
    ebase.freq = 14;
    ebase.simtime = 0;
    ebase.simstart = 0;
    reset_all ();
    init_bpt (sregs);
    sis_verbose = 0;
  }

  ~rv32_fixture ()
  {
    sis_verbose = saved_verbose;
    cputype = saved_cputype;
    archtype = saved_archtype;
    ncpu = saved_ncpu;
    arch = saved_arch;
    ms = saved_ms;
  }
};

} /* namespace */

TEST_CASE_FIXTURE (rv32_fixture, "RV32 RAM is read and written with no "
				 "waitstate")
{
  /* doc/riscv.md: RAM 0x80000000-0x84000000, the same window rv32.dts
     reports as "memory@80000000".  */
  uint32 data = 0x32010203;
  int32 ws = -1;

  CHECK (rv32.memory_write (RV32_RAM_START + 0x5000, &data, SZ_WORD, &ws) ==
	 0);
  CHECK (ws == 0);
  CHECK (memcmp (&ramb[0x5000], &data, 4) == 0);

  data = 0;
  ws = -1;
  CHECK (rv32.memory_read (RV32_RAM_START + 0x5000, &data, &ws) == 0);
  CHECK (data == 0x32010203u);
  CHECK (ws == 0);

  /* The last word of the emulated RAM still decodes as RAM.  */
  data = 0x32040506;
  CHECK (rv32.memory_write (RV32_RAM_END - 4, &data, SZ_WORD, &ws) == 0);
  data = 0;
  CHECK (rv32.memory_read (RV32_RAM_END - 4, &data, &ws) == 0);
  CHECK (data == 0x32040506u);
}

TEST_CASE_FIXTURE (rv32_fixture,
		   "RV32 a ROM write is masked into the 16 MB ROM array")
{
  /* doc/riscv.md puts the ROM at 0x20000000-0x21000000, and rv32.dts
     describes the same window as "flash@20000000".  The array behind it is
     exactly that 16 MB wide and indexed from the start of the window, the
     way the read path and get_mem_ptr index it.  A store that keeps the
     bus address indexes 512 MB past the array instead, so this case pins
     that a write and the read of the same address land on the same byte,
     at the bottom, in the middle and at the top of the window.  */
  uint32 data;
  int32 ws;

  data = 0x21000001;
  CHECK (rv32.memory_write (RV32_ROM_START, &data, SZ_WORD, &ws) == 0);
  CHECK (ws == 0);
  CHECK (memcmp (&romb[0], &data, 4) == 0);

  data = 0x21000002;
  CHECK (rv32.memory_write (RV32_ROM_START + 0x1800, &data, SZ_WORD, &ws) ==
	 0);
  CHECK (memcmp (&romb[0x1800], &data, 4) == 0);

  data = 0x21000003;
  CHECK (rv32.memory_write (RV32_ROM_END - 4, &data, SZ_WORD, &ws) == 0);
  CHECK (memcmp (&romb[RV32_ROM_SIZE - 4], &data, 4) == 0);

  data = 0;
  CHECK (rv32.memory_read (RV32_ROM_START, &data, &ws) == 0);
  CHECK (data == 0x21000001u);
  CHECK (rv32.memory_read (RV32_ROM_START + 0x1800, &data, &ws) == 0);
  CHECK (data == 0x21000002u);
  CHECK (rv32.memory_read (RV32_ROM_END - 4, &data, &ws) == 0);
  CHECK (data == 0x21000003u);

  /* get_mem_ptr masks the same way, so the byte pointer a peripheral gets
     for a ROM address is the byte the store wrote.  */
  CHECK (memcmp (rv32.get_mem_ptr (RV32_ROM_START + 0x1800, 4), &romb[0x1800],
		 4) == 0);
}

TEST_CASE_FIXTURE (rv32_fixture, "RV32 (current behaviour) RAM and ROM are "
				 "both free of waitstates")
{
  /* Unlike the SPARC boards, this one charges nothing for a ROM access:
     the emulated system has no PROM controller, only the flash window of
     rv32.dts.  */
  uint32 data = 0x21005a5a;
  int32 ws = -1;

  CHECK (rv32.memory_write (RV32_ROM_START + 0x1900, &data, SZ_WORD, &ws) ==
	 0);
  CHECK (ws == 0);

  ws = -1;
  CHECK (rv32.memory_read (RV32_ROM_START + 0x1900, &data, &ws) == 0);
  CHECK (ws == 0);

  /* memory_iread is the same entry point as memory_read on this board.  */
  ws = -1;
  CHECK (rv32.memory_iread (RV32_ROM_START + 0x1900, &data, &ws) == 0);
  CHECK (data == 0x21005a5au);
  CHECK (ws == 0);
}

TEST_CASE_FIXTURE (rv32_fixture,
		   "RV32 an address outside RAM and ROM goes to the bus, "
		   "and an unclaimed one is a memory exception")
{
  uint32 data = 0;
  int32 ws = -1;
  int reads_before = fake_ahb_read_calls;
  int writes_before = fake_ahb_write_calls;

  /* A mounted slave answers, and a bus access costs four waitstates.  */
  CHECK (rv32.memory_read (FAKE_AHB_BASE + 4, &data, &ws) == 0);
  CHECK (data == (0x32000000u | 4u));
  CHECK (ws == 4);
  CHECK (fake_ahb_read_calls == reads_before + 1);

  data = 0x32aa0011;
  CHECK (rv32.memory_write (FAKE_AHB_BASE + 8, &data, SZ_WORD, &ws) == 0);
  CHECK (ws == 4);
  CHECK (fake_ahb_write_calls == writes_before + 1);

  /* An address below every mapped area and one above the ROM window are
     both memory exceptions.  Quiet when not verbose, and ws stays at the
     bus cost.  */
  CHECK (rv32.memory_read (RV32_UNMAPPED_LOW, &data, &ws) == 1);
  CHECK (ws == 4);
  CHECK (rv32.memory_write (RV32_UNMAPPED_LOW, &data, SZ_WORD, &ws) == 1);
  CHECK (ws == 4);
  CHECK (rv32.memory_read (RV32_UNMAPPED_HIGH, &data, &ws) == 1);
  CHECK (ws == 4);
  CHECK (rv32.memory_write (RV32_UNMAPPED_HIGH, &data, SZ_WORD, &ws) == 1);
  CHECK (ws == 4);

  /* So is an address above the top of RAM.  */
  CHECK (rv32.memory_read (RV32_ABOVE_RAM, &data, &ws) == 1);
  CHECK (ws == 4);
  CHECK (rv32.memory_write (RV32_ABOVE_RAM, &data, SZ_WORD, &ws) == 1);
  CHECK (ws == 4);

  /* Narrated when verbose, and the waitstate count drops to the memory
     exception cost of one.  */
  sis_verbose = 1;
  {
    stdout_capture cap;
    CHECK (rv32.memory_read (RV32_UNMAPPED_LOW, &data, &ws) == 1);
    CHECK (ws == 1);
    CHECK (cap.str ().find ("Memory exception at 1000000") !=
	   std::string::npos);
  }
  {
    stdout_capture cap;
    CHECK (rv32.memory_write (RV32_UNMAPPED_LOW, &data, SZ_WORD, &ws) == 1);
    CHECK (ws == 1);
    CHECK (cap.str ().find ("Memory exception at 1000000") !=
	   std::string::npos);
  }

  /* Still verbose, but a bus access that succeeds stays quiet.  */
  {
    stdout_capture cap;
    CHECK (rv32.memory_read (FAKE_AHB_BASE, &data, &ws) == 0);
    CHECK (rv32.memory_write (FAKE_AHB_BASE, &data, SZ_WORD, &ws) == 0);
    CHECK (cap.str ().empty ());
  }
}

TEST_CASE_FIXTURE (rv32_fixture,
		   "RV32 a bus access is traced at the higher verbose levels")
{
  uint32 data = 0x32778899;
  int32 ws;

  /* A bus read is traced from level 2 up, a bus write from level 3 up.  A
     RAM or ROM access bypasses the bus decode and is never traced.  */
  sis_verbose = 2;
  {
    stdout_capture cap;
    CHECK (rv32.memory_read (FAKE_AHB_BASE + 0x10, &data, &ws) == 0);
    CHECK (rv32.memory_write (FAKE_AHB_BASE + 0x10, &data, SZ_WORD, &ws) == 0);
    std::string out = cap.str ();
    CHECK (out.find ("BUS read  a: 5a000010") != std::string::npos);
    CHECK (out.find ("AHB write") == std::string::npos);
  }

  sis_verbose = 3;
  {
    stdout_capture cap;
    CHECK (rv32.memory_write (FAKE_AHB_BASE + 0x14, &data, SZ_WORD, &ws) == 0);
    CHECK (cap.str ().find ("AHB write a: 5a000014") != std::string::npos);
  }

  sis_verbose = 3;
  {
    stdout_capture cap;
    CHECK (rv32.memory_read (RV32_RAM_START + 0x5400, &data, &ws) == 0);
    CHECK (rv32.memory_write (RV32_RAM_START + 0x5400, &data, SZ_WORD, &ws) ==
	   0);
    CHECK (cap.str ().empty ());
  }
}

TEST_CASE_FIXTURE (rv32_fixture, "RV32 get_mem_ptr answers for RAM and ROM "
				 "and refuses everything else")
{
  CHECK ((void *) rv32.get_mem_ptr (RV32_RAM_START + 0x5000, 4) ==
	 (void *) &ramb[0x5000]);
  CHECK ((void *) rv32.get_mem_ptr (RV32_ROM_START + 0x1800, 4) ==
	 (void *) &romb[0x1800]);

  /* An offset in the upper half of the window maps there too, so the whole
     16 MB is reachable and not a folded copy of the lower half.  */
  CHECK ((void *) rv32.get_mem_ptr (RV32_ROM_START + 0xc01800, 4) ==
	 (void *) &romb[0xc01800]);
  CHECK ((void *) rv32.get_mem_ptr (RV32_RAM_START + 0x3c01800, 4) ==
	 (void *) &ramb[0x3c01800]);

  /* Below the ROM window, above it, and a block that would run off the end
     of RAM or ROM all give nothing back.  */
  char *below_rom = rv32.get_mem_ptr (RV32_UNMAPPED_LOW, 4);
  char *above_rom = rv32.get_mem_ptr (RV32_UNMAPPED_HIGH, 4);
  char *off_ram_end = rv32.get_mem_ptr (RV32_RAM_END - 2, 4);
  char *off_rom_end = rv32.get_mem_ptr (RV32_ROM_END - 2, 4);

  CHECK (below_rom == NULL);
  CHECK (above_rom == NULL);
  CHECK (off_ram_end == NULL);
  CHECK (off_rom_end == NULL);
}

TEST_CASE_FIXTURE (rv32_fixture,
		   "RV32 sis_memory_write writes memory directly and falls "
		   "back to the bus for a word")
{
  char data[4] = { 1, 2, 3, 4 };

  CHECK (rv32.sis_memory_write (RV32_RAM_START + 0x5400, data, 4) == 4);
  CHECK (memcmp (&ramb[0x5400], data, 4) == 0);

  CHECK (rv32.sis_memory_write (RV32_ROM_START + 0x1a00, data, 4) == 4);
  CHECK (memcmp (&romb[0x1a00], data, 4) == 0);

  /* Outside memory a four byte write goes through memory_write onto the
     bus, and reports nothing written.  */
  int writes_before = fake_ahb_write_calls;
  uint32 word = 0x3200beef;
  CHECK (rv32.sis_memory_write (FAKE_AHB_BASE, (char *) &word, 4) == 0);
  CHECK (fake_ahb_write_calls == writes_before + 1);

  /* Outside memory and not a word: nothing happens at all.  */
  char one = 0x5a;
  CHECK (rv32.sis_memory_write (RV32_UNMAPPED_HIGH, &one, 1) == 0);
  CHECK (fake_ahb_write_calls == writes_before + 1);
}

TEST_CASE_FIXTURE (rv32_fixture,
		   "RV32 sis_memory_read takes the word shortcut and "
		   "otherwise copies from memory")
{
  uint32 word = 0x32334455;
  int32 ws;
  char buf[4];

  rv32.memory_write (RV32_RAM_START + 0x5800, &word, SZ_WORD, &ws);
  CHECK (rv32.sis_memory_read (RV32_RAM_START + 0x5800, buf, 4) == 4);
  CHECK (memcmp (buf, &word, 4) == 0);

  /* The shortcut is what makes a four byte read of an address with no
     memory behind it work at all: it goes onto the bus, where the mounted
     slave answers.  */
  uint32 bus = 0;
  CHECK (rv32.sis_memory_read (FAKE_AHB_BASE + 0x20, (char *) &bus, 4) == 4);
  CHECK (bus == (0x32000000u | 0x20u));

  /* A length of one comes out of the byte pointer.  */
  char one = 0;
  romb[0x1b00] = 0x37;
  CHECK (rv32.sis_memory_read (RV32_ROM_START + 0x1b00, &one, 1) == 1);
  CHECK (one == 0x37);

  /* An address with no memory behind it reads nothing.  */
  CHECK (rv32.sis_memory_read (RV32_UNMAPPED_HIGH, &one, 1) == 0);
}

TEST_CASE_FIXTURE (rv32_fixture,
		   "RV32 boot_init sets up every core's register file")
{
  /* The RISC-V boot convention gives the core a stack pointer in x2 and
     the address of the device tree in x11 (a1).  doc/riscv.md: "The DTB
     (device-tree table) is located at the end of ROM (0x20FF0000)".  Each
     core gets its own stack near the top of RAM, 0x20000 apart, and starts
     out of power-down.  The SPARC fields the loop also writes are inert on
     a RISC-V core and pin current behaviour.  */
  ncpu = NCPU;
  rv32.boot_init ();

  for (int i = 0; i < NCPU; i++)
    {
      CHECK (sregs[i].r[30] == RV32_RAM_END - (uint32) (i * 0x20000));
      CHECK (sregs[i].r[2] == sregs[i].r[30]);
      CHECK (sregs[i].r[14] == sregs[i].r[30] - 96 * 4);
      CHECK (sregs[i].r[11] == RV32_DTB);
      CHECK (sregs[i].pwd_mode == 0);
      CHECK (sregs[i].wim == 2);
      CHECK (sregs[i].psr == 0xF30010e0u);
      CHECK (sregs[i].cache_ctrl == 0x81000fu);
    }
}

TEST_CASE_FIXTURE (rv32_fixture,
		   "RV32 boot_init only touches the cores in use")
{
  /* The loop runs over ncpu, the number of cores the run was configured
     for, not over the maximum the simulator can hold.  */
  ncpu = 1;
  for (int i = 0; i < NCPU; i++)
    sregs[i].r[11] = 0;

  rv32.boot_init ();

  CHECK (sregs[0].r[11] == RV32_DTB);
  for (int i = 1; i < NCPU; i++)
    CHECK (sregs[i].r[11] == 0);
}

TEST_CASE_FIXTURE (rv32_fixture,
		   "RV32 reset, error_mode, sim_halt and exit_sim")
{
  /* reset forwards to grlib_reset, which resets every registered core, the
     private fake among them; error_mode has no body on this board;
     sim_halt only flushes the UART when FAST_UART is configured, which
     this tree never defines; exit_sim closes the shared APBUART port, and
     uart_port_close clears its open flag before closing, so this is safe
     whether or not another case has opened it.  */
  int resets_before = fake_ahb_reset_calls;

  rv32.reset ();
  CHECK (fake_ahb_reset_calls == resets_before + 1);
  rv32.error_mode (RV32_ROM_START);
  rv32.sim_halt ();
  rv32.exit_sim ();
  CHECK (true);
}

TEST_CASE_FIXTURE (rv32_fixture, "RV32 set_irq is grlib_set_irq itself")
{
  /* The board contributes no interrupt code of its own: the memsys entry
     points straight at grlib.cc's, which tests/irqmp.cc covers.  This only
     pins the table entry.  */
  CHECK ((void *) rv32.set_irq == (void *) grlib_set_irq);
}
