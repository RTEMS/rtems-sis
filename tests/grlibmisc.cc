/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Tests for the small GRLIB cores and helpers grlib.cc left uncovered after
   tests/irqmp.cc, tests/grlibcores.cc, tests/ns16550.cc and
   tests/riscvcores.cc: the SDRAM controller (sdctrl), the PROM controller
   (srctrl), the clock gating unit (clkgate), the boot-time GPTIMER
   programming helper (grlib_boot_init), the SiFive test module (s5test),
   the DSU's own plug&play registration (dsu_add is otherwise covered by
   tests/grlibcores.cc), and three leftover branches in the PLIC.

   sdctrl and srctrl are not register models at all: chapter "2037-2046:
   SDCTRL" of ref/grlib-ip-core-manual-scoped.md (126.3, tables 2350/2351)
   describes an SDCFG1/SDCFG2 configuration register pair that grlib.cc
   does not implement anywhere.  What sdctrl_read/sdctrl_write and
   srctrl_read/srctrl_write actually do is give the AHB slave address range
   direct access to the emulated RAM/PROM buffer through the shared
   grlib_store_bytes helper, so a board can mount a fast memory window
   without going through a slower per-byte register model.  There is
   nothing in ref/ for that behaviour, so every case here is marked
   "(current behaviour)".

   clkgate (GRCLKGATE) and s5test (the SiFive test module, used only to
   halt the processor) are not in the scoped manual's chapter list either
   (ref/SOURCES.md: "chapters selected: AHB/APB bridge, APB UART, DSU,
   GPTIMER, GRETH, IRQMP, L2C, SDCTRL").  s5test in particular is a
   simulator convenience, not a real GRLIB core, so its cases carry no
   citation at all.  grlib_boot_init is the same kind of thing: SIS's own
   boot loader convenience, not hardware, so it is pinned as current
   behaviour too.

   The PLIC cases close two of the three branches tests/riscvcores.cc's
   plic_fixture left open.  The third, grlib.cc:1683, is dead: plic_read's
   four-way if/else-if ladder (grlib.cc:1655-1686) tests addr against
   PLIC_THRES, PLIC_IENA and PLIC_IPEND in descending order and then falls
   to "else if (addr < PLIC_IPEND)" as its last arm.  By the time that arm
   is reached, the first three conditions have already failed, which
   proves addr < PLIC_IPEND on every path that gets there: there is no
   value of addr that reaches the arm and makes it false.  No case below
   drives it, and none can; see the final report.  */

#include "doctest.h"
#include "support.h"

#include "config.h"
#include "grlibcore.h"

#include <cstring>

using sis_tests::stdout_capture;

namespace
{

/* ------------------- SDCTRL / SRCTRL ----------------------- */

/* Both cores share grlib_store_bytes (grlib.cc:1286), so the byte, half-word
   and double-word cases only need driving through one of them; sdctrl_write
   and srctrl_write are themselves one straight-line call into it.

   Neither core has a reset (const struct grlib_ipcore sdctrl = { NULL, NULL,
   ... }; likewise srctrl), so the bytes they touch in the process-global
   ramb/romb (sis.h) survive past this fixture.  Nothing else in the tree
   reads ramb/romb today, but restore the small range used here anyway so a
   future file does not inherit it.  */

struct sdctrl_fixture : sis_tests::grlib_core_fixture
{
  sdctrl_fixture () : sis_tests::grlib_core_fixture (&sdctrl)
  {
    std::memset (ramb, 0, 32);
  }

  ~sdctrl_fixture () { std::memset (ramb, 0, 32); }
};

struct srctrl_fixture : sis_tests::grlib_core_fixture
{
  srctrl_fixture () : sis_tests::grlib_core_fixture (&srctrl)
  {
    std::memset (romb, 0, 32);
  }

  ~srctrl_fixture () { std::memset (romb, 0, 32); }
};

}

TEST_CASE_FIXTURE (
    sdctrl_fixture,
    "SDCTRL (current behaviour) a word access round-trips through RAM")
{
  /* grlib_store_bytes's sz == 2 arm (grlib.cc:1288-1289): a plain memcpy,
     the same width the grlib_core_fixture::write/read helpers always use.  */
  uint32 word = 0x11223344;
  sdctrl.write (0, &word, 2);

  uint32 got = 0;
  sdctrl.read (0, &got);
  CHECK (got == word);
}

TEST_CASE_FIXTURE (
    sdctrl_fixture,
    "SDCTRL (current behaviour) a byte access swaps for the host and core")
{
  /* grlib_store_bytes's case 0 (grlib.cc:1293-1296).  waddr is XORed with
     arch->bswap outright, which is how a big-endian core (sparc32 here, the
     grlib_core_fixture default) stores a single byte into a little-endian
     host word at the position a big-endian read would agree with.  */
  uint32 data = 0xab;
  sdctrl.write (4, &data, 0);

  CHECK ((unsigned char) ramb[4 ^ arch->bswap] == 0xab);
}

TEST_CASE_FIXTURE (
    sdctrl_fixture,
    "SDCTRL (current behaviour) a half-word access swaps for the host and "
    "core")
{
  /* grlib_store_bytes's case 1 (grlib.cc:1297-1300).  Only bit 1 of
     arch->bswap applies, since a half-word already agrees with the host on
     which half of a word it sits in.  */
  uint32 data = 0xbeef;
  sdctrl.write (8, &data, 1);

  uint16 got;
  std::memcpy (&got, &ramb[8 ^ (arch->bswap & 2)], 2);
  CHECK (got == 0xbeef);
}

TEST_CASE_FIXTURE (
    sdctrl_fixture,
    "SDCTRL (current behaviour) a double-word access is a plain 8-byte copy")
{
  /* grlib_store_bytes's case 3 (grlib.cc:1301-1303): no swap at all, unlike
     the byte and half-word cases above.  */
  uint32 data[2] = { 0x01020304, 0x05060708 };
  sdctrl.write (16, data, 3);

  uint32 got[2];
  std::memcpy (got, &ramb[16], 8);
  CHECK (got[0] == data[0]);
  CHECK (got[1] == data[1]);
}

TEST_CASE_FIXTURE (
    sdctrl_fixture,
    "SDCTRL (current behaviour) an access width other than 0, 1, 2 or 3 is "
    "ignored")
{
  /* sz is an int32 with no caller-side range check (sdctrl_write and
     srctrl_write both forward whatever sz they were given straight into
     grlib_store_bytes), so its switch (grlib.cc:1291-1304) has no default:
     an sz outside the four widths it lists falls out of the switch with
     nothing written, same shape as s5test_write's switch below.  */
  ramb[0] = 0x7a;
  uint32 data = 0x11223344;

  grlib_store_bytes (ramb, 0, &data, 5);

  CHECK ((unsigned char) ramb[0] == 0x7a);
}

TEST_CASE_FIXTURE (
    sdctrl_fixture,
    "SDCTRL (current behaviour) add logs the decoded size and address")
{
  /* A mask of 0xfff decodes to a 1 M window: sdctrl_add's own formula
     (grlib.cc:1328), (~(mask << 20) + 1) >> 20, with mask = 0xfff gives
     (~0xfff00000 + 1) >> 20 == 1.  */
  sis_verbose = 0;
  {
    stdout_capture quiet;
    sdctrl.add (0, 0x40000000, 0xfff);
    CHECK (quiet.str ().empty ());
  }

  sis_verbose = 1;
  stdout_capture cap;
  sdctrl.add (0, 0x40000000, 0xfff);
  std::string text = cap.str ();

  CHECK (text.find ("SDRAM controller") != std::string::npos);
  CHECK (text.find ("1 M") != std::string::npos);
  CHECK (text.find ("0x40000000") != std::string::npos);
}

TEST_CASE_FIXTURE (
    srctrl_fixture,
    "SRCTRL (current behaviour) a word access round-trips through the PROM "
    "buffer")
{
  /* srctrl_write/srctrl_read (grlib.cc:1338-1348) are the same shape as
     sdctrl's, over romb instead of ramb; grlib_store_bytes's own branches
     are already exercised via sdctrl above, so this only has to prove
     srctrl reaches it and lands in the right buffer.  */
  uint32 word = 0xdeadbeef;
  srctrl.write (0, &word, 2);

  uint32 got = 0;
  srctrl.read (0, &got);
  CHECK (got == word);
}

TEST_CASE_FIXTURE (
    srctrl_fixture,
    "SRCTRL (current behaviour) add logs the decoded size and address")
{
  sis_verbose = 0;
  {
    stdout_capture quiet;
    srctrl.add (0, 0x20000000, 0xfff);
    CHECK (quiet.str ().empty ());
  }

  sis_verbose = 1;
  stdout_capture cap;
  srctrl.add (0, 0x20000000, 0xfff);
  std::string text = cap.str ();

  CHECK (text.find ("PROM controller") != std::string::npos);
  CHECK (text.find ("1 M") != std::string::npos);
  CHECK (text.find ("0x20000000") != std::string::npos);
}

/* ------------------- GRCLKGATE ----------------------- */

namespace
{

const uint32 GRCLKGATE_UNLOCK = 0x00;
const uint32 GRCLKGATE_CLKEN = 0x04;
const uint32 GRCLKGATE_RESET = 0x08;
const uint32 GRCLKGATE_OVERRIDE = 0x0c;

struct clkgate_fixture : sis_tests::grlib_core_fixture
{
  clkgate_fixture () : sis_tests::grlib_core_fixture (&clkgate) {}
};

}

TEST_CASE_FIXTURE (
    clkgate_fixture,
    "GRCLKGATE (current behaviour) resets to zero and is plain read/write "
    "storage")
{
  /* Not in ref/'s scoped chapter list (see the file header).  grlib.cc's own
     comment above clkgate_reset (grlib.cc:1366-1368) states the intended
     behaviour directly: "the emulated cores are always clocked, so enabling
     or resetting a clock has no effect other than the value read back."
     That makes every register a plain storage cell with no side effect to
     assert beyond what was written coming back.  */
  CHECK (read (GRCLKGATE_UNLOCK) == 0u);
  CHECK (read (GRCLKGATE_CLKEN) == 0u);
  CHECK (read (GRCLKGATE_RESET) == 0u);
  CHECK (read (GRCLKGATE_OVERRIDE) == 0u);

  write (GRCLKGATE_UNLOCK, 0x1);
  write (GRCLKGATE_CLKEN, 0x2);
  write (GRCLKGATE_RESET, 0x4);
  write (GRCLKGATE_OVERRIDE, 0x8);

  CHECK (read (GRCLKGATE_UNLOCK) == 1u);
  CHECK (read (GRCLKGATE_CLKEN) == 2u);
  CHECK (read (GRCLKGATE_RESET) == 4u);
  CHECK (read (GRCLKGATE_OVERRIDE) == 8u);
}

TEST_CASE_FIXTURE (clkgate_fixture,
		   "GRCLKGATE (current behaviour) add logs the address")
{
  sis_verbose = 0;
  {
    stdout_capture quiet;
    clkgate.add (0, 0x80000f00, 0xfff);
    CHECK (quiet.str ().empty ());
  }

  sis_verbose = 1;
  stdout_capture cap;
  clkgate.add (0, 0x80000f00, 0xfff);

  CHECK (cap.str ().find ("Clock gating unit") != std::string::npos);
  CHECK (cap.str ().find ("0x80000f00") != std::string::npos);
}

/* ------------------- grlib_boot_init ----------------------- */

namespace
{

const uint32 GPTIMER_SCLOAD = 0x04;
const uint32 GPTIMER_TIMER1 = 0x10;
const uint32 GPTIMER_RELOAD1 = 0x14;
const uint32 GPTIMER_CTRL1 = 0x18;

struct gptimer_fixture : sis_tests::grlib_core_fixture
{
  gptimer_fixture () : sis_tests::grlib_core_fixture (&gptimer) {}
};

}

TEST_CASE_FIXTURE (
    gptimer_fixture,
    "grlib_boot_init (current behaviour) programs GPTIMER1 as a "
    "free-running down counter")
{
  /* grlib_boot_init (grlib.cc:1414-1424) is leon3.cc's, gr740.cc's and
     rv32.cc's own boot loader convenience, not a piece of GRLIB hardware,
     so this is pinned as current behaviour rather than checked against a
     spec.  It writes GPTIMER1's scale load, counter, reload and control
     registers directly through gpt_write, in that order: a one-clock
     scaler (ebase.freq - 1, and the fixture's ebase.freq is 1), a counter
     and reload of -1, and a control value of 7 (enable | restart | load).

     gpt_ctrl_write (grlib.cc:909-933, already covered) masks that 7 down to
     3 (val & 0xb), so the control register does not read back what was
     written; asserting the masked value is what proves grlib_boot_init's
     own four writes ran in the order that matters, since the load bit in
     that 7 is what makes CTRL1's write pull RELOAD1 into the counter before
     TIMER1's own write is overwritten by it.  */
  grlib_boot_init ();

  CHECK (read (GPTIMER_SCLOAD) == (uint32) (ebase.freq - 1));
  CHECK (read (GPTIMER_RELOAD1) == 0xffffffffu);
  CHECK (read (GPTIMER_CTRL1) == 3u);
  CHECK (read (GPTIMER_TIMER1) == 0xffffffffu);
}

/* ------------------- SiFive test module (s5test) ----------------------- */

namespace
{

struct s5test_fixture : sis_tests::grlib_core_fixture
{
  int saved_ncpu;

  s5test_fixture ()
      : sis_tests::grlib_core_fixture (&s5test), saved_ncpu (ncpu)
  {
    ncpu = 1;
    sregs[0].trap = 0;
  }

  ~s5test_fixture ()
  {
    sregs[0].trap = 0;
    ncpu = saved_ncpu;
  }
};

}

TEST_CASE_FIXTURE (
    s5test_fixture,
    "s5test (current behaviour) writing 0x5555 to offset 0 halts every CPU")
{
  /* Not a real GRLIB core (see the file header): s5test.c's own comment
     calls it "used to halt processor".  s5test_write's only case
     (grlib.cc:1741-1748) forces ERROR_TRAP on every CPU up to ncpu when the
     magic value 0x5555 is written to offset 0, which is how rv32.cc's board
     answers a RISC-V target's SiFive-style shutdown request.  */
  uint32 magic = 0x5555;
  stdout_capture cap;
  write (0, magic);

  CHECK (sregs[0].trap == ERROR_TRAP);
  CHECK (cap.str ().find ("Power-off issued") != std::string::npos);
}

TEST_CASE_FIXTURE (
    s5test_fixture,
    "s5test (current behaviour) any other value at offset 0 is a no-op")
{
  write (0, 0x1234);

  CHECK (sregs[0].trap == 0);
}

TEST_CASE_FIXTURE (
    s5test_fixture,
    "s5test (current behaviour) an address other than the magic offset is "
    "ignored")
{
  /* s5test_write's switch has no default (grlib.cc:1739-1749): an offset
     that matches none of its cases, unlike an unmapped read elsewhere in
     grlib.cc, is not even a documented no-op, just the absence of any case
     label.  */
  write (4, 0x5555);

  CHECK (sregs[0].trap == 0);
}

TEST_CASE_FIXTURE (s5test_fixture,
		   "s5test (current behaviour) add logs the address when "
		   "verbose")
{
  /* s5test_add (grlib.cc:1755-1761) is the board registration hook, only
     reached by rv32.cc; the write cases above drive s5test_write directly
     and never call it.  Same shape as tests/irqmp.cc's "IRQMP verbose add
     announces the controller".  */
  sis_verbose = 0;
  {
    stdout_capture quiet;
    s5test.add (0, 0x00100000, 0xfff);
    CHECK (quiet.str ().empty ());
  }

  sis_verbose = 1;
  stdout_capture cap;
  s5test.add (0, 0x00100000, 0xfff);

  CHECK (cap.str ().find ("S5 Test module") != std::string::npos);
  CHECK (cap.str ().find ("0x00100000") != std::string::npos);
}

/* ------------------- DSU add ----------------------- */

namespace
{

struct dsu_add_fixture : sis_tests::grlib_core_fixture
{
  dsu_add_fixture () : sis_tests::grlib_core_fixture (&dsu) {}
};

}

TEST_CASE_FIXTURE (dsu_add_fixture, "DSU add logs the address when verbose")
{
  /* dsu_read/dsu_write/dsu_reset are covered by tests/grlibcores.cc's own
     dsu_fixture; dsu_add (grlib.cc:1776-1782) is grlib.cc's board
     registration hook, which that file's fixture never calls since it
     drives the core directly with no bus.  Same shape as
     tests/irqmp.cc's "IRQMP verbose add announces the controller".  */
  sis_verbose = 0;
  {
    stdout_capture quiet;
    dsu.add (0, 0x90000000, 0xfff);
    CHECK (quiet.str ().empty ());
  }

  sis_verbose = 1;
  stdout_capture cap;
  dsu.add (0, 0x90000000, 0xfff);

  CHECK (cap.str ().find ("DSU") != std::string::npos);
  CHECK (cap.str ().find ("0x90000000") != std::string::npos);
}

/* ------------------- PLIC leftovers ----------------------- */

namespace
{

const uint32 PLIC_PRIO = 0x0000;
const uint32 PLIC_IPEND0 = 0x1000;
const uint32 PLIC_IENA0 = 0x2000;
const uint32 PLIC_THRES0 = 0x200000;
const uint32 PLIC_CLAIM0 = 0x200004;

/* plic has no init and no reset (const struct grlib_ipcore plic = { NULL,
   NULL, ... }, grlib.cc:1728), so plic_prio/plic_ie/plic_ip/plic_thres/
   plic_claim are process-global statics shared with tests/riscvcores.cc's
   and tests/ns16550.cc's cases.  Drain them through the register interface
   before and after, the same way tests/riscvcores.cc's plic_fixture does,
   minus the ns16550 dependency neither case below needs: open every
   source's enable and threshold, claim and complete until a claim reads
   zero (7.10/7.11 of ref/riscv-privileged-isa-v1.9-draft.md, 10.7/10.8 of
   ref/sifive-fu540-c000-manual.md), then close the enables and priorities
   again.  */
struct plic_leftover_fixture : sis_tests::grlib_core_fixture
{
  plic_leftover_fixture () : sis_tests::grlib_core_fixture (&plic)
  {
    arch = &riscv;
    drain ();
  }

  ~plic_leftover_fixture () { drain (); }

  void
  drain ()
  {
    write (PLIC_IENA0, 0xffffffff);
    write (PLIC_THRES0, 0);
    write (PLIC_CLAIM0, 0); /* completion, forces the first recompute */
    for (int tries = 0; tries < 40; tries++)
      {
	uint32 id = read (PLIC_CLAIM0);
	if (id == 0)
	  break;
	write (PLIC_CLAIM0, id);
      }
    write (PLIC_IENA0, 0);
    write (PLIC_IENA0 + 4, 0);

    for (int i = 0; i < 64; i++)
      write (PLIC_PRIO + 4 * i, 0);

    CHECK (read (PLIC_IPEND0) == 0u);
  }
};

}

TEST_CASE_FIXTURE (
    plic_leftover_fixture,
    "PLIC reading the M-Mode enable register returns word 0 (10.5, table 42)")
{
  /* tests/riscvcores.cc's drain() and its "second word" case both only ever
     write PLIC_IENA0/read PLIC_IENA0 + 4 (the second, 32-53 word, table 43);
     nothing there reads word 0 back.  plic_read's addr & 4 test
     (grlib.cc:1670) picks word 0 when it is false, which this drives for
     the first time: table 42 describes it as ordinary RW storage.  */
  write (PLIC_IENA0, 0x2a);
  CHECK (read (PLIC_IENA0) == 0x2au);
}

TEST_CASE_FIXTURE (
    plic_leftover_fixture,
    "PLIC (current behaviour) a write to the pending register range is "
    "silently ignored (10.4)")
{
  /* Tables 40 and 41 mark the pending registers RO, so nothing in chapter
     10 says what a write there should do.  plic_write's ladder
     (grlib.cc:1696-1715) has no arm at all for PLIC_IPEND0's address: it
     is above the addr < PLIC_IPEND arm and below the addr >= PLIC_IENA one,
     so the write silently falls out of the function with nothing changed.
     This is what closes grlib.cc:1712's remaining branch: addr == PLIC_IPEND0
     fails "addr < PLIC_IPEND" (0x1000 is not less than 0x1000), which is the
     one condition tests/riscvcores.cc's cases, all addressed at PLIC_PRIO or
     above PLIC_IENA0, never drove false.  */
  CHECK (read (PLIC_IPEND0) == 0u);
  CHECK (read (PLIC_PRIO) == 0u);

  write (PLIC_IPEND0, 0xffffffff);

  CHECK (read (PLIC_IPEND0) == 0u);
  /* PLIC_IPEND0 (0x1000) and PLIC_PRIO (0x0000) alias to the same index,
     (addr & 0xff) >> 2, into plic_prio (grlib.cc:1685/1714): if the write
     above landed one addr short, in the priority arm instead of falling out
     silently, it would show up here instead of on IPEND0 itself.  */
  CHECK (read (PLIC_PRIO) == 0u);
}
