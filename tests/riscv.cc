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

#include <string>

using sis_tests::stdout_capture;

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

  riscv_fixture ()
      : saved_cputype (cputype), saved_archtype (archtype),
	saved_verbose (sis_verbose), saved_ms (ms), saved_arch (arch)
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
