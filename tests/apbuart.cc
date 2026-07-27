/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Tests for the APBUART serial port core in grlib.cc.

   ref/grlib-ip-core-manual-scoped.md, chapter "130-141: APBUART", only
   keeps sections 18.2.2 (Transmitter break process), 18.2.3 (Receiver
   operation), 18.7.3 (UART Control Register) and 18.7.4 (UART Scaler
   Register); see ref/grlib-ip-core-manual-scoped.toc.md.  Sections 18.7.1
   (Data Register) and 18.7.2 (Status Register), which would give the exact
   bit layout of the data and status registers apbuart_read answers, are
   not in the scoped document: the pages between 18.2.3 and 18.7.3 render
   as unconverted "FRONTGRADE" picture placeholders rather than text, so
   those tables never made it into ref/.  Cases that rest on a register or
   bit the scoped manual does not cover say so in their name ("current
   behaviour") and cite the code instead of a section.

   Every case reaches apbuart's own read/write/flush directly, the way
   tests/irqmp.cc and tests/gptimer.cc reach their cores, and drives irqmp
   the same way to read the interrupt out of ext_irl: grlib_set_irq (which
   apbuart.cc calls with the interrupt hardwired to 3, not whatever irq a
   board passed to apbuart_add; see the "add" case below) writes into
   irqmp's global state regardless of which core object a case reaches it
   through.

   apbuart_init opens its port on uart_dev1, the same global -uart1 name a
   board reads from the command line, with the console as a fallback when
   that name is empty.  A case that let it fall back to the console would
   read and write the test binary's own stdin/stdout.  apbuart_env below
   points uart_dev1 at a private temporary file before the base fixture's
   constructor runs apbuart_init, so every case that opens the port is
   against a plain file and never touches the console.  Getting that
   ordering right needs uart_dev1 set from a constructor that runs before
   sis_tests::grlib_core_fixture's, which is what apbuart_env's first base
   class is for; C++ runs base class constructors in declaration order.

   The one thing no case here calls is apbuart_close_port: uart_port_close
   (uartport.cc) frees the port's FILE* without ever clearing
   uart_port::open, so a second close finds "open" still true and double
   frees it, and anything that touches the port in between finds "open"
   still true too and would write through a dangling FILE*.  Real callers
   (gr740.cc, rv32.cc, leon3.cc) only ever close once, right before the
   process exits.  See the final report for this as a suspected defect;
   the assignment is apbuart's own core, not uartport.cc, so it is flagged
   rather than exercised or fixed here.

   apbuart's state (the port itself, its RX queue, its TX buffer) is
   process-wide and, once the port has been opened for the first time,
   uart_port::open never goes false again through any call this file
   makes.  That makes the false side of every "if (porta.open)" in
   apbuart.cc reachable from exactly one place: before the port has ever
   been opened at all, which is only true once, at the very start of the
   whole test binary.  Every SUBCASE below other than the first constructs
   its own apbuart_env, which opens the port; the first SUBCASE is written
   to never do that, so it is guaranteed to run before any of them, the
   same way tests/greth.cc's file header explains for CTRL_RE.  All of
   this file's cases therefore live in one TEST_CASE with SUBCASEs in a
   fixed order rather than in separate TEST_CASE_FIXTUREs, since a fresh
   fixture per TEST_CASE would call apbuart_init before the first case's
   body ever ran.  */

#include "doctest.h"
#include "support.h"

#include "config.h"
#include "grlibcore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace
{

/* Register offsets, from grlib.cc's APBUART_* macros; 0x08 is also table
   129's CTRL and 0x0c is table 130's SCALER.  */
const uint32 RXTX = 0x00;
const uint32 STATUS = 0x04;
const uint32 CTRL = 0x08;
const uint32 SCALER = 0x0c;

/* Status bits apbuart_read sets.  Not in the scoped manual (see the file
   header); transcribed from the code under test rather than a table.  */
const uint32 STATUS_DR = 0x00000001;
const uint32 STATUS_READY = 0x00000006;

/* The interrupt line apbuart.cc raises on every RX/TX access.  It is a
   literal 3 in apbuart_read/apbuart_write, not the irq a board passes to
   apbuart_add; see the "add" case below.  */
const int UART_IRQ = 3;

/* The irqmp registers tests/irqmp.cc drives, reused here as a bystander
   the same way tests/greth.cc uses them.  */
const uint32 IMASK = 0x40;
const uint32 ICLEAR = 0x0c;

void
irqmp_write (uint32 offset, uint32 value)
{
  irqmp.write (offset, &value, 2);
}

/* Brings irqmp to a known state with UART_IRQ unmasked and ext_irl clear,
   the way every case here needs it.  irqmp is process-wide state no case
   owns, so this is called fresh from wherever a case sets up rather than
   trusted to carry over from the case before it.  */
void
arm_irqmp ()
{
  irqmp.init ();
  irqmp.reset ();
  irqmp_write (IMASK, 1 << UART_IRQ);
  for (int i = 0; i < NCPU; i++)
    ext_irl[i] = 0;
}

/* Creates a private temporary file and points the global uart_dev1 at it
   before anything else in apbuart_env runs; see the file header.  */
struct apbuart_host_file
{
  char path[64];
  int fd;
  char saved_uart_dev1[128];

  apbuart_host_file () : fd (-1)
  {
    strcpy (path, "/tmp/sis-apbuart-XXXXXX");
    fd = mkstemp (path);
    REQUIRE (fd >= 0);

    strcpy (saved_uart_dev1, uart_dev1);
    strcpy (uart_dev1, path);
  }

  ~apbuart_host_file ()
  {
    strcpy (uart_dev1, saved_uart_dev1);
    if (fd >= 0)
      close (fd);
    unlink (path);
  }

  /* Puts bytes in the file for the port to read.  The port's own
     descriptor, opened separately by uart_port_open, starts at offset 0
     the same as this one, so anything written here before a case reads
     the port is what apbuart_read finds waiting.  */
  void
  host_send (const char *data, size_t len)
  {
    REQUIRE (write (fd, data, len) == (ssize_t) len);
  }

  /* Reads back what apbuart wrote to the host side, through this file's
     own descriptor rather than the port's.  */
  int
  host_recv (char *buf, size_t len)
  {
    int n = 0;
    while (n < (int) len)
      {
	ssize_t r = ::read (fd, buf + n, len - n);
	if (r <= 0)
	  break;
	n += (int) r;
      }
    return n;
  }
};

/* Opens the port on the private file above and arms irqmp.  Every SUBCASE
   but the first constructs one of these; see the file header for why the
   first must not.  */
struct apbuart_env : apbuart_host_file, sis_tests::grlib_core_fixture
{
  apbuart_env () : sis_tests::grlib_core_fixture (&apbuart) { arm_irqmp (); }
};

}

TEST_CASE ("APBUART")
{
  SUBCASE ("(current behaviour) before the port is ever opened, reads and "
	   "writes are quiet")
  {
    /* No apbuart_env here: this is the one place uart_port::open is
       false, which the file header explains is otherwise unreachable.
       Housekeeping (ms/arch/ncpu/the event queue) comes from irqmp's own
       fixture, which never touches apbuart's port.  */
    sis_tests::grlib_core_fixture irqenv (&irqmp);
    arm_irqmp ();

    uint32 data = 0;

    /* RXTX: aind < anum is false (both start at zero), and with the port
       not open the fetch that would normally follow is skipped, so the
       stale queue byte is returned and nothing is raised.  */
    apbuart.read (RXTX, &data);
    CHECK (ext_irl[0] == 0);

    /* STATUS: the same two branches, so DR stays clear; bits 1 and 2 are
       unconditional so they are set regardless (table 129 gives them no
       corresponding enable this simulator honours; see the CTRL case
       below).  */
    apbuart.read (STATUS, &data);
    CHECK ((data & STATUS_DR) == 0);
    CHECK ((data & STATUS_READY) == STATUS_READY);
    CHECK (ext_irl[0] == 0);

    /* CTRL and an unimplemented offset (SCALER, table 130), at both
       verbosity settings so the default case's printf branch is taken
       both ways.  */
    apbuart.read (CTRL, &data);
    CHECK (data == 3);

    apbuart.read (SCALER, &data);
    CHECK (data == 0);
    sis_verbose = 1;
    apbuart.read (SCALER, &data);
    CHECK (data == 0);
    sis_verbose = 0;

    /* Writing RXTX with the port not open still raises the interrupt (the
       call sits outside the "if (porta.open)" block) but queues nothing,
       which the buffer-full case below would otherwise show up in.  */
    data = 'y';
    apbuart.write (RXTX, &data, 2);
    CHECK (ext_irl[0] == UART_IRQ);

    /* An unimplemented write offset, at both verbosity settings.  */
    data = 0x1234;
    apbuart.write (SCALER, &data, 2);
    sis_verbose = 1;
    apbuart.write (SCALER, &data, 2);
    sis_verbose = 0;

    /* Nothing was ever queued, so flushing does nothing; this also covers
       the "wnuma is zero" side of apbuart_flush's loop condition.  */
    apbuart_flush ();
  }

  SUBCASE ("spec (18.7.3) control register reports the transmitter and "
	   "receiver enabled, and (current behaviour) ignores writes")
  {
    apbuart_env env;

    /* Table 129 gives bit 1 as transmitter enable (TE) and bit 0 as
       receiver enable (RE); apbuart_read always answers both set.  */
    CHECK (env.read (CTRL) == 0x3);

    /* Table 129 marks TE and RE, among other bits, read-write.
       apbuart_write's CTRL case is empty, so nothing written ever
       changes what CTRL reads back as.  This is a real gap between the
       table and grlib.cc: see the final report.  */
    env.write (CTRL, 0x0);
    CHECK (env.read (CTRL) == 0x3);
    env.write (CTRL, 0xffffffff);
    CHECK (env.read (CTRL) == 0x3);

    /* apbuart_write's STATUS case is also empty; a write there is
       accepted and changes nothing observable.  */
    env.write (STATUS, 0xffffffff);
    CHECK ((env.read (STATUS) & STATUS_READY) == STATUS_READY);
  }

  SUBCASE ("(current behaviour) the scaler register (18.7.4) is unimplemented")
  {
    /* Table 130 makes SCALER a plain read-write reload value.
       apbuart_read and apbuart_write have no case for 0x0c, so it falls
       to the default: reads back zero regardless of what was written.  */
    apbuart_env env;

    CHECK (env.read (SCALER) == 0);
    env.write (SCALER, 0x1234);
    CHECK (env.read (SCALER) == 0);
  }

  SUBCASE ("(current behaviour) reading with nothing waiting is quiet")
  {
    /* The port is open this time (unlike the first SUBCASE) but nothing
       was ever sent, so the fetch both registers try comes back empty.  */
    apbuart_env env;

    env.read (RXTX);
    CHECK (ext_irl[0] == 0);

    CHECK ((env.read (STATUS) & STATUS_DR) == 0);
    CHECK (ext_irl[0] == 0);
  }

  SUBCASE ("(current behaviour) reading RXTX with one byte queued does "
	   "not raise the interrupt")
  {
    /* Fetching finds exactly one byte, so aind + 1 < anum is false: the
       "more remain" check that raises the interrupt on the other RXTX
       path is not taken.  */
    apbuart_env env;
    env.host_send ("Z", 1);

    CHECK (env.read (RXTX) == 'Z');
    CHECK (ext_irl[0] == 0);
  }

  SUBCASE (
      "(current behaviour) reading RXTX walks a multi-byte queue, raising "
      "the interrupt for every byte but the last")
  {
    /* Three bytes cover both branches of "aind < anum" (false on the
       first read, which does the fetch; true on the second and third)
       and both outcomes of "more remain" on each side of that branch.  */
    apbuart_env env;
    env.host_send ("ABC", 3);

    CHECK (env.read (RXTX) == 'A'); /* fetch, 2 more remain: irq */
    CHECK (ext_irl[0] == UART_IRQ);
    irqmp_write (ICLEAR, 1 << UART_IRQ);

    CHECK (env.read (RXTX) == 'B'); /* queued, 1 more remains: irq */
    CHECK (ext_irl[0] == UART_IRQ);
    irqmp_write (ICLEAR, 1 << UART_IRQ);

    CHECK (env.read (RXTX) == 'C'); /* queued, none remain: no irq */
    CHECK (ext_irl[0] == 0);
  }

  SUBCASE ("(current behaviour) reading STATUS raises the interrupt on every "
	   "fresh batch, even a batch of one")
  {
    /* Unlike RXTX, the STATUS case raises the interrupt whenever the
       fetch finds anything at all, with no "more than one" check; a
       single byte is enough.  A second read with the same byte still
       queued takes the "already have one" branch instead, which never
       raises the interrupt at all.  */
    apbuart_env env;
    env.host_send ("Z", 1);

    CHECK ((env.read (STATUS) & STATUS_DR) != 0);
    CHECK (ext_irl[0] == UART_IRQ);
    irqmp_write (ICLEAR, 1 << UART_IRQ);

    CHECK ((env.read (STATUS) & STATUS_DR) != 0);
    CHECK (ext_irl[0] == 0);
  }

  SUBCASE ("spec (18.2 transmitter) a written byte is queued and the "
	   "interrupt is raised")
  {
    /* The prose preceding 18.2.2 (present in the scoped manual, though
       its own heading is one of the pages lost to the picture
       placeholders the file header describes) has a transmitted
       character move into the transmitter FIFO/holding register.
       apbuart_write's RXTX case is the model of that: the byte goes into
       wbufa, and the interrupt is raised on every write.  */
    apbuart_env env;

    env.write (RXTX, 'x');
    CHECK (ext_irl[0] == UART_IRQ);

    apbuart_flush ();

    char c;
    CHECK (env.host_recv (&c, 1) == 1);
    CHECK (c == 'x');
  }

  SUBCASE ("(current behaviour) a full write buffer flushes to the host "
	   "immediately")
  {
    /* wbufa is UARTBUF (1024) bytes.  Filling it exactly takes the
       "buffer has room" branch every time; the next byte finds no room,
       so apbuart_write flushes the full buffer to the host itself before
       queuing the new byte, rather than dropping it or growing it.  */
    apbuart_env env;

    for (int i = 0; i < 1024; i++)
      env.write (RXTX, 'a');
    env.write (RXTX, 'b');

    apbuart_flush ();

    char buf[1025];
    REQUIRE (env.host_recv (buf, sizeof (buf)) == 1025);
    CHECK (buf[0] == 'a');
    CHECK (buf[1023] == 'a');
    CHECK (buf[1024] == 'b');
  }

  SUBCASE ("(current behaviour) the periodic flush event runs and "
	   "reschedules itself")
  {
    /* apbuart_reset (run once by apbuart_env's constructor) arms a
       callback 5000 clocks out that polls the status register and
       flushes.  Running the event queue past that point is the only way
       to reach it, since it is a static function with no other caller.
       Running past the point it reschedules itself for shows it is still
       being called rather than having fired once and stopped.  */
    apbuart_env env;

    env.write (RXTX, 'v');
    env.run (5000);

    char c;
    CHECK (env.host_recv (&c, 1) == 1);
    CHECK (c == 'v');

    env.run (5000);
  }

  SUBCASE ("(current behaviour) add reports the port when verbose")
  {
    /* apbuart_add's own printf is gated on sis_verbose, exercised at both
       settings so the branch is taken both ways.  The plug&play id and
       address it records are not observed by anything a case here can
       reach; what is checked is that the interrupt apbuart.cc actually
       raises stays the hardwired 3 regardless of the irq passed to add,
       which is the "current behaviour" this pins.  */
    apbuart_env env;

    env.core->add (7, 0x80000200, 0xfff);
    sis_verbose = 1;
    env.core->add (7, 0x80000200, 0xfff);
    sis_verbose = 0;

    env.write (RXTX, 'u');
    CHECK (ext_irl[0] == UART_IRQ);
  }
}
