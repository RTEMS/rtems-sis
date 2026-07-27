/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Tests for the two remaining board files, gr740.cc (the quad-core GR740
   LEON4 SoC) and rv32.cc (the generic RV32/CLINT board).

   Both are thin now: grlib.cc's IP cores are already exercised on their own
   (tests/grlibcore.h, tests/grlibbus.cc, tests/grlibcores.cc,
   tests/grlibmisc.cc, tests/irqmp.cc, tests/apbuart.cc, tests/ns16550.cc,
   tests/riscvcores.cc), so what is left here is each board's own memory
   map (which grlib_*_add calls it makes, at which address), its own
   RAM/ROM fast-path decode in memory_read/memory_write/get_mem_ptr, the
   byte-oriented sis_memory_read/sis_memory_write wrappers, boot_init's
   register setup, and the trivial memsys entry points (error_mode,
   sim_halt, exit_sim).

   Registering a board is a one-way street: grlib_ahbs_add, grlib_ahbm_add
   and grlib_apb_add only ever append to grlib.cc's static tables, with no
   way to unregister a core.  An earlier round of this work called a
   board's init_sim from every test case's fixture and broke unrelated
   cores mounted on the shared AHB/APB bridges once the bridge's 16-core
   table filled up.  This file follows tests/grlibbus.cc's rule instead:
   call gr740.init_sim and rv32.init_sim exactly once each for the whole
   binary, from the single ensure_boards_registered below, verified
   empirically before writing any of the cases here --- calling
   gr740.init_sim a second time in this tree reliably fails
   tests/grlibcores.cc's APBMST cases, because the second call fills
   apbmst's bridge to its 16-core capacity and its own "APBMST init/reset
   skip a mounted core's null callbacks" case can then no longer mount its
   own fake core.  Every fixture below reset_all()s for a clean register
   file between cases, but never re-registers.

   The two boards also share that one process-wide AHB slave table with
   each other, and grlib_read/grlib_write answer the first entry whose
   window covers an address, not the narrowest one.  gr740.cc's sdctrl
   claims the whole [0, 0x04000000) span (its own RAM window, added with
   RAM_MASKPP rather than the board's usual 1 MB granularity), which
   swallows rv32.cc's s5test and clint windows outright if gr740 registers
   first: rv32's own bus cases would then read back gr740's sdctrl/ramb
   contents instead, an interaction between two boards a real simulator run
   never has (it only ever loads one), also found empirically.
   ensure_boards_registered always registers rv32 before gr740 to avoid it,
   which is enough since nothing rv32.cc registers falls inside any window
   gr740.cc registers, so gr740's own cases read the same either way.

   Addresses cited against the GR740 come from the device documentation
   corpus at /opt/eb-docs/SoC/Gaisler/GR740 (GR740-UM-DS-2-10, chapter 2
   "Architecture", table 7 "AMBA memory map, as seen from processors",
   pages 22-23), which is one revision newer than ref/gr740-users-manual.md
   (2-9); no disagreement between the two was found in what this file
   touches.  Addresses cited against the RV32 board's CLINT/PLIC come from
   ref/sifive-fu540-c000-manual.md the same way tests/riscvcores.cc already
   does.  rv32.cc's own choice of where RAM, ROM, and the SiFive "test"
   finisher device sit is not modelled on any real chip; those cases are
   marked "(current behaviour)".  */

#include "doctest.h"
#include "support.h"

#include "config.h"
#include "riscv.h"
#include "grlib.h"

#include <cstring>

using sis_tests::stdout_capture;

/* rv32dtb.h defines rv32_dtb/rv32_dtb_len (not declares them), the same
   way rv32.cc includes it, so a second #include here would double-define
   them at link time.  Declare the two symbols instead. */
extern unsigned char rv32_dtb[];
extern unsigned int rv32_dtb_len;

namespace
{

/* memory_write's size codes (sis.h has no named constants for these; this
   matches tests/erc32.cc's own local SZ_WORD). */
const int32 SZ_WORD = 2;

/* Both boards' AHB slaves land in the one process-wide ahbscores[] table
   grlib.cc keeps (there is no per-board table), and grlib_read returns the
   first entry whose window covers the address, not the best or narrowest
   match.  gr740.cc's sdctrl claims the whole [0, 0x04000000) span (its
   own RAM window, registered with RAM_MASKPP rather than the board's
   usual 1 MB granularity), which swallows rv32.cc's s5test
   (0x00100000-0x00200000) and clint (0x02000000-0x02100000) windows
   whole.  Registering gr740 first left rv32's own CLINT bus case reading
   zeroes back from gr740's sdctrl/ramb instead of clint_read, found
   empirically while writing this file.  Registering rv32 first avoids it:
   rv32's own registrations then sit at the lower ahbscores[] indices and
   win the scan, and nothing rv32.cc registers falls inside any of
   gr740.cc's own windows, so gr740's own cases are unaffected either way.
   Both fixtures call this one function so the order holds regardless of
   which board's test case runs first, including under --order-by=rand.

   gr740.cc's registrations also collide with tests/grlibbus.cc's the same
   way: gr740.cc's own AHB/APB bridge 1 (apbmst2, added at its real
   APB2START of 0xFFA00000) and tests/grlibbus.cc's ensure_apb_bridges_
   registered (added at its own synthetic 0x80100000) both write the same
   process-wide apbbus[1].base/mask (grlib.cc's apbbus_add), and whichever
   ran last wins for every later grlib_apb_add lookup on that bridge,
   regardless of which core's window is being looked up.  If gr740
   registered after tests/grlibbus.cc's ensure_apb_bridges_registered, its
   "grlib bus ... grlib_apb_add rejects a core once its bridge already
   holds 16" case (which relies on 0x80100000 still resolving to bridge 1
   to mount its own 32 attempts) found no bridge at all and the mount
   attempts silently no-op'd instead of ever reporting "Too many cores",
   also found empirically running --order-by=rand.  A lazy, per-fixture
   ensure_boards_registered can land on either side of that file's own
   lazy ensure_apb_bridges_registered depending on which test case a given
   --order-by=rand seed happens to schedule first, so ordering the two
   boards' fixtures against each other is not enough here.  Registering
   both boards from this file-scope object's constructor instead runs it
   during static initialization, which C++ completes for every
   translation unit before doctest's main starts running any test case
   body, including the first one that reaches another file's own lazy
   registration; nothing elsewhere in the tree registers this early, so
   this is always first, deterministically, under every order.

   Both boards mount apbuart on their bridge, and grlib_init (called from
   inside each board's own init_sim) runs apbuart's own init, which opens
   the shared, process-wide porta the same way a board's -uart1 flag
   would.  tests/apbuart.cc's own first SUBCASE depends on porta.open
   still being false at that point, to reach the state before apbuart_init
   has ever run at all; registering this early would otherwise mean this
   constructor always wins that race, breaking tests/apbuart.cc's
   coverage of that branch on every run rather than only under an unlucky
   --order-by=rand seed, also found empirically.  uart_port_close does
   clear uart_port::open (uartport.cc:135), unlike what tests/apbuart.cc's
   own header says about it (see the final report), so closing the port
   again here restores exactly the state tests/apbuart.cc's first SUBCASE
   needs: apbuart.cc has no public way to reset a port short of this. */
void
ensure_boards_registered ()
{
  rv32.init_sim ();
  gr740.init_sim ();
  apbuart_close_port ();
}

const struct boards_registrar
{
  boards_registrar () { ensure_boards_registered (); }
} boards_registrar_instance;

/* ------------------------------------------------------------------- */
/* GR740                                                                */
/* ------------------------------------------------------------------- */

/* gr740.cc's own constants, transcribed since they are private macros of
   that translation unit.  */
const uint32 GR740_RAM_START = 0x00000000;
const uint32 GR740_RAM_END = 0x04000000;
const uint32 GR740_ROM_START = 0xC0000000;
const uint32 GR740_ROM_END = 0xC1000000;
const uint32 GR740_APBSTART = 0xFF900000;

/* Table 7: IRQ(A)MP at 0xFF904000-0xFF907FFF, i.e. APBSTART + 0x4000.  The
   IMASK register offset (0x40) is transcribed from tests/irqmp.cc, which
   takes it from the GRLIB manual's chapter 96.  */
const uint32 GR740_IRQMP = GR740_APBSTART + 0x4000;
const uint32 IRQMP_IMASK = 0x40;

/* Table 7: L2CACHE configuration registers at 0xF0000000-0xF03FFFFF.  The
   status register value (0x00502803) is pinned by tests/grlibcores.cc's
   "L2C the status register reports a fixed configuration" case.  */
const uint32 GR740_L2C = 0xF0000000;
const uint32 L2C_STATUS = 0x04;

/* Table 7 lists 0xE0000000-0xEFFFFFFF as "Unused. This memory range is
   occupied on the Debug AHB bus and is not visible from the processors."
   None of gr740.cc's grlib_ahbs_add windows reach this address (the
   nearest below is L2CACHE's 0xF0000000-0xF00FFFFF, the nearest above is
   GRSPWROUTER's 0xFF880000-0xFF97FFFF), so this is a genuine miss on the
   real board too, not just in sis's coarser 1 MB decode.  */
const uint32 GR740_UNMAPPED = 0xE0000000;

/* Table 7: GRPCI2's PCI memory area, 0x80000000-0xBFFFFFFF, which gr740.cc
   registers nothing in.  Also below ROM_START (0xC0000000): unlike the
   RAM check three lines above it in memory_read/memory_write (whose own
   RAM_START of 0 makes "addr >= RAM_START" true for every address, so its
   only real condition is the upper bound), the PROM check's lower bound
   is a real, fully two-sided comparison, and every other address this
   file otherwise tests happens to sit above ROM_START, so this is the one
   address needed to exercise "below the PROM window" as a distinct
   outcome from "above RAM but not yet the bus". */
const uint32 GR740_BELOW_ROM = 0x90000000;

struct gr740_fixture
{
  int saved_verbose;
  int saved_ncpu;
  const struct memsys *saved_ms;
  const struct cpu_arch *saved_arch;
  float32 saved_freq;

  gr740_fixture ()
      : saved_verbose (sis_verbose), saved_ncpu (ncpu), saved_ms (ms),
	saved_arch (arch), saved_freq (ebase.freq)
  {
    ms = &gr740;
    arch = &sparc32;
    sis_verbose = 0;
    ncpu = 4;
    ebase.freq = 1;
    ebase.simtime = 0;
    ebase.simstart = 0;

    /* Both boards are already registered by boards_registrar_instance's
       static-init-time construction, above; no per-fixture call needed
       or wanted (see ensure_boards_registered's comment). */
    reset_all ();
  }

  ~gr740_fixture ()
  {
    reset_all ();
    ncpu = saved_ncpu;
    sis_verbose = saved_verbose;
    ms = saved_ms;
    arch = saved_arch;
    ebase.freq = saved_freq;
  }

  int
  wr (uint32 addr, uint32 val, int32 sz = SZ_WORD)
  {
    uint32 d = val;
    int32 ws;
    return ms->memory_write (addr, &d, sz, &ws);
  }

  uint32
  rd (uint32 addr, int32 *ws_out = NULL)
  {
    uint32 d = 0;
    int32 ws;
    ms->memory_read (addr, &d, &ws);
    if (ws_out)
      *ws_out = ws;
    return d;
  }

  /* memory_read's own return value (grlib_read's mexc, 0 success/1 fault),
     not the data it read. */
  int
  rd_mexc (uint32 addr, int32 *ws_out = NULL)
  {
    uint32 d = 0;
    int32 ws;
    int r = ms->memory_read (addr, &d, &ws);
    if (ws_out)
      *ws_out = ws;
    return r;
  }
};

}

TEST_CASE_FIXTURE (gr740_fixture,
		   "GR740 the RAM window starts at 0x00000000 (table 7)")
{
  int32 ws = -1;

  CHECK (wr (GR740_RAM_START + 0x100, 0x11223344) == 0);
  CHECK (rd (GR740_RAM_START + 0x100, &ws) == 0x11223344u);
  CHECK (ws == 0);
}

TEST_CASE_FIXTURE (
    gr740_fixture,
    "GR740 the RAM window ends at 0x04000000, not one word short or long")
{
  /* memory_read/memory_write's own RAM_END comparison is strict (addr <
     RAM_END): the last word inside RAM takes the fast path (ws 0, no bus
     access needed), the first word past it does not (ws 4, the bus's own
     waitstate, and nothing is registered there either so it is also a
     fault). */
  int32 ws = -1;

  CHECK (wr (GR740_RAM_END - 4, 0x42) == 0);
  CHECK (rd (GR740_RAM_END - 4, &ws) == 0x42u);
  CHECK (ws == 0);

  CHECK (rd_mexc (GR740_RAM_END, &ws) == 1);
  CHECK (ws == 4);
  CHECK (wr (GR740_RAM_END, 0x1) == 1);
}

/* memory_write's own PROM branch (gr740.cc:177) is not exercised through
   ms->memory_write here: it calls grlib_store_bytes (romb, addr, data, sz)
   with the raw address, not addr & ROM_MASK the way its RAM sibling three
   lines above and its own read counterpart (gr740.cc:144) both do.  With
   ROM_START at 0xC0000000, that indexes romb (a 16 MiB, MAX_ROM_SIZE
   array) three gigabytes out of bounds and reliably segfaults the whole
   test binary; confirmed empirically while writing this file.  The
   equivalent line in rv32.cc (rv32.cc:174, ROM_START 0x20000000) and in
   leon3.cc (leon3.cc:166, ROM_START 0xC0000000) has the identical
   defect while rv32.cc's and leon3.cc's own read side mask correctly, the
   same pattern as gr740.cc.  This is a real defect, not a test artifact;
   see the final report.  Populating and reading PROM below goes through
   get_mem_ptr and sis_memory_write/sis_memory_read instead, which do mask
   correctly, so the read side of the branch (and the rest of this file)
   stays testable. */
TEST_CASE_FIXTURE (gr740_fixture,
		   "GR740 the PROM window starts at 0xC0000000 (table 7)")
{
  int32 ws = -1;

  REQUIRE (ms->sis_memory_write (GR740_ROM_START + 0x100, "abcd", 4) == 4);
  CHECK (rd (GR740_ROM_START + 0x100, &ws) == 0x64636261u); /* "abcd", LE */
  CHECK (ws == 2); /* memory_read's own ROM waitstate literal */
}

TEST_CASE_FIXTURE (
    gr740_fixture,
    "GR740 the PROM window ends at 0xC1000000, not one word short or long")
{
  /* Populated through sis_memory_write/get_mem_ptr, not ms->memory_write,
     for the reason above.  The read side's own two-sided ROM_START/
     ROM_END comparison is what this pins.  ROM_END - 8, not ROM_END - 4:
     get_mem_ptr's own check is addr + size < ROM_END, strictly, so a
     4-byte write landing exactly on ROM_END - 4 falls outside even
     get_mem_ptr's window (as the get_mem_ptr boundary case below this one
     already pins) and sis_memory_write would fall back to
     ms->memory_write instead, straight into the crash this file
     documents; confirmed empirically while writing this case. */
  int32 ws = -1;

  REQUIRE (ms->sis_memory_write (GR740_ROM_END - 8, "abcd", 4) == 4);
  CHECK (rd (GR740_ROM_END - 8, &ws) == 0x64636261u);
  CHECK (ws == 2);

  CHECK (rd_mexc (GR740_ROM_END, &ws) == 1);
  CHECK (ws == 4);
}

TEST_CASE_FIXTURE (
    gr740_fixture,
    "GR740 an address neither RAM nor PROM reaches the AHB/APB bus")
{
  /* IRQ(A)MP sits at APBSTART + 0x4000 (table 7).  A round trip through
     IMASK, reached only via grlib_read/grlib_write, proves the board's
     else branch decodes to the bridge and IRQMP is mounted on it.  */
  CHECK (wr (GR740_IRQMP + IRQMP_IMASK, 0xfffe) == 0);
  CHECK ((rd (GR740_IRQMP + IRQMP_IMASK) & 0xfffe) == 0xfffe);
}

TEST_CASE_FIXTURE (gr740_fixture,
		   "GR740 the L2 cache configuration is at 0xF0000000 "
		   "(table 7)")
{
  CHECK (rd (GR740_L2C + L2C_STATUS) == 0x00502803);
}

TEST_CASE_FIXTURE (
    gr740_fixture,
    "GR740 an address 0xE0000000-0xEFFFFFFF is unmapped (table 7)")
{
  int32 ws = -1;

  CHECK (rd_mexc (GR740_UNMAPPED, &ws) == 1);
  CHECK (ws == 4); /* quiet: the generic bus waitstate, not MEM_EX_WS */

  CHECK (wr (GR740_UNMAPPED, 0x1, SZ_WORD) == 1);
}

TEST_CASE_FIXTURE (
    gr740_fixture,
    "GR740 an address below the PROM window is unmapped, distinctly from "
    "above the PROM window (table 7, GRPCI2's PCI memory area)")
{
  CHECK (rd_mexc (GR740_BELOW_ROM) == 1);
  CHECK (wr (GR740_BELOW_ROM, 0x1, SZ_WORD) == 1);
}

TEST_CASE_FIXTURE (
    gr740_fixture,
    "GR740 (current behaviour) a verbose bus read narrates only on a fault")
{
  sis_verbose = 1;

  int32 ws = -1;
  stdout_capture quiet;
  CHECK (rd (GR740_L2C + L2C_STATUS, &ws) == 0x00502803);
  CHECK (quiet.str ().empty ());
  CHECK (ws == 4);

  stdout_capture cap;
  CHECK (rd_mexc (GR740_UNMAPPED, &ws) == 1);
  std::string text = cap.str ();

  CHECK (text.find ("Memory exception") != std::string::npos);
  CHECK (ws == 1); /* MEM_EX_WS once sis_verbose reports the fault */
}

TEST_CASE_FIXTURE (
    gr740_fixture,
    "GR740 (current behaviour) a verbose bus write narrates only on a fault")
{
  sis_verbose = 1;

  stdout_capture quiet;
  CHECK (wr (GR740_IRQMP + IRQMP_IMASK, 0) == 0);
  CHECK (quiet.str ().empty ());

  stdout_capture cap;
  int32 ws = -1;
  CHECK (wr (GR740_UNMAPPED, 0x1) == 1);
  std::string text = cap.str ();

  CHECK (text.find ("Memory exception") != std::string::npos);
  (void) ws;
}

TEST_CASE_FIXTURE (gr740_fixture,
		   "GR740 get_mem_ptr answers RAM and PROM, not the bus")
{
  char *p;

  p = ms->get_mem_ptr (GR740_RAM_START + 4, 4);
  REQUIRE (p != NULL);
  std::memcpy (p, "RAMX", 4);
  CHECK (rd (GR740_RAM_START + 4) == *reinterpret_cast<uint32 *> (p));

  p = ms->get_mem_ptr (GR740_ROM_START + 4, 4);
  REQUIRE (p != NULL);

  CHECK (ms->get_mem_ptr (GR740_UNMAPPED, 4) == nullptr);
  CHECK (ms->get_mem_ptr (GR740_IRQMP, 4) == nullptr); /* the bus has no ptr */
}

TEST_CASE_FIXTURE (
    gr740_fixture,
    "GR740 get_mem_ptr's RAM window excludes a span crossing RAM_END")
{
  /* get_mem_ptr's check is addr + size < RAM_END, strictly: a span whose
     end lands exactly on RAM_END is refused, one ending 4 bytes earlier
     is not.  */
  CHECK (ms->get_mem_ptr (GR740_RAM_END - 4, 4) == nullptr);
  CHECK (ms->get_mem_ptr (GR740_RAM_END - 8, 4) != nullptr);

  CHECK (ms->get_mem_ptr (GR740_ROM_END - 4, 4) == nullptr);
  CHECK (ms->get_mem_ptr (GR740_ROM_END - 8, 4) != nullptr);
}

TEST_CASE_FIXTURE (gr740_fixture,
		   "GR740 sis_memory_write/sis_memory_read reach RAM and "
		   "PROM through get_mem_ptr")
{
  char buf[8] = { 0 };

  CHECK (ms->sis_memory_write (GR740_RAM_START + 0x40, "ABC", 3) == 3);
  CHECK (ms->sis_memory_read (GR740_RAM_START + 0x40, buf, 3) == 3);
  CHECK (std::memcmp (buf, "ABC", 3) == 0);

  CHECK (ms->sis_memory_write (GR740_ROM_START + 0x40, "abcd", 4) == 4);
  CHECK (ms->sis_memory_read (GR740_ROM_START + 0x40, buf, 4) == 4);
  CHECK (std::memcmp (buf, "abcd", 4) == 0);
}

TEST_CASE_FIXTURE (
    gr740_fixture,
    "GR740 sis_memory_write falls back to memory_write for an unmapped "
    "4-byte write and drops any other length")
{
  uint32 word = 0x99999999;

  /* get_mem_ptr misses (not RAM/PROM), length is 4: falls through to
     memory_write, which reaches the bus and reports success. */
  CHECK (ms->sis_memory_write (GR740_IRQMP + IRQMP_IMASK,
			       reinterpret_cast<char *> (&word), 4) == 0);
  CHECK ((rd (GR740_IRQMP + IRQMP_IMASK) & 0xfffe) != 0);

  /* Same miss, length not 4: neither path applies. */
  CHECK (ms->sis_memory_write (GR740_UNMAPPED, "Z", 1) == 0);
}

TEST_CASE_FIXTURE (
    gr740_fixture,
    "GR740 sis_memory_read takes the 4-byte shortcut through memory_read "
    "and otherwise falls back to get_mem_ptr")
{
  char buf[4] = { 0 };

  /* length 4 always goes through memory_read, even inside RAM: */
  CHECK (wr (GR740_RAM_START + 0x200, 0x01020304) == 0);
  CHECK (ms->sis_memory_read (GR740_RAM_START + 0x200, buf, 4) == 4);

  /* An unmapped address with length != 4 misses get_mem_ptr too. */
  CHECK (ms->sis_memory_read (GR740_UNMAPPED, buf, 1) == 0);
}

TEST_CASE_FIXTURE (gr740_fixture,
		   "GR740 boot_init sets up all four cores' stack and cache "
		   "control")
{
  ms->boot_init ();

  for (int i = 0; i < 4; i++)
    {
      CHECK (sregs[i].wim == 2);
      CHECK (sregs[i].psr == 0xF30010e0u);
      CHECK (sregs[i].cache_ctrl == 0x81000fu);
      CHECK (sregs[i].r[30] == GR740_RAM_END - (uint32) i * 0x20000);
      CHECK (sregs[i].r[14] == sregs[i].r[30] - 96 * 4);
      CHECK (sregs[i].r[2] == sregs[i].r[30]);
    }
}

TEST_CASE_FIXTURE (gr740_fixture, "GR740 error_mode, sim_halt and exit_sim "
				  "run without a board-specific effect")
{
  /* All three are one-line memsys entry points on this board (error_mode
     is an empty stub; sim_halt only flushes under FAST_UART, which this
     build does not define; exit_sim closes the UART port apbuart_init
     already opened via reset_all's grlib_reset).  Nothing to assert on
     but that they run.  */
  ms->error_mode (0);
  ms->sim_halt ();
  ms->exit_sim ();
}

/* gr740.cc:68's "if (irqmp_extirq < 0) irqmp_extirq = 10;" is the one
   branch in this file genuinely out of reach here.  irqmp_extirq is a
   process-wide global grlib.cc initialises to -1 (grlib.cc:506), and
   gr740.cc's guard is the only place that ever writes it back to
   non-negative in a plain build; observing the branch's other arm needs a
   second call to gr740.init_sim with the global pre-set non-negative
   (mirroring what a real "-xirq" CLI run before board init would do), and
   the empirical test above shows a second call corrupts sibling tests
   irrecoverably.  boards_registrar_instance's one call to gr740.init_sim,
   at static-init time before anything else in the process has touched
   irqmp_extirq, always finds it at its process-startup default, so only
   the true arm is reachable within this test binary; the false arm is a
   structural gap of the once-per-process registration rule, not a defect
   in gr740.cc.  See the final report. */
TEST_CASE_FIXTURE (
    gr740_fixture,
    "GR740 (documented gap) irqmp_extirq defaults to 10 once the board "
    "has initialised")
{
  CHECK (irqmp_extirq == 10);
}

/* ------------------------------------------------------------------- */
/* RV32                                                                 */
/* ------------------------------------------------------------------- */

namespace
{

/* rv32.cc's own constants, transcribed since they are private macros of
   that translation unit.  */
const uint32 RV32_ROM_START = 0x20000000;
const uint32 RV32_ROM_END = 0x21000000;
const uint32 RV32_RAM_START = 0x80000000;
const uint32 RV32_RAM_END = 0x84000000;

/* ref/sifive-fu540-c000-manual.md's memory map places CLINT at
   0x0200_0000-0x0200_FFFF and PLIC at 0x0C00_0000-0x0FFF_FFFF; rv32.cc
   follows the same base addresses (tests/riscvcores.cc already cites
   this for the cores themselves).  PLIC_MASK is 0xFFC rather than the
   board's usual 0xFFF, giving the PLIC a 4 MB decode window instead of
   the standard 1 MB (~(0xFFC << 20) + 1 == 0x00400000); everything else
   on this board decodes 1 MB.  */
const uint32 RV32_CLINT = 0x02000000;
const uint32 RV32_PLIC = 0x0C000000;
const uint32 RV32_PLIC_WINDOW = 0x00400000;
const uint32 RV32_NS16550 = 0x10000000;
const uint32 RV32_TEST = 0x00100000;

const uint32 CLINT_MTIME_LO = 0xbff8;
const uint32 PLIC_PRIO = 0x0000;
const uint32 NS16550_LSR = 0x14; /* pc16550d-uart-datasheet.md, LSR */

/* Between the PROM window (ends 0x21000000) and RAM (starts 0x80000000),
   clear of every registered window above: a genuine miss, but rv32.cc's
   own choice of where nothing lives, so this is "(current behaviour)"
   rather than a specified hole.  */
const uint32 RV32_UNMAPPED = 0x30000000;

/* Above RAM_END (0x84000000): every other address this file tests for the
   RAM check's own two-sided window sits either inside RAM or well below
   RAM_START, so nothing else exercises "at or past the top of RAM" as
   its own outcome, distinct from "below RAM_START" (RV32_UNMAPPED,
   0x30000000, already covers that side). */
const uint32 RV32_ABOVE_RAM = 0x90000000;

struct rv32_fixture
{
  int saved_verbose;
  int saved_ncpu;
  const struct memsys *saved_ms;
  const struct cpu_arch *saved_arch;
  float32 saved_freq;

  rv32_fixture ()
      : saved_verbose (sis_verbose), saved_ncpu (ncpu), saved_ms (ms),
	saved_arch (arch), saved_freq (ebase.freq)
  {
    ms = &rv32;
    arch = &riscv;
    sis_verbose = 0;
    ncpu = 1;
    ebase.freq = 1;
    ebase.simtime = 0;
    ebase.simstart = 0;

    /* Both boards are already registered by boards_registrar_instance's
       static-init-time construction; no per-fixture call needed or
       wanted (see ensure_boards_registered's comment). */
    reset_all ();
  }

  ~rv32_fixture ()
  {
    reset_all ();
    ncpu = saved_ncpu;
    sis_verbose = saved_verbose;
    ms = saved_ms;
    arch = saved_arch;
    ebase.freq = saved_freq;
  }

  int
  wr (uint32 addr, uint32 val, int32 sz = SZ_WORD)
  {
    uint32 d = val;
    int32 ws;
    return ms->memory_write (addr, &d, sz, &ws);
  }

  uint32
  rd (uint32 addr, int32 *ws_out = NULL)
  {
    uint32 d = 0;
    int32 ws;
    ms->memory_read (addr, &d, &ws);
    if (ws_out)
      *ws_out = ws;
    return d;
  }

  /* memory_read's own return value (grlib_read's mexc, 0 success/1 fault),
     not the data it read. */
  int
  rd_mexc (uint32 addr, int32 *ws_out = NULL)
  {
    uint32 d = 0;
    int32 ws;
    int r = ms->memory_read (addr, &d, &ws);
    if (ws_out)
      *ws_out = ws;
    return r;
  }
};

}

TEST_CASE_FIXTURE (rv32_fixture,
		   "RV32 (current behaviour) the RAM window starts at "
		   "0x80000000")
{
  int32 ws = -1;

  CHECK (wr (RV32_RAM_START + 0x100, 0x11223344) == 0);
  CHECK (rd (RV32_RAM_START + 0x100, &ws) == 0x11223344u);
  CHECK (ws == 0);
}

TEST_CASE_FIXTURE (
    rv32_fixture,
    "RV32 (current behaviour) the RAM window is [0x80000000, 0x84000000), "
    "not one word short or long on either side")
{
  int32 ws = -1;

  CHECK (rd_mexc (RV32_RAM_START - 4, &ws) == 1);
  CHECK (ws == 4);

  CHECK (wr (RV32_RAM_START, 0x11223344) == 0);
  CHECK (rd (RV32_RAM_START, &ws) == 0x11223344u);
  CHECK (ws == 0);

  CHECK (wr (RV32_RAM_END - 4, 0x42) == 0);
  CHECK (rd (RV32_RAM_END - 4, &ws) == 0x42u);
  CHECK (ws == 0);

  CHECK (rd_mexc (RV32_RAM_END, &ws) == 1);
  CHECK (ws == 4);
}

/* As with gr740.cc:177 above, rv32.cc's own memory_write PROM branch
   (rv32.cc:174) is not called through ms->memory_write here: it passes
   the raw address, not addr & ROM_MASK, to grlib_store_bytes, and with
   ROM_START at 0x20000000 that write lands half a gigabyte past romb (16
   MiB) and reliably segfaults; confirmed empirically while writing this
   file, the same way as gr740.cc's.  See the final report. */
TEST_CASE_FIXTURE (rv32_fixture, "RV32 (current behaviour) the PROM window "
				 "starts at 0x20000000")
{
  int32 ws = -1;

  REQUIRE (ms->sis_memory_write (RV32_ROM_START + 0x100, "abcd", 4) == 4);
  CHECK (rd (RV32_ROM_START + 0x100, &ws) == 0x64636261u); /* "abcd", LE */
  CHECK (ws == 0); /* rv32.cc's ROM branch sets no extra waitstate */
}

TEST_CASE_FIXTURE (
    rv32_fixture,
    "RV32 (current behaviour) the PROM window is [0x20000000, 0x21000000), "
    "not one word short or long on either side")
{
  /* Populated through sis_memory_write/get_mem_ptr, not ms->memory_write,
     for the reason the PROM window test above gives.  ROM_END - 8, not
     ROM_END - 4, for the same reason gr740.cc's equivalent case gives:
     get_mem_ptr's own strict addr + size < ROM_END would otherwise miss
     and sis_memory_write would fall back into the crash. */
  int32 ws = -1;

  CHECK (rd_mexc (RV32_ROM_START - 4, &ws) == 1);
  CHECK (ws == 4);

  REQUIRE (ms->sis_memory_write (RV32_ROM_END - 8, "abcd", 4) == 4);
  CHECK (rd (RV32_ROM_END - 8, &ws) == 0x64636261u);
  CHECK (ws == 0);

  CHECK (rd_mexc (RV32_ROM_END, &ws) == 1);
  CHECK (ws == 4);
}

TEST_CASE_FIXTURE (
    rv32_fixture,
    "RV32 an address neither RAM nor PROM reaches the AHB bus (CLINT, "
    "sifive-fu540-c000-manual.md table 36)")
{
  ebase.simtime = 0x1122334455667788ULL;

  CHECK (rd (RV32_CLINT + CLINT_MTIME_LO) == 0x55667788u);
}

TEST_CASE_FIXTURE (
    rv32_fixture,
    "RV32 the PLIC's decode window is 4 MB wide, not the board's usual "
    "1 MB (grlib.cc's PLIC_MASK of 0xFFC)")
{
  /* Hart 0's threshold/claim-complete pair sits at offset 0x200000 within
     the PLIC (sifive-fu540-c000-manual.md 10.6/10.7, table 37; the same
     offset tests/riscvcores.cc's PLIC_THRES0/PLIC_CLAIM0 use).  A mask
     giving the board's usual 1 MB window would already have missed this
     address; reading it back as ordinary storage (10.6) proves the wider
     4 MB window reaches it. */
  const uint32 plic_thres0 = 0x200000;

  CHECK (wr (RV32_PLIC + plic_thres0, 3) == 0);
  CHECK (rd (RV32_PLIC + plic_thres0) == 3u);

  /* One word past the 4 MB window: no longer the PLIC, and nothing else
     is registered there either. */
  int32 ws = -1;
  CHECK (rd_mexc (RV32_PLIC + RV32_PLIC_WINDOW, &ws) == 1);
}

TEST_CASE_FIXTURE (
    rv32_fixture,
    "RV32 the NS16550 UART is reachable on the bus (pc16550d-uart-"
    "datasheet.md, LSR)")
{
  /* LSR bit 5 (THRE) and bit 6 (TEMT) power up set: the transmitter is
     idle and ready, matching tests/ns16550.cc's own reset expectation. */
  CHECK ((rd (RV32_NS16550 + NS16550_LSR) & 0x60) == 0x60);
}

TEST_CASE_FIXTURE (rv32_fixture,
		   "RV32 (current behaviour) an address 0x20000000-0x80000000 "
		   "away from every registered window is unmapped")
{
  int32 ws = -1;

  CHECK (rd_mexc (RV32_UNMAPPED, &ws) == 1);
  CHECK (ws == 4);

  CHECK (wr (RV32_UNMAPPED, 0x1) == 1);
}

TEST_CASE_FIXTURE (
    rv32_fixture,
    "RV32 (current behaviour) an address at or past the top of RAM is "
    "also unmapped")
{
  CHECK (rd_mexc (RV32_ABOVE_RAM) == 1);
  CHECK (wr (RV32_ABOVE_RAM, 0x1) == 1);
}

TEST_CASE_FIXTURE (
    rv32_fixture,
    "RV32 (current behaviour) a bus read narrates at verbosity 2, "
    "independently of a fault narrating at verbosity 1")
{
  /* plic has no reset (grlib.cc's "const struct grlib_ipcore plic = {
     NULL, NULL, ...}", the same fact tests/riscvcores.cc's plic_fixture
     documents at length), so plic_prio[] is a process-wide static that
     outlives this fixture's reset_all.  Write the value this case reads
     back itself instead of assuming a prior value, the same reason
     riscvcores.cc's plic_fixture drains rather than asserting a reset
     default. */
  wr (RV32_PLIC + PLIC_PRIO, 5);

  sis_verbose = 1;
  stdout_capture quiet1;
  CHECK (rd (RV32_PLIC + PLIC_PRIO) == 5u);
  CHECK (quiet1.str ().find ("BUS read") == std::string::npos);

  sis_verbose = 2;
  stdout_capture cap;
  CHECK (rd (RV32_PLIC + PLIC_PRIO) == 5u);
  std::string text = cap.str ();
  CHECK (text.find ("BUS read") != std::string::npos);

  int32 ws = -1;
  stdout_capture fault_cap;
  CHECK (rd_mexc (RV32_UNMAPPED, &ws) == 1);
  std::string fault_text = fault_cap.str ();
  CHECK (fault_text.find ("Memory exception") != std::string::npos);
  CHECK (ws == 1); /* MEM_EX_WS, sis_verbose (2) && mexc */
}

TEST_CASE_FIXTURE (
    rv32_fixture,
    "RV32 (current behaviour) a bus write narrates at verbosity 3, "
    "independently of a fault narrating at verbosity 1")
{
  sis_verbose = 2;
  stdout_capture quiet;
  CHECK (wr (RV32_PLIC + PLIC_PRIO, 3) == 0);
  CHECK (quiet.str ().find ("AHB write") == std::string::npos);

  sis_verbose = 3;
  stdout_capture cap;
  CHECK (wr (RV32_PLIC + PLIC_PRIO, 4) == 0);
  std::string text = cap.str ();
  CHECK (text.find ("AHB write") != std::string::npos);

  sis_verbose = 1;
  stdout_capture fault_cap;
  CHECK (wr (RV32_UNMAPPED, 1) == 1);
  std::string fault_text = fault_cap.str ();
  CHECK (fault_text.find ("Memory exception") != std::string::npos);
}

TEST_CASE_FIXTURE (rv32_fixture,
		   "RV32 get_mem_ptr answers RAM and PROM, not the bus")
{
  char *p;

  p = ms->get_mem_ptr (RV32_RAM_START + 4, 4);
  REQUIRE (p != NULL);
  std::memcpy (p, "RAMX", 4);
  CHECK (rd (RV32_RAM_START + 4) == *reinterpret_cast<uint32 *> (p));

  p = ms->get_mem_ptr (RV32_ROM_START + 4, 4);
  REQUIRE (p != NULL);

  CHECK (ms->get_mem_ptr (RV32_UNMAPPED, 4) == nullptr);
  CHECK (ms->get_mem_ptr (RV32_CLINT, 4) == nullptr); /* the bus has no ptr */
}

TEST_CASE_FIXTURE (
    rv32_fixture,
    "RV32 get_mem_ptr's RAM window excludes a span crossing RAM_END")
{
  CHECK (ms->get_mem_ptr (RV32_RAM_END - 4, 4) == nullptr);
  CHECK (ms->get_mem_ptr (RV32_RAM_END - 8, 4) != nullptr);

  CHECK (ms->get_mem_ptr (RV32_ROM_END - 4, 4) == nullptr);
  CHECK (ms->get_mem_ptr (RV32_ROM_END - 8, 4) != nullptr);
}

TEST_CASE_FIXTURE (rv32_fixture,
		   "RV32 sis_memory_write/sis_memory_read reach RAM and "
		   "PROM through get_mem_ptr")
{
  char buf[8] = { 0 };

  CHECK (ms->sis_memory_write (RV32_RAM_START + 0x40, "ABC", 3) == 3);
  CHECK (ms->sis_memory_read (RV32_RAM_START + 0x40, buf, 3) == 3);
  CHECK (std::memcmp (buf, "ABC", 3) == 0);

  CHECK (ms->sis_memory_write (RV32_ROM_START + 0x40, "abcd", 4) == 4);
  CHECK (ms->sis_memory_read (RV32_ROM_START + 0x40, buf, 4) == 4);
  CHECK (std::memcmp (buf, "abcd", 4) == 0);
}

TEST_CASE_FIXTURE (
    rv32_fixture,
    "RV32 sis_memory_write falls back to memory_write for an unmapped "
    "4-byte write and drops any other length")
{
  uint32 word = 5;

  CHECK (ms->sis_memory_write (RV32_PLIC + PLIC_PRIO,
			       reinterpret_cast<char *> (&word), 4) == 0);
  CHECK (rd (RV32_PLIC + PLIC_PRIO) == 5u);

  CHECK (ms->sis_memory_write (RV32_UNMAPPED, "Z", 1) == 0);
}

TEST_CASE_FIXTURE (
    rv32_fixture,
    "RV32 sis_memory_read takes the 4-byte shortcut through memory_read "
    "and otherwise falls back to get_mem_ptr")
{
  char buf[4] = { 0 };

  CHECK (wr (RV32_RAM_START + 0x200, 0x01020304) == 0);
  CHECK (ms->sis_memory_read (RV32_RAM_START + 0x200, buf, 4) == 4);

  CHECK (ms->sis_memory_read (RV32_UNMAPPED, buf, 1) == 0);
}

TEST_CASE_FIXTURE (
    rv32_fixture,
    "RV32 boot_init sets up every online core's stack, cache control and "
    "device tree pointer")
{
  int saved = ncpu;
  ncpu = 2;

  ms->boot_init ();

  for (int i = 0; i < ncpu; i++)
    {
      CHECK (sregs[i].wim == 2);
      CHECK (sregs[i].psr == 0xF30010e0u);
      CHECK (sregs[i].cache_ctrl == 0x81000fu);
      CHECK (sregs[i].r[30] == RV32_RAM_END - (uint32) i * 0x20000);
      CHECK (sregs[i].r[14] == sregs[i].r[30] - 96 * 4);
      CHECK (sregs[i].r[2] == sregs[i].r[30]);
      CHECK (sregs[i].r[11] == RV32_ROM_END - 0x10000);
      CHECK (sregs[i].pwd_mode == 0);
    }

  ncpu = saved;
}

TEST_CASE_FIXTURE (
    rv32_fixture,
    "RV32 the generated device tree blob (rv32.dts via rv32dtb.h) is "
    "copied into the last 64 KiB of PROM")
{
  char *p = ms->get_mem_ptr (RV32_ROM_END - 0x10000, rv32_dtb_len);

  REQUIRE (p != NULL);
  CHECK (std::memcmp (p, rv32_dtb, rv32_dtb_len) == 0);
}

TEST_CASE_FIXTURE (
    rv32_fixture,
    "RV32 (current behaviour) the SiFive test finisher device is reachable "
    "at 0x00100000")
{
  int32 ws = -1;
  CHECK (rd (RV32_TEST, &ws) == 0);
  CHECK (ws == 4);
}

TEST_CASE_FIXTURE (rv32_fixture, "RV32 error_mode, sim_halt and exit_sim "
				 "run without a board-specific effect")
{
  ms->error_mode (0);
  ms->sim_halt ();
  ms->exit_sim ();
}
