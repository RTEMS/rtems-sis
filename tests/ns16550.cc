/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Tests for the ns16550 UART core in grlib.cc.

   The core is a byte-mapped register file addressed at a 4-byte stride: the
   datasheet's register n (selected by A0-A2 on the real part) sits at byte
   offset 4n.  A case drives it through struct grlib_ipcore with no board, as
   tests/irqmp.cc does for IRQMP, and asserts on what the register file and
   the interrupt line to the PLIC did.

   The expectations come from ref/pc16550d-uart-datasheet.md, the National
   Semiconductor PC16550D datasheet, section 8.0 REGISTERS.  The mid-dot in
   its section numbers (8.1, 8.7, ...) survives the PDF-to-Markdown
   conversion as a stray character, so this file spells them with a plain
   dot instead of quoting the mangled text.

   ns16550_write's offset 0 case calls putchar on the host's real stdout.
   A case that reaches it wraps the call in tests/support.h's
   sis_tests::stdout_capture, the same helper tests/erc32.cc uses for MEC's
   UART, so the byte stays out of the test binary's own terminal and a
   case can assert on exactly what was written.

   The core's own statics, uart_lcr/uart_ie/uart_mcr/uart_fcr and
   ns16550_irq, persist across cases because they are file-local to
   grlib.cc.  ns16550_reset, reached by the fixture on every construction,
   clears the four registers table I lists as cleared by a master reset but
   not ns16550_irq; see the case below that tests reset directly.

   The interrupt path calls plic_irq, which reaches the shared PLIC core.
   A case that exercises it enables only its own IRQ line in the PLIC and
   raises that line's priority off the reserved 0 (FU540-C000 10.3) before
   triggering the UART, so the claim it reads back cannot be confused with
   a bit some other core's case left pending; this keeps the PLIC side of
   the test to the minimum needed to observe the UART's own behaviour, and
   the fixture drains the PLIC before and after every case because the PLIC
   core has no reset.  */

#include "doctest.h"

#include "config.h"
#include "grlibcore.h"
#include "support.h"

#ifndef _WIN32

namespace
{

using sis_tests::stdout_capture;

/* Register offsets, from the byte-offset mapping in the file header: the
   datasheet's register n is at 4n.  */
const uint32 THR = 0x00; /* register 0, write: Transmitter Holding Register */
const uint32 IER = 0x04; /* register 1: Interrupt Enable Register, 8.7 */
const uint32 FCR = 0x08; /* register 2, write: FIFO Control Register, 8.5 */
const uint32 IIR = 0x08; /* register 2, read: Interrupt Identification, 8.6 */
const uint32 LCR = 0x0c; /* register 3: Line Control Register, 8.1 */
const uint32 MCR = 0x10; /* register 4: Modem Control Register, 8.8 */
const uint32 LSR = 0x14; /* register 5: Line Status Register, 8.4 */

/* IIR values from Table IV: no interrupt pending, and the transmitter
   holding register empty interrupt.  */
const uint32 IIR_NONE = 0x01;
const uint32 IIR_THRE = 0x02;

struct ns16550_fixture : sis_tests::grlib_core_fixture
{
  ns16550_fixture () : sis_tests::grlib_core_fixture (&ns16550) { drain (); }

  ~ns16550_fixture () { drain (); }

  /* The UART's interrupt line reaches the shared PLIC core, which has no
     reset, so a case that raised one would leave a pending bit behind for
     whatever runs next.  Claiming until a claim reads zero (FU540-C000
     10.7, 10.8) is the only way back through the register interface, and
     it needs every source lifted off the reserved priority 0 (10.3) and
     enabled first.  Both are put back afterwards.  */
  void
  drain ()
  {
    uint32 value;

    for (int i = 1; i < 32; i++)
      {
	value = 1;
	plic.write (4 * i, &value, 2);
      }
    value = 0xffffffff;
    plic.write (0x2000, &value, 2);

    value = 0;
    plic.write (0x200004, &value, 2); /* completion, forces a recompute */
    for (int tries = 0; tries < 40; tries++)
      {
	uint32 id = 0;

	plic.read (0x200004, &id);
	if (id == 0)
	  break;
	plic.write (0x200004, &id, 2);
      }

    value = 0;
    plic.write (0x2000, &value, 2);
    for (int i = 0; i < 64; i++)
      {
	value = 0;
	plic.write (4 * i, &value, 2);
      }
  }
};

}

TEST_CASE_FIXTURE (ns16550_fixture, "NS16550 add stays quiet by default")
{
  /* The fixture already set sis_verbose to 0.  ns16550_add's report is
     conditional on it; this is the "not verbose" side of that branch.  */
  stdout_capture cap;
  core->add (5, 0x80000100, 0xfff);
  CHECK (cap.str ().empty ());
}

TEST_CASE_FIXTURE (ns16550_fixture,
		   "NS16550 add announces the port and IRQ when verbose")
{
  /* The "verbose" side of the same branch.  */
  sis_verbose = 1;
  stdout_capture cap;
  core->add (5, 0x80000100, 0xfff);
  CHECK (cap.str ().find ("NS16550") != std::string::npos);
}

TEST_CASE_FIXTURE (ns16550_fixture,
		   "NS16550 THR writes a byte to the console when DLAB is "
		   "clear")
{
  /* 8.1: DLAB (LCR bit 7) must be low to reach the Transmitter Holding
     Register; the fixture's reset leaves it low.  The value is also
     masked to a byte, so a set high byte must not appear in the output.  */
  stdout_capture cap;
  write (THR, 0x141); /* 'A' with garbage above bit 7 */
  CHECK (cap.str () == "A");
}

TEST_CASE_FIXTURE (ns16550_fixture,
		   "NS16550 DLAB in the LCR blocks THR and IER access")
{
  /* 8.1: DLAB set steers offsets 0 and 4 to the (unimplemented) divisor
     latches instead of THR and IER, so neither write is expected to take
     effect.  */
  write (LCR, 0x80);

  stdout_capture cap;
  write (THR, 'Z');
  CHECK (cap.str ().empty ());

  write (IER, 0x0f);
  CHECK (read (IER) == 0);
}

TEST_CASE_FIXTURE (ns16550_fixture,
		   "NS16550 all but bit 7 of an LCR write leave DLAB clear")
{
  /* 8.1: bits 6-0 are line characteristics and bit 7 alone is DLAB, so a
     write with every other bit set must still leave THR reachable.  This
     pins the DLAB test to exactly bit 7 rather than "any bit".  */
  write (LCR, 0x7f);

  stdout_capture cap;
  write (THR, 'Q');
  CHECK (cap.str () == "Q");
}

TEST_CASE_FIXTURE (ns16550_fixture,
		   "NS16550 IER write masks to a byte and reads back")
{
  /* 8.7: the four interrupt enables live in the low bits of the IER.
     grlib.cc stores `*data & 0xff`, so a write with the high bits set must
     read back with them stripped.  */
  write (IER, 0x10f);
  CHECK (read (IER) == 0x0f);
}

TEST_CASE_FIXTURE (
    ns16550_fixture,
    "NS16550 register 2 reads the IIR, not the FCR just written")
{
  /* 8.5 makes offset 2 the write-only FIFO Control Register and 8.6 makes a
     read of the same offset the Interrupt Identification Register, computed
     from the pending interrupt state (Table IV) rather than echoing what was
     written.  Table I gives the IIR a master reset value of 0000 0001, and
     8.6 bit 0: "When bit 0 is a logic 1, no interrupt is pending".  */
  CHECK (read (IIR) == IIR_NONE);

  /* 8.6 bits 6 and 7: "These two bits are set when FCR0 = 1", and 8.5 bit 0
     is the FIFO enable.  Writing the FCR must not change bits 3-0.  */
  write (FCR, 0x1c1);
  CHECK (read (IIR) == (0xc0 | IIR_NONE));

  write (FCR, 0);
  CHECK (read (IIR) == IIR_NONE);
}

TEST_CASE_FIXTURE (ns16550_fixture,
		   "NS16550 the IIR reports a pending THRE interrupt (8.6, "
		   "Table IV)")
{
  /* The LSR below is fixed at its reset value, so THRE is always set and the
     transmitter interrupt is pending as soon as ETBEI (8.7, IER bit 1) is
     enabled.  Table IV encodes "Transmitter Holding Register Empty" as IIR
     bits 3-0 = 0010, which also clears bit 0 to say an interrupt is
     pending.  */
  REQUIRE (read (LSR) == 0x60);

  write (IER, 0x02);
  CHECK (read (IIR) == IIR_THRE);

  write (IER, 0x00);
  CHECK (read (IIR) == IIR_NONE);
}

TEST_CASE_FIXTURE (ns16550_fixture,
		   "NS16550 MCR write masks to a byte and reads back")
{
  /* 8.8: OUT 1, OUT 2, DTR, RTS and loopback live in the low bits of the
     MCR.  grlib.cc stores `*data & 0xff`.  */
  write (MCR, 0x2ab);
  CHECK (read (MCR) == 0xab);
}

TEST_CASE_FIXTURE (ns16550_fixture, "NS16550 LSR always reads the power up "
				    "value")
{
  /* 8.4 / Table II: LSR resets to 0110 0000 (THRE and TEMT both set, no
     data ready, no errors).  grlib.cc has no transmitter or receiver model
     behind it and always returns that fixed value.  */
  CHECK (read (LSR) == 0x60);
}

TEST_CASE_FIXTURE (ns16550_fixture, "NS16550 the LCR reads back (8.1)")
{
  /* 8.1: "The programmer can also read the contents of the Line Control
     Register.  The read capability simplifies system programming and
     eliminates the need for separate storage in system memory of the line
     characteristics."  Table I gives it a master reset value of 0000 0000,
     which the fixture's reset leaves in place.  All eight bits are line
     characteristics (Table II), so the whole byte reads back, DLAB
     included.  */
  CHECK (read (LCR) == 0);

  write (LCR, 0x13b); /* 8 bits, 2 stop bits, even parity, DLAB clear */
  CHECK (read (LCR) == 0x3b);

  write (LCR, 0x83);
  CHECK (read (LCR) == 0x83);
}

TEST_CASE_FIXTURE (ns16550_fixture, "NS16550 writing an unmapped offset "
				    "does nothing")
{
  /* No register lives at offset 0x18; the write switch falls through with
     no effect, and so does the read switch.  */
  write (0x18, 0xff);
  CHECK (read (0x18) == 0);
  CHECK (read (IER) == 0);
  CHECK (read (MCR) == 0);
}

TEST_CASE_FIXTURE (
    ns16550_fixture,
    "NS16550 the transmit interrupt needs ETBEI and OUT 2 together")
{
  /* IER bit 1 is ETBEI (8.7) and MCR bit 3 is OUT 2 (8.8); grlib.cc raises
     the PLIC line only when both are set.  Enable this UART's own IRQ
     line in the PLIC first, so the claim below can only report this
     line's bit no matter what any other core's case left pending.  */
  const int irq = 29;
  core->add (irq, 0x80000100, 0xfff);

  uint32 mask = 1u << irq;
  plic.write (0x2000 /* PLIC_IENA, hart 0 */, &mask, 2);
  uint32 prio = 1;
  plic.write (4 * irq /* PLIC_PRIO */, &prio, 2);

  write (IER, 0x02); /* ETBEI, OUT 2 still clear: no interrupt yet */
  write (MCR, 0x08); /* OUT 2 now set too: both conditions true */

  uint32 claim = 0xffffffff;
  plic.read (0x200004 /* PLIC_CLAIM, hart 0 */, &claim);
  CHECK (claim == (uint32) irq);
}

TEST_CASE_FIXTURE (ns16550_fixture,
		   "NS16550 ETBEI alone does not reach the PLIC")
{
  /* OUT 2 (MCR bit 3) is still clear here, so the guard's second operand
     is false and plic_irq must not run.  */
  const int irq = 30;
  core->add (irq, 0x80000100, 0xfff);

  write (IER, 0x02);

  uint32 pending = 0;
  plic.read (0x1000 /* PLIC_IPEND, hart 0 */, &pending);
  CHECK (((pending >> irq) & 1) == 0);
}

TEST_CASE_FIXTURE (ns16550_fixture,
		   "NS16550 OUT 2 alone does not reach the PLIC")
{
  /* ETBEI (IER bit 1) is clear here (the fixture's reset leaves IER at 0),
     so the guard's first operand is false and short-circuits before
     plic_irq ever runs.  */
  const int irq = 31;
  core->add (irq, 0x80000100, 0xfff);

  write (MCR, 0x08);

  uint32 pending = 0;
  plic.read (0x1000 /* PLIC_IPEND, hart 0 */, &pending);
  CHECK (((pending >> irq) & 1) == 0);
}

TEST_CASE_FIXTURE (ns16550_fixture,
		   "NS16550 reset restores the values of Table I")
{
  /* Table I, UART Reset Configuration: a master reset clears the interrupt
     enable, FIFO control, line control and MODEM control registers to
     0000 0000 and leaves the IIR at 0000 0001.  IER is written before LCR
     sets DLAB, so the write actually takes effect and reset is what has to
     clear it.  */
  write (IER, 0x0f);
  write (LCR, 0x80);
  write (MCR, 0x08);
  write (FCR, 0x01);

  core->reset ();

  CHECK (read (IER) == 0);
  CHECK (read (LCR) == 0);
  CHECK (read (MCR) == 0);
  CHECK (read (IIR) == IIR_NONE);

  /* The cleared DLAB makes THR reachable again.  */
  stdout_capture cap;
  write (THR, 'R');
  CHECK (cap.str () == "R");
}

#endif /* !_WIN32 */
