/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Tests for sparc.cc, the SPARC V8 integer unit.

   The core reaches memory only through the memsys vtable, so the instruction
   cases drive it against the flat memory of tests/cpumem.cc rather than a
   board.  A case builds one instruction word, puts the machine in a known
   state and calls the core's dispatch entry directly, so what it asserts on
   is the instruction and nothing else.

   The expectations come from the SPARC Version 8 architecture manual: the
   instruction formats of appendix B, the condition codes of appendix C and
   the trap types of chapter 7.

   The trap cost cases below are older and drive execute_trap directly:
   sparc_execute_trap charges a trap TRAP_C cycles plus three bits of jitter
   taken from ninst and simtime.  The jitter must be masked to 0..7 before the
   base cost is added, so a trap always costs at least TRAP_C.  */

#include "doctest.h"

#include "config.h"
#include "sis.h"
#include "sparc.h"

#include "cpumem.h"

namespace
{

/* execute_trap takes a below-256 trap through the register-window path.  A
   trap number below 17 skips the interrupt acknowledge, so no board callback
   is reached and a zeroed pstate is enough.  */
uint32
trap_icnt (uint64 ninst, uint64 simtime)
{
  struct pstate s = {};

  s.trap = 5;
  s.psr = PSR_ET;
  s.ninst = ninst;
  s.simtime = simtime;

  uint32 saved = ebase.coven;
  ebase.coven = 0;
  sparc32.execute_trap (&s);
  ebase.coven = saved;

  return s.icnt;
}

/* Instruction fields, from the formats of appendix B.  A format 3 instruction
   is op, rd, op3, rs1 and then either rs2 or a signed immediate.  */
uint32
f3r (uint32 op, uint32 rd, uint32 op3, uint32 rs1, uint32 rs2)
{
  return (op << 30) | (rd << 25) | (op3 << 19) | (rs1 << 14) | rs2;
}

uint32
f3i (uint32 op, uint32 rd, uint32 op3, uint32 rs1, int32 simm13)
{
  return (op << 30) | (rd << 25) | (op3 << 19) | (rs1 << 14) | (1 << 13) |
	 ((uint32) simm13 & 0x1fff);
}

/* Format 2: sethi, and a branch with its condition and word displacement.  */
uint32
sethi (uint32 rd, uint32 imm22)
{
  return (rd << 25) | (0x4 << 22) | (imm22 & 0x3fffff);
}

uint32
bicc (uint32 annul, uint32 cond, int32 disp22)
{
  return (annul << 29) | (cond << 25) | (0x2 << 22) |
	 ((uint32) disp22 & 0x3fffff);
}

/* The op field of the instruction groups used here.  */
const uint32 OP_CALL = 1;
const uint32 OP_ARITH = 2;
const uint32 OP_MEM = 3;

/* op3 values from appendix B.  */
const uint32 OP3_ADD = 0x00;
const uint32 OP3_AND = 0x01;
const uint32 OP3_OR = 0x02;
const uint32 OP3_XOR = 0x03;
const uint32 OP3_SUB = 0x04;
const uint32 OP3_ADDX = 0x08;
const uint32 OP3_ADDCC = 0x10;
const uint32 OP3_ANDCC = 0x11;
const uint32 OP3_SUBCC = 0x14;
const uint32 OP3_SLL = 0x25;
const uint32 OP3_SRL = 0x26;
const uint32 OP3_SRA = 0x27;
const uint32 OP3_RDY = 0x28;
const uint32 OP3_WRY = 0x30;
const uint32 OP3_JMPL = 0x38;
const uint32 OP3_LD = 0x00;
const uint32 OP3_LDUB = 0x01;
const uint32 OP3_LDUH = 0x02;
const uint32 OP3_ST = 0x04;
const uint32 OP3_STB = 0x05;
const uint32 OP3_STH = 0x06;

/* The condition field, most significant bit first.  */
const uint32 N = 8, Z = 4, V = 2, C = 1;

/* Drives the integer unit against the flat memory with no board.  */
struct sparc_fixture
{
  int saved_cputype;
  int saved_archtype;
  int saved_verbose;
  const struct memsys *saved_ms;
  const struct cpu_arch *saved_arch;

  sparc_fixture ()
      : saved_cputype (cputype), saved_archtype (archtype),
	saved_verbose (sis_verbose), saved_ms (ms), saved_arch (arch)
  {
    cputype = CPU_ERC32;
    archtype = CPU_SPARC;
    ms = &sis_tests::flatmem;
    arch = &sparc32;
    ebase.freq = 14;
    ebase.simtime = 0;
    ebase.simstart = 0;
    sis_tests::flatmem_clear ();
    reset_all ();
    init_bpt (sregs);
    sis_verbose = 0;

    /* A clear condition field and supervisor mode, so a case only has to set
       what it is about.  */
    /* init_regs leaves the register file alone, so clear it here or a case
       inherits whatever the one before it left.  */
    for (int i = 0; i < 8; i++)
      sregs[0].g[i] = 0;
    for (int i = 0; i < 128; i++)
      sregs[0].r[i] = 0;

    sregs[0].psr = (sregs[0].psr & ~PSR_CC) | PSR_S;
    sregs[0].pc = 0x1000;
    sregs[0].npc = 0x1004;
    sregs[0].trap = 0;
  }

  ~sparc_fixture ()
  {
    sis_verbose = saved_verbose;
    cputype = saved_cputype;
    archtype = saved_archtype;
    ms = saved_ms;
    arch = saved_arch;
  }

  /* Execute one instruction word.  Returns the trap it raised, or zero.  */
  int
  exec (uint32 inst)
  {
    sregs[0].inst = inst;
    sregs[0].icnt = 1;
    sregs[0].trap = 0;
    arch->dispatch_instruction (&sregs[0]);
    return sregs[0].trap;
  }

  /* r0 to r7 are the globals and r8 upwards the current window, which is how
     the core indexes them.  r0 always reads zero, so cases use r1 up.  */
  uint32 *
  reg (int r)
  {
    if (r > 7)
      return &sregs[0].r[(((sregs[0].psr & PSR_CWP) << 4) + r) & 0x7f];
    return &sregs[0].g[r];
  }

  void
  set (int r, uint32 value)
  {
    *reg (r) = value;
  }

  uint32
  get (int r)
  {
    return *reg (r);
  }

  uint32
  icc ()
  {
    return (sregs[0].psr & PSR_CC) >> 20;
  }
};

} // namespace

TEST_CASE ("sparc trap cost never drops below the base cost")
{
  /* Before the parentheses were corrected the mask applied to TRAP_C + jitter
     rather than to the jitter alone, so a trap could cost less than TRAP_C,
     or nothing at all.  Walk a whole jitter sequence over one processor.  */
  struct pstate s = {};
  bool seen_above_base = false;

  for (int i = 0; i < 64; ++i)
    {
      s.icnt = 0;
      s.trap = 5;
      s.psr = PSR_ET;
      sparc32.execute_trap (&s);

      CHECK (s.icnt >= TRAP_C);
      CHECK (s.icnt <= TRAP_C + 7);

      if (s.icnt > TRAP_C)
	seen_above_base = true;
    }

  /* The jitter is meant to vary, not to sit at zero.  */
  CHECK (seen_above_base);
}

TEST_CASE ("sparc trap jitter does not follow the simulated time")
{
  /* Test code which hunts for a race by varying a busy wait moves both ninst
     and simtime.  A jitter derived from them would track the search variable
     instead of being independent of it, and the race window would never be
     found.  Two processors at the same point of the jitter sequence must be
     charged the same, whatever their time and instruction count.  */
  CHECK (trap_icnt (0, 0) == trap_icnt (12345, 67890));
  CHECK (trap_icnt (5, 0) == trap_icnt (0, 5));
  CHECK (trap_icnt (7, 7) == trap_icnt (1, 2));
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC add takes an immediate or a register")
{
  set (1, 100);
  set (2, 23);

  CHECK (exec (f3r (OP_ARITH, 3, OP3_ADD, 1, 2)) == 0);
  CHECK (get (3) == 123);

  CHECK (exec (f3i (OP_ARITH, 3, OP3_ADD, 1, -1)) == 0);
  CHECK (get (3) == 99);

  /* An immediate is sign extended from thirteen bits.  */
  CHECK (exec (f3i (OP_ARITH, 3, OP3_ADD, 1, -4096)) == 0);
  CHECK (get (3) == 100 - 4096);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC r0 reads zero and discards writes")
{
  set (1, 0x1234);

  CHECK (exec (f3i (OP_ARITH, 0, OP3_ADD, 1, 1)) == 0);
  CHECK (get (0) == 0);

  CHECK (exec (f3r (OP_ARITH, 2, OP3_ADD, 0, 0)) == 0);
  CHECK (get (2) == 0);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC add without cc leaves the codes alone")
{
  /* The result is zero with a carry out, but add does not write the
     condition codes.  */
  set (1, 0xffffffff);
  set (2, 1);

  CHECK (exec (f3r (OP_ARITH, 3, OP3_ADD, 1, 2)) == 0);
  CHECK (get (3) == 0);
  CHECK (icc () == 0);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC addcc sets the codes of appendix C")
{
  /* Zero, with a carry out of the most significant bit.  */
  set (1, 0xffffffff);
  set (2, 1);
  CHECK (exec (f3r (OP_ARITH, 3, OP3_ADDCC, 1, 2)) == 0);
  CHECK (get (3) == 0);
  CHECK (icc () == (Z | C));

  /* Negative, with neither carry nor overflow.  */
  set (1, 0);
  CHECK (exec (f3i (OP_ARITH, 3, OP3_ADDCC, 1, -1)) == 0);
  CHECK (get (3) == 0xffffffff);
  CHECK (icc () == N);

  /* Two positives whose sum will not fit: negative and overflow.  */
  set (1, 0x7fffffff);
  set (2, 1);
  CHECK (exec (f3r (OP_ARITH, 3, OP3_ADDCC, 1, 2)) == 0);
  CHECK (get (3) == 0x80000000);
  CHECK (icc () == (N | V));

  /* Two negatives whose sum does fit: zero, overflow and carry.  */
  set (1, 0x80000000);
  set (2, 0x80000000);
  CHECK (exec (f3r (OP_ARITH, 3, OP3_ADDCC, 1, 2)) == 0);
  CHECK (get (3) == 0);
  CHECK (icc () == (Z | V | C));
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC subcc sets the codes of appendix C")
{
  /* Equal operands: zero, and no borrow.  */
  set (1, 5);
  set (2, 5);
  CHECK (exec (f3r (OP_ARITH, 3, OP3_SUBCC, 1, 2)) == 0);
  CHECK (get (3) == 0);
  CHECK (icc () == Z);

  /* A borrow sets the carry.  */
  set (1, 4);
  CHECK (exec (f3r (OP_ARITH, 3, OP3_SUBCC, 1, 2)) == 0);
  CHECK (get (3) == 0xffffffff);
  CHECK (icc () == (N | C));

  /* The most negative value less one overflows.  */
  set (1, 0x80000000);
  set (2, 1);
  CHECK (exec (f3r (OP_ARITH, 3, OP3_SUBCC, 1, 2)) == 0);
  CHECK (get (3) == 0x7fffffff);
  CHECK (icc () == V);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC sub and addx use the carry")
{
  set (1, 30);
  set (2, 12);
  CHECK (exec (f3r (OP_ARITH, 3, OP3_SUB, 1, 2)) == 0);
  CHECK (get (3) == 18);

  set (1, 10);
  set (2, 20);
  sregs[0].psr &= ~PSR_C;
  CHECK (exec (f3r (OP_ARITH, 3, OP3_ADDX, 1, 2)) == 0);
  CHECK (get (3) == 30);

  sregs[0].psr |= PSR_C;
  CHECK (exec (f3r (OP_ARITH, 3, OP3_ADDX, 1, 2)) == 0);
  CHECK (get (3) == 31);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC logical operations")
{
  set (1, 0xf0f0f0f0);
  set (2, 0x00ffff00);

  CHECK (exec (f3r (OP_ARITH, 3, OP3_AND, 1, 2)) == 0);
  CHECK (get (3) == 0x00f0f000);

  CHECK (exec (f3r (OP_ARITH, 3, OP3_OR, 1, 2)) == 0);
  CHECK (get (3) == 0xf0fffff0);

  CHECK (exec (f3r (OP_ARITH, 3, OP3_XOR, 1, 2)) == 0);
  CHECK (get (3) == 0xf00f0ff0);

  /* A logical operation never sets carry or overflow.  */
  set (1, 0xf0000000);
  CHECK (exec (f3i (OP_ARITH, 3, OP3_ANDCC, 1, 0)) == 0);
  CHECK (get (3) == 0);
  CHECK (icc () == Z);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC shifts")
{
  set (1, 0x80000001);

  CHECK (exec (f3i (OP_ARITH, 3, OP3_SLL, 1, 4)) == 0);
  CHECK (get (3) == 0x00000010);

  CHECK (exec (f3i (OP_ARITH, 3, OP3_SRL, 1, 4)) == 0);
  CHECK (get (3) == 0x08000000);

  /* An arithmetic shift copies the sign.  */
  CHECK (exec (f3i (OP_ARITH, 3, OP3_SRA, 1, 4)) == 0);
  CHECK (get (3) == 0xf8000000);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC sethi loads the upper bits")
{
  CHECK (exec (sethi (3, 0x3fffff)) == 0);
  CHECK (get (3) == 0xfffffc00);

  /* sethi with rd and immediate both zero is the nop of B.10.  */
  CHECK (exec (sethi (0, 0)) == 0);
  CHECK (get (0) == 0);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC y register round-trips")
{
  set (1, 0xdeadbeef);

  CHECK (exec (f3i (OP_ARITH, 0, OP3_WRY, 1, 0)) == 0);
  CHECK (sregs[0].y == 0xdeadbeef);

  CHECK (exec (f3r (OP_ARITH, 3, OP3_RDY, 0, 0)) == 0);
  CHECK (get (3) == 0xdeadbeef);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC word load and store")
{
  const uint32 addr = 0x2000;

  set (1, addr);
  set (2, 0x12345678);

  CHECK (exec (f3i (OP_MEM, 2, OP3_ST, 1, 0)) == 0);
  CHECK (sis_tests::flatmem_peek (addr) == 0x12345678);

  CHECK (exec (f3i (OP_MEM, 3, OP3_LD, 1, 0)) == 0);
  CHECK (get (3) == 0x12345678);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC byte and half-word access")
{
  const uint32 addr = 0x2000;

  set (1, addr);
  set (2, 0xaabbccdd);
  CHECK (exec (f3i (OP_MEM, 2, OP3_ST, 1, 0)) == 0);

  /* SPARC is big endian, so the first byte of the word is the most
     significant one.  */
  CHECK (exec (f3i (OP_MEM, 3, OP3_LDUB, 1, 0)) == 0);
  CHECK (get (3) == 0xaa);

  CHECK (exec (f3i (OP_MEM, 3, OP3_LDUB, 1, 3)) == 0);
  CHECK (get (3) == 0xdd);

  CHECK (exec (f3i (OP_MEM, 3, OP3_LDUH, 1, 2)) == 0);
  CHECK (get (3) == 0xccdd);

  /* A byte store touches only its own byte.  */
  set (2, 0x99);
  CHECK (exec (f3i (OP_MEM, 2, OP3_STB, 1, 1)) == 0);
  CHECK (sis_tests::flatmem_peek (addr) == 0xaa99ccdd);

  set (2, 0x1122);
  CHECK (exec (f3i (OP_MEM, 2, OP3_STH, 1, 2)) == 0);
  CHECK (sis_tests::flatmem_peek (addr) == 0xaa991122);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC unaligned access traps")
{
  set (1, 0x2001);

  /* A word access must be word aligned and a half-word access half-word
     aligned, or the memory address not aligned trap is taken.  */
  CHECK (exec (f3i (OP_MEM, 3, OP3_LD, 1, 0)) == TRAP_UNALI);
  CHECK (exec (f3i (OP_MEM, 3, OP3_ST, 1, 0)) == TRAP_UNALI);
  CHECK (exec (f3i (OP_MEM, 3, OP3_LDUH, 1, 0)) == TRAP_UNALI);

  /* A byte access has no alignment requirement.  */
  CHECK (exec (f3i (OP_MEM, 3, OP3_LDUB, 1, 0)) == 0);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC access outside memory traps")
{
  set (1, sis_tests::FLATMEM_SIZE + 0x1000);

  CHECK (exec (f3i (OP_MEM, 3, OP3_LD, 1, 0)) == TRAP_DEXC);
  CHECK (sis_tests::flatmem_faults () > 0);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC call writes the return address")
{
  /* A call writes its own address, which is the program counter, into r15,
     and branches to that address plus the displacement in words.  */
  CHECK (exec ((OP_CALL << 30) | 4) == 0);
  CHECK (get (15) == 0x1000);
  CHECK (sregs[0].npc == 0x1010);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC branch taken and not taken")
{
  /* Branch on equal with the zero bit set is taken, and the displacement is
     in words from the branch.  */
  sregs[0].psr |= PSR_Z;
  CHECK (exec (bicc (0, BICC_BE, 4)) == 0);
  CHECK (sregs[0].npc == 0x1010);

  /* The same branch with the zero bit clear falls through.  */
  sregs[0].psr &= ~PSR_Z;
  sregs[0].pc = 0x1000;
  sregs[0].npc = 0x1004;
  CHECK (exec (bicc (0, BICC_BE, 4)) == 0);
  CHECK (sregs[0].npc == 0x1008);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC branch always and never")
{
  CHECK (exec (bicc (0, BICC_BA, 8)) == 0);
  CHECK (sregs[0].npc == 0x1020);

  sregs[0].pc = 0x1000;
  sregs[0].npc = 0x1004;
  CHECK (exec (bicc (0, BICC_BN, 8)) == 0);
  CHECK (sregs[0].npc == 0x1008);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC jmpl jumps and saves")
{
  set (1, 0x3000);

  /* jmpl saves the address of the jump itself, which is the program
     counter.  */
  CHECK (exec (f3i (OP_ARITH, 3, OP3_JMPL, 1, 0)) == 0);
  CHECK (get (3) == 0x1000);
  CHECK (sregs[0].npc == 0x3000);
}
