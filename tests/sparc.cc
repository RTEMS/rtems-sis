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
