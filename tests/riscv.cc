/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Tests for riscv.cc, the RV32 core.

   The core reaches memory only through the memsys vtable, so the instruction
   cases drive it against the flat memory of tests/cpumem.cc rather than a
   board.  A case builds one instruction word, puts the machine in a known
   state and calls the core's dispatch entry directly, so what it asserts on
   is the instruction and nothing else.

   The expectations come from the RISC-V unprivileged and privileged
   specifications in ref/: the instruction formats and the operation of each
   opcode, and the trap causes of the machine level.

   riscv_execute_trap charges a trap the same way sparc.cc does: TRAP_C cycles
   plus three bits of pseudo random jitter.  A trap always costs at least
   TRAP_C, and the jitter must not follow the simulated time.  */

#include "doctest.h"
#include "support.h"

#include "config.h"
#include "riscv.h"

#include "cpumem.h"

#include <limits>
#include <string>

using sis_tests::stdout_capture;

/* The coverage bitmap of func.cc, one byte per word of memory.  It has no
   declaration in sis.h because nothing outside func.cc reads it.  */
extern unsigned char covram[];

namespace
{

/* An EBREAK trap sets mtval and falls straight through to the cost line.  It
   is not in the interrupt range, so no board callback is reached and a zeroed
   pstate is enough.  */
uint32
trap_icnt (uint64 ninst, uint64 simtime)
{
  struct pstate s = {};

  s.trap = TRAP_EBREAK;
  s.ninst = ninst;
  s.simtime = simtime;

  uint32 saved_coven = ebase.coven;
  int saved_verbose = sis_verbose;
  ebase.coven = 0;
  sis_verbose = 0;
  riscv.execute_trap (&s);
  ebase.coven = saved_coven;
  sis_verbose = saved_verbose;

  return s.icnt;
}

/* Which half of a double register holds a single precision value, the way
   riscv.cc names it.  */
#ifdef WORDS_BIGENDIAN
#define BEH 1
#else
#define BEH 0
#endif

/* Instruction formats, from chapter 2 of the unprivileged specification.
   The opcode field is the low seven bits, which is the five bit op the core
   decodes followed by the two bits that say the instruction is not
   compressed.  */
uint32
opcode (uint32 op)
{
  return (op << 2) | 3;
}

uint32
rtype (uint32 op, uint32 rd, uint32 funct3, uint32 rs1, uint32 rs2,
       uint32 funct7)
{
  return opcode (op) | (rd << 7) | (funct3 << 12) | (rs1 << 15) | (rs2 << 20) |
	 (funct7 << 25);
}

uint32
itype (uint32 op, uint32 rd, uint32 funct3, uint32 rs1, int32 imm)
{
  return opcode (op) | (rd << 7) | (funct3 << 12) | (rs1 << 15) |
	 (((uint32) imm & 0xfff) << 20);
}

uint32
stype (uint32 op, uint32 funct3, uint32 rs1, uint32 rs2, int32 imm)
{
  uint32 i = (uint32) imm & 0xfff;

  return opcode (op) | ((i & 0x1f) << 7) | (funct3 << 12) | (rs1 << 15) |
	 (rs2 << 20) | ((i >> 5) << 25);
}

/* A branch displacement is even and its bits are scattered, so a case gives
   the byte offset and this puts the bits where the format wants them.  */
uint32
btype (uint32 funct3, uint32 rs1, uint32 rs2, int32 imm)
{
  uint32 i = (uint32) imm;

  return opcode (OP_BRANCH) | (((i >> 11) & 1) << 7) |
	 (((i >> 1) & 0xf) << 8) | (funct3 << 12) | (rs1 << 15) | (rs2 << 20) |
	 (((i >> 5) & 0x3f) << 25) | (((i >> 12) & 1) << 31);
}

uint32
utype (uint32 op, uint32 rd, uint32 imm)
{
  return opcode (op) | (rd << 7) | (imm & 0xfffff000);
}

/* The jump displacement is scattered the same way.  */
uint32
jtype (uint32 rd, int32 imm)
{
  uint32 i = (uint32) imm;

  return opcode (OP_JAL) | (rd << 7) | (i & 0xff000) |
	 (((i >> 11) & 1) << 20) | (((i >> 1) & 0x3ff) << 21) |
	 (((i >> 20) & 1) << 31);
}

/* Drives the core against the flat memory with no board.  */
struct riscv_fixture
{
  int saved_cputype;
  int saved_archtype;
  int saved_verbose;
  const struct memsys *saved_ms;
  const struct cpu_arch *saved_arch;

  /* The flat memory installs no interrupt acknowledge, so a case which sets
     one would otherwise leave it pointing into this file for every case
     after it.  */
  void (*saved_intack) (int32, int32);

  riscv_fixture ()
      : saved_cputype (cputype), saved_archtype (archtype),
	saved_verbose (sis_verbose), saved_ms (ms), saved_arch (arch),
	saved_intack (sregs[0].intack)
  {
    cputype = CPU_RISCV;
    archtype = CPU_RISCV;
    ms = &sis_tests::flatmem;
    arch = &riscv;
    ebase.freq = 14;
    ebase.simtime = 0;
    ebase.simstart = 0;
    sis_tests::flatmem_clear ();
    reset_all ();
    init_bpt (sregs);
    sis_verbose = 0;

    /* reset_all leaves the register file alone, so a case would inherit
       whatever the one before it left.  Clear all of it here.  */
    for (int i = 0; i < 32; i++)
      sregs[0].r[i] = 0;
    for (int i = 0; i < 32; i++)
      sregs[0].fd[i] = 0;

    /* Machine mode, which is the only one a bare program runs in.  */
    sregs[0].mode = 3;
    sregs[0].mpp = 3;
    sregs[0].mstatus = 0;
    sregs[0].mtvec = 0;
    sregs[0].mie = 0;
    sregs[0].mip = 0;
    sregs[0].pc = 0x1000;
    sregs[0].npc = 0x1004;
    sregs[0].trap = 0;
    sregs[0].fpstate = 0;
  }

  ~riscv_fixture ()
  {
    sis_verbose = saved_verbose;
    cputype = saved_cputype;
    archtype = saved_archtype;
    ms = saved_ms;
    arch = saved_arch;
    sregs[0].intack = saved_intack;
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

  void
  set (int r, uint32 value)
  {
    sregs[0].r[r] = value;
  }

  /* A single precision register is the low half of a double register and is
     boxed by setting the other half to all ones, which is how the
     specification says a narrow value sits in a wide register.  A case
     reaches one through these and never indexes the arrays itself.  */
  float32 &
  fs (int n)
  {
    return sregs[0].fs[(n << 1) + BEH];
  }

  int32 &
  fsi (int n)
  {
    return sregs[0].fsi[(n << 1) + BEH];
  }

  int32 &
  fbox (int n)
  {
    return sregs[0].fsi[(n << 1) + 1 - BEH];
  }

  float64 &
  fd (int n)
  {
    return sregs[0].fd[n];
  }

  /* Put a single precision value in a register the way a load does.  */
  void
  setfs (int n, float32 value)
  {
    fs (n) = value;
    fbox (n) = -1;
  }

  uint32
  get (int r)
  {
    return sregs[0].r[r];
  }
};

} // namespace

TEST_CASE ("riscv trap cost never drops below the base cost")
{
  /* Before the parentheses were corrected the mask applied to TRAP_C + jitter
     rather than to the jitter alone, so a trap could cost less than TRAP_C,
     or nothing at all.  Walk a whole jitter sequence over one processor.  */
  struct pstate s = {};
  bool seen_above_base = false;

  for (int i = 0; i < 64; ++i)
    {
      s.icnt = 0;
      s.trap = TRAP_EBREAK;
      riscv.execute_trap (&s);

      CHECK (s.icnt >= TRAP_C);
      CHECK (s.icnt <= TRAP_C + 7);

      if (s.icnt > TRAP_C)
	seen_above_base = true;
    }

  /* The jitter is meant to vary, not to sit at zero.  */
  CHECK (seen_above_base);
}

TEST_CASE ("riscv trap jitter does not follow the simulated time")
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

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V register zero stays zero")
{
  /* Chapter 2: x0 is hardwired to zero, so a result written to it is
     discarded.  */
  set (1, 7);

  CHECK (exec (itype (OP_IMM, 0, ADD, 1, 5)) == 0);
  CHECK (get (0) == 0);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the immediate operations")
{
  /* The register-immediate group of chapter 2, with the immediate sign
     extended from twelve bits.  */
  set (1, 0xf0f0);

  CHECK (exec (itype (OP_IMM, 2, ADD, 1, 0x10)) == 0);
  CHECK (get (2) == 0xf100);

  CHECK (exec (itype (OP_IMM, 2, ADD, 1, -0x10)) == 0);
  CHECK (get (2) == 0xf0e0);

  CHECK (exec (itype (OP_IMM, 2, IXOR, 1, 0x0ff)) == 0);
  CHECK (get (2) == (0xf0f0u ^ 0xffu));

  CHECK (exec (itype (OP_IMM, 2, IOR, 1, 0x00f)) == 0);
  CHECK (get (2) == (0xf0f0u | 0xfu));

  CHECK (exec (itype (OP_IMM, 2, IAND, 1, 0x0ff)) == 0);
  CHECK (get (2) == (0xf0f0u & 0xffu));
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the immediate comparisons")
{
  /* Set-less-than writes one or zero, and the unsigned form compares the
     same immediate as an unsigned word after the sign extension.  */
  set (1, 5);

  CHECK (exec (itype (OP_IMM, 2, SLT, 1, 6)) == 0);
  CHECK (get (2) == 1);
  CHECK (exec (itype (OP_IMM, 2, SLT, 1, 4)) == 0);
  CHECK (get (2) == 0);

  CHECK (exec (itype (OP_IMM, 2, SLTU, 1, -1)) == 0);
  CHECK (get (2) == 1);
  CHECK (exec (itype (OP_IMM, 2, SLTU, 1, 4)) == 0);
  CHECK (get (2) == 0);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the immediate shifts")
{
  /* The shift amount is the low five bits of the immediate and bit ten of
     the field selects the arithmetic shift right.  */
  set (1, 0x80000010);

  CHECK (exec (itype (OP_IMM, 2, SLL, 1, 4)) == 0);
  CHECK (get (2) == 0x00000100);

  CHECK (exec (itype (OP_IMM, 2, SRL, 1, 4)) == 0);
  CHECK (get (2) == 0x08000001);

  CHECK (exec (itype (OP_IMM, 2, SRL, 1, 4 | (1 << 10))) == 0);
  CHECK (get (2) == 0xf8000001);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the register operations")
{
  /* The same group with both operands in registers, and the subtract which
     bit thirty of the instruction selects.  */
  set (1, 0xf0f0);
  set (2, 0x0ff0);

  CHECK (exec (rtype (OP_REG, 3, ADD, 1, 2, 0)) == 0);
  CHECK (get (3) == 0xf0f0 + 0x0ff0);

  CHECK (exec (rtype (OP_REG, 3, ADD, 1, 2, 0x20)) == 0);
  CHECK (get (3) == 0xf0f0 - 0x0ff0);

  CHECK (exec (rtype (OP_REG, 3, IXOR, 1, 2, 0)) == 0);
  CHECK (get (3) == (0xf0f0u ^ 0x0ff0u));

  CHECK (exec (rtype (OP_REG, 3, IOR, 1, 2, 0)) == 0);
  CHECK (get (3) == (0xf0f0u | 0x0ff0u));

  CHECK (exec (rtype (OP_REG, 3, IAND, 1, 2, 0)) == 0);
  CHECK (get (3) == (0xf0f0u & 0x0ff0u));
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the register shifts and comparisons")
{
  /* A register shift takes its amount from the low five bits of the second
     operand, so a larger value wraps rather than shifting everything out.  */
  set (1, 0x80000010);
  set (2, 0x24); /* four, once masked */

  CHECK (exec (rtype (OP_REG, 3, SLL, 1, 2, 0)) == 0);
  CHECK (get (3) == 0x00000100);

  CHECK (exec (rtype (OP_REG, 3, SRL, 1, 2, 0)) == 0);
  CHECK (get (3) == 0x08000001);

  CHECK (exec (rtype (OP_REG, 3, SRL, 1, 2, 0x20)) == 0);
  CHECK (get (3) == 0xf8000001);

  set (1, 5);
  set (2, 6);
  CHECK (exec (rtype (OP_REG, 3, SLT, 1, 2, 0)) == 0);
  CHECK (get (3) == 1);
  CHECK (exec (rtype (OP_REG, 3, SLT, 2, 1, 0)) == 0);
  CHECK (get (3) == 0);

  set (1, 0xffffffff);
  CHECK (exec (rtype (OP_REG, 3, SLT, 1, 2, 0)) == 0);
  CHECK (get (3) == 1);
  CHECK (exec (rtype (OP_REG, 3, SLTU, 1, 2, 0)) == 0);
  CHECK (get (3) == 0);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the upper immediate operations")
{
  /* lui puts the immediate in the top twenty bits, and auipc adds it to the
     address of the instruction itself.  */
  sregs[0].pc = 0x2000;
  sregs[0].npc = 0x2004;

  CHECK (exec (utype (OP_LUI, 1, 0xabcde000)) == 0);
  CHECK (get (1) == 0xabcde000);

  sregs[0].pc = 0x2000;
  sregs[0].npc = 0x2004;
  CHECK (exec (utype (OP_AUIPC, 1, 0x00010000)) == 0);
  CHECK (get (1) == 0x12000);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V every branch condition")
{
  /* The six conditions of chapter 2, each checked both ways, and the two
     encodings the format leaves unassigned.  The branch is relative to the
     address of the instruction itself.  */
  struct
  {
    uint32 funct3;
    uint32 a, b;
    bool taken;
  } cases[] = {
    { B_BE, 5, 5, true },
    { B_BE, 5, 6, false },
    { B_BNE, 5, 6, true },
    { B_BNE, 5, 5, false },
    { B_BLT, 0xffffffff, 1, true },
    { B_BLT, 1, 0xffffffff, false },
    { B_BGE, 1, 0xffffffff, true },
    { B_BGE, 0xffffffff, 1, false },
    { B_BLTU, 1, 0xffffffff, true },
    { B_BLTU, 0xffffffff, 1, false },
    { B_BGEU, 0xffffffff, 1, true },
    { B_BGEU, 1, 0xffffffff, false },
  };

  for (auto c : cases)
    {
      INFO ("condition " << c.funct3);
      sregs[0].pc = 0x2000;
      sregs[0].npc = 0x2004;
      set (1, c.a);
      set (2, c.b);

      CHECK (exec (btype (c.funct3, 1, 2, 0x40)) == 0);
      CHECK (sregs[0].pc == (c.taken ? 0x2040u : 0x2004u));
    }

  /* A negative displacement, which is how a loop is written.  */
  sregs[0].pc = 0x2000;
  sregs[0].npc = 0x2004;
  set (1, 5);
  set (2, 5);
  CHECK (exec (btype (B_BE, 1, 2, -0x40)) == 0);
  CHECK (sregs[0].pc == 0x1fc0);

  /* The two unassigned conditions are illegal instructions.  */
  CHECK (exec (btype (2, 1, 2, 4)) == TRAP_ILLEG);
  CHECK (exec (btype (3, 1, 2, 4)) == TRAP_ILLEG);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the jumps save the return address")
{
  /* Both jumps write the address of the instruction after them and clear the
     low bit of the target.  jal is relative to itself, jalr to a register.  */
  sregs[0].pc = 0x2000;
  sregs[0].npc = 0x2004;

  CHECK (exec (jtype (1, 0x100)) == 0);
  CHECK (get (1) == 0x2004);
  CHECK (sregs[0].pc == 0x2100);

  sregs[0].pc = 0x2000;
  sregs[0].npc = 0x2004;
  set (2, 0x3001);
  CHECK (exec (itype (OP_JALR, 1, 0, 2, 0)) == 0);
  CHECK (get (1) == 0x2004);
  CHECK (sregs[0].pc == 0x3000);

  /* A backward jump, and a target of zero which halts the run.  */
  sregs[0].pc = 0x2000;
  sregs[0].npc = 0x2004;
  CHECK (exec (jtype (1, -0x100)) == 0);
  CHECK (sregs[0].pc == 0x1f00);

  set (2, 0);
  CHECK (exec (itype (OP_JALR, 1, 0, 2, 0)) == NULL_TRAP);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the loads read every width")
{
  /* The load group of chapter 2: a byte and a half word are sign extended
     unless the unsigned form is used, and a word is taken as it stands.  */
  sis_tests::flatmem_poke (0x40, 0x81828384);
  set (1, 0x40);

  CHECK (exec (itype (OP_LOAD, 2, LW, 1, 0)) == 0);
  CHECK (get (2) == 0x81828384);

  CHECK (exec (itype (OP_LOAD, 2, LB, 1, 0)) == 0);
  CHECK (get (2) == 0xffffff84);
  CHECK (exec (itype (OP_LOAD, 2, LBU, 1, 0)) == 0);
  CHECK (get (2) == 0x84);

  CHECK (exec (itype (OP_LOAD, 2, LH, 1, 0)) == 0);
  CHECK (get (2) == 0xffff8384);
  CHECK (exec (itype (OP_LOAD, 2, LHU, 1, 0)) == 0);
  CHECK (get (2) == 0x8384);

  /* The offset reaches the other bytes of the word.  */
  CHECK (exec (itype (OP_LOAD, 2, LBU, 1, 1)) == 0);
  CHECK (get (2) == 0x83);
  CHECK (exec (itype (OP_LOAD, 2, LHU, 1, 2)) == 0);
  CHECK (get (2) == 0x8182);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the stores write every width")
{
  /* A store writes only the bytes of its width and leaves the rest of the
     word alone.  */
  set (1, 0x40);
  set (2, 0x81828384);

  sis_tests::flatmem_poke (0x40, 0);
  CHECK (exec (stype (OP_STORE, SW, 1, 2, 0)) == 0);
  CHECK (sis_tests::flatmem_peek (0x40) == 0x81828384);

  sis_tests::flatmem_poke (0x40, 0);
  CHECK (exec (stype (OP_STORE, SB, 1, 2, 0)) == 0);
  CHECK (sis_tests::flatmem_peek (0x40) == 0x84);

  sis_tests::flatmem_poke (0x40, 0);
  CHECK (exec (stype (OP_STORE, SB, 1, 2, 2)) == 0);
  CHECK (sis_tests::flatmem_peek (0x40) == 0x840000);

  sis_tests::flatmem_poke (0x40, 0);
  CHECK (exec (stype (OP_STORE, SH, 1, 2, 0)) == 0);
  CHECK (sis_tests::flatmem_peek (0x40) == 0x8384);

  sis_tests::flatmem_poke (0x40, 0);
  CHECK (exec (stype (OP_STORE, SH, 1, 2, 2)) == 0);
  CHECK (sis_tests::flatmem_peek (0x40) == 0x83840000);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V a misaligned access traps")
{
  /* Chapter 2 allows an implementation to trap a misaligned access, and this
     one does, reporting the address it refused.  */
  set (1, 0x41);
  set (2, 0x12345678);

  CHECK (exec (itype (OP_LOAD, 2, LW, 1, 0)) == TRAP_LMALI);
  CHECK (sregs[0].wpaddress == 0x41);

  CHECK (exec (itype (OP_LOAD, 2, LH, 1, 0)) == TRAP_LMALI);
  CHECK (exec (itype (OP_LOAD, 2, LHU, 1, 0)) == TRAP_LMALI);

  CHECK (exec (stype (OP_STORE, SW, 1, 2, 0)) == TRAP_SMALI);
  CHECK (sregs[0].wpaddress == 0x41);
  CHECK (exec (stype (OP_STORE, SH, 1, 2, 0)) == TRAP_SMALI);

  /* A byte access has no alignment to break.  */
  CHECK (exec (itype (OP_LOAD, 2, LB, 1, 0)) == 0);
  CHECK (exec (stype (OP_STORE, SB, 1, 2, 0)) == 0);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V an access outside memory traps")
{
  /* A window the board does not answer reports a load or store access
     fault, with the address that caused it.  */
  set (1, sis_tests::FLATMEM_SIZE);
  set (2, 0x12345678);

  CHECK (exec (itype (OP_LOAD, 2, LW, 1, 0)) == TRAP_LEXC);
  CHECK (sregs[0].wpaddress == sis_tests::FLATMEM_SIZE);

  CHECK (exec (stype (OP_STORE, SW, 1, 2, 0)) == TRAP_SEXC);
  CHECK (sregs[0].wpaddress == sis_tests::FLATMEM_SIZE);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the unassigned widths are illegal")
{
  /* The load and store groups leave part of their width field unassigned,
     and those encodings are illegal instructions.  */
  set (1, 0x40);

  CHECK (exec (itype (OP_LOAD, 2, 3, 1, 0)) == TRAP_ILLEG);
  CHECK (exec (itype (OP_LOAD, 2, 6, 1, 0)) == TRAP_ILLEG);
  CHECK (exec (itype (OP_LOAD, 2, 7, 1, 0)) == TRAP_ILLEG);

  CHECK (exec (stype (OP_STORE, 3, 1, 2, 0)) == TRAP_ILLEG);
  CHECK (exec (stype (OP_STORE, 7, 1, 2, 0)) == TRAP_ILLEG);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V an unassigned opcode is illegal")
{
  /* The opcode field has more encodings than the core implements, and every
     one it does not is an illegal instruction.  */
  CHECK (exec (opcode (0x02)) == TRAP_ILLEG);
  CHECK (exec (opcode (0x0a)) == TRAP_ILLEG);
  CHECK (exec (opcode (0x1e)) == TRAP_ILLEG);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the multiplies")
{
  /* The M extension: the low word of the product, and the high word for
     each of the three signedness combinations.  */
  set (1, 0x00010000);
  set (2, 0x00010000);

  CHECK (exec (rtype (OP_REG, 3, 0, 1, 2, 1)) == 0);
  CHECK (get (3) == 0);
  CHECK (exec (rtype (OP_REG, 3, 3, 1, 2, 1)) == 0);
  CHECK (get (3) == 1);

  set (1, 0xffffffff); /* minus one signed, the largest word unsigned */
  set (2, 2);

  CHECK (exec (rtype (OP_REG, 3, 0, 1, 2, 1)) == 0);
  CHECK (get (3) == 0xfffffffe);
  CHECK (exec (rtype (OP_REG, 3, 1, 1, 2, 1)) == 0);
  CHECK (get (3) == 0xffffffff); /* signed by signed */
  CHECK (exec (rtype (OP_REG, 3, 2, 1, 2, 1)) == 0);
  CHECK (get (3) == 0xffffffff); /* signed by unsigned */
  CHECK (exec (rtype (OP_REG, 3, 3, 1, 2, 1)) == 0);
  CHECK (get (3) == 1); /* unsigned by unsigned */
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the divides")
{
  set (1, 100);
  set (2, 7);

  CHECK (exec (rtype (OP_REG, 3, 4, 1, 2, 1)) == 0);
  CHECK (get (3) == 14);
  CHECK (exec (rtype (OP_REG, 3, 5, 1, 2, 1)) == 0);
  CHECK (get (3) == 14);
  CHECK (exec (rtype (OP_REG, 3, 6, 1, 2, 1)) == 0);
  CHECK (get (3) == 2);
  CHECK (exec (rtype (OP_REG, 3, 7, 1, 2, 1)) == 0);
  CHECK (get (3) == 2);

  /* A negative dividend, where the signed and unsigned forms part.  */
  set (1, (uint32) -100);
  CHECK (exec (rtype (OP_REG, 3, 4, 1, 2, 1)) == 0);
  CHECK (get (3) == (uint32) -14);
  CHECK (exec (rtype (OP_REG, 3, 6, 1, 2, 1)) == 0);
  CHECK (get (3) == (uint32) -2);
}

TEST_CASE_FIXTURE (riscv_fixture,
		   "RISC-V a divide by zero has a defined result")
{
  /* The M extension defines division by zero rather than trapping: the
     quotient is all ones, the unsigned quotient the largest word, and the
     remainder is the dividend.  */
  set (1, 100);
  set (2, 0);

  CHECK (exec (rtype (OP_REG, 3, 4, 1, 2, 1)) == 0);
  CHECK (get (3) == 0xffffffff);
  CHECK (exec (rtype (OP_REG, 3, 5, 1, 2, 1)) == 0);
  CHECK (get (3) == 0xffffffff);
  CHECK (exec (rtype (OP_REG, 3, 6, 1, 2, 1)) == 0);
  CHECK (get (3) == 100);
  CHECK (exec (rtype (OP_REG, 3, 7, 1, 2, 1)) == 0);
  CHECK (get (3) == 100);
}

TEST_CASE_FIXTURE (riscv_fixture,
		   "RISC-V a divide overflow has a defined result")
{
  /* The most negative word divided by minus one does not fit, and the
     extension defines the answer rather than trapping: the quotient is the
     dividend and the remainder is zero.  The unsigned forms have no such
     case and compute the ordinary answer.  */
  set (1, 0x80000000);
  set (2, 0xffffffff);

  CHECK (exec (rtype (OP_REG, 3, 4, 1, 2, 1)) == 0);
  CHECK (get (3) == 0x80000000);
  CHECK (exec (rtype (OP_REG, 3, 6, 1, 2, 1)) == 0);
  CHECK (get (3) == 0);

  CHECK (exec (rtype (OP_REG, 3, 5, 1, 2, 1)) == 0);
  CHECK (get (3) == 0);
  CHECK (exec (rtype (OP_REG, 3, 7, 1, 2, 1)) == 0);
  CHECK (get (3) == 0x80000000);
}

TEST_CASE_FIXTURE (riscv_fixture,
		   "RISC-V a control register is read and written")
{
  /* The CSR group of the privileged specification: each instruction reads
     the register into rd and then writes it, so one instruction both
     collects the old value and installs the new one.  */
  set (1, 0x1234);

  CHECK (exec (itype (OP_SYS, 2, CSRRW, 1, CSR_MSCRATCH)) == 0);
  CHECK (sregs[0].mscratch == 0x1234);

  set (1, 0x0f00);
  CHECK (exec (itype (OP_SYS, 2, CSRRW, 1, CSR_MSCRATCH)) == 0);
  CHECK (get (2) == 0x1234);
  CHECK (sregs[0].mscratch == 0x0f00);

  /* Set and clear turn the source into a mask.  */
  set (1, 0x00ff);
  CHECK (exec (itype (OP_SYS, 2, CSRRS, 1, CSR_MSCRATCH)) == 0);
  CHECK (get (2) == 0x0f00);
  CHECK (sregs[0].mscratch == 0x0fff);

  CHECK (exec (itype (OP_SYS, 2, CSRRC, 1, CSR_MSCRATCH)) == 0);
  CHECK (get (2) == 0x0fff);
  CHECK (sregs[0].mscratch == 0x0f00);
}

TEST_CASE_FIXTURE (riscv_fixture,
		   "RISC-V a control register read does not write")
{
  /* A set or clear naming x0 as its source reads the register without
     writing it, which is how a program reads a register it must not
     disturb.  */
  sregs[0].mscratch = 0x1234;

  CHECK (exec (itype (OP_SYS, 2, CSRRS, 0, CSR_MSCRATCH)) == 0);
  CHECK (get (2) == 0x1234);
  CHECK (sregs[0].mscratch == 0x1234);

  CHECK (exec (itype (OP_SYS, 2, CSRRC, 0, CSR_MSCRATCH)) == 0);
  CHECK (sregs[0].mscratch == 0x1234);

  CHECK (exec (itype (OP_SYS, 2, CSRRSI, 0, CSR_MSCRATCH)) == 0);
  CHECK (sregs[0].mscratch == 0x1234);

  CHECK (exec (itype (OP_SYS, 2, CSRRCI, 0, CSR_MSCRATCH)) == 0);
  CHECK (sregs[0].mscratch == 0x1234);
}

TEST_CASE_FIXTURE (riscv_fixture,
		   "RISC-V the immediate control register forms")
{
  /* The immediate forms take a five bit value out of the source field
     rather than a register.  */
  sregs[0].mscratch = 0;

  CHECK (exec (itype (OP_SYS, 2, CSRRWI, 0x15, CSR_MSCRATCH)) == 0);
  CHECK (sregs[0].mscratch == 0x15);

  CHECK (exec (itype (OP_SYS, 2, CSRRSI, 0x0a, CSR_MSCRATCH)) == 0);
  CHECK (get (2) == 0x15);
  CHECK (sregs[0].mscratch == 0x1f);

  CHECK (exec (itype (OP_SYS, 2, CSRRCI, 0x0a, CSR_MSCRATCH)) == 0);
  CHECK (sregs[0].mscratch == 0x15);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the machine registers answer")
{
  /* Every machine register the core models, read back through the same
     instruction a program uses.  */
  sregs[0].mstatus = MSTATUS_MIE;
  sregs[0].mtvec = 0x4000;
  sregs[0].epc = 0x2000;
  sregs[0].mie = MIP_MTIP;
  sregs[0].mip = MIP_MSIP;
  sregs[0].mcause = 7;
  sregs[0].mtval = 0x1234;
  sregs[0].simtime = 0x1'0000'0002;

  struct
  {
    uint32 csr;
    uint32 value;
  } cases[] = {
    { CSR_MSTATUS, MSTATUS_MIE },
    { CSR_MTVEC, 0x4000 },
    { CSR_MEPC, 0x2000 },
    { CSR_MIE, MIP_MTIP },
    { CSR_MIP, MIP_MSIP },
    { CSR_MCAUSE, 7 },
    { CSR_MTVAL, 0x1234 },
    { CSR_MHARTID, 0 },
    { CSR_MISA, 0x40000100 },
    { CSR_TIME, 2 },
    { CSR_TIMEH, 1 },
    { CSR_MSTATUSH, 0 },
  };

  for (auto c : cases)
    {
      INFO ("register " << c.csr);
      CHECK (exec (itype (OP_SYS, 2, CSRRS, 0, c.csr)) == 0);
      CHECK (get (2) == c.value);
    }
}

TEST_CASE_FIXTURE (
    riscv_fixture,
    "RISC-V an external interrupt shows in the pending register")
{
  /* The interrupt controller asserts the external line outside the register,
     so a read of the pending register reports it alongside what software
     set.  */
  sregs[0].mip = 0;
  ext_irl[0] = 0x1b;

  CHECK (exec (itype (OP_SYS, 2, CSRRS, 0, CSR_MIP)) == 0);
  CHECK ((get (2) & MIP_MEIP) != 0);

  ext_irl[0] = 0;
  CHECK (exec (itype (OP_SYS, 2, CSRRS, 0, CSR_MIP)) == 0);
  CHECK ((get (2) & MIP_MEIP) == 0);
}

TEST_CASE_FIXTURE (riscv_fixture,
		   "RISC-V a write of an unmodelled register is illegal")
{
  /* A write of a register the core does not model is an illegal
     instruction.  The read of one answers zero, because the core models the
     registers a program needs rather than the whole space.  */
  set (1, 1);

  CHECK (exec (itype (OP_SYS, 2, CSRRW, 1, 0x7c0)) == TRAP_ILLEG);
  CHECK (exec (itype (OP_SYS, 2, CSRRS, 1, 0x7c0)) == TRAP_ILLEG);
  CHECK (exec (itype (OP_SYS, 2, CSRRC, 1, 0x7c0)) == TRAP_ILLEG);
  CHECK (exec (itype (OP_SYS, 2, CSRRWI, 1, 0x7c0)) == TRAP_ILLEG);
  CHECK (exec (itype (OP_SYS, 2, CSRRSI, 1, 0x7c0)) == TRAP_ILLEG);
  CHECK (exec (itype (OP_SYS, 2, CSRRCI, 1, 0x7c0)) == TRAP_ILLEG);

  CHECK (exec (itype (OP_SYS, 2, CSRRS, 0, 0x7c0)) == 0);
  CHECK (get (2) == 0);

  /* The unassigned function code is illegal too.  */
  CHECK (exec (itype (OP_SYS, 2, 4, 1, 0)) == TRAP_ILLEG);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the mandatory high status register")
{
  /* mstatush is mandatory on RV32 and every field of it belongs to an
     extension the core does not emulate, so it reads zero and a write is
     discarded rather than refused.  */
  set (1, 0xffffffff);

  CHECK (exec (itype (OP_SYS, 2, CSRRW, 1, CSR_MSTATUSH)) == 0);
  CHECK (get (2) == 0);
  CHECK (exec (itype (OP_SYS, 2, CSRRS, 0, CSR_MSTATUSH)) == 0);
  CHECK (get (2) == 0);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the environment instructions")
{
  /* ecall stops the run and ebreak takes the breakpoint trap, or reports a
     debugger breakpoint while the stub is running.  */
  int saved = sis_gdb_break;

  sis_gdb_break = 0;
  CHECK (exec (itype (OP_SYS, 0, 0, 0, 0)) == ERROR_TRAP);
  CHECK (exec (itype (OP_SYS, 0, 0, 0, 1)) == TRAP_EBREAK);

  sis_gdb_break = 1;
  CHECK (exec (itype (OP_SYS, 0, 0, 0, 1)) == WPT_TRAP);
  CHECK (sregs[0].bphit == 1);

  sregs[0].bphit = 0;
  sis_gdb_break = saved;

  /* The unassigned codes of the group are illegal.  */
  CHECK (exec (itype (OP_SYS, 0, 0, 0, 3)) == TRAP_ILLEG);
  CHECK (exec (itype (OP_SYS, 0, 0, 0, 4)) == TRAP_ILLEG);
}

TEST_CASE_FIXTURE (riscv_fixture,
		   "RISC-V a return from a trap restores the mode")
{
  /* mret goes back to the saved program counter, restores the mode the trap
     came from and puts the saved interrupt enable back.  */
  sregs[0].epc = 0x3000;
  sregs[0].mpp = 3;
  sregs[0].mode = 1;
  sregs[0].mstatus = MSTATUS_MPIE;
  sregs[0].pc = 0x2000;
  sregs[0].npc = 0x2004;

  CHECK (exec (itype (OP_SYS, 0, 0, 0, 2)) == 0);
  CHECK (sregs[0].pc == 0x3000);
  CHECK (sregs[0].mode == 3);
  CHECK ((sregs[0].mstatus & MSTATUS_MIE) != 0);
  CHECK ((sregs[0].mstatus & MSTATUS_MPIE) != 0);
}

TEST_CASE_FIXTURE (riscv_fixture,
		   "RISC-V wait for interrupt enters power-down")
{
  /* wfi stops the core until something interrupts it, which the simulator
     models by skipping to the next event.  */
  sregs[0].pwd_mode = 0;
  sregs[0].hold = 0;

  CHECK (exec (itype (OP_SYS, 0, 0, 0, 5)) == 0);
  CHECK (sregs[0].pwd_mode == 1);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V a fence costs a trap entry")
{
  /* The core has no store buffer to drain, so a fence only costs the time
     an ordering barrier would.  */
  CHECK (exec (itype (OP_FENCE, 0, 0, 0, 0)) == 0);
  CHECK (sregs[0].icnt == TRAP_C);
}

namespace
{

/* An atomic instruction is the register format with the operation in the top
   five bits, above the acquire and release ordering bits.  */
uint32
amo (uint32 funct5, uint32 rd, uint32 rs1, uint32 rs2)
{
  return rtype (OP_AMO, rd, 2, rs1, rs2, funct5 << 2);
}

} /* namespace */

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the atomic read-modify-writes")
{
  /* The A extension: each one returns the word it found and leaves the
     result of the operation behind.  */
  struct
  {
    uint32 funct5;
    uint32 memory, operand, result;
  } cases[] = {
    { AMOSWAP, 100, 7, 7 },
    { AMOADD, 100, 7, 107 },
    { AMOXOR, 0xf0f0, 0x0ff0, 0xf0f0 ^ 0x0ff0 },
    { AMOOR, 0xf0f0, 0x0ff0, 0xf0f0 | 0x0ff0 },
    { AMOAND, 0xf0f0, 0x0ff0, 0xf0f0 & 0x0ff0 },
    { AMOMIN, (uint32) -5, 3, (uint32) -5 },
    { AMOMIN, 3, (uint32) -5, (uint32) -5 },
    { AMOMAX, (uint32) -5, 3, 3 },
    { AMOMAX, 3, (uint32) -5, 3 },
    { AMOMINU, (uint32) -5, 3, 3 },
    { AMOMINU, 3, (uint32) -5, 3 },
    { AMOMAXU, (uint32) -5, 3, (uint32) -5 },
    { AMOMAXU, 3, (uint32) -5, (uint32) -5 },
  };

  for (auto c : cases)
    {
      INFO ("operation " << c.funct5 << " on " << c.memory);
      sis_tests::flatmem_poke (0x40, c.memory);
      set (1, 0x40);
      set (2, c.operand);

      CHECK (exec (amo (c.funct5, 3, 1, 2)) == 0);
      CHECK (get (3) == c.memory);
      CHECK (sis_tests::flatmem_peek (0x40) == c.result);
    }
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the reservation pair")
{
  /* Load reserved takes a reservation on the word it read and store
     conditional writes only while that reservation still names the same
     address, reporting zero when it did and one when it did not.  */
  sis_tests::flatmem_poke (0x40, 0x12345678);
  set (1, 0x40);
  set (2, 0x11);

  CHECK (exec (amo (LRQ, 3, 1, 0)) == 0);
  CHECK (get (3) == 0x12345678);

  CHECK (exec (amo (SCQ, 3, 1, 2)) == 0);
  CHECK (get (3) == 0);
  CHECK (sis_tests::flatmem_peek (0x40) == 0x11);

  /* The reservation is spent, so a second store conditional fails.  */
  CHECK (exec (amo (SCQ, 3, 1, 2)) == 0);
  CHECK (get (3) == 1);

  /* A store conditional to another address fails as well.  */
  CHECK (exec (amo (LRQ, 3, 1, 0)) == 0);
  set (1, 0x80);
  CHECK (exec (amo (SCQ, 3, 1, 2)) == 0);
  CHECK (get (3) == 1);
  CHECK (sis_tests::flatmem_peek (0x80) == 0);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V an atomic access must be aligned")
{
  /* Every atomic instruction works on a whole word, so a misaligned address
     is refused before anything is read.  */
  set (1, 0x41);
  set (2, 0x11);

  CHECK (exec (amo (AMOADD, 3, 1, 2)) == TRAP_LMALI);
  CHECK (sregs[0].wpaddress == 0x41);
  CHECK (exec (amo (LRQ, 3, 1, 0)) == TRAP_LMALI);
  CHECK (exec (amo (SCQ, 3, 1, 2)) == TRAP_LMALI);
}

TEST_CASE_FIXTURE (riscv_fixture,
		   "RISC-V an atomic access reports a refused word")
{
  /* A word outside memory is a load fault, and one which reads but refuses
     a write is a store fault, which is the path between the read and the
     write of a read-modify-write.  */
  set (1, sis_tests::FLATMEM_SIZE);
  set (2, 0x11);

  CHECK (exec (amo (AMOADD, 3, 1, 2)) == TRAP_LEXC);
  CHECK (exec (amo (LRQ, 3, 1, 0)) == TRAP_LEXC);

  sis_tests::flatmem_fail_write (0x40);
  set (1, 0x40);
  CHECK (exec (amo (AMOADD, 3, 1, 2)) == TRAP_SEXC);
  CHECK (sregs[0].wpaddress == 0x40);

  CHECK (exec (amo (LRQ, 3, 1, 0)) == 0);
  CHECK (exec (amo (SCQ, 3, 1, 2)) == TRAP_SEXC);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the unassigned atomics are illegal")
{
  /* The operation field has more encodings than the extension assigns.  */
  set (1, 0x40);
  set (2, 0x11);

  CHECK (exec (amo (4, 3, 1, 2)) == TRAP_ILLEG);
  CHECK (exec (amo (6, 3, 1, 2)) == TRAP_ILLEG);
  CHECK (exec (amo (0x1f, 3, 1, 2)) == TRAP_ILLEG);
}

namespace
{

/* A floating point operation is the register format with the operation in
   the top five bits and the format in the two below them.  */
uint32
fpu (uint32 funct5, uint32 fmt, uint32 rd, uint32 funct3, uint32 rs1,
     uint32 rs2)
{
  return rtype (OP_FPU, rd, funct3, rs1, rs2, (funct5 << 2) | fmt);
}

const uint32 FMT_S = 0;
const uint32 FMT_D = 1;

/* funct5 encodings of the single precision group.  */
const uint32 F_ADD = 0x00;
const uint32 F_SUB = 0x01;
const uint32 F_MUL = 0x02;
const uint32 F_DIV = 0x03;
const uint32 F_SGNJ = 0x04;
const uint32 F_MINMAX = 0x05;
const uint32 F_SQRT = 0x0b;
const uint32 F_CMP = 0x14;
const uint32 F_CVT_W = 0x18;
const uint32 F_CVT_F = 0x1a;
const uint32 F_MV_X = 0x1c;
const uint32 F_MV_F = 0x1e;

} /* namespace */

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V single precision arithmetic")
{
  /* The F extension, with the result boxed into the wide register.  */
  setfs (1, 3.5f);
  setfs (2, 1.25f);

  CHECK (exec (fpu (F_ADD, FMT_S, 3, 0, 1, 2)) == 0);
  CHECK (fs (3) == 4.75f);
  CHECK (fbox (3) == -1);

  CHECK (exec (fpu (F_SUB, FMT_S, 3, 0, 1, 2)) == 0);
  CHECK (fs (3) == 2.25f);

  CHECK (exec (fpu (F_MUL, FMT_S, 3, 0, 1, 2)) == 0);
  CHECK (fs (3) == 4.375f);

  CHECK (exec (fpu (F_DIV, FMT_S, 3, 0, 1, 2)) == 0);
  CHECK (fs (3) == 2.8f);

  setfs (1, 4.0f);
  CHECK (exec (fpu (F_SQRT, FMT_S, 3, 0, 1, 0)) == 0);
  CHECK (fs (3) == 2.0f);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V single precision sign injection")
{
  /* The three sign injection forms take the magnitude from the first source
     and the sign from the second, negated or exclusive-ored.  */
  setfs (1, -2.5f);
  setfs (2, 1.0f);

  CHECK (exec (fpu (F_SGNJ, FMT_S, 3, 0, 1, 2)) == 0);
  CHECK (fs (3) == 2.5f);
  CHECK (fbox (3) == -1);

  CHECK (exec (fpu (F_SGNJ, FMT_S, 3, 1, 1, 2)) == 0);
  CHECK (fs (3) == -2.5f);

  CHECK (exec (fpu (F_SGNJ, FMT_S, 3, 2, 1, 2)) == 0);
  CHECK (fs (3) == -2.5f);

  setfs (2, -1.0f);
  CHECK (exec (fpu (F_SGNJ, FMT_S, 3, 2, 1, 2)) == 0);
  CHECK (fs (3) == 2.5f);

  /* The rest of the function field is unassigned.  */
  CHECK (exec (fpu (F_SGNJ, FMT_S, 3, 3, 1, 2)) == TRAP_ILLEG);
}

TEST_CASE_FIXTURE (riscv_fixture,
		   "RISC-V single precision minimum and maximum")
{
  setfs (1, 3.5f);
  setfs (2, 1.25f);

  CHECK (exec (fpu (F_MINMAX, FMT_S, 3, 0, 1, 2)) == 0);
  CHECK (fs (3) == 1.25f);
  CHECK (fbox (3) == -1);

  CHECK (exec (fpu (F_MINMAX, FMT_S, 3, 1, 1, 2)) == 0);
  CHECK (fs (3) == 3.5f);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V single precision comparisons")
{
  /* The comparisons write an integer register, so a program branches on
     them like any other test.  */
  setfs (1, 1.0f);
  setfs (2, 2.0f);

  CHECK (exec (fpu (F_CMP, FMT_S, 3, 0, 1, 2)) == 0); /* less or equal */
  CHECK (get (3) == 1);
  CHECK (exec (fpu (F_CMP, FMT_S, 3, 1, 1, 2)) == 0); /* less */
  CHECK (get (3) == 1);
  CHECK (exec (fpu (F_CMP, FMT_S, 3, 2, 1, 2)) == 0); /* equal */
  CHECK (get (3) == 0);

  setfs (2, 1.0f);
  CHECK (exec (fpu (F_CMP, FMT_S, 3, 0, 1, 2)) == 0);
  CHECK (get (3) == 1);
  CHECK (exec (fpu (F_CMP, FMT_S, 3, 1, 1, 2)) == 0);
  CHECK (get (3) == 0);
  CHECK (exec (fpu (F_CMP, FMT_S, 3, 2, 1, 2)) == 0);
  CHECK (get (3) == 1);

  CHECK (exec (fpu (F_CMP, FMT_S, 3, 3, 1, 2)) == TRAP_ILLEG);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V single precision conversions")
{
  /* Both directions between an integer register and a floating point one,
     signed and unsigned.  */
  setfs (1, -3.75f);

  CHECK (exec (fpu (F_CVT_W, FMT_S, 3, 0, 1, 0)) == 0);
  CHECK (get (3) == (uint32) -3); /* rounds towards zero */

  setfs (1, 3.75f);
  CHECK (exec (fpu (F_CVT_W, FMT_S, 3, 0, 1, 1)) == 0);
  CHECK (get (3) == 3);

  set (1, (uint32) -5);
  CHECK (exec (fpu (F_CVT_F, FMT_S, 3, 0, 1, 0)) == 0);
  CHECK (fs (3) == -5.0f);
  CHECK (fbox (3) == -1);

  CHECK (exec (fpu (F_CVT_F, FMT_S, 3, 0, 1, 1)) == 0);
  CHECK (fs (3) == 4294967296.0f - 5.0f);

  /* The rest of the source field is unassigned in both directions.  */
  CHECK (exec (fpu (F_CVT_W, FMT_S, 3, 0, 1, 2)) == TRAP_ILLEG);
  CHECK (exec (fpu (F_CVT_F, FMT_S, 3, 0, 1, 2)) == TRAP_ILLEG);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V a value moves between the files")
{
  /* The move instructions carry the bits across without converting them.  */
  fsi (1) = 0x40490fdb;

  CHECK (exec (fpu (F_MV_X, FMT_S, 3, 0, 1, 0)) == 0);
  CHECK (get (3) == 0x40490fdb);

  set (1, 0x3f800000);
  CHECK (exec (fpu (F_MV_F, FMT_S, 3, 0, 1, 0)) == 0);
  CHECK (fs (3) == 1.0f);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V a value is classified")
{
  /* fclass reports what kind of value a register holds as a one-hot word,
     with the bit numbers of the classification table.  */
  struct
  {
    float32 value;
    uint32 bit;
  } cases[] = {
    { -1.0f, 1 },
    { 1.0f, 6 },
    { -0.0f, 3 },
    { 0.0f, 4 },
  };

  for (auto c : cases)
    {
      INFO ("bit " << c.bit);
      setfs (1, c.value);
      CHECK (exec (fpu (F_MV_X, FMT_S, 3, 1, 1, 0)) == 0);
      CHECK (get (3) == (1u << c.bit));
    }

  setfs (1, std::numeric_limits<float>::infinity ());
  CHECK (exec (fpu (F_MV_X, FMT_S, 3, 1, 1, 0)) == 0);
  CHECK (get (3) == (1u << 7));

  setfs (1, -std::numeric_limits<float>::infinity ());
  CHECK (exec (fpu (F_MV_X, FMT_S, 3, 1, 1, 0)) == 0);
  CHECK (get (3) == (1u << 0));

  /* Bit 8 is a signalling and bit 9 a quiet value which is not a number.  */
  setfs (1, std::numeric_limits<float>::quiet_NaN ());
  CHECK (exec (fpu (F_MV_X, FMT_S, 3, 1, 1, 0)) == 0);
  CHECK (get (3) == (1u << 9));

  fsi (1) = 0x7f800001; /* a signalling one: the top mantissa bit clear */
  fbox (1) = -1;
  CHECK (exec (fpu (F_MV_X, FMT_S, 3, 1, 1, 0)) == 0);
  CHECK (get (3) == (1u << 8));

  /* And a subnormal, which the exponent of zero and a non-zero mantissa
     make.  */
  fsi (1) = 0x00000001;
  fbox (1) = -1;
  CHECK (exec (fpu (F_MV_X, FMT_S, 3, 1, 1, 0)) == 0);
  CHECK (get (3) == (1u << 5));

  fsi (1) = 0x80000001;
  fbox (1) = -1;
  CHECK (exec (fpu (F_MV_X, FMT_S, 3, 1, 1, 0)) == 0);
  CHECK (get (3) == (1u << 2));
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the floating point loads and stores")
{
  /* A single precision load boxes the value and a double one fills both
     halves.  */
  sis_tests::flatmem_poke (0x40, 0x3f800000);
  sis_tests::flatmem_poke (0x44, 0x40000000);
  set (1, 0x40);

  CHECK (exec (itype (OP_FLOAD, 2, LW, 1, 0)) == 0);
  CHECK (fs (2) == 1.0f);
  CHECK (fbox (2) == -1);

  CHECK (exec (itype (OP_FLOAD, 2, LD, 1, 0)) == 0);
  CHECK (fsi (2) == 0x3f800000);
  CHECK (fbox (2) == 0x40000000);

  /* And back out again.  */
  setfs (3, 2.5f);
  set (1, 0x80);
  CHECK (exec (stype (OP_FSW, SW, 1, 3, 0)) == 0);
  CHECK (sis_tests::flatmem_peek (0x80) == 0x40200000);

  CHECK (exec (stype (OP_FSW, LD, 1, 2, 0)) == 0);
  CHECK (sis_tests::flatmem_peek (0x80) == 0x3f800000);
  CHECK (sis_tests::flatmem_peek (0x84) == 0x40000000);
}

TEST_CASE_FIXTURE (riscv_fixture,
		   "RISC-V a floating point access must be aligned")
{
  /* A single precision access needs a word and a double one needs a double
     word, and each reports the address it refused.  */
  set (1, 0x42);
  setfs (2, 1.0f);

  CHECK (exec (itype (OP_FLOAD, 2, LW, 1, 0)) == TRAP_LMALI);
  CHECK (sregs[0].wpaddress == 0x42);
  CHECK (exec (stype (OP_FSW, SW, 1, 2, 0)) == TRAP_SMALI);

  set (1, 0x44);
  CHECK (exec (itype (OP_FLOAD, 2, LD, 1, 0)) == TRAP_LMALI);
  CHECK (exec (stype (OP_FSW, LD, 1, 2, 0)) == TRAP_SMALI);

  /* And a word outside memory is an access fault.  */
  set (1, sis_tests::FLATMEM_SIZE);
  CHECK (exec (itype (OP_FLOAD, 2, LW, 1, 0)) == TRAP_LEXC);
  CHECK (exec (itype (OP_FLOAD, 2, LD, 1, 0)) == TRAP_LEXC);
  CHECK (exec (stype (OP_FSW, SW, 1, 2, 0)) == TRAP_SEXC);
  CHECK (exec (stype (OP_FSW, LD, 1, 2, 0)) == TRAP_SEXC);

  /* The unassigned widths are illegal.  */
  set (1, 0x40);
  CHECK (exec (itype (OP_FLOAD, 2, LB, 1, 0)) == TRAP_ILLEG);
  CHECK (exec (stype (OP_FSW, SB, 1, 2, 0)) == TRAP_ILLEG);
}

/* Compressed instructions.  The encodings below were produced by the RISC-V
   assembler from the mnemonic in the comment, so a case states the
   instruction it means rather than the bit fields it happens to have.  The
   register numbers are a0 to a5 and sp, which the three register widths of
   the format can all name.  */

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the compressed stack arithmetic")
{
  /* c.addi4spn adds a scaled unsigned immediate to the stack pointer, and
     c.addi16sp adjusts the stack pointer itself.  */
  set (2, 0x1000);

  CHECK (exec (0x0808) == 0); /* c.addi4spn a0, sp, 16 */
  CHECK (get (10) == 0x1010);

  CHECK (exec (0x6105) == 0); /* c.addi16sp sp, 32 */
  CHECK (get (2) == 0x1020);

  /* An all zero word is not an instruction, which is what makes an empty
     page trap rather than run.  */
  CHECK (exec (0) == TRAP_ILLEG);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the compressed loads and stores")
{
  /* The three register form reaches the eight registers a program uses
     most, with a scaled unsigned offset.  */
  sis_tests::flatmem_poke (0x48, 0x12345678);
  set (11, 0x40); /* a1 */

  CHECK (exec (0x4588) == 0); /* c.lw a0, 8(a1) */
  CHECK (get (10) == 0x12345678);

  set (10, 0x11);
  CHECK (exec (0xc588) == 0); /* c.sw a0, 8(a1) */
  CHECK (sis_tests::flatmem_peek (0x48) == 0x11);

  /* The stack pointer form has its own wider offset.  */
  set (2, 0x40);
  sis_tests::flatmem_poke (0x48, 0x2222);
  CHECK (exec (0x47a2) == 0); /* c.lwsp a5, 8(sp) */
  CHECK (get (15) == 0x2222);

  set (15, 0x3333);
  CHECK (exec (0xc43e) == 0); /* c.swsp a5, 8(sp) */
  CHECK (sis_tests::flatmem_peek (0x48) == 0x3333);
}

TEST_CASE_FIXTURE (riscv_fixture,
		   "RISC-V a compressed access reports its faults")
{
  /* The compressed loads and stores check alignment and report a refused
     word the way the full width ones do.  */
  set (11, 0x41);
  set (2, 0x41);

  CHECK (exec (0x4588) == TRAP_LMALI); /* c.lw a0, 8(a1) */
  CHECK (sregs[0].wpaddress == 0x49);
  CHECK (exec (0xc588) == TRAP_SMALI); /* c.sw a0, 8(a1) */
  CHECK (exec (0x47a2) == TRAP_LMALI); /* c.lwsp a5, 8(sp) */
  CHECK (exec (0xc43e) == TRAP_SMALI); /* c.swsp a5, 8(sp) */

  set (11, sis_tests::FLATMEM_SIZE);
  set (2, sis_tests::FLATMEM_SIZE);
  CHECK (exec (0x4588) == TRAP_LEXC);
  CHECK (exec (0xc588) == TRAP_SEXC);
  CHECK (exec (0x47a2) == TRAP_LEXC);
  CHECK (exec (0xc43e) == TRAP_SEXC);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the compressed immediate operations")
{
  set (15, 10);

  CHECK (exec (0x0795) == 0); /* c.addi a5, 5 */
  CHECK (get (15) == 15);

  CHECK (exec (0x57f5) == 0); /* c.li a5, -3 */
  CHECK (get (15) == (uint32) -3);

  CHECK (exec (0x6789) == 0); /* c.lui a5, 2 */
  CHECK (get (15) == 0x2000);

  CHECK (exec (0x0792) == 0); /* c.slli a5, 4 */
  CHECK (get (15) == 0x20000);

  /* c.nop is an add of zero to x0, which must leave the file alone.  */
  CHECK (exec (0x0001) == 0);
  CHECK (get (0) == 0);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the compressed register operations")
{
  /* The two register forms of the third quadrant, on the eight register
     window and on the whole file.  */
  set (10, 0xf0f0);
  set (11, 0x0ff0);

  CHECK (exec (0x8d0d) == 0); /* c.sub a0, a1 */
  CHECK (get (10) == 0xf0f0 - 0x0ff0);

  set (10, 0xf0f0);
  CHECK (exec (0x8d2d) == 0); /* c.xor a0, a1 */
  CHECK (get (10) == (0xf0f0u ^ 0x0ff0u));

  set (10, 0xf0f0);
  CHECK (exec (0x8d4d) == 0); /* c.or a0, a1 */
  CHECK (get (10) == (0xf0f0u | 0x0ff0u));

  set (10, 0xf0f0);
  CHECK (exec (0x8d6d) == 0); /* c.and a0, a1 */
  CHECK (get (10) == (0xf0f0u & 0x0ff0u));

  set (10, 0xf0f0);
  CHECK (exec (0x891d) == 0); /* c.andi a0, 7 */
  CHECK (get (10) == (0xf0f0u & 7u));

  set (14, 0x1234);
  CHECK (exec (0x87ba) == 0); /* c.mv a5, a4 */
  CHECK (get (15) == 0x1234);

  set (15, 1);
  CHECK (exec (0x97ba) == 0); /* c.add a5, a4 */
  CHECK (get (15) == 0x1235);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the compressed shifts")
{
  set (10, 0x80000010);

  CHECK (exec (0x8111) == 0); /* c.srli a0, 4 */
  CHECK (get (10) == 0x08000001);

  set (10, 0x80000010);
  CHECK (exec (0x8511) == 0); /* c.srai a0, 4 */
  CHECK (get (10) == 0xf8000001);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the compressed branches and jumps")
{
  /* Every compressed transfer is relative to the instruction itself, and the
     one which links writes the address two bytes on rather than four.  */
  sregs[0].pc = 0x2000;
  sregs[0].npc = 0x2002;
  CHECK (exec (0xa001) == 0); /* c.j . */
  CHECK (sregs[0].pc == 0x2000);

  sregs[0].pc = 0x2000;
  sregs[0].npc = 0x2002;
  CHECK (exec (0x2001) == 0); /* c.jal . */
  CHECK (sregs[0].pc == 0x2000);
  CHECK (get (1) == 0x2002);

  /* The two conditional branches, each way.  */
  sregs[0].pc = 0x2000;
  sregs[0].npc = 0x2002;
  set (10, 0);
  CHECK (exec (0xc101) == 0); /* c.beqz a0, . */
  CHECK (sregs[0].pc == 0x2000);

  sregs[0].pc = 0x2000;
  sregs[0].npc = 0x2002;
  set (10, 1);
  CHECK (exec (0xc101) == 0);
  CHECK (sregs[0].pc == 0x2002);

  sregs[0].pc = 0x2000;
  sregs[0].npc = 0x2002;
  CHECK (exec (0xe101) == 0); /* c.bnez a0, . */
  CHECK (sregs[0].pc == 0x2000);

  sregs[0].pc = 0x2000;
  sregs[0].npc = 0x2002;
  set (10, 0);
  CHECK (exec (0xe101) == 0);
  CHECK (sregs[0].pc == 0x2002);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the compressed register jumps")
{
  /* c.jr goes to the register and c.jalr also links, which is how a
     compressed call and return are written.  */
  sregs[0].pc = 0x2000;
  sregs[0].npc = 0x2002;
  set (15, 0x3000);

  CHECK (exec (0x8782) == 0); /* c.jr a5 */
  CHECK (sregs[0].pc == 0x3000);

  sregs[0].pc = 0x2000;
  sregs[0].npc = 0x2002;
  CHECK (exec (0x9782) == 0); /* c.jalr a5 */
  CHECK (sregs[0].pc == 0x3000);
  CHECK (get (1) == 0x2002);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the compressed breakpoint")
{
  int saved = sis_gdb_break;

  sis_gdb_break = 0;
  CHECK (exec (0x9002) == TRAP_EBREAK); /* c.ebreak */

  sis_gdb_break = 1;
  CHECK (exec (0x9002) == WPT_TRAP);
  CHECK (sregs[0].bphit == 1);

  sregs[0].bphit = 0;
  sis_gdb_break = saved;
}

TEST_CASE_FIXTURE (riscv_fixture,
		   "RISC-V the compressed floating point accesses")
{
  /* The compressed format reaches the floating point file too, in both the
     three register and the stack pointer form, and in both widths.  */
  sis_tests::flatmem_poke (0x48, 0x3f800000);
  sis_tests::flatmem_poke (0x4c, 0x40000000);
  set (11, 0x40); /* a1 */
  set (2, 0x40);  /* sp */

  CHECK (exec (0x6588) == 0); /* c.flw fa0, 8(a1) */
  CHECK (fs (10) == 1.0f);
  CHECK (fbox (10) == -1);

  CHECK (exec (0x2588) == 0); /* c.fld fa0, 8(a1) */
  CHECK (fsi (10) == 0x3f800000);
  CHECK (fbox (10) == 0x40000000);

  CHECK (exec (0x67a2) == 0); /* c.flwsp fa5, 8(sp) */
  CHECK (fs (15) == 1.0f);

  CHECK (exec (0x27a2) == 0); /* c.fldsp fa5, 8(sp) */
  CHECK (fsi (15) == 0x3f800000);
  CHECK (fbox (15) == 0x40000000);

  /* And back out again.  */
  setfs (10, 2.5f);
  sis_tests::flatmem_poke (0x48, 0);
  CHECK (exec (0xe588) == 0); /* c.fsw fa0, 8(a1) */
  CHECK (sis_tests::flatmem_peek (0x48) == 0x40200000);

  CHECK (exec (0xa588) == 0); /* c.fsd fa0, 8(a1) */
  CHECK (sis_tests::flatmem_peek (0x48) == 0x40200000);
  CHECK (sis_tests::flatmem_peek (0x4c) == 0xffffffff);

  setfs (15, 3.5f);
  CHECK (exec (0xe43e) == 0); /* c.fswsp fa5, 8(sp) */
  CHECK (sis_tests::flatmem_peek (0x48) == 0x40600000);

  CHECK (exec (0xa43e) == 0); /* c.fsdsp fa5, 8(sp) */
  CHECK (sis_tests::flatmem_peek (0x48) == 0x40600000);
  CHECK (sis_tests::flatmem_peek (0x4c) == 0xffffffff);
}

TEST_CASE_FIXTURE (riscv_fixture,
		   "RISC-V a compressed floating point access reports faults")
{
  /* Each width checks its own alignment, and a word outside memory is an
     access fault.  */
  set (11, 0x42);
  set (2, 0x42);

  CHECK (exec (0x6588) == TRAP_LMALI); /* c.flw */
  CHECK (exec (0xe588) == TRAP_SMALI); /* c.fsw */
  CHECK (exec (0x67a2) == TRAP_LMALI); /* c.flwsp */
  CHECK (exec (0xe43e) == TRAP_SMALI); /* c.fswsp */

  set (11, 0x44);
  set (2, 0x44);
  CHECK (exec (0x2588) == TRAP_LMALI); /* c.fld */
  CHECK (exec (0xa588) == TRAP_SMALI); /* c.fsd */
  CHECK (exec (0x27a2) == TRAP_LMALI); /* c.fldsp */
  CHECK (exec (0xa43e) == TRAP_SMALI); /* c.fsdsp */

  set (11, sis_tests::FLATMEM_SIZE);
  set (2, sis_tests::FLATMEM_SIZE);
  CHECK (exec (0x6588) == TRAP_LEXC);
  CHECK (exec (0xe588) == TRAP_SEXC);
  CHECK (exec (0x2588) == TRAP_LEXC);
  CHECK (exec (0xa588) == TRAP_SEXC);
  CHECK (exec (0x67a2) == TRAP_LEXC);
  CHECK (exec (0xe43e) == TRAP_SEXC);
  CHECK (exec (0x27a2) == TRAP_LEXC);
  CHECK (exec (0xa43e) == TRAP_SEXC);
}

TEST_CASE_FIXTURE (riscv_fixture,
		   "RISC-V the compressed transfers reach both directions")
{
  /* A forward and a backward displacement of each transfer, and the target
     of zero which halts the run.  */
  sregs[0].pc = 0x2000;
  sregs[0].npc = 0x2002;
  CHECK (exec (0xa801) == 0); /* c.j .+16 */
  CHECK (sregs[0].pc == 0x2010);

  sregs[0].pc = 0x2000;
  sregs[0].npc = 0x2002;
  CHECK (exec (0x2801) == 0); /* c.jal .+16 */
  CHECK (sregs[0].pc == 0x2010);
  CHECK (get (1) == 0x2002);

  sregs[0].pc = 0x2000;
  sregs[0].npc = 0x2002;
  set (10, 0);
  CHECK (exec (0xc901) == 0); /* c.beqz a0, .+16 */
  CHECK (sregs[0].pc == 0x2010);

  sregs[0].pc = 0x2000;
  sregs[0].npc = 0x2002;
  set (10, 1);
  CHECK (exec (0xe901) == 0); /* c.bnez a0, .+16 */
  CHECK (sregs[0].pc == 0x2010);

  /* Backward, which is how a compressed loop closes.  */
  sregs[0].pc = 0x2000;
  sregs[0].npc = 0x2002;
  set (10, 0);
  CHECK (exec (0xd965) == 0); /* c.beqz a0, .-16 */
  CHECK (sregs[0].pc == 0x1ff0);

  sregs[0].pc = 0x2000;
  sregs[0].npc = 0x2002;
  set (10, 1);
  CHECK (exec (0xf965) == 0); /* c.bnez a0, .-16 */
  CHECK (sregs[0].pc == 0x1ff0);

  /* A branch not taken with a backward displacement falls through.  */
  sregs[0].pc = 0x2000;
  sregs[0].npc = 0x2002;
  set (10, 1);
  CHECK (exec (0xd965) == 0);
  CHECK (sregs[0].pc == 0x2002);

  sregs[0].pc = 0x2000;
  sregs[0].npc = 0x2002;
  set (10, 0);
  CHECK (exec (0xf965) == 0);
  CHECK (sregs[0].pc == 0x2002);

  /* A jump to zero halts rather than running from the bottom of memory.  */
  sregs[0].pc = 0;
  sregs[0].npc = 2;
  CHECK (exec (0xa001) == NULL_TRAP); /* c.j . */
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the compressed negative immediates")
{
  /* The immediate of the first quadrant is signed, so each of these takes
     the sign extension path.  */
  set (15, 10);
  CHECK (exec (0x17ed) == 0); /* c.addi a5, -5 */
  CHECK (get (15) == 5);

  CHECK (exec (0x77fd) == 0); /* c.lui a5, 0xfffff */
  CHECK (get (15) == 0xfffff000);

  set (2, 0x1000);
  CHECK (exec (0x713d) == 0); /* c.addi16sp sp, -32 */
  CHECK (get (2) == 0x0fe0);

  set (10, 0xf0f0);
  CHECK (exec (0x9961) == 0); /* c.andi a0, -8 */
  CHECK (get (10) == (0xf0f0u & (uint32) -8));
}

TEST_CASE_FIXTURE (riscv_fixture,
		   "RISC-V the reserved compressed encodings are illegal")
{
  /* Each quadrant leaves encodings unassigned on RV32, and the shift
     amounts which need a sixth bit belong to the wider architecture.  */
  CHECK (exec (0x9001 | (1u << 12) | (0u << 10)) == TRAP_ILLEG); /* srli 32+ */
  CHECK (exec (0x9001 | (1u << 12) | (1u << 10)) == TRAP_ILLEG); /* srai 32+ */
  CHECK (exec (0x9001 | (1u << 12) | (3u << 10)) == TRAP_ILLEG); /* subw */
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V a register is set by its number")
{
  /* The debugger names a register by its number: the integer file, then the
     program counter, then the floating point file.  A value written to one
     must read back from the same place.  */
  char buf[65 * 4];

  arch->set_register (&sregs[0], NULL, 0x11, 1);
  CHECK (get (1) == 0x11);

  arch->set_register (&sregs[0], NULL, 0x2000, 32);
  CHECK (sregs[0].pc == 0x2000);

  arch->set_register (&sregs[0], NULL, 0x3f800000, 33 + 2);
  CHECK (fsi (2) == 0x3f800000);

  /* And the whole file as one packet reports what was written.  */
  CHECK (arch->gdb_get_reg (buf) == 65 * 4);
  CHECK ((buf[(33 + 2) * 4] & 0xff) == 0x00);
  CHECK ((buf[(33 + 2) * 4 + 3] & 0xff) == 0x3f);
}

namespace
{

int riscv_intack_level;
int riscv_intack_calls;

void
riscv_count_intack (int32 level, int32 cpu)
{
  (void) cpu;
  riscv_intack_level = level;
  riscv_intack_calls++;
}

} /* namespace */

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V a trap enters the handler")
{
  /* The privileged specification: a trap saves the program counter in mepc,
     records the cause in mcause, drops to machine mode and enters the
     handler the trap vector names.  */
  sregs[0].mtvec = 0x4000;
  sregs[0].pc = 0x2000;
  sregs[0].mstatus = MSTATUS_MIE;
  sregs[0].mode = 3;
  sregs[0].trap = TRAP_EBREAK;

  CHECK (riscv.execute_trap (&sregs[0]) == 0);
  CHECK (sregs[0].pc == 0x4000);
  CHECK (sregs[0].epc == 0x2000);
  CHECK (sregs[0].mcause == TRAP_EBREAK);
  CHECK (sregs[0].mtval == 0x2000);
  CHECK (sregs[0].mode == 1);
  CHECK (sregs[0].mpp == 3);

  /* The interrupt enable moves into its saved copy and is cleared, so the
     handler runs with interrupts off.  */
  CHECK ((sregs[0].mstatus & MSTATUS_MPIE) != 0);
  CHECK ((sregs[0].mstatus & MSTATUS_MIE) == 0);
  CHECK (sregs[0].icnt >= TRAP_C);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V a trap records what it was about")
{
  /* Each group of causes leaves a different value behind for the handler to
     read, and the ones a bare program cannot recover from stop the run.  */
  sregs[0].mtvec = 0x4000;
  sregs[0].pc = 0x2000;

  SUBCASE ("an illegal instruction reports the word")
  {
    sregs[0].inst = 0x12345678;
    sregs[0].trap = TRAP_ILLEG;
    CHECK (riscv.execute_trap (&sregs[0]) == ERROR_MODE);
    CHECK (sregs[0].mtval == 0x12345678);
  }

  SUBCASE ("the RTEMS unimplemented test is allowed to continue")
  {
    /* RTEMS plants one encoding to check that an illegal instruction traps,
       so that one does not stop the run.  */
    sregs[0].inst = 0xc0001073;
    sregs[0].trap = TRAP_ILLEG;
    CHECK (riscv.execute_trap (&sregs[0]) == 0);
    CHECK (sregs[0].err_mode == 0);

    sregs[0].inst = 0x00010000;
    sregs[0].trap = TRAP_ILLEG;
    CHECK (riscv.execute_trap (&sregs[0]) == 0);
  }

  SUBCASE ("an access fault reports the address")
  {
    sregs[0].wpaddress = 0x1234;
    sregs[0].trap = TRAP_LEXC;
    CHECK (riscv.execute_trap (&sregs[0]) == ERROR_MODE);
    CHECK (sregs[0].mtval == 0x1234);
  }

  SUBCASE ("an instruction fetch fault stops the run")
  {
    sregs[0].trap = TRAP_IEXC;
    CHECK (riscv.execute_trap (&sregs[0]) == ERROR_MODE);
    CHECK (sregs[0].mtval == 0x2000);
  }
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V an interrupt is marked as one")
{
  /* An interrupt cause has its top bit set, which is how a handler tells it
     from an exception.  A controller interrupt is numbered 16 upwards, which
     the cause keeps, while the controller is told the level below that so it
     stops asserting it.  */
  sregs[0].intack = riscv_count_intack;
  sregs[0].mtvec = 0x4000;
  riscv_intack_calls = 0;

  sregs[0].trap = 16 + 5;
  CHECK (riscv.execute_trap (&sregs[0]) == 0);
  CHECK (sregs[0].mcause == (0x80000000 | 21));
  CHECK (riscv_intack_calls == 1);
  CHECK (riscv_intack_level == 5);
}

TEST_CASE_FIXTURE (riscv_fixture,
		   "RISC-V the local interrupts clear their bit")
{
  /* The software, timer and external interrupts of the machine level clear
     the pending bit they were raised by and drop the external line rather
     than acknowledging a controller level.  */
  struct
  {
    uint32 trap, bit;
  } cases[] = {
    { 0x23, MIP_MSIP },
    { 0x27, MIP_MTIP },
    { 0x2b, MIP_MEIP },
  };

  for (auto c : cases)
    {
      INFO ("cause " << c.trap);
      sregs[0].mtvec = 0x4000;
      sregs[0].mip = MIP_MSIP | MIP_MTIP | MIP_MEIP;
      ext_irl[0] = 0x1b;
      sregs[0].trap = c.trap;

      CHECK (riscv.execute_trap (&sregs[0]) == 0);
      /* The three local causes mask down to the numbers the privileged
	 specification gives them: software 3, timer 7, external 11.  */
      CHECK ((sregs[0].mcause & 0x80000000) != 0);
      CHECK ((sregs[0].mcause & 0x1f) == (c.trap & 0x1f));
      CHECK ((sregs[0].mip & c.bit) == 0);
      CHECK (ext_irl[0] == 0);
    }
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the pseudo traps report their state")
{
  /* The traps at or above 256 are the simulator's own, not the
     architecture's: they stop the run rather than entering a handler.  */
  sregs[0].trap = ERROR_TRAP;
  CHECK (riscv.execute_trap (&sregs[0]) == ERROR_MODE);

  sregs[0].trap = WPT_TRAP;
  CHECK (riscv.execute_trap (&sregs[0]) == WPT_HIT);

  sregs[0].trap = NULL_TRAP;
  CHECK (riscv.execute_trap (&sregs[0]) == NULL_HIT);

  /* And 256 restarts from the bottom of memory.  */
  sregs[0].pc = 0x2000;
  sregs[0].trap = 256;
  CHECK (riscv.execute_trap (&sregs[0]) == 0);
  CHECK (sregs[0].pc == 0);
  CHECK (sregs[0].trap == 0);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V a trap is recorded for coverage")
{
  uint32 saved = ebase.coven;

  sregs[0].mtvec = 0x4000;
  sregs[0].pc = 0x2000;
  sregs[0].trap = TRAP_EBREAK;
  covram[0x2000 >> 2] = 0;
  ebase.coven = 1;

  CHECK (riscv.execute_trap (&sregs[0]) == 0);
  CHECK (covram[0x2000 >> 2] != 0);

  ebase.coven = saved;
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V an interrupt is taken when enabled")
{
  /* An external interrupt reaches the core only with the global enable and
     the external enable both set, and it wakes a core in power-down.  */
  ext_irl[0] = 0x1b;
  sregs[0].trap = 0;

  sregs[0].mstatus = 0;
  sregs[0].mie = MIE_MEIE;
  CHECK (riscv.check_interrupts (&sregs[0]) == 0);

  sregs[0].mstatus = MSTATUS_MIE;
  sregs[0].mie = 0;
  CHECK (riscv.check_interrupts (&sregs[0]) == 0);

  sregs[0].mie = MIE_MEIE;
  CHECK (riscv.check_interrupts (&sregs[0]) == 0x1b);

  /* A trap already pending is taken first.  */
  sregs[0].trap = TRAP_EBREAK;
  CHECK (riscv.check_interrupts (&sregs[0]) == 0);
  sregs[0].trap = 0;

  /* And it ends power-down.  */
  sregs[0].pwd_mode = 1;
  sregs[0].pwdstart = 0;
  sregs[0].simtime = 100;
  CHECK (riscv.check_interrupts (&sregs[0]) == 0x1b);
  CHECK (sregs[0].pwd_mode == 0);
  CHECK (sregs[0].pwdtime == 100);

  ext_irl[0] = 0;
}

TEST_CASE_FIXTURE (riscv_fixture,
		   "RISC-V the disassembler decodes every group")
{
  stdout_capture cap;

  /* Sweep the instruction space so the decode tables are reached: every
     opcode with every function code, both operand forms, and the whole
     compressed space.  The output is not asserted on beyond being produced,
     because the assembly syntax is the disassembler's own.  */
  uint32 addr = 0x200;

  for (uint32 op = 0; op < 0x20; op++)
    {
      for (uint32 funct3 = 0; funct3 < 8; funct3++)
	{
	  for (uint32 funct7 : { 0u, 1u, 0x20u })
	    {
	      sis_tests::flatmem_poke (addr,
				       rtype (op, 3, funct3, 1, 2, funct7));
	      arch->disas (addr);
	    }
	  sis_tests::flatmem_poke (addr, itype (op, 3, funct3, 1, 5));
	  arch->disas (addr);
	  sis_tests::flatmem_poke (addr, itype (op, 3, funct3, 1, -5));
	  arch->disas (addr);
	  sis_tests::flatmem_poke (addr, stype (op, funct3, 1, 2, 5));
	  arch->disas (addr);
	}
    }

  /* The atomic and floating point groups have their operation above the
     function code.  */
  for (uint32 funct5 = 0; funct5 < 0x20; funct5++)
    {
      sis_tests::flatmem_poke (addr, rtype (OP_AMO, 3, 2, 1, 2, funct5 << 2));
      arch->disas (addr);

      for (uint32 fmt = 0; fmt < 4; fmt++)
	{
	  for (uint32 funct3 = 0; funct3 < 4; funct3++)
	    {
	      for (uint32 rs2 = 0; rs2 < 4; rs2++)
		{
		  sis_tests::flatmem_poke (
		      addr, fpu (funct5, fmt, 3, funct3, 1, rs2));
		  arch->disas (addr);
		}
	    }
	}
    }

  /* The fused multiplies, which name three source registers.  */
  for (uint32 op : { OP_FMADD, OP_FMSUB, OP_FNMSUB, OP_FNMADD })
    {
      for (uint32 fmt = 0; fmt < 4; fmt++)
	{
	  sis_tests::flatmem_poke (addr,
				   rtype (op, 3, 0, 1, 2, (4 << 2) | fmt));
	  arch->disas (addr);
	}
    }

  /* The control register group, over the registers the core models and one
     it does not.  */
  for (uint32 funct3 = 0; funct3 < 8; funct3++)
    {
      for (uint32 csr :
	   { (uint32) CSR_MSTATUS, (uint32) CSR_MTVEC, (uint32) CSR_MEPC,
	     (uint32) CSR_MIE, (uint32) CSR_MIP, (uint32) CSR_MSCRATCH,
	     (uint32) CSR_MCAUSE, (uint32) CSR_MTVAL, (uint32) CSR_MISA,
	     (uint32) CSR_MHARTID, (uint32) CSR_TIME, (uint32) CSR_TIMEH,
	     (uint32) CSR_MSTATUSH, (uint32) CSR_FFLAGS, (uint32) CSR_FRM,
	     (uint32) CSR_FCSR, 0x7c0u })
	{
	  sis_tests::flatmem_poke (addr, itype (OP_SYS, 3, funct3, 1, csr));
	  arch->disas (addr);
	}
    }

  /* The whole compressed space, which is only sixteen bits wide.  */
  for (uint32 quadrant = 0; quadrant < 3; quadrant++)
    {
      for (uint32 funct3 = 0; funct3 < 8; funct3++)
	{
	  for (uint32 rest = 0; rest < 0x2000; rest += 0x111)
	    {
	      uint32 inst = quadrant | (funct3 << 13) | (rest & 0x1ffc);

	      sis_tests::flatmem_poke (addr, inst | 0xffff0000);
	      arch->disas (addr);
	    }
	}
    }

  CHECK (!cap.str ().empty ());
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the disassembler names its operands")
{
  /* The sweep above only proves the decode tables were reached.  These check
     what came out: the register a field selects, under the ABI names of the
     unprivileged specification, and the value of each immediate form.  The
     mnemonic is left out because its spelling is the disassembler's own,
     while the operands are the instruction's.  */
  uint32 addr = 0x200;

  auto disas = [&] (uint32 inst)
    {
      stdout_capture cap;

      sis_tests::flatmem_poke (addr, inst);
      arch->disas (addr);
      return cap.str ();
    };

  /* x10 and x11 are a0 and a1, x2 is sp, x1 is ra.  */
  CHECK (disas (rtype (OP_REG, 10, ADD, 11, 12, 0)).find ("a0,a1,a2") !=
	 std::string::npos);

  /* A signed immediate is printed as a signed decimal.  */
  CHECK (disas (itype (OP_IMM, 10, ADD, 11, -5)).find ("a0,a1,-5") !=
	 std::string::npos);
  CHECK (disas (itype (OP_IMM, 10, ADD, 11, 5)).find ("a0,a1,5") !=
	 std::string::npos);

  /* A load and a store name the base register in parentheses, and the store
     takes its offset from the two halves of the format.  */
  CHECK (disas (itype (OP_LOAD, 10, LW, 11, 8)).find ("a0,8(a1)") !=
	 std::string::npos);
  CHECK (disas (stype (OP_STORE, SW, 11, 10, 8)).find ("a0,8(a1)") !=
	 std::string::npos);
  CHECK (disas (stype (OP_STORE, SW, 11, 10, -8)).find ("a0,-8(a1)") !=
	 std::string::npos);

  /* A branch and a jump print the address they reach, not the
     displacement.  The two are padded differently, so a case looks for the
     value rather than for one spelling of it.  */
  auto reaches = [] (const std::string &text, const std::string &hex)
    {
      std::string padded = "0x" + std::string (8 - hex.size (), '0') + hex;
      std::string bare = "0x" + hex;

      return text.find (padded) != std::string::npos ||
	     text.find (bare) != std::string::npos;
    };

  CHECK (reaches (disas (btype (B_BNE, 10, 11, 0x40)), "240"));
  CHECK (reaches (disas (jtype (1, 0x40)), "240"));
  CHECK (reaches (disas (jtype (0, 0x40)), "240"));
  CHECK (reaches (disas (btype (B_BNE, 10, 11, -0x40)), "1c0"));

  /* The upper immediate is printed as the value it loads.  */
  CHECK (disas (utype (OP_LUI, 10, 0xabcde000)).find ("a0") !=
	 std::string::npos);

  /* A floating point register uses its own name table.  */
  CHECK (disas (fpu (F_ADD, FMT_S, 10, 0, 11, 12)).find ("fa0,fa1,fa2") !=
	 std::string::npos);

  /* A control register instruction names the register, not its number.  */
  CHECK (disas (itype (OP_SYS, 10, CSRRS, 11, CSR_MSTATUS)).find ("mstatus") !=
	 std::string::npos);

  /* A compressed instruction names the same registers as its wide form.  */
  CHECK (disas (0x4588 | 0xffff0000).find ("a0,8(a1)") != std::string::npos);
  CHECK (disas (0x87ba | 0xffff0000).find ("a5,a4") != std::string::npos);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V a double value is classified")
{
  /* The classification table is the same for both formats, but the bit which
     separates a quiet value from a signalling one is the most significant
     bit of the significand, which for a double is bit 19 of its high
     word.  */
  struct
  {
    float64 value;
    uint32 bit;
  } cases[] = {
    { -1.0, 1 },
    { 1.0, 6 },
    { -0.0, 3 },
    { 0.0, 4 },
  };

  for (auto c : cases)
    {
      INFO ("bit " << c.bit);
      fd (2) = c.value;
      CHECK (exec (fpu (F_MV_X, FMT_D, 3, 1, 2, 0)) == 0);
      CHECK (get (3) == (1u << c.bit));
    }

  fd (2) = std::numeric_limits<double>::infinity ();
  CHECK (exec (fpu (F_MV_X, FMT_D, 3, 1, 2, 0)) == 0);
  CHECK (get (3) == (1u << 7));

  fd (2) = -std::numeric_limits<double>::infinity ();
  CHECK (exec (fpu (F_MV_X, FMT_D, 3, 1, 2, 0)) == 0);
  CHECK (get (3) == (1u << 0));

  fd (2) = std::numeric_limits<double>::quiet_NaN ();
  CHECK (exec (fpu (F_MV_X, FMT_D, 3, 1, 2, 0)) == 0);
  CHECK (get (3) == (1u << 9));

  /* A signalling one: the exponent all ones and the top significand bit
     clear.  */
  fsi (2) = 1;
  fbox (2) = 0x7ff00000;
  CHECK (exec (fpu (F_MV_X, FMT_D, 3, 1, 2, 0)) == 0);
  CHECK (get (3) == (1u << 8));

  /* And a subnormal, which a zero exponent and a non-zero significand
     make.  */
  fsi (2) = 1;
  fbox (2) = 0;
  CHECK (exec (fpu (F_MV_X, FMT_D, 3, 1, 2, 0)) == 0);
  CHECK (get (3) == (1u << 5));

  fbox (2) = 0x80000000;
  CHECK (exec (fpu (F_MV_X, FMT_D, 3, 1, 2, 0)) == 0);
  CHECK (get (3) == (1u << 2));
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V double precision arithmetic")
{
  fd (2) = 3.5;
  fd (4) = 1.25;

  CHECK (exec (fpu (F_ADD, FMT_D, 6, 0, 2, 4)) == 0);
  CHECK (fd (6) == 4.75);

  CHECK (exec (fpu (F_SUB, FMT_D, 6, 0, 2, 4)) == 0);
  CHECK (fd (6) == 2.25);

  CHECK (exec (fpu (F_MUL, FMT_D, 6, 0, 2, 4)) == 0);
  CHECK (fd (6) == 4.375);

  CHECK (exec (fpu (F_DIV, FMT_D, 6, 0, 2, 4)) == 0);
  CHECK (fd (6) == 2.8);

  fd (2) = 4.0;
  CHECK (exec (fpu (F_SQRT, FMT_D, 6, 0, 2, 0)) == 0);
  CHECK (fd (6) == 2.0);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V double precision sign injection")
{
  /* The sign lives in the high word of a double, so these move that word
     and copy the low one across untouched.  */
  fd (2) = -2.5;
  fd (4) = 1.0;

  CHECK (exec (fpu (F_SGNJ, FMT_D, 6, 0, 2, 4)) == 0);
  CHECK (fd (6) == 2.5);

  CHECK (exec (fpu (F_SGNJ, FMT_D, 6, 1, 2, 4)) == 0);
  CHECK (fd (6) == -2.5);

  CHECK (exec (fpu (F_SGNJ, FMT_D, 6, 2, 2, 4)) == 0);
  CHECK (fd (6) == -2.5);

  fd (4) = -1.0;
  CHECK (exec (fpu (F_SGNJ, FMT_D, 6, 2, 2, 4)) == 0);
  CHECK (fd (6) == 2.5);

  CHECK (exec (fpu (F_SGNJ, FMT_D, 6, 3, 2, 4)) == TRAP_ILLEG);
}

TEST_CASE_FIXTURE (riscv_fixture,
		   "RISC-V double precision minimum and maximum")
{
  fd (2) = 3.5;
  fd (4) = 1.25;

  CHECK (exec (fpu (F_MINMAX, FMT_D, 6, 0, 2, 4)) == 0);
  CHECK (fd (6) == 1.25);

  CHECK (exec (fpu (F_MINMAX, FMT_D, 6, 1, 2, 4)) == 0);
  CHECK (fd (6) == 3.5);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V double precision comparisons")
{
  fd (2) = 1.0;
  fd (4) = 2.0;

  CHECK (exec (fpu (F_CMP, FMT_D, 3, 0, 2, 4)) == 0);
  CHECK (get (3) == 1);
  CHECK (exec (fpu (F_CMP, FMT_D, 3, 1, 2, 4)) == 0);
  CHECK (get (3) == 1);
  CHECK (exec (fpu (F_CMP, FMT_D, 3, 2, 2, 4)) == 0);
  CHECK (get (3) == 0);

  fd (4) = 1.0;
  CHECK (exec (fpu (F_CMP, FMT_D, 3, 0, 2, 4)) == 0);
  CHECK (get (3) == 1);
  CHECK (exec (fpu (F_CMP, FMT_D, 3, 1, 2, 4)) == 0);
  CHECK (get (3) == 0);
  CHECK (exec (fpu (F_CMP, FMT_D, 3, 2, 2, 4)) == 0);
  CHECK (get (3) == 1);

  CHECK (exec (fpu (F_CMP, FMT_D, 3, 3, 2, 4)) == TRAP_ILLEG);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the conversions between the formats")
{
  /* A double narrows to a single and a single widens to a double, each
     selected by the format of the instruction and the source field.  */
  fd (2) = 2.5;
  CHECK (exec (fpu (0x08, FMT_S, 6, 0, 2, 1)) == 0); /* fcvt.s.d */
  CHECK (fs (6) == 2.5f);
  CHECK (fbox (6) == -1);

  setfs (2, 3.5f);
  CHECK (exec (fpu (0x08, FMT_D, 6, 0, 2, 0)) == 0); /* fcvt.d.s */
  CHECK (fd (6) == 3.5);

  /* The format of the instruction alone says which way the conversion
     goes, so the source field is not consulted.  */
  fd (2) = 4.5;
  CHECK (exec (fpu (0x08, FMT_S, 6, 0, 2, 0)) == 0);
  CHECK (fs (6) == 4.5f);
}

TEST_CASE_FIXTURE (riscv_fixture,
		   "RISC-V double precision integer conversions")
{
  fd (2) = -3.75;
  CHECK (exec (fpu (F_CVT_W, FMT_D, 3, 0, 2, 0)) == 0);
  CHECK (get (3) == (uint32) -3);

  fd (2) = 3.75;
  CHECK (exec (fpu (F_CVT_W, FMT_D, 3, 0, 2, 1)) == 0);
  CHECK (get (3) == 3);

  set (1, (uint32) -5);
  CHECK (exec (fpu (F_CVT_F, FMT_D, 6, 0, 1, 0)) == 0);
  CHECK (fd (6) == -5.0);

  CHECK (exec (fpu (F_CVT_F, FMT_D, 6, 0, 1, 1)) == 0);
  CHECK (fd (6) == 4294967296.0 - 5.0);

  CHECK (exec (fpu (F_CVT_W, FMT_D, 3, 0, 2, 2)) == TRAP_ILLEG);
  CHECK (exec (fpu (F_CVT_F, FMT_D, 6, 0, 1, 2)) == TRAP_ILLEG);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the fused multiply and add")
{
  /* The four fused forms take a third source from the top of the
     instruction and differ only in which signs they apply.  */
  setfs (1, 2.0f);
  setfs (2, 3.0f);
  setfs (4, 1.0f);

  struct
  {
    uint32 op;
    float32 result;
  } cases[] = {
    { OP_FMADD, 7.0f },	  /* a * b + c */
    { OP_FMSUB, 5.0f },	  /* a * b - c */
    { OP_FNMSUB, -5.0f }, /* -(a * b) + c is the specification's naming */
    { OP_FNMADD, -7.0f },
  };

  for (auto c : cases)
    {
      INFO ("opcode " << c.op);
      CHECK (exec (rtype (c.op, 3, 0, 1, 2, (4 << 2) | FMT_S)) == 0);
      CHECK (fs (3) == c.result);
      CHECK (fbox (3) == -1);
    }

  /* And the double forms.  */
  fd (2) = 2.0;
  fd (4) = 3.0;
  fd (8) = 1.0;
  CHECK (exec (rtype (OP_FMADD, 6, 0, 2, 4, (8 << 2) | FMT_D)) == 0);
  CHECK (fd (6) == 7.0);

  /* The remaining format codes are unassigned.  */
  CHECK (exec (rtype (OP_FMADD, 6, 0, 2, 4, (8 << 2) | 2)) == TRAP_ILLEG);
}

/* The ABI register names of the calling convention.  They are written out
   here rather than reached from the core, so a case checks the core against
   the specification and not against itself.  */
static const char *const abi_int[32] = {
  "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0", "s1", "a0",
  "a1",	  "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3", "s4", "s5",
  "s6",	  "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
};

static const char *const abi_fp[32] = {
  "ft0", "ft1", "ft2",	"ft3",	"ft4", "ft5", "ft6",  "ft7",
  "fs0", "fs1", "fa0",	"fa1",	"fa2", "fa3", "fa4",  "fa5",
  "fa6", "fa7", "fs2",	"fs3",	"fs4", "fs5", "fs6",  "fs7",
  "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11"
};

TEST_CASE_FIXTURE (riscv_fixture,
		   "RISC-V every integer register answers to its ABI name")
{
  stdout_capture cap;

  for (int i = 1; i < 32; i++)
    {
      arch->set_register (&sregs[0], (char *) abi_int[i], 0x1000 + i, 0);
      INFO ("register ", abi_int[i]);
      CHECK (get (i) == (uint32) (0x1000 + i));
    }

  /* x0 is hard wired to zero and a write to it is refused, not silently
     dropped.  */
  arch->set_register (&sregs[0], (char *) "zero", 0x1234, 0);
  CHECK (get (0) == 0);
  CHECK (cap.str ().find ("cannot set zero") != std::string::npos);
}

TEST_CASE_FIXTURE (riscv_fixture,
		   "RISC-V every float register answers to its ABI name")
{
  stdout_capture cap;

  for (int i = 0; i < 32; i++)
    {
      arch->set_register (&sregs[0], (char *) abi_fp[i], 0x2000 + i, 0);
      INFO ("register ", abi_fp[i]);
      CHECK (fsi (i) == (int32) (0x2000 + i));
    }
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the named control registers")
{
  stdout_capture cap;

  arch->set_register (&sregs[0], (char *) "pc", 0x2000, 0);
  CHECK (sregs[0].pc == 0x2000);

  /* A control register is written the way csrw writes it, so mstatus keeps
     only the fields this core implements while mtvec keeps the whole word.  */
  arch->set_register (&sregs[0], (char *) "mstatus", 0xffffffff, 0);
  CHECK (sregs[0].mstatus == MSTATUS_MASK);

  arch->set_register (&sregs[0], (char *) "mtvec", 0x40001234, 0);
  CHECK (sregs[0].mtvec == 0x40001234);

  arch->set_register (&sregs[0], (char *) "mepc", 0x40002000, 0);
  CHECK (sregs[0].epc == 0x40002000);

  arch->set_register (&sregs[0], (char *) "mcause", 7, 0);
  CHECK (sregs[0].mcause == 7);

  arch->set_register (&sregs[0], (char *) "mie", MIE_MEIE, 0);
  CHECK (sregs[0].mie == MIE_MEIE);

  arch->set_register (&sregs[0], (char *) "mip", MIP_MTIP, 0);
  CHECK (sregs[0].mip == MIP_MTIP);

  arch->set_register (&sregs[0], (char *) "mscratch", 0xcafe, 0);
  CHECK (sregs[0].mscratch == 0xcafe);

  /* The floating point status is one register seen through three windows:
     fcsr is the whole of it, fflags its low five bits and frm the three
     above them.  */
  arch->set_register (&sregs[0], (char *) "fcsr", 0xff, 0);
  CHECK (sregs[0].fsr == 0xff);

  arch->set_register (&sregs[0], (char *) "fflags", 0x1f, 0);
  CHECK ((sregs[0].fsr & 0x1f) == 0x1f);

  arch->set_register (&sregs[0], (char *) "frm", 2, 0);
  CHECK (((sregs[0].fsr >> 5) & 7) == 2);

  CHECK (!cap.str ().empty ());

  /* A name the core does not know is reported and changes nothing.  A
     capture answers once, so each phase gets its own.  */
  stdout_capture unknown;
  arch->set_register (&sregs[0], (char *) "nosuchreg", 1, 0);
  CHECK (unknown.str ().find ("no such register: nosuchreg") !=
	 std::string::npos);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V registers are reachable by number")
{
  /* With no name the number selects the register.  That numbering is the
     GDB one: x0 to x31, then the pc, then the float file, then the CSRs.  */
  for (int i = 1; i < 32; i++)
    {
      arch->set_register (&sregs[0], NULL, 0x3000 + i, i);
      INFO ("register number ", i);
      CHECK (get (i) == (uint32) (0x3000 + i));
    }

  arch->set_register (&sregs[0], NULL, 0x4000, 32);
  CHECK (sregs[0].pc == 0x4000);

  for (int i = 0; i < 32; i++)
    {
      arch->set_register (&sregs[0], NULL, 0x5000 + i, 33 + i);
      INFO ("float register number ", i);
      CHECK (fsi (i) == (int32) (0x5000 + i));
    }

  /* A CSR is numbered by its address above the last float register.  */
  arch->set_register (&sregs[0], NULL, 0x99, 65 + CSR_MSCRATCH);
  CHECK (sregs[0].mscratch == 0x99);

  /* A number below the first and past the last register changes nothing.  */
  arch->set_register (&sregs[0], NULL, 0xdeadbeef, -1);
  arch->set_register (&sregs[0], NULL, 0xdeadbeef, 4161);
  CHECK (sregs[0].pc == 0x4000);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the GDB stub packs every register")
{
  char buf[65 * 4];

  set (1, 0x11223344);
  sregs[0].pc = 0x2000;

  /* The stub carries the integer file, the pc and the float file, and packs
     each register least significant byte first for a little endian target.  */
  CHECK (arch->gdb_get_reg (buf) == 65 * 4);
  CHECK ((unsigned char) buf[4] == 0x44);
  CHECK ((unsigned char) buf[5] == 0x33);
  CHECK ((unsigned char) buf[6] == 0x22);
  CHECK ((unsigned char) buf[7] == 0x11);
  CHECK ((unsigned char) buf[32 * 4] == 0x00);
  CHECK ((unsigned char) buf[32 * 4 + 1] == 0x20);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the register displays print")
{
  stdout_capture cap;

  set (10, 0x0a0a0a0a);
  fd (5) = 1.5;
  sregs[0].mtvec = 0x40000100;

  arch->display_registers (&sregs[0]);
  arch->display_ctrl (&sregs[0]);
  arch->display_special (&sregs[0]);
  arch->display_fpu (&sregs[0]);

  std::string out = cap.str ();

  /* The displays name the registers the way the ABI does, and the control
     display disassembles the instruction the pc stands on.  */
  CHECK (out.find ("0A0A0A0A") != std::string::npos);
  CHECK (out.find ("mtvec") != std::string::npos);
  CHECK (out.find ("40000100") != std::string::npos);
  CHECK (out.find ("misa") != std::string::npos);
  for (int i = 0; i < 32; i++)
    {
      INFO ("float name ", abi_fp[i]);
      CHECK (out.find (abi_fp[i]) != std::string::npos);
    }
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V the control display reports a halt")
{
  /* A stopped core says why it stopped.  The two reasons are exclusive: a
     core in error mode reports that and nothing else.  */
  {
    stdout_capture cap;
    arch->display_ctrl (&sregs[0]);
    CHECK (cap.str ().find (" mode") == std::string::npos);
  }

  {
    sregs[0].pwd_mode = 1;
    stdout_capture cap;
    arch->display_ctrl (&sregs[0]);
    CHECK (cap.str ().find ("power-down mode") != std::string::npos);
  }

  {
    sregs[0].err_mode = 1;
    stdout_capture cap;
    arch->display_ctrl (&sregs[0]);
    std::string out = cap.str ();
    CHECK (out.find ("error mode") != std::string::npos);
    CHECK (out.find ("power-down") == std::string::npos);
  }
}

/* The bits cov_bt, cov_bnt and cov_jmp set in the coverage bitmap, from
   func.cc.  A word is marked executed, the start of a block, the site of a
   jump, or a branch that was taken or not taken.  */
#define COV_EXEC  1
#define COV_START 2
#define COV_JMP	  4
#define COV_BT	  8
#define COV_BNT	  16

namespace
{

/* Turns coverage collection on for one case and clears the window of the
   bitmap the case works in.  */
struct coverage_on
{
  uint32 saved;

  coverage_on () : saved (ebase.coven)
  {
    ebase.coven = 1;
    for (int i = 0; i < 0x400; i++)
      covram[i] = 0;
  }

  ~coverage_on () { ebase.coven = saved; }

  unsigned char
  at (uint32 addr)
  {
    return covram[addr >> 2];
  }
};

} // namespace

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V a branch is recorded for coverage")
{
  coverage_on cov;

  /* A taken branch marks its own word taken and the target as the start of a
     block.  A branch not taken marks its word not taken and the word after
     it as the start, which is where the run goes on.  */
  sregs[0].pc = 0x200;
  sregs[0].npc = 0x204;
  set (1, 1);
  set (2, 1);
  CHECK (exec (btype (B_BE, 1, 2, 0x40)) == 0);
  CHECK (sregs[0].pc == 0x240);
  CHECK ((cov.at (0x200) & (COV_BT | COV_EXEC)) == (COV_BT | COV_EXEC));
  CHECK ((cov.at (0x240) & COV_START) == COV_START);

  sregs[0].pc = 0x300;
  sregs[0].npc = 0x304;
  set (2, 2);
  CHECK (exec (btype (B_BE, 1, 2, 0x40)) == 0);
  CHECK (sregs[0].pc == 0x304);
  CHECK ((cov.at (0x300) & COV_BNT) == COV_BNT);
  CHECK ((cov.at (0x304) & COV_START) == COV_START);

  /* A backward branch is the loop case: taken costs nothing, and it is the
     one not taken which pays the misprediction.  */
  sregs[0].pc = 0x380;
  sregs[0].npc = 0x384;
  set (2, 1);
  CHECK (exec (btype (B_BE, 1, 2, -0x40)) == 0);
  CHECK (sregs[0].pc == 0x340);
  CHECK ((cov.at (0x380) & COV_BT) == COV_BT);

  sregs[0].pc = 0x390;
  sregs[0].npc = 0x394;
  set (2, 2);
  CHECK (exec (btype (B_BE, 1, 2, -0x40)) == 0);
  CHECK ((cov.at (0x390) & COV_BNT) == COV_BNT);
}

TEST_CASE_FIXTURE (riscv_fixture, "RISC-V a jump is recorded for coverage")
{
  coverage_on cov;

  sregs[0].pc = 0x200;
  sregs[0].npc = 0x204;
  CHECK (exec (jtype (1, 0x40)) == 0);
  CHECK (sregs[0].pc == 0x240);
  CHECK ((cov.at (0x200) & (COV_JMP | COV_EXEC)) == (COV_JMP | COV_EXEC));
  CHECK ((cov.at (0x240) & COV_START) == COV_START);

  /* And the register form.  */
  sregs[0].pc = 0x280;
  sregs[0].npc = 0x284;
  set (5, 0x300);
  CHECK (exec (itype (OP_JALR, 1, 0, 5, 0)) == 0);
  CHECK (sregs[0].pc == 0x300);
  CHECK ((cov.at (0x280) & COV_JMP) == COV_JMP);
  CHECK ((cov.at (0x300) & COV_START) == COV_START);
}

TEST_CASE_FIXTURE (riscv_fixture,
		   "RISC-V the compressed control flow is recorded too")
{
  coverage_on cov;

  /* c.j and c.jal reach the same recording as the full width jump.  */
  sregs[0].pc = 0x200;
  sregs[0].npc = 0x204;
  CHECK (exec (0xa011) == 0); /* c.j .+4 */
  CHECK ((cov.at (0x200) & COV_JMP) == COV_JMP);
  CHECK ((cov.at (sregs[0].pc) & COV_START) == COV_START);

  sregs[0].pc = 0x280;
  sregs[0].npc = 0x284;
  CHECK (exec (0x2011) == 0); /* c.jal .+4 */
  CHECK ((cov.at (0x280) & COV_JMP) == COV_JMP);
  CHECK (get (1) == 0x282);

  /* c.jr and c.jalr take the target from a register.  */
  sregs[0].pc = 0x300;
  sregs[0].npc = 0x304;
  set (15, 0x340);
  CHECK (exec (0x8782) == 0); /* c.jr a5 */
  CHECK (sregs[0].pc == 0x340);
  CHECK ((cov.at (0x300) & COV_JMP) == COV_JMP);
  CHECK ((cov.at (0x340) & COV_START) == COV_START);

  sregs[0].pc = 0x380;
  sregs[0].npc = 0x384;
  CHECK (exec (0x9782) == 0); /* c.jalr a5 */
  CHECK ((cov.at (0x380) & COV_JMP) == COV_JMP);
}

TEST_CASE_FIXTURE (riscv_fixture,
		   "RISC-V the compressed branches are recorded too")
{
  coverage_on cov;

  /* c.beqz and c.bnez, each taken and not taken, forward and back.  */
  sregs[0].pc = 0x200;
  sregs[0].npc = 0x204;
  set (10, 0);
  CHECK (exec (0xc111) == 0); /* c.beqz a0, .+4 */
  CHECK (sregs[0].pc == 0x204);
  CHECK ((cov.at (0x200) & COV_BT) == COV_BT);
  CHECK ((cov.at (0x204) & COV_START) == COV_START);

  sregs[0].pc = 0x280;
  sregs[0].npc = 0x284;
  set (10, 1);
  CHECK (exec (0xc111) == 0); /* not taken */
  CHECK (sregs[0].pc == 0x282);
  CHECK ((cov.at (0x280) & COV_BNT) == COV_BNT);
  CHECK ((cov.at (0x282) & COV_START) == COV_START);

  sregs[0].pc = 0x300;
  sregs[0].npc = 0x304;
  CHECK (exec (0xe111) == 0); /* c.bnez a0, .+4, taken */
  CHECK (sregs[0].pc == 0x304);
  CHECK ((cov.at (0x300) & COV_BT) == COV_BT);

  sregs[0].pc = 0x380;
  sregs[0].npc = 0x384;
  set (10, 0);
  CHECK (exec (0xe111) == 0); /* not taken */
  CHECK ((cov.at (0x380) & COV_BNT) == COV_BNT);
}
