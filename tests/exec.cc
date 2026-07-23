/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Tests for exec.cc, the shared SPARC integer helpers.

   Expected values are derived from The SPARC Architecture Manual Version 8,
   appendix E, which defines UMUL, SMUL, UDIV and SDIV in terms of the exact
   64-bit product and quotient, and from section 4.2 on the reset state of
   the IU registers.  */

#include "doctest.h"

#include "config.h"
#include "sis.h"

#include <fenv.h>
#include <vector>

namespace
{

/* init_regs writes NCPU entries whatever ncpu says, so a caller must hand it
   a full array.  It also reads the cputype and nfp globals, which the cases
   below set, so the fixture puts them back.  */
struct exec_fixture
{
  std::vector<struct pstate> regs;
  int saved_cputype;
  int saved_nfp;

  exec_fixture () : regs (NCPU), saved_cputype (cputype), saved_nfp (nfp)
  {
    ebase.wphit = 1;
  }

  ~exec_fixture ()
  {
    cputype = saved_cputype;
    nfp = saved_nfp;
  }
};

uint64
mul (uint32 n1, uint32 n2, int msigned)
{
  uint32 hi = 0xdeadbeef, lo = 0xdeadbeef;

  mul64 (n1, n2, &hi, &lo, msigned);
  return ((uint64) hi << 32) | lo;
}

uint32
divide (uint64 n1, uint32 n2, int msigned)
{
  uint32 result = 0xdeadbeef;

  div64 ((uint32) (n1 >> 32), (uint32) n1, n2, &result, msigned);
  return result;
}

} /* namespace */

TEST_CASE ("mul64 unsigned")
{
  /* No carry out of the partial sums.  */
  CHECK (mul (0, 0, 0) == 0);
  CHECK (mul (1, 1, 0) == 1);
  CHECK (mul (3, 7, 0) == 21);
  CHECK (mul (0xffff, 0xffff, 0) == 0xfffe0001ull);

  /* Carry out of the low half, which is the second arc of add32.  */
  CHECK (mul (0x10000, 0x10000, 0) == 0x100000000ull);
  CHECK (mul (0xffffffff, 0xffffffff, 0) == 0xfffffffe00000001ull);
  CHECK (mul (0xffffffff, 2, 0) == 0x1fffffffeull);
  CHECK (mul (0x80000000, 2, 0) == 0x100000000ull);

  /* The top bit of either operand is data, not a sign, when msigned is 0.  */
  CHECK (mul (0x80000000, 0x80000000, 0) == 0x4000000000000000ull);
}

TEST_CASE ("mul64 signed")
{
  /* Both operands positive: the sign block runs but negates nothing.  */
  CHECK (mul (3, 7, 1) == 21);
  CHECK (mul (0x7fffffff, 0x7fffffff, 1) == 0x3fffffff00000001ull);

  /* First operand negative.  */
  CHECK (mul (-3, 7, 1) == (uint64) -21);
  /* Second operand negative.  */
  CHECK (mul (3, -7, 1) == (uint64) -21);
  /* Both negative, so the result is positive again.  */
  CHECK (mul (-3, -7, 1) == 21);
  CHECK (mul (0x80000000, 0x80000000, 1) == 0x4000000000000000ull);

  /* A negative result whose low word is zero, which is the only way the
     borrow into the high word is taken.  */
  CHECK (mul (0x10000, -0x10000, 1) == (uint64) -0x100000000ll);
  CHECK (mul (-0x10000, 0x10000, 1) == (uint64) -0x100000000ll);
  CHECK (mul (1, -1, 1) == (uint64) -1ll);
}

TEST_CASE ("div64 unsigned")
{
  CHECK (divide (100, 7, 0) == 14);
  CHECK (divide (0, 1, 0) == 0);
  CHECK (divide (0xffffffffull, 1, 0) == 0xffffffff);
  CHECK (divide (0x100000000ull, 2, 0) == 0x80000000);

  /* The quotient is truncated to 32 bits, as SPARC V8 UDIV leaves it.  */
  CHECK (divide (0xffffffffffffffffull, 1, 0) == 0xffffffff);
}

TEST_CASE ("div64 signed")
{
  CHECK (divide (100, 7, 1) == 14);
  CHECK (divide ((uint64) -100ll, 7, 1) == (uint32) -14);
  CHECK (divide (100, -7, 1) == (uint32) -14);
  CHECK (divide ((uint64) -100ll, -7, 1) == 14);

  /* Truncation is towards zero, not towards minus infinity.  */
  CHECK (divide ((uint64) -1ll, 2, 1) == 0);
}

TEST_CASE ("clear_accex clears the accrued FPU exceptions")
{
  feraiseexcept (FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW);
  REQUIRE (fetestexcept (FE_ALL_EXCEPT) != 0);

  clear_accex ();

  CHECK (fetestexcept (FE_ALL_EXCEPT) == 0);
}

TEST_CASE_FIXTURE (exec_fixture, "init_regs resets every cpu")
{
  cputype = CPU_LEON3;
  nfp = 0;

  for (int i = 0; i < NCPU; i++)
    {
      regs[i].pc = 0x1234;
      regs[i].npc = 0x1238;
      regs[i].trap = 7;
      regs[i].err_mode = 1;
      regs[i].bphit = 1;
      ext_irl[i] = 1;
    }

  init_regs (&regs[0]);

  CHECK (ebase.wphit == 0);

  for (int i = 0; i < NCPU; i++)
    {
      CHECK (regs[i].pc == 0);
      CHECK (regs[i].npc == 4);
      CHECK (regs[i].trap == 0);
      CHECK (regs[i].err_mode == 0);
      CHECK (regs[i].bphit == 0);
      CHECK (regs[i].breakpoint == 0);
      CHECK (regs[i].g[0] == 0);
      CHECK (regs[i].r[0] == 0);
      CHECK (regs[i].fsr == 0);
      CHECK (regs[i].y == 0);
      CHECK (regs[i].simtime == 0);
      CHECK (regs[i].mode == 1);
      CHECK (ext_irl[i] == 0);
      CHECK (regs[i].cpu == i);
      CHECK (regs[i].fs == (float32 *) regs[i].fd);
      CHECK (regs[i].fsi == (int32 *) regs[i].fd);

      /* Only the boot cpu comes out of reset running.  */
      CHECK (regs[i].pwd_mode == (i == 0 ? 0 : 1));

      /* ASR17 carries the cpu index in its top four bits.  */
      CHECK ((regs[i].asr17 >> 28) == (uint32) i);
    }
}

TEST_CASE_FIXTURE (exec_fixture, "init_regs sets the supervisor bit per cpu")
{
  nfp = 0;

  SUBCASE ("erc32")
  {
    cputype = CPU_ERC32;
    regs[0].psr = 0xffffffff;
    init_regs (&regs[0]);
    CHECK (regs[0].psr == ((0xffffffff & 0x00f03fdf) | 0x11000080));
  }

  SUBCASE ("leon2")
  {
    cputype = CPU_LEON2;
    regs[0].psr = 0xffffffff;
    init_regs (&regs[0]);
    CHECK (regs[0].psr == ((0xffffffff & 0x00f03fdf) | 0x00000080));
  }

  SUBCASE ("leon3 and anything else")
  {
    cputype = CPU_LEON3;
    regs[0].psr = 0xffffffff;
    init_regs (&regs[0]);
    CHECK (regs[0].psr == ((0xffffffff & 0x00f03fdf) | 0xf3000080));
  }
}

TEST_CASE_FIXTURE (exec_fixture, "init_regs reports the FPU through nfp")
{
  cputype = CPU_LEON3;

  SUBCASE ("present")
  {
    nfp = 0;
    init_regs (&regs[0]);
    CHECK (regs[0].fpu_pres == 1);
    /* Meiko FPU in ASR17 bits 11:10.  */
    CHECK ((regs[0].asr17 & (3 << 10)) == (3 << 10));
  }

  SUBCASE ("absent")
  {
    nfp = 1;
    init_regs (&regs[0]);
    CHECK (regs[0].fpu_pres == 0);
    CHECK ((regs[0].asr17 & (3 << 10)) == 0);
  }
}
