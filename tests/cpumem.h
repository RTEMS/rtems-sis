/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* A flat memory for driving a CPU core without a board.

   The cores reach memory only through the memsys vtable, so a case can
   execute instructions against this instead of a board: there are no
   peripheral registers, no waitstate model and no interrupt controller, so
   what a case sets up is exactly what the instruction sees.

   Memory is held the way a board holds it, as bytes in host word order with
   sub-word addresses swapped by arch->bswap, so one window serves a big
   endian and a little endian core without a rebuild.  */

#ifndef SIS_TESTS_CPUMEM_H
#define SIS_TESTS_CPUMEM_H

#include "sis.h"

namespace sis_tests
{

/* The window mapped at address zero.  An access outside it takes a memory
   exception, which is how a case reaches the fault paths of a core.  */
const uint32 FLATMEM_SIZE = 0x10000;

/* The memsys to point ms at.  */
extern const struct memsys flatmem;

/* Fill the whole window with zero.  */
void flatmem_clear ();

/* Read and write a word without going through the core, for setting up an
   instruction stream or asserting on what a store left behind.  ADDR must be
   word aligned and inside the window.  */
void flatmem_poke (uint32 addr, uint32 value);
uint32 flatmem_peek (uint32 addr);

/* How many memory exceptions the window has reported since it was
   cleared.  */
int flatmem_faults ();

/* Refuse every write to the word at ADDR, while reads of it still succeed.
   A board can protect a word it lets a program read, and the atomic
   instructions have a fault path for exactly that case which no address of a
   plain window reaches.  An address of ~0 turns the refusal off, which
   flatmem_clear also does.  */
void flatmem_fail_write (uint32 addr);

} /* namespace sis_tests */

#endif
