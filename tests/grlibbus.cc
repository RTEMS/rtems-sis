/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Tests for the AMBA bus layer in grlib.cc itself: the AHB master/slave
   registration and dispatch (grlib_ahbm_add, grlib_ahbs_add, grlib_init,
   grlib_reset, grlib_read, grlib_write), the AHB plug&play area
   (grlib_ahbmpp_add, grlib_ahbspp_add), and the AHB/APB bridge lookup
   (grlib_apb_bus, reached only through grlib_apb_add).  Every board file
   (leon3.cc, gr740.cc, rv32.cc) builds its peripheral map by calling into
   this layer; tests/grlibcores.cc and tests/irqmp.cc exercise individual
   cores mounted on it, but nothing before this file drove grlib_init,
   grlib_reset, grlib_read, grlib_write, grlib_ahbm_add or grlib_ahbs_add at
   all.

   The APBCTRL chapter of the GRLIB IP core manual in ref/ ("115-119:
   APBCTRL") is, as tests/grlibcores.cc's header already notes, a VHDL
   component declaration and an instantiation example with no register
   table.  It says nothing about how an AHB slave or an APB core decodes
   its address range either; that comes only from grlib.cc's own
   arithmetic (the 1 MB AHB granularity of grlib_ahbs_add's `mask << 20`
   and the 256 byte APB granularity of grlib_apb_add's `mask << 8`) and
   from the encoding macros in grlib.h.  So every case below is marked
   "(current behaviour)": it pins what the code does, not a documented
   interface.

   grlib.cc has no way to unregister an AHB master, AHB slave or APB core,
   and grlib_ahbm_add/grlib_ahbs_add do not bound their arrays at all, so
   this file follows tests/grlibcore.h's rule: register a small, fixed set
   of fake cores exactly once for the whole binary (ensure_ahb_registered,
   ensure_apb_bridges_registered below), never per case.  The two AHB/APB
   bridges (apbmst, apbmst2) and the plug&play indices are process-global
   and shared with tests/grlibcores.cc, so:

   - The AHB master/slave fakes here live at 1 MB windows (0x50000000,
     0x51000000, 0x52000000) clear of every board's own registrations
     (erc32.cc is self-contained; leon2.cc/leon3.cc use 0x00000000,
     0x40000000, 0x80000000 and leon3.cc also 0x90000000; gr740.cc uses
     0x00000000, 0xC0000000 and the 0xFF8xxxxx-0xFFExxxxx range; rv32.cc
     uses 0x00100000, 0x02000000, 0x0C000000, 0x10000000, 0x20000000 and
     0x80000000).  This file originally used 0x20000000 and 0x21000000,
     which were clear of every board at the time but collide with
     rv32.cc's own ROM window once tests/rv32board.cc registers the real
     rv32 board: whichever of the two lazily-registering files' first
     test case happens to run first under --order-by=rand wins that
     address in grlib.cc's shared, append-only ahbscores[], and the
     other's cases then read back the winner's data instead of their
     own.  Moved here rather than in tests/rv32board.cc since rv32.cc's
     address is the real one and this file's was always arbitrary.
   - The APB fakes mount on bridge 0 (apbmst) at offset 0xd00 and on
     bridge 1 (apbmst2) at offsets 0xc00 and 0x1000-0x3f00, all clear of
     the 0x200/0x300/0x500 offsets tests/grlibcores.cc's own cases rely on
     being a mounted core, a second mounted core, or a hole.
   - grlib_apbpp_add's shared index and grlib_ahbmpp_add/grlib_ahbspp_add's
     own indices are never asserted at an absolute position; the
     wraparound cases drive them with a do/while, the same trick
     tests/grlibcores.cc uses for the bridge 0 plug&play area.  */

#include "doctest.h"
#include "support.h"

#include "sis.h"
#include "grlib.h"

namespace
{

/* ------------------- AHB master/slave fakes ----------------------- */

int ahbm_init_calls;
int ahbm_reset_calls;
int ahbm_add_calls;
int ahbm_add_irq;

void
ahbm_init ()
{
  ahbm_init_calls++;
}

void
ahbm_reset ()
{
  ahbm_reset_calls++;
}

void
ahbm_add (int irq, uint32 addr, uint32 mask)
{
  (void) addr;
  (void) mask;
  ahbm_add_calls++;
  ahbm_add_irq = irq;
}

/* Has every callback, so it covers the "true" side of grlib_init's,
   grlib_reset's and grlib_ahbm_add's own null checks for a master.  */
const struct grlib_ipcore ahbm_with_callbacks = { ahbm_init, ahbm_reset, NULL,
						  NULL, ahbm_add };

/* Has none, covering the "false" side of the same checks.  */
const struct grlib_ipcore ahbm_no_callbacks = { NULL, NULL, NULL, NULL, NULL };

int ahbs_init_calls;
int ahbs_reset_calls;
int ahbs_read_calls;
int ahbs_write_calls;
uint32 ahbs_last_read_addr;
uint32 ahbs_last_write_addr;
uint32 ahbs_last_write_data;

void
ahbs_init ()
{
  ahbs_init_calls++;
}

void
ahbs_reset ()
{
  ahbs_reset_calls++;
}

int
ahbs_read (uint32 addr, uint32 *data)
{
  ahbs_read_calls++;
  ahbs_last_read_addr = addr;
  *data = 0xb00b0000u | addr;
  return 1;
}

int
ahbs_write (uint32 addr, uint32 *data, uint32 size)
{
  (void) size;
  ahbs_write_calls++;
  ahbs_last_write_addr = addr;
  ahbs_last_write_data = *data;
  return 1;
}

void
ahbs_add (int irq, uint32 addr, uint32 mask)
{
  (void) irq;
  (void) addr;
  (void) mask;
}

/* Mounted at 0x50000000, 1 MB window: every callback set, so it drives the
   "core found, callback present" path of grlib_init/grlib_reset/
   grlib_read/grlib_write.  */
const struct grlib_ipcore ahbs_full = { ahbs_init, ahbs_reset, ahbs_read,
					ahbs_write, ahbs_add };

/* Mounted at 0x51000000: init/reset/read/write are all NULL but add is
   set, so grlib_ahbs_add still claims its window (the "true" side of its
   own null check) while grlib_read/grlib_write hit the "no callback"
   fallback (their own "else res = 1") for an address that does match a
   slave.  */
const struct grlib_ipcore ahbs_noio = { NULL, NULL, NULL, NULL, ahbs_add };

/* Every callback NULL, including add: grlib_ahbs_add's null check takes
   its "false" branch, so this core's start/end/mask are left at zero and
   it never claims any address.  */
const struct grlib_ipcore ahbs_no_add = { NULL, NULL, NULL, NULL, NULL };

const uint32 ahbs_full_base = 0x50000000;
const uint32 ahbs_noio_base = 0x51000000;
const uint32 ahbs_no_add_base = 0x52000000;

/* Registers the fakes above exactly once for the whole binary.  Idempotent
   by construction: every case below calls it before touching any of the
   fakes, and only the first call does anything.  */
void
ensure_ahb_registered ()
{
  static bool done = false;

  if (done)
    return;
  done = true;

  grlib_ahbm_add (&ahbm_with_callbacks, 5);
  grlib_ahbm_add (&ahbm_no_callbacks, 6);

  grlib_ahbs_add (&ahbs_full, 7, ahbs_full_base, 0xfff);
  grlib_ahbs_add (&ahbs_noio, 0, ahbs_noio_base, 0xfff);
  grlib_ahbs_add (&ahbs_no_add, 0, ahbs_no_add_base, 0xfff);
}

/* ------------------- APB fakes ----------------------- */

int apb_fake_read_calls;
int apb_fake_write_calls;
int apb_fake_add_calls;
uint32 apb_fake_last_write_addr;
uint32 apb_fake_last_write_data;

int
apb_fake_read (uint32 addr, uint32 *data)
{
  apb_fake_read_calls++;
  *data = 0xcaca0000u | addr;
  return 1;
}

int
apb_fake_write (uint32 addr, uint32 *data, uint32 size)
{
  (void) size;
  apb_fake_write_calls++;
  apb_fake_last_write_addr = addr;
  apb_fake_last_write_data = *data;
  return 1;
}

void
apb_fake_add (int irq, uint32 addr, uint32 mask)
{
  (void) irq;
  (void) addr;
  (void) mask;
  apb_fake_add_calls++;
}

const struct grlib_ipcore apb_fake_core = { NULL, NULL, apb_fake_read,
					    apb_fake_write, apb_fake_add };

/* add is NULL: mounting this exercises grlib_apb_add's own "core->add is
   NULL" branch, which leaves the entry's window unclaimed.  */
const struct grlib_ipcore apb_no_add_core = { NULL, NULL, NULL, NULL, NULL };

/* Sets up the two bridges apbmst/apbmst2 exactly once, and mounts
   apb_fake_core on each at offset 0xc00.  tests/grlibcores.cc calls
   apbmst.add/apbmst2.add itself in several cases with the same addresses,
   so the bridge setup is idempotent with that file regardless of which
   runs first.

   The mount happens here rather than in the individual dispatch test
   below so that it is guaranteed to claim its slot before the "rejects a
   core once its bridge already holds 16" case below can ever fill bridge
   1 up: every case in this file calls this function first, and only the
   first call does anything, so whichever case's call is the one that
   actually runs the body always mounts this core before any case fills
   the bridge to capacity.  */
void
ensure_apb_bridges_registered ()
{
  static bool done = false;

  if (done)
    return;
  done = true;

  apbmst.add (0, 0x80000000, 0xfff);
  apbmst2.add (0, 0x80100000, 0xfff);

  grlib_apb_add (&apb_fake_core, 0, 0x80000c00, 0xfff);
  grlib_apb_add (&apb_fake_core, 0, 0x80100c00, 0xfff);
}

struct bus_fixture
{
  int saved_verbose;

  bus_fixture () : saved_verbose (sis_verbose) { sis_verbose = 0; }
  ~bus_fixture () { sis_verbose = saved_verbose; }
};

}

/* -------------------------------------------------------------------- */
/* AHB plug&play area wraparound                                        */
/* -------------------------------------------------------------------- */

TEST_CASE_FIXTURE (
    bus_fixture,
    "grlib bus (current behaviour) the AHB master plug&play area wraps "
    "after 64 entries")
{
  /* grlib_ahbmpp_add's index is shared with every AHB master's add
     callback in the whole binary and only ever grows in steps of 8 within
     [0, 64*8), so this cannot assume where it starts.  Drive it round
     until a call reports the wrapped index (0) exactly, the same trick
     tests/grlibcores.cc uses for the APB plug&play index.

     The call that returns 0 is the one that tripped the wrap check: it
     still wrote its own id at the slot just below the wrap (index 504),
     not at index 0, and the reset to 0 only takes effect for the call
     after it.  So this cannot check index 0 against that call's id;
     instead make one further call, which is now guaranteed to start at
     exactly index 0, and check that one.  */
  int idx;

  do
    {
      idx = grlib_ahbmpp_add (0xaa550001);
      /* Bounds every step, not just the final one: 64*8 itself is never a
	 valid return (the wrap check must have already reset it to 0), so
	 this is strict.  A wrap check off by even one call would let idx
	 run into the slave area's own indices before finally wrapping,
	 which the final landing spot alone would not catch.  */
      REQUIRE (idx < 64 * 8);
    }
  while (idx != 0);

  uint32 id = 0xaa550003;
  idx = grlib_ahbmpp_add (id);

  REQUIRE (idx == 8);
  CHECK (grlib_ahbpnp_read (0) == id);
}

TEST_CASE_FIXTURE (
    bus_fixture,
    "grlib bus (current behaviour) the AHB slave plug&play area wraps "
    "after 64 entries")
{
  /* Same wraparound, but grlib_ahbspp_add advances its own index
     (ahbsppindex) by 8 per call within [64*8, 128*8), wrapping back to
     64*8 rather than 0.  As above, the call that reports the wrapped
     index is not the one whose id landed there; one further call is.  */
  int idx;

  do
    {
      idx = grlib_ahbspp_add (0xaa550002, 1, 2, 3, 4);
      /* 128*8 itself is never a valid return, the same reasoning as the
	 master area above.  */
      REQUIRE (idx < 128 * 8);
    }
  while (idx != 64 * 8);

  uint32 id = 0xaa550004;
  idx = grlib_ahbspp_add (id, 1, 2, 3, 4);

  REQUIRE (idx == 64 * 8 + 8);
  CHECK (grlib_ahbpnp_read (64 * 8 * 4) == id);
}

/* -------------------------------------------------------------------- */
/* grlib_ahbm_add / grlib_ahbs_add                                      */
/* -------------------------------------------------------------------- */

TEST_CASE_FIXTURE (
    bus_fixture,
    "grlib bus (current behaviour) grlib_ahbm_add calls a master's add "
    "callback with the irq and skips a master with none")
{
  ensure_ahb_registered ();

  /* Registration happens once, on the first call to ensure_ahb_registered
     from any case in this file, so assert on the result rather than on a
     delta: whichever case runs first sees the same outcome as any other.  */
  CHECK (ahbm_add_calls == 1);
  CHECK (ahbm_add_irq == 5);
}

TEST_CASE_FIXTURE (
    bus_fixture,
    "grlib bus (current behaviour) grlib_ahbs_add calls a slave's add "
    "callback and skips a slave with none")
{
  ensure_ahb_registered ();

  /* ahbs_full has an add callback (called once, from ensure_ahb_registered
     above); ahbs_no_add has none, so grlib_ahbs_add's own null check must
     have skipped setting up its window.  Reading inside where that window
     would have been (it was registered at ahbs_no_add_base with the same
     0xfff mask as the others) must therefore miss every slave: no crash,
     and grlib_read reports failure rather than routing to a slave whose
     start/end/mask were never set.  */
  uint32 data = 0x11223344;
  int rc = grlib_read (ahbs_no_add_base, &data);

  CHECK (rc == 1);
  CHECK (data == 0x11223344);
}

/* -------------------------------------------------------------------- */
/* grlib_init / grlib_reset                                             */
/* -------------------------------------------------------------------- */

TEST_CASE_FIXTURE (bus_fixture,
		   "grlib bus (current behaviour) grlib_init reaches every "
		   "registered AHB master and slave that has one")
{
  ensure_ahb_registered ();

  int master_before = ahbm_init_calls;
  int slave_before = ahbs_init_calls;

  grlib_init ();

  /* ahbm_with_callbacks and ahbs_full are the only two of the five fakes
     with an init callback; ahbm_no_callbacks, ahbs_noio and ahbs_no_add
     must not crash grlib_init's null check.  */
  CHECK (ahbm_init_calls == master_before + 1);
  CHECK (ahbs_init_calls == slave_before + 1);
}

TEST_CASE_FIXTURE (bus_fixture,
		   "grlib bus (current behaviour) grlib_reset reaches every "
		   "registered AHB master and slave that has one")
{
  ensure_ahb_registered ();

  int master_before = ahbm_reset_calls;
  int slave_before = ahbs_reset_calls;

  grlib_reset ();

  CHECK (ahbm_reset_calls == master_before + 1);
  CHECK (ahbs_reset_calls == slave_before + 1);
}

/* -------------------------------------------------------------------- */
/* grlib_read                                                           */
/* -------------------------------------------------------------------- */

TEST_CASE_FIXTURE (
    bus_fixture,
    "grlib bus (current behaviour) grlib_read dispatches to the AHB "
    "slave whose 1 MB window covers the address")
{
  ensure_ahb_registered ();

  uint32 data = 0;
  int rc = grlib_read (ahbs_full_base + 0x10, &data);

  CHECK (rc == 0);
  CHECK (ahbs_last_read_addr == 0x10);
  CHECK (data == (0xb00b0000u | 0x10));
}

TEST_CASE_FIXTURE (
    bus_fixture,
    "grlib bus (current behaviour) grlib_read reports success without "
    "touching *data for a matched slave with no read callback")
{
  ensure_ahb_registered ();

  uint32 data = 0xcafebabe;
  int rc = grlib_read (ahbs_noio_base + 4, &data);

  /* ahbs_noio's read is NULL: grlib_read's own else branch sets res = 1
     (success) but never writes through data, unlike the matched-slave
     path above.  */
  CHECK (rc == 0);
  CHECK (data == 0xcafebabe);
}

TEST_CASE_FIXTURE (
    bus_fixture,
    "grlib bus (current behaviour) grlib_read falls back to the AHB "
    "plug&play area outside every slave's window")
{
  ensure_ahb_registered ();

  /* AHBPP_START/END is a fixed 4 kB window (grlib.h) distinct from every
     AHB slave's own 1 MB window, so this cannot match ahbs_full, ahbs_noio
     or ahbs_no_add.  Cross-check against grlib_ahbpnp_read directly rather
     than asserting a value: the plug&play area's contents are shared with
     every AHB master/slave add callback in the whole binary and are not
     this case's to know.  */
  uint32 data = 0;
  int rc = grlib_read (AHBPP_START, &data);

  CHECK (rc == 0);
  CHECK (data == grlib_ahbpnp_read (AHBPP_START));
}

TEST_CASE_FIXTURE (
    bus_fixture,
    "grlib bus (current behaviour) grlib_read verbose tracing follows the "
    "plug&play fallback")
{
  ensure_ahb_registered ();

  {
    sis_tests::stdout_capture cap;
    uint32 data = 0;
    grlib_read (AHBPP_START, &data);
    CHECK (cap.str ().empty ());
  }

  {
    sis_verbose = 2;
    sis_tests::stdout_capture cap;
    uint32 data = 0;
    grlib_read (AHBPP_START, &data);
    CHECK (cap.str ().find ("AHB PP read a:") != std::string::npos);
  }
}

TEST_CASE_FIXTURE (
    bus_fixture,
    "grlib bus (current behaviour) grlib_read fails for an address no "
    "slave and no plug&play window covers")
{
  ensure_ahb_registered ();

  /* 0x00000000 would have been the obvious unmapped address, but it is
     leon2.cc's/leon3.cc's ROM and gr740.cc's RAM window (see the address
     list above): a genuine board test registering either would make this
     address mapped whenever it registered first.  ahbs_no_add_base's own
     1 MB window ends here, so one word past it is still clear of every
     board and of every fake this file mounts. */
  uint32 data = 0x55667788;
  int rc = grlib_read (ahbs_no_add_base + 0x00100000, &data);

  CHECK (rc == 1);
  CHECK (data == 0x55667788);
}

/* -------------------------------------------------------------------- */
/* grlib_write                                                          */
/* -------------------------------------------------------------------- */

TEST_CASE_FIXTURE (
    bus_fixture,
    "grlib bus (current behaviour) grlib_write dispatches to the AHB "
    "slave whose 1 MB window covers the address")
{
  ensure_ahb_registered ();

  uint32 data = 0x99;
  int rc = grlib_write (ahbs_full_base + 0x20, &data, 2);

  CHECK (rc == 0);
  CHECK (ahbs_last_write_addr == 0x20);
  CHECK (ahbs_last_write_data == 0x99);
}

TEST_CASE_FIXTURE (
    bus_fixture,
    "grlib bus (current behaviour) grlib_write reports success for a "
    "matched slave with no write callback")
{
  ensure_ahb_registered ();

  int calls_before = ahbs_write_calls;
  uint32 data = 0x77;
  int rc = grlib_write (ahbs_noio_base + 4, &data, 2);

  CHECK (rc == 0);
  CHECK (ahbs_write_calls == calls_before);
}

TEST_CASE_FIXTURE (
    bus_fixture,
    "grlib bus (current behaviour) grlib_write fails for an address no "
    "slave covers, and never falls back to the plug&play area")
{
  ensure_ahb_registered ();

  /* Unlike grlib_read, grlib_write has no plug&play fallback at all: the
     plug&play area is read-only from the CPU's point of view.  0x00000000
     is avoided for the same reason as the grlib_read case above. */
  uint32 data = 0x11;
  int rc = grlib_write (ahbs_no_add_base + 0x00100000, &data, 2);

  CHECK (rc == 1);
}

TEST_CASE_FIXTURE (bus_fixture,
		   "grlib bus (current behaviour) grlib_write verbose "
		   "tracing follows a matched slave")
{
  ensure_ahb_registered ();

  {
    sis_tests::stdout_capture cap;
    uint32 data = 1;
    grlib_write (ahbs_full_base, &data, 2);
    CHECK (cap.str ().empty ());
  }

  {
    sis_verbose = 3;
    sis_tests::stdout_capture cap;
    uint32 data = 1;
    grlib_write (ahbs_full_base, &data, 2);
    CHECK (cap.str ().find ("AHB write a:") != std::string::npos);
  }
}

/* -------------------------------------------------------------------- */
/* grlib_apb_bus, reached only through grlib_apb_add                    */
/* -------------------------------------------------------------------- */

TEST_CASE_FIXTURE (
    bus_fixture,
    "grlib bus (current behaviour) grlib_apb_add mounts a core on "
    "whichever bridge's window covers the address")
{
  /* ensure_apb_bridges_registered mounts apb_fake_core at offset 0xc00 on
     both bridges (see its comment for why the mount itself has to live
     there rather than here).  This case only has to prove the lookup
     actually distinguishes the two: bridge 0's window is
     0x80000000-0x800fffff, so an address inside bridge 1's window at the
     same relative offset must have made grlib_apb_add's bridge lookup
     reject bridge 0 and try the next one, not land on bridge 0 by
     accident.  */
  ensure_apb_bridges_registered ();

  uint32 data = 0;
  apbmst.read (0xc00, &data);
  CHECK (data == (0xcaca0000u | 0x00));

  data = 0;
  apbmst2.read (0xc00, &data);
  CHECK (data == (0xcaca0000u | 0x00));
}

TEST_CASE_FIXTURE (
    bus_fixture,
    "grlib bus (current behaviour) grlib_apb_add reports and skips an "
    "address no bridge covers")
{
  ensure_apb_bridges_registered ();

  /* Outside both bridges' 1 MB windows (0x80000000 and 0x80100000).  */
  int calls_before = apb_fake_add_calls;
  sis_tests::stdout_capture cap;

  grlib_apb_add (&apb_fake_core, 0, 0x90000000, 0xfff);

  CHECK (cap.str ().find ("No AHB/APB bridge covers address") !=
	 std::string::npos);
  CHECK (apb_fake_add_calls == calls_before);
}

TEST_CASE_FIXTURE (
    bus_fixture,
    "grlib bus (current behaviour) grlib_apb_add skips a core with no add "
    "callback, leaving its window unclaimed")
{
  ensure_apb_bridges_registered ();

  grlib_apb_add (&apb_no_add_core, 0, 0x80000d00, 0xfff);

  /* apb_no_add_core's add is NULL, so grlib_apb_add never set its
     start/end/mask: reading its would-be window on bridge 0 must miss it
     and fall through to the plug&play area's own "hole" (0 in this
     range), not route to the core.  */
  uint32 data = 0;
  apbmst.read (0xd00, &data);
  CHECK (data == 0);
}

TEST_CASE_FIXTURE (
    bus_fixture,
    "grlib bus (current behaviour) grlib_apb_add rejects a core once its "
    "bridge already holds 16")
{
  ensure_apb_bridges_registered ();

  /* Bridge 1 (apbmst2) is not otherwise used by any file in this binary
     except this one, so its core count starts wherever earlier cases in
     this file (if any ran first) left it and only grows.  Mount enough
     distinct-window cores that it must fill up and start rejecting,
     without assuming its starting count: 32 attempts is comfortably more
     than the 16-entry cap could ever need from a start of 0.  */
  bool saw_reject = false;

  for (int i = 0; i < 32 && !saw_reject; i++)
    {
      sis_tests::stdout_capture cap;

      grlib_apb_add (&apb_fake_core, 0, 0x80100000 + 0x1000 + i * 0x100,
		     0xfff);

      if (cap.str ().find ("Too many cores") != std::string::npos)
	saw_reject = true;
    }

  REQUIRE (saw_reject);

  /* Once full, the bridge keeps rejecting: the add callback is not
     called, so no plug&play entry is written for the rejected core.  */
  int calls_before = apb_fake_add_calls;
  sis_tests::stdout_capture cap;

  grlib_apb_add (&apb_fake_core, 0, 0x80100000 + 0x3f00, 0xfff);

  CHECK (cap.str ().find ("Too many cores on the AHB/APB bridge") !=
	 std::string::npos);
  CHECK (apb_fake_add_calls == calls_before);
}
