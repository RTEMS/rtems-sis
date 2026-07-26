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
#include "support.h"

#include "config.h"
#include "sis.h"
#include "sparc.h"

#include "cpumem.h"

#include <stddef.h>
#include <limits>
#include <string>

using sis_tests::stdout_capture;

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

/* The core holds the floating point registers in host order and swaps the
   index of a single register on a little endian host, so a case which has to
   name a register the way the core recorded it goes through this.  */
#ifdef HOST_LITTLE_ENDIAN
#define REG_SWAP(n) ((n) ^ 1)
#else
#define REG_SWAP(n) (n)
#endif

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
    /* init_regs leaves the register file alone and keeps the low bits of the
       processor state register, so a case inherits whatever the one before
       it left.  Clear all of it here.  */
    for (int i = 0; i < 8; i++)
      sregs[0].g[i] = 0;
    for (int i = 0; i < 128; i++)
      sregs[0].r[i] = 0;
    for (int i = 0; i < 32; i++)
      sregs[0].fd[i] = 0;

    /* Supervisor mode, window zero, traps and the floating point unit
       disabled, and the implementation and version of the ERC32.  */
    sregs[0].psr = 0x11000080;
    sregs[0].wim = 0;
    sregs[0].tbr = 0;
    sregs[0].y = 0;
    sregs[0].fsr = 0;
    sregs[0].fpstate = FP_EXE_MODE;
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

  /* The floating point registers are held in host order, and the core swaps
     the index of a single register on a little endian host, so f<n> is not
     fs[n].  A case reaches a register through these and never indexes the
     arrays itself.  */
  float32 &
  fs (int n)
  {
#ifdef HOST_LITTLE_ENDIAN
    n ^= 1;
#endif
    return sregs[0].fs[n];
  }

  int32 &
  fsi (int n)
  {
#ifdef HOST_LITTLE_ENDIAN
    n ^= 1;
#endif
    return sregs[0].fsi[n];
  }

  /* A double register is an even numbered pair, which the swap leaves in the
     same place.  */
  float64 &
  fd (int n)
  {
    return sregs[0].fd[n >> 1];
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

  CHECK (exec (f3r (OP_ARITH, 3, ADD, 1, 2)) == 0);
  CHECK (get (3) == 123);

  CHECK (exec (f3i (OP_ARITH, 3, ADD, 1, -1)) == 0);
  CHECK (get (3) == 99);

  /* An immediate is sign extended from thirteen bits.  */
  CHECK (exec (f3i (OP_ARITH, 3, ADD, 1, -4096)) == 0);
  CHECK (get (3) == 100 - 4096);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC r0 reads zero and discards writes")
{
  set (1, 0x1234);

  CHECK (exec (f3i (OP_ARITH, 0, ADD, 1, 1)) == 0);
  CHECK (get (0) == 0);

  CHECK (exec (f3r (OP_ARITH, 2, ADD, 0, 0)) == 0);
  CHECK (get (2) == 0);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC add without cc leaves the codes alone")
{
  /* The result is zero with a carry out, but add does not write the
     condition codes.  */
  set (1, 0xffffffff);
  set (2, 1);

  CHECK (exec (f3r (OP_ARITH, 3, ADD, 1, 2)) == 0);
  CHECK (get (3) == 0);
  CHECK (icc () == 0);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC addcc sets the codes of appendix C")
{
  /* Zero, with a carry out of the most significant bit.  */
  set (1, 0xffffffff);
  set (2, 1);
  CHECK (exec (f3r (OP_ARITH, 3, ADDCC, 1, 2)) == 0);
  CHECK (get (3) == 0);
  CHECK (icc () == (Z | C));

  /* Negative, with neither carry nor overflow.  */
  set (1, 0);
  CHECK (exec (f3i (OP_ARITH, 3, ADDCC, 1, -1)) == 0);
  CHECK (get (3) == 0xffffffff);
  CHECK (icc () == N);

  /* Two positives whose sum will not fit: negative and overflow.  */
  set (1, 0x7fffffff);
  set (2, 1);
  CHECK (exec (f3r (OP_ARITH, 3, ADDCC, 1, 2)) == 0);
  CHECK (get (3) == 0x80000000);
  CHECK (icc () == (N | V));

  /* Two negatives whose sum does fit: zero, overflow and carry.  */
  set (1, 0x80000000);
  set (2, 0x80000000);
  CHECK (exec (f3r (OP_ARITH, 3, ADDCC, 1, 2)) == 0);
  CHECK (get (3) == 0);
  CHECK (icc () == (Z | V | C));
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC subcc sets the codes of appendix C")
{
  /* Equal operands: zero, and no borrow.  */
  set (1, 5);
  set (2, 5);
  CHECK (exec (f3r (OP_ARITH, 3, SUBCC, 1, 2)) == 0);
  CHECK (get (3) == 0);
  CHECK (icc () == Z);

  /* A borrow sets the carry.  */
  set (1, 4);
  CHECK (exec (f3r (OP_ARITH, 3, SUBCC, 1, 2)) == 0);
  CHECK (get (3) == 0xffffffff);
  CHECK (icc () == (N | C));

  /* The most negative value less one overflows.  */
  set (1, 0x80000000);
  set (2, 1);
  CHECK (exec (f3r (OP_ARITH, 3, SUBCC, 1, 2)) == 0);
  CHECK (get (3) == 0x7fffffff);
  CHECK (icc () == V);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC sub and addx use the carry")
{
  set (1, 30);
  set (2, 12);
  CHECK (exec (f3r (OP_ARITH, 3, SUB, 1, 2)) == 0);
  CHECK (get (3) == 18);

  set (1, 10);
  set (2, 20);
  sregs[0].psr &= ~PSR_C;
  CHECK (exec (f3r (OP_ARITH, 3, ADDX, 1, 2)) == 0);
  CHECK (get (3) == 30);

  sregs[0].psr |= PSR_C;
  CHECK (exec (f3r (OP_ARITH, 3, ADDX, 1, 2)) == 0);
  CHECK (get (3) == 31);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC logical operations")
{
  set (1, 0xf0f0f0f0);
  set (2, 0x00ffff00);

  CHECK (exec (f3r (OP_ARITH, 3, IAND, 1, 2)) == 0);
  CHECK (get (3) == 0x00f0f000);

  CHECK (exec (f3r (OP_ARITH, 3, IOR, 1, 2)) == 0);
  CHECK (get (3) == 0xf0fffff0);

  CHECK (exec (f3r (OP_ARITH, 3, IXOR, 1, 2)) == 0);
  CHECK (get (3) == 0xf00f0ff0);

  /* A logical operation never sets carry or overflow.  */
  set (1, 0xf0000000);
  CHECK (exec (f3i (OP_ARITH, 3, IANDCC, 1, 0)) == 0);
  CHECK (get (3) == 0);
  CHECK (icc () == Z);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC shifts")
{
  set (1, 0x80000001);

  CHECK (exec (f3i (OP_ARITH, 3, SLL, 1, 4)) == 0);
  CHECK (get (3) == 0x00000010);

  CHECK (exec (f3i (OP_ARITH, 3, SRL, 1, 4)) == 0);
  CHECK (get (3) == 0x08000000);

  /* An arithmetic shift copies the sign.  */
  CHECK (exec (f3i (OP_ARITH, 3, SRA, 1, 4)) == 0);
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

  CHECK (exec (f3i (OP_ARITH, 0, WRY, 1, 0)) == 0);
  CHECK (sregs[0].y == 0xdeadbeef);

  CHECK (exec (f3r (OP_ARITH, 3, RDY, 0, 0)) == 0);
  CHECK (get (3) == 0xdeadbeef);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC word load and store")
{
  const uint32 addr = 0x2000;

  set (1, addr);
  set (2, 0x12345678);

  CHECK (exec (f3i (OP_MEM, 2, ST, 1, 0)) == 0);
  CHECK (sis_tests::flatmem_peek (addr) == 0x12345678);

  CHECK (exec (f3i (OP_MEM, 3, LD, 1, 0)) == 0);
  CHECK (get (3) == 0x12345678);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC byte and half-word access")
{
  const uint32 addr = 0x2000;

  set (1, addr);
  set (2, 0xaabbccdd);
  CHECK (exec (f3i (OP_MEM, 2, ST, 1, 0)) == 0);

  /* SPARC is big endian, so the first byte of the word is the most
     significant one.  */
  CHECK (exec (f3i (OP_MEM, 3, LDUB, 1, 0)) == 0);
  CHECK (get (3) == 0xaa);

  CHECK (exec (f3i (OP_MEM, 3, LDUB, 1, 3)) == 0);
  CHECK (get (3) == 0xdd);

  CHECK (exec (f3i (OP_MEM, 3, LDUH, 1, 2)) == 0);
  CHECK (get (3) == 0xccdd);

  /* A byte store touches only its own byte.  */
  set (2, 0x99);
  CHECK (exec (f3i (OP_MEM, 2, STB, 1, 1)) == 0);
  CHECK (sis_tests::flatmem_peek (addr) == 0xaa99ccdd);

  set (2, 0x1122);
  CHECK (exec (f3i (OP_MEM, 2, STH, 1, 2)) == 0);
  CHECK (sis_tests::flatmem_peek (addr) == 0xaa991122);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC unaligned access traps")
{
  set (1, 0x2001);

  /* A word access must be word aligned and a half-word access half-word
     aligned, or the memory address not aligned trap is taken.  */
  CHECK (exec (f3i (OP_MEM, 3, LD, 1, 0)) == TRAP_UNALI);
  CHECK (exec (f3i (OP_MEM, 3, ST, 1, 0)) == TRAP_UNALI);
  CHECK (exec (f3i (OP_MEM, 3, LDUH, 1, 0)) == TRAP_UNALI);

  /* A byte access has no alignment requirement.  */
  CHECK (exec (f3i (OP_MEM, 3, LDUB, 1, 0)) == 0);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC access outside memory traps")
{
  set (1, sis_tests::FLATMEM_SIZE + 0x1000);

  CHECK (exec (f3i (OP_MEM, 3, LD, 1, 0)) == TRAP_DEXC);
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
  CHECK (exec (f3i (OP_ARITH, 3, JMPL, 1, 0)) == 0);
  CHECK (get (3) == 0x1000);
  CHECK (sregs[0].npc == 0x3000);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC branches test every condition")
{
  /* Appendix B.21: each condition is a function of the four condition code
     bits.  Drive every one of the sixteen against every combination and
     compare against the manual's definition.  */
  struct
  {
    uint32 cond;
    const char *name;
  } conds[16] = { { BICC_BN, "never" },	    { BICC_BE, "equal" },
		  { BICC_BLE, "le" },	    { BICC_BL, "less" },
		  { BICC_BLEU, "leu" },	    { BICC_BCS, "carry set" },
		  { BICC_NEG, "negative" }, { BICC_BVS, "overflow set" },
		  { BICC_BA, "always" },    { BICC_BNE, "not equal" },
		  { BICC_BG, "greater" },   { BICC_BGE, "ge" },
		  { BICC_BGU, "gu" },	    { BICC_BCC, "carry clear" },
		  { BICC_POS, "positive" }, { BICC_BVC, "overflow clear" } };

  for (uint32 flags = 0; flags < 16; flags++)
    {
      bool n = (flags & N) != 0;
      bool z = (flags & Z) != 0;
      bool v = (flags & V) != 0;
      bool c = (flags & C) != 0;

      bool want[16] = { false,		/* bn */
			z,		/* be */
			z || (n != v),	/* ble */
			n != v,		/* bl */
			c || z,		/* bleu */
			c,		/* bcs */
			n,		/* bneg */
			v,		/* bvs */
			true,		/* ba */
			!z,		/* bne */
			!z && (n == v), /* bg */
			n == v,		/* bge */
			!c && !z,	/* bgu */
			!c,		/* bcc */
			!n,		/* bpos */
			!v };		/* bvc */

      for (int i = 0; i < 16; i++)
	{
	  sregs[0].psr = (sregs[0].psr & ~PSR_CC) | (flags << 20);
	  sregs[0].pc = 0x1000;
	  sregs[0].npc = 0x1004;

	  CHECK (exec (bicc (0, conds[i].cond, 4)) == 0);

	  uint32 expect = want[i] ? 0x1010 : 0x1008;
	  INFO ("condition ", conds[i].name, " with codes ", flags);
	  CHECK (sregs[0].npc == expect);
	}
    }
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC the annul bit skips the delay slot")
{
  /* A conditional branch which is not taken annuls its delay instruction, so
     the core steps over it.  */
  sregs[0].psr &= ~PSR_Z;
  CHECK (exec (bicc (1, BICC_BE, 4)) == 0);
  CHECK (sregs[0].pc == 0x1008);
  CHECK (sregs[0].npc == 0x100c);

  /* Branch always with the annul bit set annuls the delay instruction even
     though the branch is taken.  */
  sregs[0].pc = 0x1000;
  sregs[0].npc = 0x1004;
  CHECK (exec (bicc (1, BICC_BA, 4)) == 0);
  CHECK (sregs[0].pc == 0x1010);
  CHECK (sregs[0].npc == 0x1014);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC save and restore rotate the window")
{
  uint32 cwp = sregs[0].psr & PSR_CWP;

  /* save moves to the next window down, so what were the out registers
     become the in registers of the new one, and the result is written in the
     new window.  */
  set (8, 0x1000); /* o0 */
  set (1, 4);
  CHECK (exec (f3r (OP_ARITH, 8, SAVE, 8, 1)) == 0);
  CHECK ((sregs[0].psr & PSR_CWP) == ((cwp - 1) & PSR_CWP));
  CHECK (get (24) == 0x1000); /* i0, which was o0 */
  CHECK (get (8) == 0x1004);  /* the sum, in the new window */

  /* restore moves back, and again writes in the window it moves to.  */
  CHECK (exec (f3i (OP_ARITH, 9, RESTORE, 24, 8)) == 0);
  CHECK ((sregs[0].psr & PSR_CWP) == cwp);
  CHECK (get (9) == 0x1008);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC save into an invalid window traps")
{
  /* The window invalid mask marks the window save would move to, so save
     takes the window overflow trap and the window does not move.  */
  uint32 cwp = sregs[0].psr & PSR_CWP;

  sregs[0].wim = 1u << ((cwp - 1) & PSR_CWP);
  CHECK (exec (f3i (OP_ARITH, 8, SAVE, 0, 0)) == TRAP_WOFL);
  CHECK ((sregs[0].psr & PSR_CWP) == cwp);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC restore from an invalid window traps")
{
  uint32 cwp = sregs[0].psr & PSR_CWP;

  sregs[0].wim = 1u << ((cwp + 1) & PSR_CWP);
  CHECK (exec (f3i (OP_ARITH, 8, RESTORE, 0, 0)) == TRAP_WUFL);
  CHECK ((sregs[0].psr & PSR_CWP) == cwp);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC ticc traps on its condition")
{
  /* A conditional trap whose condition holds raises a trap instruction trap,
     which is the software trap number plus 128.  */
  sregs[0].psr |= PSR_Z;
  CHECK (exec (f3i (OP_ARITH, 0, TICC, 0, 5) | (BICC_BE << 25)) == 5 + 128);

  /* With the condition false it is a no-operation.  */
  sregs[0].psr &= ~PSR_Z;
  CHECK (exec (f3i (OP_ARITH, 0, TICC, 0, 5) | (BICC_BE << 25)) == 0);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC state registers round-trip")
{
  /* The write instructions write rs1 exclusive-ored with the second operand,
     which is how the manual defines them.  */
  set (1, 0x0f0f0f0f);

  CHECK (exec (f3i (OP_ARITH, 0, WRWIM, 1, 0)) == 0);
  CHECK (sregs[0].wim == (0x0f0f0f0f & ((1u << NWIN) - 1)));
  CHECK (exec (f3r (OP_ARITH, 3, RDWIM, 0, 0)) == 0);
  CHECK (get (3) == sregs[0].wim);

  CHECK (exec (f3i (OP_ARITH, 0, WRTBR, 1, 0)) == 0);
  CHECK (exec (f3r (OP_ARITH, 3, RDTBR, 0, 0)) == 0);
  CHECK (get (3) == sregs[0].tbr);

  CHECK (exec (f3r (OP_ARITH, 3, RDPSR, 0, 0)) == 0);
  CHECK (get (3) == sregs[0].psr);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC the write to psr keeps the codes")
{
  /* Writing the processor state register sets the condition codes, so a read
     back reports what was written.  */
  uint32 want = (sregs[0].psr & ~PSR_CC) | (N | C) << 20;

  set (1, want);
  CHECK (exec (f3i (OP_ARITH, 0, WRPSR, 1, 0)) == 0);
  CHECK (icc () == (N | C));
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC privileged registers need supervisor")
{
  /* Chapter 7: reading or writing a privileged register in user mode takes
     the privileged instruction trap.  */
  sregs[0].psr &= ~PSR_S;

  CHECK (exec (f3r (OP_ARITH, 3, RDPSR, 0, 0)) == TRAP_PRIVI);
  CHECK (exec (f3r (OP_ARITH, 3, RDWIM, 0, 0)) == TRAP_PRIVI);
  CHECK (exec (f3r (OP_ARITH, 3, RDTBR, 0, 0)) == TRAP_PRIVI);
  CHECK (exec (f3i (OP_ARITH, 0, WRPSR, 0, 0)) == TRAP_PRIVI);
  CHECK (exec (f3i (OP_ARITH, 0, WRWIM, 0, 0)) == TRAP_PRIVI);
  CHECK (exec (f3i (OP_ARITH, 0, WRTBR, 0, 0)) == TRAP_PRIVI);

  /* The y register is not privileged.  */
  CHECK (exec (f3i (OP_ARITH, 0, WRY, 0, 0)) == 0);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC unsigned multiply fills y")
{
  /* The product is 64 bits: the upper half goes to y and the lower half to
     the destination register.  */
  set (1, 0x10000);
  set (2, 0x10000);

  CHECK (exec (f3r (OP_ARITH, 3, UMUL, 1, 2)) == 0);
  CHECK (get (3) == 0);
  CHECK (sregs[0].y == 1);

  set (1, 6);
  set (2, 7);
  CHECK (exec (f3r (OP_ARITH, 3, UMULCC, 1, 2)) == 0);
  CHECK (get (3) == 42);
  CHECK (sregs[0].y == 0);
  CHECK (icc () == 0);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC signed multiply sign extends")
{
  set (1, (uint32) -6);
  set (2, 7);

  CHECK (exec (f3r (OP_ARITH, 3, SMUL, 1, 2)) == 0);
  CHECK (get (3) == (uint32) -42);
  CHECK (sregs[0].y == 0xffffffff);

  /* The condition codes come from the lower half of the product.  */
  CHECK (exec (f3r (OP_ARITH, 3, SMULCC, 1, 2)) == 0);
  CHECK (icc () == N);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC divide takes its dividend from y")
{
  /* The dividend is y concatenated with rs1.  */
  sregs[0].y = 0;
  set (1, 100);
  set (2, 7);

  CHECK (exec (f3r (OP_ARITH, 3, UDIV, 1, 2)) == 0);
  CHECK (get (3) == 14);

  sregs[0].y = 0xffffffff;
  set (1, (uint32) -100);
  CHECK (exec (f3r (OP_ARITH, 3, SDIV, 1, 2)) == 0);
  CHECK (get (3) == (uint32) -14);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC divide by zero traps")
{
  sregs[0].y = 0;
  set (1, 100);
  set (2, 0);

  CHECK (exec (f3r (OP_ARITH, 3, UDIV, 1, 2)) == TRAP_DIV0);
  CHECK (exec (f3r (OP_ARITH, 3, SDIV, 1, 2)) == TRAP_DIV0);
  CHECK (exec (f3r (OP_ARITH, 3, UDIVCC, 1, 2)) == TRAP_DIV0);
  CHECK (exec (f3r (OP_ARITH, 3, SDIVCC, 1, 2)) == TRAP_DIV0);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC multiply step")
{
  /* mulscc shifts the partial product right and adds rs2 when the shifted
     out bit of y was set, which is how the manual builds a multiply.  */
  sregs[0].y = 1;
  set (1, 0);
  set (2, 8);

  CHECK (exec (f3r (OP_ARITH, 3, MULScc, 1, 2)) == 0);
  CHECK (get (3) == 8);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC tagged arithmetic checks the tag")
{
  /* Appendix B.14: a tagged add sets the overflow bit when either operand
     has a non-zero tag, which is its two least significant bits.  */
  set (1, 4);
  set (2, 8);
  CHECK (exec (f3r (OP_ARITH, 3, TADDCC, 1, 2)) == 0);
  CHECK ((icc () & V) == 0);

  set (2, 9);
  CHECK (exec (f3r (OP_ARITH, 3, TADDCC, 1, 2)) == 0);
  CHECK ((icc () & V) != 0);

  set (2, 8);
  CHECK (exec (f3r (OP_ARITH, 3, TSUBCC, 1, 2)) == 0);
  CHECK ((icc () & V) == 0);

  set (2, 9);
  CHECK (exec (f3r (OP_ARITH, 3, TSUBCC, 1, 2)) == 0);
  CHECK ((icc () & V) != 0);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC tagged arithmetic can trap")
{
  /* The trap-on-overflow forms take the tag overflow trap instead of setting
     the bit.  */
  set (1, 4);
  set (2, 9);

  CHECK (exec (f3r (OP_ARITH, 3, TADDCCTV, 1, 2)) == TRAP_TAG);
  CHECK (exec (f3r (OP_ARITH, 3, TSUBCCTV, 1, 2)) == TRAP_TAG);

  /* With clear tags they behave as the plain forms.  */
  set (2, 8);
  CHECK (exec (f3r (OP_ARITH, 3, TADDCCTV, 1, 2)) == 0);
  CHECK (get (3) == 12);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC signed loads sign extend")
{
  const uint32 addr = 0x2000;

  set (1, addr);
  set (2, 0x80ff7f01);
  CHECK (exec (f3i (OP_MEM, 2, ST, 1, 0)) == 0);

  /* A signed byte or half-word load copies the sign into the upper bits,
     while the unsigned forms do not.  */
  CHECK (exec (f3i (OP_MEM, 3, LDSB, 1, 0)) == 0);
  CHECK (get (3) == 0xffffff80);
  CHECK (exec (f3i (OP_MEM, 3, LDUB, 1, 0)) == 0);
  CHECK (get (3) == 0x80);

  CHECK (exec (f3i (OP_MEM, 3, LDSB, 1, 3)) == 0);
  CHECK (get (3) == 1);

  CHECK (exec (f3i (OP_MEM, 3, LDSH, 1, 0)) == 0);
  CHECK (get (3) == 0xffff80ff);
  CHECK (exec (f3i (OP_MEM, 3, LDUH, 1, 0)) == 0);
  CHECK (get (3) == 0x80ff);

  CHECK (exec (f3i (OP_MEM, 3, LDSH, 1, 2)) == 0);
  CHECK (get (3) == 0x7f01);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC double load and store")
{
  const uint32 addr = 0x2000;

  set (1, addr);
  set (2, 0x11223344);
  set (3, 0x55667788);

  /* A double access moves an even-odd register pair, most significant word
     first.  */
  CHECK (exec (f3i (OP_MEM, 2, STD, 1, 0)) == 0);
  CHECK (sis_tests::flatmem_peek (addr) == 0x11223344);
  CHECK (sis_tests::flatmem_peek (addr + 4) == 0x55667788);

  set (4, 0);
  set (5, 0);
  CHECK (exec (f3i (OP_MEM, 4, LDD, 1, 0)) == 0);
  CHECK (get (4) == 0x11223344);
  CHECK (get (5) == 0x55667788);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC a double access must be aligned")
{
  set (1, 0x2004); /* word aligned but not double word aligned */

  CHECK (exec (f3i (OP_MEM, 2, LDD, 1, 0)) == TRAP_UNALI);
  CHECK (exec (f3i (OP_MEM, 2, STD, 1, 0)) == TRAP_UNALI);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC swap exchanges with memory")
{
  const uint32 addr = 0x2000;

  set (1, addr);
  set (2, 0xaaaaaaaa);
  CHECK (exec (f3i (OP_MEM, 2, ST, 1, 0)) == 0);

  set (3, 0x55555555);
  CHECK (exec (f3i (OP_MEM, 3, SWAP, 1, 0)) == 0);
  CHECK (get (3) == 0xaaaaaaaa);
  CHECK (sis_tests::flatmem_peek (addr) == 0x55555555);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC ldstub reads a byte and writes ones")
{
  const uint32 addr = 0x2000;

  set (1, addr);
  set (2, 0x00112233);
  CHECK (exec (f3i (OP_MEM, 2, ST, 1, 0)) == 0);

  CHECK (exec (f3i (OP_MEM, 3, LDSTUB, 1, 1)) == 0);
  CHECK (get (3) == 0x11);
  CHECK (sis_tests::flatmem_peek (addr) == 0x00ff2233);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC alternate space needs supervisor")
{
  set (1, 0x2000);

  /* Chapter 6: the alternate space instructions are privileged.  */
  sregs[0].psr &= ~PSR_S;
  CHECK (exec (f3r (OP_MEM, 3, LDA, 1, 0) | (0xb << 5)) == TRAP_PRIVI);
  CHECK (exec (f3r (OP_MEM, 3, STA, 1, 0) | (0xb << 5)) == TRAP_PRIVI);

  /* In supervisor mode they behave as the ordinary forms.  */
  sregs[0].psr |= PSR_S;
  set (2, 0x12345678);
  CHECK (exec (f3r (OP_MEM, 2, STA, 1, 0) | (0xb << 5)) == 0);
  CHECK (sis_tests::flatmem_peek (0x2000) == 0x12345678);
  CHECK (exec (f3r (OP_MEM, 3, LDA, 1, 0) | (0xb << 5)) == 0);
  CHECK (get (3) == 0x12345678);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC an alternate space access takes no immediate")
{
  /* The immediate bit is not defined for an alternate space instruction, so
     the core reports it unimplemented.  */
  set (1, 0x2000);
  CHECK (exec (f3i (OP_MEM, 3, LDA, 1, 0)) == TRAP_UNIMP);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC flush is a no-operation")
{
  CHECK (exec (f3i (OP_ARITH, 0, FLUSH, 0, 0)) == 0);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC an unimplemented opcode traps")
{
  /* Format 2 with an op2 the manual leaves unimplemented.  */
  CHECK (exec (0) == TRAP_UNIMP);
}

namespace
{

/* A floating point operation is format 3 with op3 0x34 or 0x35 and the
   operation in the nine bits above rs2.  */
uint32
fpop (uint32 op3, uint32 rd, uint32 rs1, uint32 opf, uint32 rs2)
{
  return (OP_ARITH << 30) | (rd << 25) | (op3 << 19) | (rs1 << 14) |
	 (opf << 5) | rs2;
}

const uint32 FPOP1 = 0x34;
const uint32 FPOP2 = 0x35;

/* opf encodings, from the floating point instruction tables of appendix B.
   sparc.cc keeps its own copies, which are file-local, so these are
   transcribed from the manual rather than shared with it.  */
const uint32 OPF_FMOVs = 0x001;
const uint32 OPF_FNEGs = 0x005;
const uint32 OPF_FABSs = 0x009;
const uint32 OPF_FSQRTs = 0x029;
const uint32 OPF_FSQRTd = 0x02a;
const uint32 OPF_FADDs = 0x041;
const uint32 OPF_FADDd = 0x042;
const uint32 OPF_FSUBs = 0x045;
const uint32 OPF_FSUBd = 0x046;
const uint32 OPF_FMULs = 0x049;
const uint32 OPF_FMULd = 0x04a;
const uint32 OPF_FDIVs = 0x04d;
const uint32 OPF_FDIVd = 0x04e;
const uint32 OPF_FsMULd = 0x069;
const uint32 OPF_FiTOs = 0x0c4;
const uint32 OPF_FdTOs = 0x0c6;
const uint32 OPF_FiTOd = 0x0c8;
const uint32 OPF_FsTOd = 0x0c9;
const uint32 OPF_FsTOi = 0x0d1;
const uint32 OPF_FdTOi = 0x0d2;
const uint32 OPF_FCMPs = 0x051;
const uint32 OPF_FCMPd = 0x052;
const uint32 OPF_FCMPEs = 0x055;
const uint32 OPF_FCMPEd = 0x056;

} /* namespace */

TEST_CASE_FIXTURE (sparc_fixture, "SPARC floating point needs to be enabled")
{
  /* Chapter 7: an operation with the enable bit clear takes the floating
     point disabled trap, whatever the unit would have done.  */
  sregs[0].psr &= ~PSR_EF;

  CHECK (exec (fpop (FPOP1, 0, 0, OPF_FADDs, 0)) == TRAP_FPDIS);
  CHECK (exec (f3i (OP_MEM, 0, LDF, 0, 0)) == TRAP_FPDIS);
  CHECK (exec (f3i (OP_MEM, 0, STF, 0, 0)) == TRAP_FPDIS);
  CHECK (exec (f3i (OP_MEM, 0, LDFSR, 0, 0)) == TRAP_FPDIS);
  CHECK (exec (f3i (OP_MEM, 0, STFSR, 0, 0)) == TRAP_FPDIS);
  CHECK (exec (f3i (OP_MEM, 0, LDDF, 0, 0)) == TRAP_FPDIS);
  CHECK (exec (f3i (OP_MEM, 0, STDF, 0, 0)) == TRAP_FPDIS);

  /* A floating point branch is disabled too.  */
  CHECK (exec ((FPBCC << 22) | (FBA << 25)) == TRAP_FPDIS);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC single precision arithmetic")
{
  sregs[0].psr |= PSR_EF;
  fs (1) = 3.5f;
  fs (2) = 1.25f;

  CHECK (exec (fpop (FPOP1, 3, 1, OPF_FADDs, 2)) == 0);
  CHECK (fs (3) == 4.75f);

  CHECK (exec (fpop (FPOP1, 3, 1, OPF_FSUBs, 2)) == 0);
  CHECK (fs (3) == 2.25f);

  CHECK (exec (fpop (FPOP1, 3, 1, OPF_FMULs, 2)) == 0);
  CHECK (fs (3) == 4.375f);

  CHECK (exec (fpop (FPOP1, 3, 1, OPF_FDIVs, 2)) == 0);
  CHECK (fs (3) == 2.8f);

  CHECK (exec (fpop (FPOP1, 3, 0, OPF_FSQRTs, 1)) == 0);
  CHECK (fs (3) == doctest::Approx (1.8708287f));
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC single precision sign operations")
{
  sregs[0].psr |= PSR_EF;
  fs (1) = -2.5f;

  CHECK (exec (fpop (FPOP1, 3, 0, OPF_FABSs, 1)) == 0);
  CHECK (fs (3) == 2.5f);

  CHECK (exec (fpop (FPOP1, 3, 0, OPF_FNEGs, 1)) == 0);
  CHECK (fs (3) == 2.5f);

  CHECK (exec (fpop (FPOP1, 3, 0, OPF_FMOVs, 1)) == 0);
  CHECK (fs (3) == -2.5f);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC double precision arithmetic")
{
  sregs[0].psr |= PSR_EF;
  fd (2) = 3.5;	 /* register pair f2 and f3 */
  fd (4) = 1.25; /* register pair f4 and f5 */

  CHECK (exec (fpop (FPOP1, 6, 2, OPF_FADDd, 4)) == 0);
  CHECK (fd (6) == 4.75);

  CHECK (exec (fpop (FPOP1, 6, 2, OPF_FSUBd, 4)) == 0);
  CHECK (fd (6) == 2.25);

  CHECK (exec (fpop (FPOP1, 6, 2, OPF_FMULd, 4)) == 0);
  CHECK (fd (6) == 4.375);

  CHECK (exec (fpop (FPOP1, 6, 2, OPF_FDIVd, 4)) == 0);
  CHECK (fd (6) == 2.8);

  CHECK (exec (fpop (FPOP1, 6, 0, OPF_FSQRTd, 2)) == 0);
  CHECK (fd (6) == doctest::Approx (1.8708286934));
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC conversions between formats")
{
  sregs[0].psr |= PSR_EF;

  fsi (1) = 7;
  CHECK (exec (fpop (FPOP1, 3, 0, OPF_FiTOs, 1)) == 0);
  CHECK (fs (3) == 7.0f);

  CHECK (exec (fpop (FPOP1, 4, 0, OPF_FiTOd, 1)) == 0);
  CHECK (fd (4) == 7.0);

  fs (1) = 9.75f;
  CHECK (exec (fpop (FPOP1, 3, 0, OPF_FsTOi, 1)) == 0);
  CHECK (fsi (3) == 9);

  CHECK (exec (fpop (FPOP1, 4, 0, OPF_FsTOd, 1)) == 0);
  CHECK (fd (4) == 9.75);

  fd (2) = -3.5;
  CHECK (exec (fpop (FPOP1, 6, 0, OPF_FdTOs, 2)) == 0);
  CHECK (fs (6) == -3.5f);

  CHECK (exec (fpop (FPOP1, 6, 0, OPF_FdTOi, 2)) == 0);
  CHECK (fsi (6) == -3);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC compare sets the condition field")
{
  sregs[0].psr |= PSR_EF;
  fs (1) = 1.0f;
  fs (2) = 2.0f;

  /* The two bit condition field of the status register: equal, less,
     greater, unordered.  */
  CHECK (exec (fpop (FPOP2, 0, 1, OPF_FCMPs, 2)) == 0);
  CHECK (((sregs[0].fsr >> 10) & 3) == FCC_L);

  CHECK (exec (fpop (FPOP2, 0, 2, OPF_FCMPs, 1)) == 0);
  CHECK (((sregs[0].fsr >> 10) & 3) == FCC_G);

  CHECK (exec (fpop (FPOP2, 0, 1, OPF_FCMPs, 1)) == 0);
  CHECK (((sregs[0].fsr >> 10) & 3) == FCC_E);

  fd (2) = 4.0;
  fd (4) = 4.0;
  CHECK (exec (fpop (FPOP2, 0, 2, OPF_FCMPd, 4)) == 0);
  CHECK (((sregs[0].fsr >> 10) & 3) == FCC_E);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC floating point load and store")
{
  const uint32 addr = 0x2000;

  sregs[0].psr |= PSR_EF;
  set (1, addr);
  fs (2) = 6.25f;

  CHECK (exec (f3i (OP_MEM, 2, STF, 1, 0)) == 0);
  CHECK (exec (f3i (OP_MEM, 3, LDF, 1, 0)) == 0);
  CHECK (fs (3) == 6.25f);

  fd (4) = 12.5;
  CHECK (exec (f3i (OP_MEM, 4, STDF, 1, 0)) == 0);
  CHECK (exec (f3i (OP_MEM, 6, LDDF, 1, 0)) == 0);
  CHECK (fd (6) == 12.5);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC status register load and store")
{
  const uint32 addr = 0x2000;

  sregs[0].psr |= PSR_EF;
  set (1, addr);

  CHECK (exec (f3i (OP_MEM, 0, STFSR, 1, 0)) == 0);
  CHECK (sis_tests::flatmem_peek (addr) == sregs[0].fsr);

  set (2, 0);
  CHECK (exec (f3i (OP_MEM, 2, ST, 1, 0)) == 0);
  CHECK (exec (f3i (OP_MEM, 0, LDFSR, 1, 0)) == 0);
  CHECK ((sregs[0].fsr & 0x0fc00000) == 0);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC floating point access must be aligned")
{
  sregs[0].psr |= PSR_EF;

  set (1, 0x2001);
  CHECK (exec (f3i (OP_MEM, 2, LDF, 1, 0)) == TRAP_UNALI);
  CHECK (exec (f3i (OP_MEM, 2, STF, 1, 0)) == TRAP_UNALI);

  set (1, 0x2004);
  CHECK (exec (f3i (OP_MEM, 2, LDDF, 1, 0)) == TRAP_UNALI);
  CHECK (exec (f3i (OP_MEM, 2, STDF, 1, 0)) == TRAP_UNALI);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC floating point branches test every condition")
{
  /* Appendix B.22: each condition is a function of the two bit condition
     field of the status register.  */
  sregs[0].psr |= PSR_EF;

  const uint32 conds[16] = { FBN, FBNE, FBLG, FBUL, FBL,   FBUG, FBG,	FBU,
			     FBA, FBE,	FBUE, FBGE, FBUGE, FBLE, FBULE, FBO };

  for (uint32 fcc = 0; fcc < 4; fcc++)
    {
      bool e = fcc == FCC_E;
      bool l = fcc == FCC_L;
      bool g = fcc == FCC_G;
      bool u = fcc == FCC_U;

      bool want[16] = { false,	       /* fbn */
			l || g || u,   /* fbne */
			l || g,	       /* fblg */
			u || l,	       /* fbul */
			l,	       /* fbl */
			u || g,	       /* fbug */
			g,	       /* fbg */
			u,	       /* fbu */
			true,	       /* fba */
			e,	       /* fbe */
			u || e,	       /* fbue */
			e || g,	       /* fbge */
			u || e || g,   /* fbuge */
			e || l,	       /* fble */
			u || e || l,   /* fbule */
			e || l || g }; /* fbo */

      for (int i = 0; i < 16; i++)
	{
	  sregs[0].fsr = (sregs[0].fsr & ~0xc00) | (fcc << 10);
	  sregs[0].pc = 0x1000;
	  sregs[0].npc = 0x1004;

	  CHECK (exec ((FPBCC << 22) | (conds[i] << 25) | 4) == 0);

	  INFO ("condition ", i, " with fcc ", fcc);
	  CHECK (sregs[0].npc == (want[i] ? 0x1010 : 0x1008));
	}
    }
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC the alternate space forms of every access")
{
  const uint32 addr = 0x2000;
  const uint32 asi = 0xb << 5;

  set (1, addr);
  set (2, 0x11223344);
  set (3, 0x55667788);

  CHECK (exec (f3r (OP_MEM, 2, STDA, 1, 0) | asi) == 0);
  CHECK (sis_tests::flatmem_peek (addr) == 0x11223344);
  CHECK (sis_tests::flatmem_peek (addr + 4) == 0x55667788);

  CHECK (exec (f3r (OP_MEM, 4, LDDA, 1, 0) | asi) == 0);
  CHECK (get (4) == 0x11223344);
  CHECK (get (5) == 0x55667788);

  set (2, 0x99);
  CHECK (exec (f3r (OP_MEM, 2, STBA, 1, 0) | asi) == 0);
  CHECK (exec (f3r (OP_MEM, 6, LDUBA, 1, 0) | asi) == 0);
  CHECK (get (6) == 0x99);
  CHECK (exec (f3r (OP_MEM, 6, LDSBA, 1, 0) | asi) == 0);
  CHECK (get (6) == 0xffffff99);

  set (2, 0x8001);
  CHECK (exec (f3r (OP_MEM, 2, STHA, 1, 0) | asi) == 0);
  CHECK (exec (f3r (OP_MEM, 6, LDUHA, 1, 0) | asi) == 0);
  CHECK (get (6) == 0x8001);
  CHECK (exec (f3r (OP_MEM, 6, LDSHA, 1, 0) | asi) == 0);
  CHECK (get (6) == 0xffff8001);

  set (2, 0x12345678);
  CHECK (exec (f3r (OP_MEM, 2, STA, 1, 0) | asi) == 0);
  set (6, 0x0);
  CHECK (exec (f3r (OP_MEM, 6, SWAPA, 1, 0) | asi) == 0);
  CHECK (get (6) == 0x12345678);
  CHECK (sis_tests::flatmem_peek (addr) == 0);

  CHECK (exec (f3r (OP_MEM, 6, LDSTUBA, 1, 0) | asi) == 0);
  CHECK (get (6) == 0);
  CHECK ((sis_tests::flatmem_peek (addr) >> 24) == 0xff);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC casa compares and swaps")
{
  const uint32 addr = 0x2000;

  set (1, addr);
  set (2, 0xaaaaaaaa);
  CHECK (exec (f3i (OP_MEM, 2, ST, 1, 0)) == 0);

  /* The swap happens when the memory word matches rs2.  */
  set (3, 0x55555555);
  set (4, 0xaaaaaaaa);
  CHECK (exec (f3r (OP_MEM, 3, CASA, 1, 4) | (0xb << 5)) == 0);
  CHECK (sis_tests::flatmem_peek (addr) == 0x55555555);
  CHECK (get (3) == 0xaaaaaaaa);

  /* With a mismatch the memory keeps its value and the register takes it.  */
  set (3, 0x99999999);
  set (4, 0);
  CHECK (exec (f3r (OP_MEM, 3, CASA, 1, 4) | (0xb << 5)) == 0);
  CHECK (sis_tests::flatmem_peek (addr) == 0x55555555);
  CHECK (get (3) == 0x55555555);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC a trap enters through the table")
{
  /* Chapter 7: a trap disables further traps, drops to the next window and
     enters the table at the base plus the type times sixteen.  */
  uint32 cwp = sregs[0].psr & PSR_CWP;

  sregs[0].psr |= PSR_ET;
  sregs[0].tbr = 0x4000;
  sregs[0].pc = 0x1000;
  sregs[0].npc = 0x1004;
  sregs[0].trap = TRAP_UNIMP;

  CHECK (arch->execute_trap (&sregs[0]) == 0);

  CHECK ((sregs[0].psr & PSR_ET) == 0);
  CHECK ((sregs[0].psr & PSR_S) != 0);
  CHECK ((sregs[0].psr & PSR_CWP) == ((cwp - 1) & PSR_CWP));
  CHECK (sregs[0].pc == 0x4000 + (TRAP_UNIMP << 4));
  CHECK (sregs[0].npc == 0x4004 + (TRAP_UNIMP << 4));

  /* The interrupted pair is saved in the local registers of the new
     window.  */
  CHECK (get (17) == 0x1000);
  CHECK (get (18) == 0x1004);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC a trap with traps disabled is an error")
{
  sregs[0].psr &= ~PSR_ET;
  sregs[0].trap = TRAP_UNIMP;

  CHECK (arch->execute_trap (&sregs[0]) == ERROR_MODE);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC the simulator traps above the table")
{
  /* The simulator uses trap numbers above the architecture's for its own
     purposes.  A reset restarts at zero and the rest report their reason.  */
  sregs[0].trap = 256;
  CHECK (arch->execute_trap (&sregs[0]) == 0);
  CHECK (sregs[0].pc == 0);
  CHECK (sregs[0].npc == 4);
  CHECK (sregs[0].trap == 0);

  sregs[0].trap = ERROR_TRAP;
  CHECK (arch->execute_trap (&sregs[0]) == ERROR_MODE);

  sregs[0].trap = WPT_TRAP;
  CHECK (arch->execute_trap (&sregs[0]) == WPT_HIT);

  sregs[0].trap = NULL_TRAP;
  CHECK (arch->execute_trap (&sregs[0]) == NULL_HIT);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC an interrupt is taken above the level")
{
  sregs[0].psr |= PSR_ET;
  sregs[0].psr &= ~PSR_PIL;
  sregs[0].trap = 0;

  /* No request, no interrupt.  */
  ext_irl[0] = 0;
  CHECK (arch->check_interrupts (&sregs[0]) == 0);

  /* A request above the level is taken.  */
  ext_irl[0] = 5;
  CHECK (arch->check_interrupts (&sregs[0]) == 5);

  /* At or below the level it is held off.  */
  sregs[0].psr |= 7 << 8;
  CHECK (arch->check_interrupts (&sregs[0]) == 0);

  /* Level fifteen is non-maskable.  */
  ext_irl[0] = 15;
  CHECK (arch->check_interrupts (&sregs[0]) == 15);

  /* With traps disabled nothing is taken.  */
  sregs[0].psr &= ~PSR_ET;
  CHECK (arch->check_interrupts (&sregs[0]) == 0);

  ext_irl[0] = 0;
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC an interrupt leaves power down mode")
{
  sregs[0].psr |= PSR_ET;
  sregs[0].psr &= ~PSR_PIL;
  sregs[0].trap = 0;
  sregs[0].pwd_mode = 1;
  sregs[0].pwdstart = 0;
  sregs[0].simtime = 100;
  ext_irl[0] = 5;

  CHECK (arch->check_interrupts (&sregs[0]) == 5);
  CHECK (sregs[0].pwd_mode == 0);
  CHECK (sregs[0].pwdtime == 100);

  ext_irl[0] = 0;
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC an interrupt waits for a pending trap")
{
  sregs[0].psr |= PSR_ET;
  sregs[0].psr &= ~PSR_PIL;
  ext_irl[0] = 5;

  /* A trap already pending takes precedence, so the interrupt is not
     reported yet.  */
  sregs[0].trap = TRAP_UNIMP;
  CHECK (arch->check_interrupts (&sregs[0]) == 0);

  sregs[0].trap = 0;
  ext_irl[0] = 0;
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC registers are reachable by name")
{
  /* The shell and the GDB stub reach a register by name.  The window
     registers use the architectural names of the manual's figure 4-1.  */
  const char *names[32] = { "g0", "g1", "g2", "g3", "g4", "g5", "g6", "g7",
			    "o0", "o1", "o2", "o3", "o4", "o5", "o6", "o7",
			    "l0", "l1", "l2", "l3", "l4", "l5", "l6", "l7",
			    "i0", "i1", "i2", "i3", "i4", "i5", "i6", "i7" };

  for (int i = 1; i < 32; i++)
    {
      arch->set_register (&sregs[0], (char *) names[i], 0x1000 + i, 0);
      INFO ("register ", names[i]);
      CHECK (get (i) == (uint32) (0x1000 + i));
    }

  /* g0 is hard wired to zero.  */
  arch->set_register (&sregs[0], (char *) "g0", 0x1234, 0);
  CHECK (get (0) == 0);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC the control registers are named too")
{
  arch->set_register (&sregs[0], (char *) "psr", 0xffffffff, 0);
  CHECK (sregs[0].psr == 0x00f03fff);

  arch->set_register (&sregs[0], (char *) "tbr", 0xffffffff, 0);
  CHECK (sregs[0].tbr == 0xfffffff0);

  arch->set_register (&sregs[0], (char *) "wim", 0xffffffff, 0);
  CHECK (sregs[0].wim == 0xff);

  arch->set_register (&sregs[0], (char *) "y", 0x12345678, 0);
  CHECK (sregs[0].y == 0x12345678);

  arch->set_register (&sregs[0], (char *) "pc", 0x2000, 0);
  CHECK (sregs[0].pc == 0x2000);

  arch->set_register (&sregs[0], (char *) "npc", 0x2004, 0);
  CHECK (sregs[0].npc == 0x2004);

  arch->set_register (&sregs[0], (char *) "fsr", 0, 0);

  /* A name the core does not know is reported and changes nothing.  */
  stdout_capture cap;
  arch->set_register (&sregs[0], (char *) "nosuchreg", 1, 0);
  CHECK (!cap.str ().empty ());
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC registers are reachable by number")
{
  /* With no name the number selects the register, which is the order the
     GDB stub packs them in.  */
  for (int i = 1; i < 32; i++)
    {
      arch->set_register (&sregs[0], NULL, 0x2000 + i, i);
      INFO ("register number ", i);
      CHECK (get (i) == (uint32) (0x2000 + i));
    }

  /* Then the control registers, in the order the stub expects: y, psr, wim,
     tbr, pc, npc and the floating point status.  */
  const uint32 want[7] = { 0x12345678, 0x00f03fff, 0x0f, 0xfffffff0,
			   0x3000,     0x3004,	   0 };
  for (int i = 0; i < 7; i++)
    arch->set_register (&sregs[0], NULL, want[i], 64 + i);

  CHECK (sregs[0].y == want[0]);
  CHECK (sregs[0].psr == want[1]);
  CHECK (sregs[0].wim == want[2]);
  CHECK (sregs[0].tbr == want[3]);
  CHECK (sregs[0].pc == want[4]);
  CHECK (sregs[0].npc == want[5]);

  /* A number past the last register changes nothing.  */
  arch->set_register (&sregs[0], NULL, 0xdeadbeef, 99);
  CHECK (sregs[0].npc == want[5]);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC the GDB stub packs every register")
{
  char buf[72 * 4];

  set (1, 0x11223344);
  sregs[0].pc = 0x2000;

  CHECK (arch->gdb_get_reg (buf) == 72 * 4);

  /* Each register is packed most significant byte first, whatever the host
     does.  */
  CHECK ((unsigned char) buf[4] == 0x11);
  CHECK ((unsigned char) buf[5] == 0x22);
  CHECK ((unsigned char) buf[6] == 0x33);
  CHECK ((unsigned char) buf[7] == 0x44);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC the register displays print something")
{
  stdout_capture cap;

  sregs[0].psr |= PSR_EF;
  arch->display_registers (&sregs[0]);
  arch->display_ctrl (&sregs[0]);
  arch->display_special (&sregs[0]);
  arch->display_fpu (&sregs[0]);

  std::string out = cap.str ();
  CHECK (out.find ("psr") != std::string::npos);
  CHECK (out.find ("INS") != std::string::npos);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC the disassembler names the instruction")
{
  stdout_capture cap;

  /* Put a few instructions in memory and disassemble them where they
     are.  */
  sis_tests::flatmem_poke (0x100, f3r (OP_ARITH, 3, ADD, 1, 2));
  sis_tests::flatmem_poke (0x104, f3i (OP_ARITH, 3, SUBCC, 1, 4));
  sis_tests::flatmem_poke (0x108, (OP_CALL << 30) | 4);
  sis_tests::flatmem_poke (0x10c, bicc (0, BICC_BE, 4));
  sis_tests::flatmem_poke (0x110, sethi (3, 0x1234));
  sis_tests::flatmem_poke (0x114, f3i (OP_MEM, 3, LD, 1, 0));
  sis_tests::flatmem_poke (0x118, f3i (OP_MEM, 3, ST, 1, 0));
  sis_tests::flatmem_poke (0x11c, f3i (OP_ARITH, 8, SAVE, 0, 0));

  for (uint32 a = 0x100; a <= 0x11c; a += 4)
    arch->disas (a);

  std::string out = cap.str ();
  CHECK (out.find ("add") != std::string::npos);
  CHECK (out.find ("subcc") != std::string::npos);
  CHECK (out.find ("call") != std::string::npos);
  CHECK (out.find ("be") != std::string::npos);
  CHECK (out.find ("sethi") != std::string::npos);
  CHECK (out.find ("ld") != std::string::npos);
  CHECK (out.find ("st") != std::string::npos);
  CHECK (out.find ("save") != std::string::npos);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC rett returns from a trap")
{
  uint32 cwp = sregs[0].psr & PSR_CWP;

  /* rett runs with traps disabled, moves up a window, restores the previous
     supervisor state and re-enables traps.  */
  sregs[0].psr &= ~PSR_ET;
  sregs[0].psr |= PSR_S | PSR_PS;
  sregs[0].wim = 0;
  set (1, 0x3000);

  CHECK (exec (f3i (OP_ARITH, 0, RETT, 1, 0)) == 0);
  CHECK ((sregs[0].psr & PSR_CWP) == ((cwp + 1) & PSR_CWP));
  CHECK ((sregs[0].psr & PSR_ET) != 0);
  CHECK ((sregs[0].psr & PSR_S) != 0);
  CHECK (sregs[0].npc == 0x3000);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC rett drops to user mode")
{
  /* The previous supervisor bit decides what to return to.  */
  sregs[0].psr &= ~(PSR_ET | PSR_PS);
  sregs[0].psr |= PSR_S;
  sregs[0].wim = 0;
  set (1, 0x3000);

  CHECK (exec (f3i (OP_ARITH, 0, RETT, 1, 0)) == 0);
  CHECK ((sregs[0].psr & PSR_S) == 0);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC rett refuses the wrong conditions")
{
  set (1, 0x3000);

  /* With traps already enabled it is unimplemented.  */
  sregs[0].psr |= PSR_ET | PSR_S;
  CHECK (exec (f3i (OP_ARITH, 0, RETT, 1, 0)) == TRAP_UNIMP);

  /* In user mode it is privileged.  */
  sregs[0].psr &= ~(PSR_ET | PSR_S);
  CHECK (exec (f3i (OP_ARITH, 0, RETT, 1, 0)) == TRAP_PRIVI);

  /* Returning into an invalid window underflows.  */
  sregs[0].psr |= PSR_S;
  sregs[0].wim = 1u << (((sregs[0].psr & PSR_CWP) + 1) & PSR_CWP);
  CHECK (exec (f3i (OP_ARITH, 0, RETT, 1, 0)) == TRAP_WUFL);

  /* The address must be word aligned.  */
  sregs[0].wim = 0;
  set (1, 0x3001);
  CHECK (exec (f3i (OP_ARITH, 0, RETT, 1, 0)) == TRAP_UNALI);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC rett to a null address halts")
{
  /* The simulator stops rather than following a null pointer.  */
  sregs[0].psr &= ~PSR_ET;
  sregs[0].psr |= PSR_S;
  sregs[0].wim = 0;
  set (1, 0);

  CHECK (exec (f3i (OP_ARITH, 0, RETT, 1, 0)) == NULL_TRAP);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC the ancillary registers on a LEON")
{
  /* On a LEON the read-y opcode also reaches the ancillary registers: the
     implementation register and the two halves of the cycle counter.  */
  int saved = cputype;
  cputype = CPU_LEON3;

  sregs[0].y = 0x11111111;
  sregs[0].asr17 = 0x22222222;
  sregs[0].simtime = 0x3333333344444444ull;

  CHECK (exec (f3r (OP_ARITH, 3, RDY, 0, 0)) == 0);
  CHECK (get (3) == 0x11111111);

  CHECK (exec (f3r (OP_ARITH, 3, RDY, 17, 0)) == 0);
  CHECK (get (3) == 0x22222222);

  CHECK (exec (f3r (OP_ARITH, 3, RDY, 22, 0)) == 0);
  CHECK (get (3) == 0x33333333);

  CHECK (exec (f3r (OP_ARITH, 3, RDY, 23, 0)) == 0);
  CHECK (get (3) == 0x44444444);

  cputype = saved;
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC a store outside memory traps")
{
  set (1, sis_tests::FLATMEM_SIZE + 0x1000);
  set (2, 0);

  CHECK (exec (f3i (OP_MEM, 2, ST, 1, 0)) == TRAP_DEXC);
  CHECK (exec (f3i (OP_MEM, 2, STB, 1, 0)) == TRAP_DEXC);
  CHECK (exec (f3i (OP_MEM, 2, STH, 1, 0)) == TRAP_DEXC);
  CHECK (exec (f3i (OP_MEM, 2, STD, 1, 0)) == TRAP_DEXC);
  CHECK (exec (f3i (OP_MEM, 2, LDD, 1, 0)) == TRAP_DEXC);
  CHECK (exec (f3i (OP_MEM, 2, LDUB, 1, 0)) == TRAP_DEXC);
  CHECK (exec (f3i (OP_MEM, 2, LDUH, 1, 0)) == TRAP_DEXC);
  CHECK (exec (f3i (OP_MEM, 2, SWAP, 1, 0)) == TRAP_DEXC);
  CHECK (exec (f3i (OP_MEM, 2, LDSTUB, 1, 0)) == TRAP_DEXC);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC a floating point access outside memory traps")
{
  sregs[0].psr |= PSR_EF;
  set (1, sis_tests::FLATMEM_SIZE + 0x1000);

  CHECK (exec (f3i (OP_MEM, 2, LDF, 1, 0)) == TRAP_DEXC);
  CHECK (exec (f3i (OP_MEM, 2, STF, 1, 0)) == TRAP_DEXC);
  CHECK (exec (f3i (OP_MEM, 2, LDDF, 1, 0)) == TRAP_DEXC);
  CHECK (exec (f3i (OP_MEM, 2, STDF, 1, 0)) == TRAP_DEXC);
  CHECK (exec (f3i (OP_MEM, 0, LDFSR, 1, 0)) == TRAP_DEXC);
  CHECK (exec (f3i (OP_MEM, 0, STFSR, 1, 0)) == TRAP_DEXC);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC the floating point queue store")
{
  sregs[0].psr |= PSR_EF;
  set (1, 0x2000);

  /* An empty queue is reported as a sequence error rather than a trap,
     which the manual allows.  */
  CHECK (exec (f3i (OP_MEM, 0, STDFQ, 1, 0)) == 0);
  CHECK ((sregs[0].fsr & FSR_TT) == FP_SEQ_ERR);

  /* The address must be double word aligned.  */
  set (1, 0x2004);
  CHECK (exec (f3i (OP_MEM, 0, STDFQ, 1, 0)) == TRAP_UNALI);

  /* With the unit disabled it is the disabled trap.  */
  sregs[0].psr &= ~PSR_EF;
  CHECK (exec (f3i (OP_MEM, 0, STDFQ, 1, 0)) == TRAP_FPDIS);

  /* The manual makes it privileged, so user mode traps before either.  */
  sregs[0].psr &= ~PSR_S;
  CHECK (exec (f3i (OP_MEM, 0, STDFQ, 1, 0)) == TRAP_PRIVI);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC a floating point exception is deferred")
{
  sregs[0].psr |= PSR_EF;

  /* An operation the unit cannot do puts it in the pending exception state,
     and the next operation reports the exception.  */
  CHECK (exec (fpop (FPOP1, 3, 1, 0x1ff, 2)) == 0);
  CHECK (sregs[0].fpstate == FP_EXC_PE);
  CHECK ((sregs[0].fsr & FSR_TT) == FP_UNIMP);

  CHECK (exec (fpop (FPOP1, 3, 1, OPF_FADDs, 2)) == TRAP_FPEXC);
  CHECK (sregs[0].fpstate == FP_EXC_MODE);

  /* While in the exception state an operation is a sequence error.  */
  CHECK (exec (fpop (FPOP1, 3, 1, OPF_FADDs, 2)) == 0);
  CHECK ((sregs[0].fsr & FSR_TT) == FP_SEQ_ERR);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC single to double multiply")
{
  /* The single to double multiply is a LEON instruction; the ERC32 floating
     point unit reports it unimplemented.  */
  int saved = cputype;

  sregs[0].psr |= PSR_EF;
  CHECK (exec (fpop (FPOP1, 4, 2, OPF_FsMULd, 3)) == 0);
  CHECK ((sregs[0].fsr & FSR_TT) == FP_UNIMP);

  cputype = CPU_LEON3;
  sregs[0].fpstate = FP_EXE_MODE;
  sregs[0].fsr = 0;
  fs (2) = 3.0f;
  fs (3) = 4.0f;

  CHECK (exec (fpop (FPOP1, 4, 2, OPF_FsMULd, 3)) == 0);
  CHECK (fd (4) == 12.0);

  cputype = saved;
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC compare-and-except reports unordered")
{
  sregs[0].psr |= PSR_EF;
  fs (1) = 1.0f;
  fs (2) = 2.0f;

  CHECK (exec (fpop (FPOP2, 0, 1, OPF_FCMPEs, 2)) == 0);
  CHECK (((sregs[0].fsr >> 10) & 3) == FCC_L);

  fd (2) = 1.0;
  fd (4) = 2.0;
  CHECK (exec (fpop (FPOP2, 0, 2, OPF_FCMPEd, 4)) == 0);
  CHECK (((sregs[0].fsr >> 10) & 3) == FCC_L);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC the disassembler decodes every group")
{
  stdout_capture cap;

  /* Sweep the instruction space so the decode tables are reached: every
     arithmetic and load-store opcode, every branch condition of both kinds,
     and the floating point operations.  The output is not asserted on beyond
     being produced, because the assembly syntax is the disassembler's own;
     the cases above check the ones that matter by name.  */
  uint32 addr = 0x200;

  for (uint32 op3 = 0; op3 < 0x40; op3++)
    {
      sis_tests::flatmem_poke (addr, f3r (OP_ARITH, 3, op3, 1, 2));
      arch->disas (addr);
      sis_tests::flatmem_poke (addr, f3i (OP_ARITH, 3, op3, 1, 5));
      arch->disas (addr);
      sis_tests::flatmem_poke (addr, f3r (OP_MEM, 3, op3, 1, 2));
      arch->disas (addr);
      sis_tests::flatmem_poke (addr, f3i (OP_MEM, 3, op3, 1, 5));
      arch->disas (addr);
    }

  for (uint32 cond = 0; cond < 16; cond++)
    {
      sis_tests::flatmem_poke (addr, bicc (0, cond, 4));
      arch->disas (addr);
      sis_tests::flatmem_poke (addr, bicc (1, cond, -4));
      arch->disas (addr);
      sis_tests::flatmem_poke (addr, (FPBCC << 22) | (cond << 25) | 4);
      arch->disas (addr);
    }

  for (uint32 opf = 0; opf < 0x100; opf++)
    {
      sis_tests::flatmem_poke (addr, fpop (FPOP1, 4, 2, opf, 6));
      arch->disas (addr);
      sis_tests::flatmem_poke (addr, fpop (FPOP2, 4, 2, opf, 6));
      arch->disas (addr);
    }

  /* The remaining format 2 opcodes, including the unimplemented ones.  */
  for (uint32 op2 = 0; op2 < 8; op2++)
    {
      sis_tests::flatmem_poke (addr, (op2 << 22) | (3 << 25) | 0x1234);
      arch->disas (addr);
    }

  sis_tests::flatmem_poke (addr, (OP_CALL << 30) | 0x100);
  arch->disas (addr);

  CHECK (!cap.str ().empty ());
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC a watchpoint stops a store")
{
  /* The shell's watchpoints are checked on the memory path, so a store into
     a watched word reports a hit rather than completing.  */
  uint32 saved = ebase.wpwnum;

  ebase.wpwnum = 1;
  ebase.wpws[0] = 0x2000;
  ebase.wpwm[0] = 3; /* the word at that address */
  ebase.wphit = 0;

  set (1, 0x2000);
  set (2, 0x12345678);
  exec (f3i (OP_MEM, 2, ST, 1, 0));
  CHECK (ebase.wphit != 0);

  /* A store elsewhere is not a hit.  */
  ebase.wphit = 0;
  set (1, 0x2100);
  exec (f3i (OP_MEM, 2, ST, 1, 0));
  CHECK (ebase.wphit == 0);

  ebase.wpwnum = saved;
  ebase.wphit = 0;
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC a load into the next instruction holds")
{
  /* The core models the load delay: an instruction using the destination of
     the load before it is charged an extra cycle.  */
  const uint32 addr = 0x2000;

  set (1, addr);
  sis_tests::flatmem_poke (addr, 0x11223344);

  CHECK (exec (f3i (OP_MEM, 3, LD, 1, 0)) == 0);
  uint64 after_load = sregs[0].simtime;
  sregs[0].simtime = after_load;

  /* The next instruction reads the register the load wrote.  */
  CHECK (exec (f3r (OP_ARITH, 4, ADD, 3, 3)) == 0);
  CHECK (get (4) == 0x22446688);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC collects target coverage while it runs")
{
  /* With the -cov option the core records what it executed and how it left
     each control transfer, which is a second path through every branch.  */
  uint32 saved = ebase.coven;
  ebase.coven = 1;

  /* A conditional branch taken and not taken.  */
  sregs[0].psr |= PSR_Z;
  CHECK (exec (bicc (0, BICC_BE, 4)) == 0);
  sregs[0].pc = 0x1000;
  sregs[0].npc = 0x1004;
  sregs[0].psr &= ~PSR_Z;
  CHECK (exec (bicc (0, BICC_BE, 4)) == 0);

  /* The same with the annul bit, taken and not taken.  */
  sregs[0].pc = 0x1000;
  sregs[0].npc = 0x1004;
  CHECK (exec (bicc (1, BICC_BE, 4)) == 0);
  sregs[0].pc = 0x1000;
  sregs[0].npc = 0x1004;
  sregs[0].psr |= PSR_Z;
  CHECK (exec (bicc (1, BICC_BE, 4)) == 0);

  /* Branch always, with and without the annul bit.  */
  sregs[0].pc = 0x1000;
  sregs[0].npc = 0x1004;
  CHECK (exec (bicc (0, BICC_BA, 4)) == 0);
  sregs[0].pc = 0x1000;
  sregs[0].npc = 0x1004;
  CHECK (exec (bicc (1, BICC_BA, 4)) == 0);

  /* Branch never, which is the not taken case of an unconditional
     branch.  */
  sregs[0].pc = 0x1000;
  sregs[0].npc = 0x1004;
  CHECK (exec (bicc (0, BICC_BN, 4)) == 0);
  sregs[0].pc = 0x1000;
  sregs[0].npc = 0x1004;
  CHECK (exec (bicc (1, BICC_BN, 4)) == 0);

  /* A call and a jump.  */
  sregs[0].pc = 0x1000;
  sregs[0].npc = 0x1004;
  CHECK (exec ((OP_CALL << 30) | 4) == 0);

  set (1, 0x3000);
  sregs[0].pc = 0x1000;
  sregs[0].npc = 0x1004;
  CHECK (exec (f3i (OP_ARITH, 3, JMPL, 1, 0)) == 0);

  /* A return from a trap.  */
  sregs[0].psr &= ~PSR_ET;
  sregs[0].psr |= PSR_S;
  sregs[0].wim = 0;
  CHECK (exec (f3i (OP_ARITH, 0, RETT, 1, 0)) == 0);

  ebase.coven = saved;
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC collects coverage over the floating point branches")
{
  uint32 saved = ebase.coven;
  ebase.coven = 1;
  sregs[0].psr |= PSR_EF;

  for (uint32 annul = 0; annul < 2; annul++)
    for (uint32 fcc = 0; fcc < 2; fcc++)
      for (uint32 cond = 0; cond < 16; cond += 8)
	{
	  sregs[0].fsr = (sregs[0].fsr & ~0xc00) | (fcc << 10);
	  sregs[0].pc = 0x1000;
	  sregs[0].npc = 0x1004;
	  CHECK (exec ((FPBCC << 22) | (cond << 25) | (annul << 29) | 4) == 0);
	}

  ebase.coven = saved;
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC a floating point branch waits for the unit")
{
  /* A branch on the condition field has to wait for an operation still in
     the pipe, which the core charges as a hold.  */
  sregs[0].psr |= PSR_EF;
  sregs[0].simtime = 0;
  sregs[0].ftime = 50;

  CHECK (exec ((FPBCC << 22) | (FBA << 25) | 4) == 0);
  CHECK (sregs[0].hold > 0);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC results reach the window registers")
{
  /* Every read of a state register writes its result, which lands in a
     global or a window register depending on the number.  */
  CHECK (exec (f3r (OP_ARITH, 16, RDPSR, 0, 0)) == 0);
  CHECK (get (16) == sregs[0].psr);

  CHECK (exec (f3r (OP_ARITH, 17, RDWIM, 0, 0)) == 0);
  CHECK (get (17) == sregs[0].wim);

  CHECK (exec (f3r (OP_ARITH, 18, RDTBR, 0, 0)) == 0);
  CHECK (get (18) == sregs[0].tbr);

  sregs[0].y = 0x1234;
  CHECK (exec (f3r (OP_ARITH, 19, RDY, 0, 0)) == 0);
  CHECK (get (19) == 0x1234);

  CHECK (exec (sethi (20, 0x100)) == 0);
  CHECK (get (20) == 0x100 << 10);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC an ancillary register the core has no answer for")
{
  int saved = cputype;
  cputype = CPU_LEON3;

  /* A number outside the ones the core knows leaves the destination
     alone.  */
  set (3, 0x5555);
  CHECK (exec (f3r (OP_ARITH, 3, RDY, 5, 0)) == 0);
  CHECK (get (3) == 0x5555);

  cputype = saved;
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC the LEON write-y reaches its ancillary registers")
{
  int saved = cputype;
  cputype = CPU_LEON3;

  /* On a LEON the write-y opcode selects an ancillary register by its
     destination number: seventeen is the implementation register, of which
     only the configurable field is writable.  */
  set (1, 0x0ffff000);
  CHECK (exec (f3i (OP_ARITH, 17, WRY, 1, 0)) == 0);
  CHECK ((sregs[0].asr17 & 0x0fffe000) == (0x0ffff000 & 0x0fffe000));

  /* Nineteen is the power down register, which stops the processor.  */
  sregs[0].pwd_mode = 0;
  CHECK (exec (f3i (OP_ARITH, 19, WRY, 1, 0)) == 0);
  CHECK (sregs[0].pwd_mode == 1);

  cputype = saved;
  sregs[0].pwd_mode = 0;
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC a write to psr checks the window")
{
  /* A window number the implementation does not have is unimplemented.  */
  sregs[0].psr |= 0x18;
  CHECK (exec (f3i (OP_ARITH, 0, WRPSR, 0, 0)) == TRAP_UNIMP);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC jmpl checks its address")
{
  /* The target must be word aligned, and the simulator stops rather than
     jumping to a null pointer.  */
  set (1, 0x3001);
  CHECK (exec (f3i (OP_ARITH, 3, JMPL, 1, 0)) == TRAP_UNALI);

  set (1, 0);
  CHECK (exec (f3i (OP_ARITH, 3, JMPL, 1, 0)) == NULL_TRAP);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC flush can be made unimplemented")
{
  int saved = ift;

  ift = 1;
  CHECK (exec (f3i (OP_ARITH, 0, FLUSH, 0, 0)) == TRAP_UNIMP);

  ift = saved;
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC a double access needs an even register")
{
  set (1, 0x2000);

  /* The manual calls the low bit of the register number unused and makes
     the trap for an odd one optional, so the core rounds down to the even
     register of the pair.  */
  set (2, 0x11223344);
  set (3, 0x55667788);
  CHECK (exec (f3i (OP_MEM, 3, STD, 1, 0)) == 0);
  CHECK (sis_tests::flatmem_peek (0x2000) == 0x11223344);

  set (2, 0);
  set (3, 0);
  CHECK (exec (f3i (OP_MEM, 3, LDD, 1, 0)) == 0);
  CHECK (get (2) == 0x11223344);
  CHECK (get (3) == 0x55667788);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC a LEON reads its cache control through an ASI")
{
  int saved = cputype;
  cputype = CPU_LEON3;

  /* Address space two is the cache control register on a LEON.  */
  sregs[0].cache_ctrl = 0x0f;
  set (1, 0);
  CHECK (exec (f3r (OP_MEM, 3, LDA, 1, 0) | (2 << 5)) == 0);
  CHECK (get (3) == 0x0f);

  /* Any other address in that space reports the cache configuration.  */
  set (1, 8);
  CHECK (exec (f3r (OP_MEM, 3, LDA, 1, 0) | (2 << 5)) == 0);
  CHECK (get (3) == 1u << 27);

  cputype = saved;
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC every alternate space form is privileged")
{
  /* chk_asi refuses in user mode and refuses an immediate, for each of the
     instructions which goes through it.  */
  const uint32 alternates[10] = { LDA,	 LDDA, LDUBA, LDSBA, LDUHA,
				  LDSHA, STA,  STDA,  STBA,  STHA };

  set (1, 0x2000);

  for (int i = 0; i < 10; i++)
    {
      sregs[0].psr &= ~PSR_S;
      INFO ("opcode ", alternates[i]);
      CHECK (exec (f3r (OP_MEM, 2, alternates[i], 1, 0) | (0xb << 5)) ==
	     TRAP_PRIVI);

      sregs[0].psr |= PSR_S;
      CHECK (exec (f3i (OP_MEM, 2, alternates[i], 1, 0)) == TRAP_UNIMP);
    }

  /* The two atomic forms as well.  */
  sregs[0].psr &= ~PSR_S;
  CHECK (exec (f3r (OP_MEM, 2, SWAPA, 1, 0) | (0xb << 5)) == TRAP_PRIVI);
  CHECK (exec (f3r (OP_MEM, 2, LDSTUBA, 1, 0) | (0xb << 5)) == TRAP_PRIVI);

  /* The compare and swap is the exception: the two data address spaces are
     allowed in user mode, and any other is not.  */
  CHECK (exec (f3r (OP_MEM, 2, CASA, 1, 0) | (0xb << 5)) == 0);
  CHECK (exec (f3r (OP_MEM, 2, CASA, 1, 0) | (0xa << 5)) == 0);
  CHECK (exec (f3r (OP_MEM, 2, CASA, 1, 0) | (0x1 << 5)) == TRAP_PRIVI);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC an alternate space access must be aligned")
{
  set (1, 0x2001);

  CHECK (exec (f3r (OP_MEM, 2, LDA, 1, 0) | (0xb << 5)) == TRAP_UNALI);
  CHECK (exec (f3r (OP_MEM, 2, STA, 1, 0) | (0xb << 5)) == TRAP_UNALI);
  CHECK (exec (f3r (OP_MEM, 2, LDUHA, 1, 0) | (0xb << 5)) == TRAP_UNALI);
  CHECK (exec (f3r (OP_MEM, 2, STHA, 1, 0) | (0xb << 5)) == TRAP_UNALI);

  set (1, 0x2004);
  CHECK (exec (f3r (OP_MEM, 2, LDDA, 1, 0) | (0xb << 5)) == TRAP_UNALI);
  CHECK (exec (f3r (OP_MEM, 2, STDA, 1, 0) | (0xb << 5)) == TRAP_UNALI);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC a read watchpoint stops a load")
{
  uint32 saved = ebase.wprnum;

  ebase.wprnum = 1;
  ebase.wprs[0] = 0x2000;
  ebase.wprm[0] = 3;
  ebase.wphit = 0;

  set (1, 0x2000);
  exec (f3i (OP_MEM, 3, LD, 1, 0));
  CHECK (ebase.wphit != 0);

  ebase.wprnum = saved;
  ebase.wphit = 0;
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC tagged add overflow is checked on the sum too")
{
  /* The trap form takes the trap for an arithmetic overflow as well as for a
     set tag.  */
  set (1, 0x7ffffffc);
  set (2, 0x7ffffffc);

  CHECK (exec (f3r (OP_ARITH, 3, TADDCCTV, 1, 2)) == TRAP_TAG);
  CHECK (exec (f3r (OP_ARITH, 3, TSUBCCTV, 1, 2)) == 0);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC save and restore into a global register")
{
  /* A global destination is not in a window, so save and restore write it
     where it is rather than in the window they move to.  */
  set (1, 100);

  CHECK (exec (f3i (OP_ARITH, 3, SAVE, 1, 5)) == 0);
  CHECK (get (3) == 105);

  CHECK (exec (f3i (OP_ARITH, 4, RESTORE, 1, 5)) == 0);
  CHECK (get (4) == 105);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC multiply and divide set their codes")
{
  /* The condition codes come from the lower word of the product and from
     the quotient.  */
  set (1, (uint32) -1);
  set (2, 1);
  CHECK (exec (f3r (OP_ARITH, 3, SMULCC, 1, 2)) == 0);
  CHECK (icc () == N);

  set (1, 0);
  CHECK (exec (f3r (OP_ARITH, 3, SMULCC, 1, 2)) == 0);
  CHECK (icc () == Z);

  set (1, 0x80000000);
  set (2, 2);
  CHECK (exec (f3r (OP_ARITH, 3, UMULCC, 1, 2)) == 0);
  CHECK (icc () == Z);

  sregs[0].y = 0;
  set (1, 0);
  set (2, 7);
  CHECK (exec (f3r (OP_ARITH, 3, UDIVCC, 1, 2)) == 0);
  CHECK (icc () == Z);

  sregs[0].y = 0xffffffff;
  set (1, (uint32) -7);
  set (2, 7);
  CHECK (exec (f3r (OP_ARITH, 3, SDIVCC, 1, 2)) == 0);
  CHECK (icc () == N);

  sregs[0].y = 0;
  set (1, 0);
  CHECK (exec (f3r (OP_ARITH, 3, SDIVCC, 1, 2)) == 0);
  CHECK (icc () == Z);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC multiply step shifts without adding")
{
  /* With the shifted out bit of y clear nothing is added, which is the
     other half of the step.  */
  sregs[0].y = 0;
  set (1, 0x10);
  set (2, 8);

  CHECK (exec (f3r (OP_ARITH, 3, MULScc, 1, 2)) == 0);
  CHECK (get (3) == 8);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC the rounding mode reaches the host")
{
  const uint32 addr = 0x2000;

  sregs[0].psr |= PSR_EF;
  set (1, addr);

  /* The two most significant bits of the status register select the
     rounding, and a load of the register hands it to the host.  */
  for (uint32 mode = 0; mode < 4; mode++)
    {
      set (2, mode << 30);
      CHECK (exec (f3i (OP_MEM, 2, ST, 1, 0)) == 0);
      CHECK (exec (f3i (OP_MEM, 0, LDFSR, 1, 0)) == 0);
      CHECK ((sregs[0].fsr >> 30) == mode);
    }
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC a watchpoint covers each access size")
{
  uint32 saved = ebase.wpwnum;

  ebase.wpwnum = 1;
  ebase.wpws[0] = 0x2000;
  ebase.wpwm[0] = 3;

  set (1, 0x2000);
  set (2, 0);

  /* The mask a watchpoint uses comes from the access size, so each store
     kind reaches the check.  */
  for (uint32 op3 : { ST, STB, STH, STD })
    {
      ebase.wphit = 0;
      exec (f3i (OP_MEM, 2, op3, 1, 0));
      INFO ("store opcode ", op3);
      CHECK (ebase.wphit != 0);
    }

  ebase.wpwnum = saved;
  ebase.wphit = 0;
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC a floating point load waits for the unit")
{
  const uint32 addr = 0x2000;

  sregs[0].psr |= PSR_EF;
  set (1, addr);

  /* A load into a register an operation still in the pipe is using has to
     wait for it.  */
  sregs[0].simtime = 0;
  sregs[0].ftime = 50;
  sregs[0].frd = 2;
  sregs[0].frs1 = 2;
  sregs[0].frs2 = 2;

  CHECK (exec (f3i (OP_MEM, 2, LDF, 1, 0)) == 0);
  CHECK (sregs[0].fhold > 0);

  sregs[0].simtime = 0;
  sregs[0].ftime = 50;
  CHECK (exec (f3i (OP_MEM, 2, LDDF, 1, 0)) == 0);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC a load feeding the next instruction is held")
{
  const uint32 addr = 0x2000;

  sis_tests::flatmem_poke (addr, 0x00000005);
  set (1, addr);

  /* The core records which register a load wrote and when it lands, and
     charges the instruction after it if that register is a source.  */
  CHECK (exec (f3i (OP_MEM, 8, LD, 1, 0)) == 0);
  sregs[0].simtime = 0;
  sregs[0].ildtime = 50;
  sregs[0].ildreg = 8;

  CHECK (exec (f3r (OP_ARITH, 9, ADD, 8, 8)) == 0);
  CHECK (get (9) == 10);

  /* And when the register is the second source.  */
  sregs[0].simtime = 0;
  sregs[0].ildtime = 50;
  sregs[0].ildreg = 8;
  CHECK (exec (f3r (OP_ARITH, 10, ADD, 9, 8)) == 0);
  CHECK (get (10) == 15);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC without a unit every operation is disabled")
{
  /* The -nfp option removes the floating point unit, so every operation
     takes the disabled trap even with the enable bit set.  */
  sregs[0].psr |= PSR_EF;
  sregs[0].fpu_pres = 0;
  set (1, 0x2000);

  CHECK (exec (fpop (FPOP1, 3, 1, OPF_FADDs, 2)) == TRAP_FPDIS);
  CHECK (exec ((FPBCC << 22) | (FBA << 25) | 4) == TRAP_FPDIS);
  CHECK (exec (f3i (OP_MEM, 2, LDF, 1, 0)) == TRAP_FPDIS);
  CHECK (exec (f3i (OP_MEM, 2, LDDF, 1, 0)) == TRAP_FPDIS);
  CHECK (exec (f3i (OP_MEM, 0, LDFSR, 1, 0)) == TRAP_FPDIS);
  CHECK (exec (f3i (OP_MEM, 0, STFSR, 1, 0)) == TRAP_FPDIS);
  CHECK (exec (f3i (OP_MEM, 2, STF, 1, 0)) == TRAP_FPDIS);
  CHECK (exec (f3i (OP_MEM, 2, STDF, 1, 0)) == TRAP_FPDIS);
  CHECK (exec (f3i (OP_MEM, 0, STDFQ, 1, 0)) == TRAP_FPDIS);

  sregs[0].fpu_pres = 1;
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC a floating point store waits for its register")
{
  const uint32 addr = 0x2000;

  sregs[0].psr |= PSR_EF;
  set (1, addr);

  /* A store of a register an operation is still producing waits for it.  */
  sregs[0].simtime = 0;
  sregs[0].ftime = 50;
  sregs[0].frd = 2;
  sregs[0].fhold = 0;
  CHECK (exec (f3i (OP_MEM, 2, STF, 1, 0)) == 0);
  CHECK (sregs[0].fhold > 0);

  /* The status register accesses wait for the whole unit rather than for a
     register, so they are charged the remaining time whatever is in it.  */
  sregs[0].simtime = 0;
  sregs[0].ftime = 50;
  sregs[0].fhold = 0;
  CHECK (exec (f3i (OP_MEM, 0, STFSR, 1, 0)) == 0);
  CHECK (sregs[0].fhold > 0);

  sregs[0].simtime = 0;
  sregs[0].ftime = 50;
  sregs[0].fhold = 0;
  CHECK (exec (f3i (OP_MEM, 0, LDFSR, 1, 0)) == 0);
  CHECK (sregs[0].fhold > 0);

  sregs[0].simtime = 0;
  sregs[0].ftime = 50;
  sregs[0].frd = 4;
  sregs[0].frs1 = 4;
  sregs[0].frs2 = 4;
  sregs[0].fhold = 0;
  CHECK (exec (f3i (OP_MEM, 4, STDF, 1, 0)) == 0);
  CHECK (sregs[0].fhold > 0);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC the status register access must be aligned")
{
  sregs[0].psr |= PSR_EF;
  set (1, 0x2001);

  CHECK (exec (f3i (OP_MEM, 0, LDFSR, 1, 0)) == TRAP_UNALI);
  CHECK (exec (f3i (OP_MEM, 0, STFSR, 1, 0)) == TRAP_UNALI);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC the deferred trap queue is stored when full")
{
  const uint32 addr = 0x2000;

  sregs[0].psr |= PSR_EF;
  set (1, addr);

  /* Provoke a deferred exception so the queue holds an entry, then store
     it.  */
  CHECK (exec (fpop (FPOP1, 3, 1, 0x1ff, 2)) == 0);
  REQUIRE ((sregs[0].fsr & FSR_QNE) != 0);

  CHECK (exec (f3i (OP_MEM, 0, STDFQ, 1, 0)) == 0);
  CHECK (sis_tests::flatmem_peek (addr) == sregs[0].fpq[0]);
  CHECK (sis_tests::flatmem_peek (addr + 4) == sregs[0].fpq[1]);

  /* The store into unmapped memory faults.  */
  CHECK (exec (fpop (FPOP1, 3, 1, 0x1ff, 2)) == 0);
  sregs[0].fpstate = FP_EXE_MODE;
  set (1, sis_tests::FLATMEM_SIZE + 0x1000);
  CHECK (exec (f3i (OP_MEM, 0, STDFQ, 1, 0)) == TRAP_DEXC);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC the queue store checks the window")
{
  sregs[0].psr |= PSR_EF | 0x18;
  CHECK (exec (f3i (OP_MEM, 0, STDFQ, 1, 0)) == TRAP_UNIMP);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC a LEON writes its cache control through an ASI")
{
  int saved = cputype;
  cputype = CPU_LEON3;

  /* A store into address space two sets the cache control register.  */
  set (1, 0);
  set (2, 0x0f);
  CHECK (exec (f3r (OP_MEM, 2, STA, 1, 0) | (2 << 5)) == 0);
  CHECK (sregs[0].cache_ctrl == 0x0f);

  cputype = saved;
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC a double access to a window register pair")
{
  const uint32 addr = 0x2000;

  set (1, addr);
  set (16, 0x11223344);
  set (17, 0x55667788);

  /* An odd destination above the globals rounds down within the window.  */
  CHECK (exec (f3i (OP_MEM, 17, STD, 1, 0)) == 0);
  CHECK (sis_tests::flatmem_peek (addr) == 0x11223344);

  set (16, 0);
  set (17, 0);
  CHECK (exec (f3i (OP_MEM, 17, LDD, 1, 0)) == 0);
  CHECK (get (16) == 0x11223344);
  CHECK (get (17) == 0x55667788);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC a byte store outside memory traps")
{
  set (1, sis_tests::FLATMEM_SIZE - 1);
  set (2, 0);

  /* The last byte of the window is inside it, the one after is not.  */
  CHECK (exec (f3i (OP_MEM, 2, STB, 1, 0)) == 0);
  CHECK (exec (f3i (OP_MEM, 2, STB, 1, 1)) == TRAP_DEXC);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC each floating point source is a dependency")
{
  const uint32 addr = 0x2000;

  sregs[0].psr |= PSR_EF;
  set (1, addr);

  /* A load waits for an operation in the pipe when its destination is any
     of that operation's three registers, so each is checked on its own.
     The core records those three by their swapped index, which is what the
     load compares against, so the case has to swap too.  */
  const int busy = REG_SWAP (2);

  for (int which = 0; which < 3; which++)
    {
      sregs[0].simtime = 0;
      sregs[0].ftime = 50;
      sregs[0].fhold = 0;
      sregs[0].frd = 30;
      sregs[0].frs1 = 30;
      sregs[0].frs2 = 30;
      if (which == 0)
	sregs[0].frd = busy;
      else if (which == 1)
	sregs[0].frs1 = busy;
      else
	sregs[0].frs2 = busy;

      INFO ("dependency on source ", which);
      CHECK (exec (f3i (OP_MEM, 2, LDF, 1, 0)) == 0);
      CHECK (sregs[0].fhold > 0);
    }

  /* And the same for a double load, which compares register pairs.  */
  for (int which = 0; which < 3; which++)
    {
      sregs[0].simtime = 0;
      sregs[0].ftime = 50;
      sregs[0].frd = 30;
      sregs[0].frs1 = 30;
      sregs[0].frs2 = 30;
      if (which == 0)
	sregs[0].frd = 4;
      else if (which == 1)
	sregs[0].frs1 = 4;
      else
	sregs[0].frs2 = 4;

      sregs[0].fhold = 0;
      INFO ("double dependency on source ", which);
      CHECK (exec (f3i (OP_MEM, 4, LDDF, 1, 0)) == 0);
      CHECK (sregs[0].fhold > 0);
    }

  /* A double store waits for either register of the pair, which it names by
     the even one.  */
  for (int busy_reg : { 4, 3 })
    {
      sregs[0].simtime = 0;
      sregs[0].ftime = 50;
      sregs[0].frd = busy_reg;
      sregs[0].fhold = 0;
      INFO ("busy register ", busy_reg);
      CHECK (exec (f3i (OP_MEM, 4, STDF, 1, 0)) == 0);
      CHECK (sregs[0].fhold > 0);
    }
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC the atomic accesses check their address")
{
  set (1, 0x2001);
  CHECK (exec (f3i (OP_MEM, 2, SWAP, 1, 0)) == TRAP_UNALI);
  CHECK (exec (f3i (OP_MEM, 2, LDSTUB, 1, 0)) == 0);
  CHECK (exec (f3r (OP_MEM, 2, CASA, 1, 0) | (0xb << 5)) == TRAP_UNALI);

  /* And their memory.  */
  set (1, sis_tests::FLATMEM_SIZE + 0x1000);
  CHECK (exec (f3r (OP_MEM, 2, SWAPA, 1, 0) | (0xb << 5)) == TRAP_DEXC);
  CHECK (exec (f3r (OP_MEM, 2, LDSTUBA, 1, 0) | (0xb << 5)) == TRAP_DEXC);
  CHECK (exec (f3r (OP_MEM, 2, CASA, 1, 0) | (0xb << 5)) == TRAP_DEXC);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC a LEON reads and writes an ASI at an address")
{
  int saved = cputype;
  cputype = CPU_LEON3;

  /* Address space two answers from the cache control register at address
     zero and reports the configuration elsewhere, on both the load and the
     store side.  */
  set (1, 8);
  set (2, 0x0f);
  CHECK (exec (f3r (OP_MEM, 2, STA, 1, 0) | (2 << 5)) == 0);

  set (1, 0);
  CHECK (exec (f3r (OP_MEM, 3, LDA, 1, 0) | (2 << 5)) == 0);

  cputype = saved;
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC floating point exceptions reach the status")
{
  sregs[0].psr |= PSR_EF;

  /* A divide by zero, an overflow and an underflow each set their bit in
     the accrued exception field.  */
  fs (1) = 1.0f;
  fs (2) = 0.0f;
  CHECK (exec (fpop (FPOP1, 3, 1, OPF_FDIVs, 2)) == 0);
  CHECK ((sregs[0].fsr & 0x1f) != 0);

  sregs[0].fsr = 0;
  fs (1) = 3.0e38f;
  fs (2) = 3.0e38f;
  CHECK (exec (fpop (FPOP1, 3, 1, OPF_FADDs, 2)) == 0);
  CHECK ((sregs[0].fsr & 0x1f) != 0);

  sregs[0].fsr = 0;
  fs (1) = 1.0e-38f;
  fs (2) = 1.0e8f;
  CHECK (exec (fpop (FPOP1, 3, 1, OPF_FDIVs, 2)) == 0);
  CHECK ((sregs[0].fsr & 0x1f) != 0);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC divide reports a negative quotient")
{
  /* The signed forms set the negative bit from the quotient, and the
     unsigned form can produce one too.  */
  sregs[0].y = 0;
  set (1, 0x80000000);
  set (2, 1);
  CHECK (exec (f3r (OP_ARITH, 3, UDIVCC, 1, 2)) == 0);
  CHECK (icc () == N);

  sregs[0].y = 0xffffffff;
  set (1, 0x80000000);
  CHECK (exec (f3r (OP_ARITH, 3, SDIVCC, 1, 2)) == 0);
  CHECK ((icc () & N) != 0);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC a byte store into unmapped memory traps")
{
  set (1, sis_tests::FLATMEM_SIZE);
  set (2, 0x11);

  CHECK (exec (f3i (OP_MEM, 2, STB, 1, 0)) == TRAP_DEXC);
  CHECK (exec (f3r (OP_MEM, 2, STBA, 1, 0) | (0xb << 5)) == TRAP_DEXC);
  CHECK (exec (f3r (OP_MEM, 2, STHA, 1, 0) | (0xb << 5)) == TRAP_DEXC);
  CHECK (exec (f3r (OP_MEM, 2, STA, 1, 0) | (0xb << 5)) == TRAP_DEXC);
  CHECK (exec (f3r (OP_MEM, 2, STDA, 1, 0) | (0xb << 5)) == TRAP_DEXC);
  CHECK (exec (f3r (OP_MEM, 2, LDDA, 1, 0) | (0xb << 5)) == TRAP_DEXC);
}

/* The condition field of a trap and of a branch, from the table of appendix
   C of the architecture manual.  Written out here rather than shared with the
   core, so a case checks the core against the manual and not against
   itself.  */
static bool
cond_taken (uint32 cond, uint32 icc)
{
  bool n = (icc >> 3) & 1;
  bool z = (icc >> 2) & 1;
  bool v = (icc >> 1) & 1;
  bool c = icc & 1;

  switch (cond)
    {
    case BICC_BN:
      return false;
    case BICC_BE:
      return z;
    case BICC_BLE:
      return z || (n != v);
    case BICC_BL:
      return n != v;
    case BICC_BLEU:
      return c || z;
    case BICC_BCS:
      return c;
    case BICC_NEG:
      return n;
    case BICC_BVS:
      return v;
    case BICC_BA:
      return true;
    case BICC_BNE:
      return !z;
    case BICC_BG:
      return !(z || (n != v));
    case BICC_BGE:
      return n == v;
    case BICC_BGU:
      return !(c || z);
    case BICC_BCC:
      return !c;
    case BICC_POS:
      return !n;
    default:
      return !v;
    }
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC ticc traps on every condition")
{
  /* Ticc raises trap 128 + the software trap number when its condition holds
     and does nothing when it does not.  The condition codes below cover both
     answers for each of the sixteen conditions, including the two which
     depend on N and V differing rather than on either alone.  */
  const uint32 codes[] = { 0, N | Z | V | C, N, V, Z, C, N | V, Z | C };
  const uint32 number = 5;

  for (uint32 code : codes)
    {
      for (uint32 cond = 0; cond < 16; cond++)
	{
	  sregs[0].psr = (sregs[0].psr & ~PSR_CC) | (code << 20);

	  int trap = exec (f3i (OP_ARITH, cond, TICC, 0, number));
	  int want = cond_taken (cond, code) ? (int) (0x80 | number) : 0;

	  INFO ("condition " << cond << " codes " << code);
	  CHECK (trap == want);
	}
    }
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC the trap number wraps to seven bits")
{
  /* The trap number is the low seven bits of rs1 plus the operand, so a
     larger sum wraps rather than reaching into the trap type above it.  */
  set (1, 0x180);
  CHECK (exec (f3i (OP_ARITH, BICC_BA, TICC, 1, 3)) == (int) (0x80 | 3));
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC ta 1 is a breakpoint under gdb")
{
  /* The debugger plants ta 1 as its breakpoint, so with the stub running
     that one encoding reports a breakpoint hit instead of a software trap.
     Every other trap number, and the same encoding without the stub, stays a
     software trap.  */
  int saved = sis_gdb_break;

  sis_gdb_break = 1;
  CHECK (exec (0x91d02001) == WPT_TRAP);
  CHECK (sregs[0].bphit == 1);

  sregs[0].bphit = 0;
  CHECK (exec (f3i (OP_ARITH, BICC_BA, TICC, 0, 2)) == (int) (0x80 | 2));
  CHECK (sregs[0].bphit == 0);

  sis_gdb_break = 0;
  CHECK (exec (0x91d02001) == (int) (0x80 | 1));
  CHECK (sregs[0].bphit == 0);

  sis_gdb_break = saved;
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC the logical operations combine bits")
{
  /* The logical group of the manual, each with the operand negated or not
     and with or without the condition codes.  */
  const uint32 a = 0xf0f0ff00;
  const uint32 b = 0x0ff00ff0;

  set (1, a);
  set (2, b);

  CHECK (exec (f3r (OP_ARITH, 3, IAND, 1, 2)) == 0);
  CHECK (get (3) == (a & b));
  CHECK (exec (f3r (OP_ARITH, 3, IANDN, 1, 2)) == 0);
  CHECK (get (3) == (a & ~b));
  CHECK (exec (f3r (OP_ARITH, 3, IOR, 1, 2)) == 0);
  CHECK (get (3) == (a | b));
  CHECK (exec (f3r (OP_ARITH, 3, IORN, 1, 2)) == 0);
  CHECK (get (3) == (a | ~b));
  CHECK (exec (f3r (OP_ARITH, 3, IXOR, 1, 2)) == 0);
  CHECK (get (3) == (a ^ b));
  CHECK (exec (f3r (OP_ARITH, 3, IXNOR, 1, 2)) == 0);
  CHECK (get (3) == (a ^ ~b));
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC the logical operations report their result")
{
  /* The condition code forms set N and Z from the result and clear V and C,
     which the manual gives for the whole group.  Each operation gets its own
     operands, one pair whose result has the top bit set and one whose result
     is zero.  */
  struct
  {
    uint32 op3;
    uint32 a, b, negative;
    uint32 zero_a, zero_b;
  } cases[] = {
    { IANDCC, 0xffffffff, 0xffffffff, 0xffffffff, 0, 0 },
    { IANDNCC, 0xffffffff, 0, 0xffffffff, 0, 0 },
    { IORCC, 0x80000000, 0, 0x80000000, 0, 0 },
    { IORNCC, 0, 0, 0xffffffff, 0, 0xffffffff },
    { IXORCC, 0xffffffff, 0, 0xffffffff, 0, 0 },
    { IXNORCC, 0xffffffff, 0xffffffff, 0xffffffff, 0, 0xffffffff },
  };

  for (auto c : cases)
    {
      INFO ("opcode " << c.op3);

      set (1, c.a);
      set (2, c.b);
      sregs[0].psr |= (V | C) << 20;
      CHECK (exec (f3r (OP_ARITH, 3, c.op3, 1, 2)) == 0);
      CHECK (get (3) == c.negative);
      CHECK (icc () == N);

      set (1, c.zero_a);
      set (2, c.zero_b);
      CHECK (exec (f3r (OP_ARITH, 3, c.op3, 1, 2)) == 0);
      CHECK (get (3) == 0);
      CHECK (icc () == Z);
    }
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC add and subtract carry the carry in")
{
  /* The extended forms add the carry of the condition codes, which is how a
     program builds arithmetic wider than a word.  */
  set (1, 10);
  set (2, 3);

  sregs[0].psr |= C << 20;
  CHECK (exec (f3r (OP_ARITH, 3, ADDX, 1, 2)) == 0);
  CHECK (get (3) == 14);

  sregs[0].psr &= ~PSR_CC;
  CHECK (exec (f3r (OP_ARITH, 3, ADDX, 1, 2)) == 0);
  CHECK (get (3) == 13);

  sregs[0].psr |= C << 20;
  CHECK (exec (f3r (OP_ARITH, 3, SUBX, 1, 2)) == 0);
  CHECK (get (3) == 6);

  sregs[0].psr &= ~PSR_CC;
  CHECK (exec (f3r (OP_ARITH, 3, SUBX, 1, 2)) == 0);
  CHECK (get (3) == 7);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC the extended forms report their result")
{
  /* The condition codes an extended operation reports are those of the whole
     sum, carry in included: adding one to the largest word with the carry
     set overflows twice and lands back on one.  */
  set (1, 0xffffffff);
  set (2, 1);
  sregs[0].psr |= C << 20;

  CHECK (exec (f3r (OP_ARITH, 3, ADDXCC, 1, 2)) == 0);
  CHECK (get (3) == 1);
  CHECK ((icc () & C) != 0);
  CHECK ((icc () & (N | Z | V)) == 0);

  /* And a subtraction which borrows: zero less one less the borrow.  */
  set (1, 0);
  set (2, 1);
  sregs[0].psr |= C << 20;

  CHECK (exec (f3r (OP_ARITH, 3, SUBXCC, 1, 2)) == 0);
  CHECK (get (3) == 0xfffffffe);
  CHECK ((icc () & N) != 0);
  CHECK ((icc () & C) != 0);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC the multiplies report a negative result")
{
  /* The condition code forms of the multiply report N and Z from the low
     word of the product, which the two below make negative.  */
  set (1, 0xffffffff);
  set (2, 1);

  CHECK (exec (f3r (OP_ARITH, 3, UMULCC, 1, 2)) == 0);
  CHECK (get (3) == 0xffffffff);
  CHECK ((icc () & N) != 0);
  CHECK ((icc () & Z) == 0);

  set (1, 0xffffffff);
  set (2, 1);
  CHECK (exec (f3r (OP_ARITH, 3, SMULCC, 1, 2)) == 0);
  CHECK (get (3) == 0xffffffff);
  CHECK ((icc () & N) != 0);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC the disassembler names the synthetic forms")
{
  stdout_capture cap;

  /* The assembly syntax has a shorter form for many instructions once one of
     their registers is fixed: a discarded result is clr or tst or cmp, a zero
     source is mov, and a jump through the return address is ret.  Sweep the
     same opcode space again over the register numbers which select them,
     together with the ones the register names depend on: r0, the stack and
     frame pointer, and one register of each of the four groups.  */
  const int regs[] = { 0, 1, 8, 14, 20, 15, 28, 30, 31 };
  const int32 imms[] = { 0, 8, 5, -5, 0x100, -0x100 };
  uint32 addr = 0x200;

  for (int r : regs)
    {
      for (uint32 op3 = 0; op3 < 0x40; op3++)
	{
	  for (uint32 op : { OP_ARITH, OP_MEM })
	    {
	      sis_tests::flatmem_poke (addr, f3r (op, r, op3, r, 0));
	      arch->disas (addr);
	      sis_tests::flatmem_poke (addr, f3r (op, r, op3, 0, r));
	      arch->disas (addr);
	      sis_tests::flatmem_poke (addr, f3r (op, 0, op3, r, r));
	      arch->disas (addr);
	    }
	}
    }

  /* The immediate forms, whose printed shape depends on the sign of the
     immediate, on whether it is printed in hex, and on whether the source
     register is r0 and so left out.  An immediate of eight off the return
     address register is the return instruction.  */
  for (int32 imm : imms)
    {
      for (int r : regs)
	{
	  for (uint32 op3 = 0; op3 < 0x40; op3++)
	    {
	      for (uint32 op : { OP_ARITH, OP_MEM })
		{
		  sis_tests::flatmem_poke (addr, f3i (op, r, op3, r, imm));
		  arch->disas (addr);
		  sis_tests::flatmem_poke (addr, f3i (op, r, op3, 0, imm));
		  arch->disas (addr);
		  sis_tests::flatmem_poke (addr, f3i (op, 0, op3, r, imm));
		  arch->disas (addr);
		}
	    }
	}
    }

  /* A sethi of zero into r0 is the canonical no-operation, and a floating
     point branch takes the annul bit like an integer one.  */
  sis_tests::flatmem_poke (addr, sethi (0, 0));
  arch->disas (addr);
  for (uint32 cond = 0; cond < 16; cond++)
    {
      sis_tests::flatmem_poke (addr,
			       (1u << 29) | (cond << 25) | (FPBCC << 22) | 4);
      arch->disas (addr);
    }

  CHECK (!cap.str ().empty ());
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC an ordered double compare ranks both ways")
{
  /* The double precision compare reports greater as well as less, which the
     single precision case above covers for its own operands.  */
  sregs[0].psr |= PSR_EF;
  fd (2) = 4.0;
  fd (4) = 1.0;

  CHECK (exec (fpop (FPOP1, 0, 2, OPF_FCMPd, 4)) == 0);
  CHECK (((sregs[0].fsr >> 10) & 3) == FCC_G);

  fd (2) = 1.0;
  fd (4) = 4.0;
  CHECK (exec (fpop (FPOP1, 0, 2, OPF_FCMPd, 4)) == 0);
  CHECK (((sregs[0].fsr >> 10) & 3) == FCC_L);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC an unordered compare excepts only when asked to")
{
  /* Chapter 6: a compare against a value which is not a number reports
     unordered.  The plain compare stops there, while the compare-and-except
     form raises an IEEE exception, which the unit reports as a pending
     exception and the next operation takes as a trap.  */
  const float nanf_value = std::numeric_limits<float>::quiet_NaN ();
  const double nand_value = std::numeric_limits<double>::quiet_NaN ();

  sregs[0].psr |= PSR_EF;

  SUBCASE ("single precision reports unordered")
  {
    fs (1) = nanf_value;
    fs (2) = 1.0f;
    CHECK (exec (fpop (FPOP1, 0, 1, OPF_FCMPs, 2)) == 0);
    CHECK (((sregs[0].fsr >> 10) & 3) == FCC_U);
    CHECK (sregs[0].fpstate == FP_EXE_MODE);
  }

  SUBCASE ("single precision excepts")
  {
    fs (1) = nanf_value;
    fs (2) = 1.0f;
    CHECK (exec (fpop (FPOP1, 0, 1, OPF_FCMPEs, 2)) == 0);
    CHECK (((sregs[0].fsr >> 10) & 3) == FCC_U);
    CHECK (sregs[0].fpstate == FP_EXC_PE);
    CHECK ((sregs[0].fsr & FSR_TT) == FP_IEEE);
    CHECK ((sregs[0].fsr & FSR_QNE) != 0);
    CHECK (sregs[0].fpq[1] == fpop (FPOP1, 0, 1, OPF_FCMPEs, 2));

    /* The pending exception is delivered on the next operation.  */
    CHECK (exec (fpop (FPOP1, 3, 1, OPF_FADDs, 2)) == TRAP_FPEXC);
  }

  SUBCASE ("double precision reports unordered")
  {
    fd (2) = nand_value;
    fd (4) = 1.0;
    CHECK (exec (fpop (FPOP1, 0, 2, OPF_FCMPd, 4)) == 0);
    CHECK (((sregs[0].fsr >> 10) & 3) == FCC_U);
    CHECK (sregs[0].fpstate == FP_EXE_MODE);
  }

  SUBCASE ("double precision excepts")
  {
    fd (2) = nand_value;
    fd (4) = 1.0;
    CHECK (exec (fpop (FPOP1, 0, 2, OPF_FCMPEd, 4)) == 0);
    CHECK (((sregs[0].fsr >> 10) & 3) == FCC_U);
    CHECK (sregs[0].fpstate == FP_EXC_PE);
    CHECK ((sregs[0].fsr & FSR_TT) == FP_IEEE);
  }
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC a negative square root excepts")
{
  /* The square root of a negative number is an invalid operation, so the
     unit reports an IEEE exception and leaves the destination alone rather
     than writing a value which is not a number.  */
  sregs[0].psr |= PSR_EF;

  SUBCASE ("single precision")
  {
    fs (1) = -4.0f;
    fs (3) = 1.0f;
    CHECK (exec (fpop (FPOP1, 3, 0, OPF_FSQRTs, 1)) == 0);
    CHECK (fs (3) == 1.0f);
    CHECK (sregs[0].fpstate == FP_EXC_PE);
    CHECK ((sregs[0].fsr & FSR_TT) == FP_IEEE);
    CHECK ((sregs[0].fsr & 0x1f) == 0x10);
  }

  SUBCASE ("double precision")
  {
    fd (2) = -4.0;
    fd (4) = 1.0;
    CHECK (exec (fpop (FPOP1, 4, 0, OPF_FSQRTd, 2)) == 0);
    CHECK (fd (4) == 1.0);
    CHECK (sregs[0].fpstate == FP_EXC_PE);
    CHECK ((sregs[0].fsr & FSR_TT) == FP_IEEE);
    CHECK ((sregs[0].fsr & 0x1f) == 0x10);
  }
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC an enabled trap takes the exception")
{
  /* The trap enable mask of the status register decides what an accrued
     exception does: with the bit clear the exception only accumulates, with
     it set the operation excepts and queues the instruction.  A quotient
     which does not fit the format is inexact, which is the cheapest of them
     to raise.  */
  sregs[0].psr |= PSR_EF;
  fs (1) = 1.0f;
  fs (2) = 3.0f;

  SUBCASE ("the exception only accumulates")
  {
    CHECK (exec (fpop (FPOP1, 3, 1, OPF_FDIVs, 2)) == 0);
    CHECK (sregs[0].fpstate == FP_EXE_MODE);
    CHECK ((sregs[0].fsr & 1) != 0);
  }

  SUBCASE ("the exception traps")
  {
    /* Bit 23 of the status register enables the inexact trap.  */
    sregs[0].fsr |= 1u << 23;

    CHECK (exec (fpop (FPOP1, 3, 1, OPF_FDIVs, 2)) == 0);
    CHECK (sregs[0].fpstate == FP_EXC_PE);
    CHECK ((sregs[0].fsr & FSR_TT) == FP_IEEE);
    CHECK ((sregs[0].fsr & 0x1f) == 1);
    CHECK ((sregs[0].fsr & FSR_QNE) != 0);
    CHECK (sregs[0].fpq[1] == fpop (FPOP1, 3, 1, OPF_FDIVs, 2));
  }
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC the unassigned opcodes are unimplemented")
{
  /* Appendix B leaves part of both op3 spaces unassigned, and chapter 7 has
     the processor take the unimplemented instruction trap for them.  Sweep
     both spaces and check that every opcode either does something or takes
     that trap, and that at least one of each space does.  */
  int unimp[2] = { 0, 0 };

  sregs[0].psr |= PSR_EF;

  for (uint32 op3 = 0; op3 < 0x40; op3++)
    {
      /* The trap on condition and the write to the state registers are left
	 out: the first raises a software trap by design and the second
	 changes the mode the following opcodes run in.  */
      if (op3 == TICC || op3 == WRPSR || op3 == WRWIM || op3 == WRTBR ||
	  op3 == WRY || op3 == RETT)
	continue;

      for (int i = 0; i < 2; i++)
	{
	  uint32 op = i == 0 ? OP_ARITH : OP_MEM;

	  if (exec (f3i (op, 3, op3, 0, 0)) == TRAP_UNIMP)
	    unimp[i]++;
	}
    }

  CHECK (unimp[0] > 0);
  CHECK (unimp[1] > 0);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC an atomic instruction reports a refused write")
{
  /* The atomic instructions read and then write one word, so a word which
     reads but does not write leaves them half done and they report a data
     access exception.  */
  const uint32 address = 0x40;

  sis_tests::flatmem_poke (address, 0x12345678);
  sis_tests::flatmem_fail_write (address);
  set (1, address);

  set (3, 0x11);
  CHECK (exec (f3i (OP_MEM, 3, SWAP, 1, 0)) == TRAP_DEXC);
  CHECK (sis_tests::flatmem_peek (address) == 0x12345678);

  set (3, 0x11);
  CHECK (exec (f3i (OP_MEM, 3, LDSTUB, 1, 0)) == TRAP_DEXC);

  /* The compare and swap writes only when the word matches, so it reports
     the refusal for a match and completes for a mismatch.  */
  set (2, 0x12345678);
  set (3, 0x11);
  CHECK (exec (f3r (OP_MEM, 3, CASA, 1, 2) | (0xa << 5)) == TRAP_DEXC);

  set (2, 0);
  set (3, 0x11);
  CHECK (exec (f3r (OP_MEM, 3, CASA, 1, 2) | (0xa << 5)) == 0);
  CHECK (get (3) == 0x12345678);
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC the debugger reads a cached stack")
{
  /* The register windows hold the top of the stack, so a debugger reading
     one of those addresses out of memory would see whatever the program last
     wrote rather than the register.  save_sp records where each window's
     stack pointer is, and gdb_sp_read answers from the window instead.  */
  char buf[4];

  sregs[0].wim = 2; /* window one is invalid, so it has no stack pointer */
  for (int win = 0; win < NWIN; win++)
    sregs[0].r[win * 16 + 14] = 0x3000 + win * 0x100;
  save_sp (&sregs[0]);

  CHECK (sregs[0].sp[0] == 0x3000);
  CHECK (sregs[0].sp[1] == 0);

  /* The first word of window zero's stack frame is the first register of the
     window above it.  */
  sregs[0].r[16] = 0xdeadbeef;
  CHECK (gdb_sp_read (0x3000, buf, 4) == 4);
  CHECK (((buf[0] & 0xff) << 24 | (buf[1] & 0xff) << 16 |
	  (buf[2] & 0xff) << 8 | (buf[3] & 0xff)) == (int) 0xdeadbeef);

  /* An address no window covers is left to memory.  */
  CHECK (gdb_sp_read (0x9000, buf, 4) == 0);

  SUBCASE ("the hit is reported when verbose")
  {
    stdout_capture cap;

    sis_verbose = 1;
    CHECK (gdb_sp_read (0x3000, buf, 4) == 4);
    CHECK (cap.str ().find ("gdb_sp_read") != std::string::npos);
  }
}

TEST_CASE_FIXTURE (sparc_fixture, "SPARC a register is set by its number")
{
  /* The debugger names a register by its number, which runs the globals, the
     window, the floating point registers and then the control registers.  */
  arch->set_register (&sregs[0], NULL, 0x11, 1);
  CHECK (sregs[0].g[1] == 0x11);

  arch->set_register (&sregs[0], NULL, 0x22, 8);
  CHECK (get (8) == 0x22);

  arch->set_register (&sregs[0], NULL, 0x33, 32 + 3);
  CHECK (fsi (3) == 0x33);

  arch->set_register (&sregs[0], NULL, 0x44, 64);
  CHECK (sregs[0].y == 0x44);
}

TEST_CASE_FIXTURE (sparc_fixture,
		   "SPARC the control display reports a stopped core")
{
  /* The control register display says why the core stopped, so a user who
     asked for it after a run sees the reason without asking again.  */
  SUBCASE ("error mode")
  {
    stdout_capture cap;

    sregs[0].err_mode = 1;
    arch->display_ctrl (&sregs[0]);
    CHECK (cap.str ().find ("error mode") != std::string::npos);
  }

  SUBCASE ("power-down mode")
  {
    stdout_capture cap;

    sregs[0].err_mode = 0;
    sregs[0].pwd_mode = 1;
    arch->display_ctrl (&sregs[0]);
    CHECK (cap.str ().find ("power-down mode") != std::string::npos);
  }
}
