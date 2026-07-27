/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Tests for interf.cc, the sim_* glue the GDB stub in remote.cc calls.

   The reference is the GDB remote serial protocol, not a hardware manual.
   The packets that reach this layer are:

     m/M   memory read and write, payload in target byte order
	   (GDB manual, "Packets": "Memory is transmitted ... in target
	   byte order")            -> sim_read, sim_write
     c/s   continue and step                 -> sim_resume
     C/k/R/vRun/vKill  restart               -> sim_create_inferior
     Z0/z0 software breakpoint               -> sim_set/clear_watchpoint 0
     Z1/z1 hardware breakpoint               -> type 1
     Z2/z2 write watchpoint                  -> type 2
     Z3/z3 read watchpoint                   -> type 3
     Z4/z4 access watchpoint                 -> type 4

   The Z packet type numbering is the GDB manual, "Packets", Z0..Z4.  A Z
   packet answered with a zero return makes remote.cc reply E01, a non-zero
   return makes it reply OK, so the return value is the protocol-visible
   result and every case asserts it.

   A case drives the layer against a fake board rather than a real one: the
   glue only reaches ms->sis_memory_read/write, ms->boot_init, ms->sim_halt
   and ms->exit_sim, so a memsys with a small RAM window covers everything
   without registering peripherals or touching host stdio.  */

#include "doctest.h"
#include "support.h"

#include "config.h"
#include "sis.h"

#include <stdio.h>
#include <string.h>
#include <string>

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

using sis_tests::stdout_capture;

/* The legacy GDB simulator entry points interf.cc still exports.  They have
   no caller in the tree, so sis.h does not declare them; the signatures are
   the ones interf.cc defines (SIM_DESC is an int there).  */
void sim_close (int sd, int quitting);
void sim_info (int sd, int verbose);
int sim_stop (int sd);
int sim_can_use_hw_breakpoint (int sd, int type, int cnt, int othertype);
int sim_stopped_by_watchpoint (int sd);
int sim_watchpoint_address (int sd);

namespace
{

/* The fake board's RAM window.  0xA0000000 is clear of the addresses the
   other test files register cores at.  */
const uint32 RAM = 0xA0000000;
const uint32 RAM_WORDS = 16;
const uint32 RAM_BYTES = RAM_WORDS * 4;

char fake_ram[RAM_BYTES];

struct fake_counts
{
  int reset;
  int boot_init;
  int sim_halt;
  int exit_sim;
  int error_mode;
};

fake_counts counts;

int
in_ram (uint32 addr, uint32 length)
{
  return (addr >= RAM) && (addr + length <= RAM + RAM_BYTES);
}

void
fake_init_sim (void)
{
}

void
fake_reset (void)
{
  counts.reset++;
}

void
fake_error_mode (uint32 pc)
{
  (void) pc;
  counts.error_mode++;
}

void
fake_sim_halt (void)
{
  counts.sim_halt++;
}

void
fake_exit_sim (void)
{
  counts.exit_sim++;
}

void
fake_init_stdio (void)
{
}

void
fake_restore_stdio (void)
{
}

int
fake_memory_read (uint32 addr, uint32 *data, int32 *ws)
{
  *ws = 0;
  if (!in_ram (addr, 4))
    return 1;
  memcpy (data, &fake_ram[addr - RAM], 4);
  return 0;
}

int
fake_memory_write (uint32 addr, uint32 *data, int32 sz, int32 *ws)
{
  (void) sz;
  *ws = 0;
  if (!in_ram (addr, 4))
    return 1;
  memcpy (&fake_ram[addr - RAM], data, 4);
  return 0;
}

int
fake_sis_memory_write (uint32 addr, const char *data, uint32 length)
{
  if (!in_ram (addr, length))
    return 0;
  memcpy (&fake_ram[addr - RAM], data, length);
  return length;
}

int
fake_sis_memory_read (uint32 addr, char *data, uint32 length)
{
  if (!in_ram (addr, length))
    return 0;
  memcpy (data, &fake_ram[addr - RAM], length);
  return length;
}

void
fake_boot_init (void)
{
  counts.boot_init++;
}

char *
fake_get_mem_ptr (uint32 addr, uint32 size)
{
  if (!in_ram (addr, size))
    return (char *) -1;
  return &fake_ram[addr - RAM];
}

void
fake_set_irq (int32 level)
{
  (void) level;
}

const struct memsys fake_board = { fake_init_sim,	  fake_reset,
				   fake_error_mode,	  fake_sim_halt,
				   fake_exit_sim,	  fake_init_stdio,
				   fake_restore_stdio,	  fake_memory_read,
				   fake_memory_read,	  fake_memory_write,
				   fake_sis_memory_write, fake_sis_memory_read,
				   fake_boot_init,	  fake_get_mem_ptr,
				   fake_set_irq };

/* Everything the glue reaches is a global, so the fixture takes a full copy
   of the state it disturbs and puts it back.  stdin's descriptor flags are
   included because sim_close writes termsave back onto file descriptor 0,
   which is process wide.  */
struct interf_fixture
{
  int saved_verbose;
  int saved_cputype;
  int saved_archtype;
  int saved_gdb_break;
  int saved_ctrl_c;
  int saved_ncpu;
  int saved_cpu;
  uint32 saved_load_addr;
  const struct memsys *saved_ms;
  const struct cpu_arch *saved_arch;
  int saved_simstat;
  int saved_flags;

  interf_fixture ()
      : saved_verbose (sis_verbose), saved_cputype (cputype),
	saved_archtype (archtype), saved_gdb_break (sis_gdb_break),
	saved_ctrl_c (ctrl_c), saved_ncpu (ncpu), saved_cpu (cpu),
	saved_load_addr (last_load_addr), saved_ms (ms), saved_arch (arch),
	saved_simstat (simstat), saved_flags (-1)
  {
#if defined(F_GETFL)
    saved_flags = fcntl (0, F_GETFL);
#endif
    cputype = CPU_ERC32;
    archtype = CPU_SPARC;
    ms = &fake_board;
    arch = &sparc32;
    ncpu = 1;
    cpu = 0;
    ctrl_c = 0;
    sis_gdb_break = 0;
    ebase.freq = 14;
    ebase.simtime = 0;
    ebase.simstart = 0;
    ebase.coven = 0;
    ebase.tottime = 0.0;
    memset (fake_ram, 0, sizeof (fake_ram));
    reset_all ();
    init_bpt (sregs);
    for (int i = 0; i < NCPU; i++)
      reset_stat (&sregs[i]); /* the retired instruction counts are not */
    ebase.simstart = 0;	      /* part of a reset, and a case reads them */
    memset (&counts, 0, sizeof (counts));
    sis_verbose = 0;
    simstat = OK;

    /* The tables the watchpoint and breakpoint calls append to, and the
       saved stack pointers sim_read consults, are not cleared by
       reset_all.  Start every case from an empty set.  */
    ebase.wphit = 0;
    ebase.wptype = 0;
    ebase.wpaddress = 0;
    memset (ebase.bpts, 0, sizeof (ebase.bpts));
    memset (ebase.bpsave, 0, sizeof (ebase.bpsave));
    memset (ebase.wprs, 0, sizeof (ebase.wprs));
    memset (ebase.wprm, 0, sizeof (ebase.wprm));
    memset (ebase.wpws, 0, sizeof (ebase.wpws));
    memset (ebase.wpwm, 0, sizeof (ebase.wpwm));
    memset (sregs[0].sp, 0xff, sizeof (sregs[0].sp));

    REQUIRE (ebase.bptnum == 0);
    REQUIRE (ebase.wprnum == 0);
    REQUIRE (ebase.wpwnum == 0);
  }

  ~interf_fixture ()
  {
    init_bpt (sregs);
    sis_verbose = saved_verbose;
    cputype = saved_cputype;
    archtype = saved_archtype;
    sis_gdb_break = saved_gdb_break;
    ctrl_c = saved_ctrl_c;
    ncpu = saved_ncpu;
    cpu = saved_cpu;
    last_load_addr = saved_load_addr;
    ms = saved_ms;
    arch = saved_arch;
    simstat = saved_simstat;
#if defined(F_SETFL)
    if (saved_flags >= 0)
      fcntl (0, F_SETFL, saved_flags);
#endif
  }
};

/* The word the fake board holds at a RAM address, in host layout.  */
uint32
ram_word (uint32 addr)
{
  uint32 w = 0;
  memcpy (&w, &fake_ram[addr - RAM], 4);
  return w;
}

void
set_ram_word (uint32 addr, uint32 value)
{
  memcpy (&fake_ram[addr - RAM], &value, 4);
}

} // namespace

TEST_CASE ("sim_write and sim_read carry an M packet in target byte order")
{
  interf_fixture f;

  /* GDB sends memory contents most significant byte first, so byte i of the
     packet is target address mem+i.  The layer converts to the host layout
     the board stores with the arch's bswap, which is 3 for big endian SPARC
     on a little endian host and 0 when the two agree.  */
  const char packet[4] = { 0x11, 0x22, 0x33, 0x44 };
  char back[4] = { 0, 0, 0, 0 };

  CHECK (sim_write (RAM, packet, 4) == 4);

  for (int i = 0; i < 4; i++)
    CHECK (fake_ram[i ^ arch->bswap] == packet[i]);

  CHECK (sim_read (RAM, back, 4) == 4);
  CHECK (memcmp (back, packet, 4) == 0);

  /* A single byte is addressed the same way, so a byte written on its own
     lands where the word access sees it.  */
  const char one = 0x55;
  CHECK (sim_write (RAM + 1, &one, 1) == 1);
  CHECK (fake_ram[1 ^ arch->bswap] == 0x55);
}

TEST_CASE ("sim_write and sim_read accept a zero length packet")
{
  interf_fixture f;

  /* GDB probes with a zero length m packet.  Nothing may be transferred and
     the reported length stays zero.  */
  char back[4] = { 1, 2, 3, 4 };

  set_ram_word (RAM, 0xa5a5a5a5);
  CHECK (sim_write (RAM, back, 0) == 0);
  CHECK (ram_word (RAM) == 0xa5a5a5a5);
  CHECK (sim_read (RAM, back, 0) == 0);
  CHECK (back[0] == 1);
}

TEST_CASE ("sim_read serves a stacked window register instead of memory")
{
  interf_fixture f;

  /* With sis_gdb_break the SPARC target may still hold a stack slot in a
     register window that has not been spilled, so a word read of an address
     inside a saved window frame must come from the register file.  The
     window frame is 64 bytes at the saved %sp of a valid window.  */
  sis_gdb_break = 1;
  archtype = CPU_SPARC;
  sregs[0].wim = 0;
  sregs[0].r[14] = RAM;	       /* window 0 %sp */
  sregs[0].r[16] = 0xdeadbeef; /* the word cached at that address */
  save_sp (&sregs[0]);
  REQUIRE (sregs[0].sp[0] == RAM);

  set_ram_word (RAM, 0x01020304);

  char buf[4] = { 0, 0, 0, 0 };
  CHECK (sim_read (RAM, buf, 4) == 4);

  /* The register value is delivered most significant byte first, the same
     order the m packet reply uses.  */
  CHECK ((unsigned char) buf[0] == 0xde);
  CHECK ((unsigned char) buf[1] == 0xad);
  CHECK ((unsigned char) buf[2] == 0xbe);
  CHECK ((unsigned char) buf[3] == 0xef);
}

TEST_CASE ("sim_read reads memory outside every saved window")
{
  interf_fixture f;

  /* Same setup, but the address is not inside any window frame, so the read
     falls through to the board.  */
  sis_gdb_break = 1;
  archtype = CPU_SPARC;
  sregs[0].wim = 0;
  sregs[0].r[14] = 0x40000000;
  sregs[0].r[16] = 0xdeadbeef;
  save_sp (&sregs[0]);
  REQUIRE (sregs[0].sp[0] == 0x40000000);

  const char packet[4] = { 0x01, 0x02, 0x03, 0x04 };
  char buf[4] = { 0, 0, 0, 0 };

  REQUIRE (sim_write (RAM, packet, 4) == 4);
  CHECK (sim_read (RAM, buf, 4) == 4);
  CHECK (memcmp (buf, packet, 4) == 0);
}

TEST_CASE ("sim_read consults the windows only for a SPARC word access")
{
  interf_fixture f;

  /* The register window cache is a SPARC property and needs a whole word,
     so a sub word read and a RISC-V target both go to memory even when the
     address is inside a saved frame.  */
  sregs[0].wim = 0;
  sregs[0].r[14] = RAM;
  sregs[0].r[16] = 0xdeadbeef;
  save_sp (&sregs[0]);
  set_ram_word (RAM, 0);

  const char packet[4] = { 0x01, 0x02, 0x03, 0x04 };
  REQUIRE (sim_write (RAM, packet, 4) == 4);

  char buf[4] = { 0, 0, 0, 0 };

  SUBCASE ("gdb break off")
  {
    sis_gdb_break = 0;
    archtype = CPU_SPARC;
    CHECK (sim_read (RAM, buf, 4) == 4);
    CHECK (memcmp (buf, packet, 4) == 0);
  }

  SUBCASE ("RISC-V target")
  {
    sis_gdb_break = 1;
    archtype = CPU_RISCV;
    CHECK (sim_read (RAM, buf, 4) == 4);
    CHECK (memcmp (buf, packet, 4) == 0);
  }

  SUBCASE ("short read")
  {
    sis_gdb_break = 1;
    archtype = CPU_SPARC;
    CHECK (sim_read (RAM, buf, 2) == 2);
    CHECK (buf[0] == packet[0]);
    CHECK (buf[1] == packet[1]);
  }
}

TEST_CASE ("sim_create_inferior restarts every cpu at the load address")
{
  interf_fixture f;

  /* R, k, vRun and vKill all restart the program.  Every cpu goes back to
     the entry point of the loaded file with the simulated time cleared.  The
     low bit of the recorded entry is not part of the address.  */
  ncpu = 2;
  last_load_addr = RAM | 1;
  ebase.simtime = 4711;
  ebase.simstart = 4711;
  sregs[0].ninst = 99;

  sim_create_inferior ();

  CHECK (ebase.simtime == 0);
  CHECK (ebase.simstart == 0);
  CHECK (counts.reset == 1);
  CHECK (sregs[0].ninst == 0);
  for (int i = 0; i < 2; i++)
    {
      CHECK (sregs[i].pc == RAM);
      CHECK (sregs[i].npc == RAM + 4);
    }
}

/* Three NOPs at the start of the fake RAM and a hardware breakpoint on the
   instruction after them, so a step and a continue end at different
   addresses.  A SPARC NOP is sethi %hi(0), %g0.  */
static void
load_nop_run (void)
{
  const uint32 SPARC_NOP = 0x01000000;

  set_ram_word (RAM, SPARC_NOP);
  set_ram_word (RAM + 4, SPARC_NOP);
  set_ram_word (RAM + 8, SPARC_NOP);
  REQUIRE (sim_set_watchpoint (RAM + 8, 4, 1) == 1);
  sregs[0].pc = RAM;
  sregs[0].npc = RAM + 4;
}

TEST_CASE ("sim_resume steps one instruction and boots on the first run")
{
  interf_fixture f;

  /* The s packet retires exactly one instruction and reports back.  The
     board is booted once, on the first resume after a load, and halted again
     when the step is over so the stub can serve the following packets.  */
  load_nop_run ();

  sim_resume (1);

  CHECK (simstat == TIME_OUT);
  CHECK (sregs[0].pc == RAM + 4);
  CHECK (sregs[0].ninst == 1);
  CHECK (counts.boot_init == 1);
  CHECK (counts.sim_halt == 1);

  /* Simulated time has moved off zero, so a second step does not boot the
     board again.  */
  REQUIRE (ebase.simtime > 0);
  sim_resume (1);
  CHECK (sregs[0].pc == RAM + 8);
  CHECK (sregs[0].ninst == 2);
  CHECK (counts.boot_init == 1);
  CHECK (counts.sim_halt == 2);
}

TEST_CASE ("sim_resume continue runs on and drops the gdb socket poll")
{
  interf_fixture f;

  /* The c packet runs until something stops it, here the breakpoint two
     instructions on.  The stub has to keep watching its socket for an
     interrupt request while that runs, so sim_resume arms the poll for the
     duration and takes it back out afterwards, leaving the event queue as it
     found it.  */
  load_nop_run ();

  sim_resume (0);

  CHECK (simstat == BPT_HIT);
  CHECK (sregs[0].pc == RAM + 8);
  CHECK (sregs[0].ninst == 2);
  CHECK (counts.boot_init == 1);
  CHECK (counts.sim_halt == 1);

  /* The only event left is the sentinel at the end of time.  */
  CHECK (ebase.evtime == UINT64_MAX);
}

TEST_CASE ("sim_resume saves the stack pointers for a SPARC target only")
{
  interf_fixture f;

  /* The register window cache sim_read uses is refreshed when the target
     stops, and only a SPARC target has one.  */
  sregs[0].err_mode = 1;
  sregs[0].wim = 0;
  sregs[0].r[14] = RAM;

  SUBCASE ("gdb break off")
  {
    sis_gdb_break = 0;
    cputype = CPU_ERC32;
    sim_resume (1);
    CHECK (sregs[0].sp[0] == 0xffffffff);
  }

  SUBCASE ("RISC-V target")
  {
    sis_gdb_break = 1;
    cputype = CPU_RISCV;
    sim_resume (1);
    CHECK (sregs[0].sp[0] == 0xffffffff);
  }

  SUBCASE ("SPARC target")
  {
    sis_gdb_break = 1;
    cputype = CPU_ERC32;
    sim_resume (1);
    CHECK (sregs[0].sp[0] == RAM);
  }
}

TEST_CASE ("Z0 patches a word breakpoint and saves the opcode")
{
  interf_fixture f;

  /* A Z0 packet carries the address and a kind, the instruction length in
     bytes (GDB manual, "Packets", Z0).  Kind 4 is a 32 bit instruction, and
     the patched word is the target's breakpoint instruction: EBREAK,
     0x00100073, on RISC-V (RISC-V unprivileged ISA, "Environment Call and
     Breakpoint"), and ta 1, 0x91d02001, on SPARC (SPARC V8 appendix B.27,
     format 3 with cond TA, i = 1 and software_trap# = 1), which is the word
     sparc.cc treats as a debugger breakpoint.  The replaced opcode is kept
     so z0 can put it back.  */
  set_ram_word (RAM, 0x12345678);
  set_ram_word (RAM + 4, 0x87654321);

  uint32 expected;

  SUBCASE ("RISC-V target")
  {
    archtype = CPU_RISCV;
    expected = 0x00100073;
  }

  SUBCASE ("SPARC target")
  {
    archtype = CPU_SPARC;
    expected = 0x91d02001;
  }

  CHECK (sim_set_watchpoint (RAM, 4, 0) == 1);

  CHECK (ebase.bptnum == 1);
  CHECK (ebase.bpts[0] == RAM);
  CHECK (ebase.bpsave[0] == 0x12345678);
  CHECK (ram_word (RAM) == expected);
  CHECK (ram_word (RAM + 4) == 0x87654321);

  CHECK (sim_clear_watchpoint (RAM, 4, 0) == 1);
  CHECK (ebase.bptnum == 0);
  CHECK (ram_word (RAM) == 0x12345678);
}

TEST_CASE ("Z0 kind 2 patches a halfword breakpoint with C.EBREAK")
{
  interf_fixture f;

  /* Kind 2 is a 16 bit instruction, so the patch is the compressed
     breakpoint.  The RISC-V unprivileged ISA, C extension, "Breakpoint
     Instruction", defines c.ebreak as the c.add opcode with rd and rs2 both
     zero: funct4 1001, rd 00000, rs2 00000, op 10, which is 0x9002.  */
  unsigned short patched;

  archtype = CPU_RISCV;
  set_ram_word (RAM, 0x12345678);

  CHECK (sim_set_watchpoint (RAM, 2, 0) == 1);
  CHECK (ebase.bptnum == 1);

  memcpy (&patched, fake_ram, 2);
  CHECK (patched == 0x9002);

  /* Only the halfword is touched, and z0 with the same kind restores it.  */
  CHECK (sim_clear_watchpoint (RAM, 2, 0) == 1);
  CHECK (ram_word (RAM) == 0x12345678);
  CHECK (ebase.bptnum == 0);
}

TEST_CASE ("Z0 is refused once the breakpoint table is full")
{
  interf_fixture f;

  /* A refused Z packet makes remote.cc answer E01, which tells GDB the
     breakpoint could not be set.  */
  ebase.bptnum = BPT_MAX;
  CHECK (sim_set_watchpoint (RAM, 4, 0) == 0);
  CHECK (ebase.bptnum == BPT_MAX);
  CHECK (ram_word (RAM) == 0);
}

TEST_CASE ("z0 restores the opcode and closes the gap in the table")
{
  interf_fixture f;

  /* Three breakpoints, the middle one removed: its opcode goes back and the
     entries above it move down, so the table stays dense.  The target is
     RISC-V here, so the patched word is EBREAK.  */
  archtype = CPU_RISCV;
  set_ram_word (RAM, 0x11111111);
  set_ram_word (RAM + 4, 0x22222222);
  set_ram_word (RAM + 8, 0x33333333);

  REQUIRE (sim_insert_swbreakpoint (RAM, 4) == 1);
  REQUIRE (sim_insert_swbreakpoint (RAM + 4, 4) == 1);
  REQUIRE (sim_insert_swbreakpoint (RAM + 8, 4) == 1);
  REQUIRE (ebase.bptnum == 3);

  CHECK (sim_remove_swbreakpoint (RAM + 4, 4) == 1);

  CHECK (ebase.bptnum == 2);
  CHECK (ebase.bpts[0] == RAM);
  CHECK (ebase.bpts[1] == RAM + 8);
  CHECK (ebase.bpsave[0] == 0x11111111);
  CHECK (ebase.bpsave[1] == 0x33333333);
  CHECK (ram_word (RAM) == 0x00100073);
  CHECK (ram_word (RAM + 4) == 0x22222222);
  CHECK (ram_word (RAM + 8) == 0x00100073);

  /* The first one still removes cleanly through the same path.  */
  CHECK (sim_remove_swbreakpoint (RAM, 4) == 1);
  CHECK (ebase.bptnum == 1);
  CHECK (ram_word (RAM) == 0x11111111);
}

TEST_CASE ("z0 for an address that carries no breakpoint fails")
{
  interf_fixture f;

  /* Nothing is written back and the packet is answered E01.  */
  set_ram_word (RAM, 0x11111111);
  set_ram_word (RAM + 4, 0x22222222);

  CHECK (sim_clear_watchpoint (RAM + 4, 4, 0) == 0);

  REQUIRE (sim_insert_swbreakpoint (RAM, 4) == 1);
  CHECK (sim_remove_swbreakpoint (RAM + 4, 4) == 0);
  CHECK (ebase.bptnum == 1);
  CHECK (ram_word (RAM + 4) == 0x22222222);
}

TEST_CASE ("Z1 and z1 keep a hardware breakpoint in the breakpoint table")
{
  interf_fixture f;

  /* A Z1 hardware breakpoint is recorded in the same table as Z0 but leaves
     the instruction in memory alone.  */
  set_ram_word (RAM, 0x11111111);

  CHECK (sim_set_watchpoint (RAM, 4, 1) == 1);
  CHECK (ebase.bptnum == 1);
  CHECK (ebase.bpts[0] == RAM);
  CHECK (ram_word (RAM) == 0x11111111);

  CHECK (sim_clear_watchpoint (RAM, 4, 1) == 1);
  CHECK (ebase.bptnum == 0);
}

TEST_CASE ("z1 for an unknown address leaves the table alone")
{
  interf_fixture f;

  /* Removing from an empty table, and removing an address that is not in a
     populated one, are both silent successes.  */
  CHECK (sim_clear_watchpoint (RAM, 4, 1) == 1);
  CHECK (ebase.bptnum == 0);

  REQUIRE (sim_set_watchpoint (RAM, 4, 1) == 1);
  CHECK (sim_clear_watchpoint (RAM + 8, 4, 1) == 1);
  CHECK (ebase.bptnum == 1);
  CHECK (ebase.bpts[0] == RAM);
}

TEST_CASE ("Z1 is bounded by the watchpoint count (suspected defect)")
{
  interf_fixture f;

  /* The hardware breakpoint table is ebase.bpts, sized BPT_MAX, but the
     insert tests ebase.wprnum, the number of read watchpoints, against that
     bound.  A target with a full read watchpoint table therefore refuses a
     Z1 packet although no breakpoint has been set at all.  Pinned as a
     suspected defect: the count tested should be ebase.bptnum.  */
  ebase.wprnum = WPR_MAX;
  REQUIRE (ebase.bptnum == 0);

  CHECK (sim_set_watchpoint (RAM, 4, 1) == 0);
  CHECK (ebase.bptnum == 0);

  ebase.wprnum = 0;
  CHECK (sim_set_watchpoint (RAM, 4, 1) == 1);
  CHECK (ebase.bptnum == 1);
}

TEST_CASE ("z1 shifts breakpoints into the watchpoints (suspected defect)")
{
  interf_fixture f;

  /* Removing a hardware breakpoint that is not the last one has to close the
     gap in ebase.bpts.  The shift loop writes ebase.wprs instead, so the
     breakpoint table keeps the stale entry and the read watchpoint table is
     overwritten with breakpoint addresses.  Pinned as a suspected defect:
     the destination should be ebase.bpts.  */
  REQUIRE (sim_set_watchpoint (RAM, 4, 1) == 1);
  REQUIRE (sim_set_watchpoint (RAM + 4, 4, 1) == 1);
  REQUIRE (ebase.bptnum == 2);

  CHECK (sim_clear_watchpoint (RAM, 4, 1) == 1);

  CHECK (ebase.bptnum == 1);
  CHECK (ebase.bpts[0] == RAM);	    /* should be RAM + 4 */
  CHECK (ebase.wprs[0] == RAM + 4); /* should not have been touched */
  CHECK (ebase.wprnum == 0);
}

TEST_CASE ("Z2 and Z3 record a watchpoint with the size mask of its kind")
{
  interf_fixture f;

  /* The kind of a Z2/Z3 packet is the length of the watched region in
     bytes.  check_wpr and check_wpw compare an access against the entry
     under ~(access mask | entry mask), so a region of n bytes is stored as
     the mask n - 1.  */
  CHECK (sim_set_watchpoint (RAM, 4, 2) == 1);
  CHECK (ebase.wpwnum == 1);
  CHECK (ebase.wpws[0] == RAM);
  CHECK (ebase.wpwm[0] == 3);
  CHECK (ebase.wprnum == 0);

  CHECK (sim_set_watchpoint (RAM + 8, 2, 3) == 1);
  CHECK (ebase.wprnum == 1);
  CHECK (ebase.wprs[0] == RAM + 8);
  CHECK (ebase.wprm[0] == 1);
  CHECK (ebase.wpwnum == 1);

  CHECK (sim_clear_watchpoint (RAM, 4, 2) == 1);
  CHECK (ebase.wpwnum == 0);
  CHECK (sim_clear_watchpoint (RAM + 8, 2, 3) == 1);
  CHECK (ebase.wprnum == 0);
}

TEST_CASE ("Z4 sets both halves of an access watchpoint")
{
  interf_fixture f;

  /* An access watchpoint watches reads and writes, so it is stored as one
     entry in each table.  */
  CHECK (sim_set_watchpoint (RAM, 4, 4) == 1);
  CHECK (ebase.wpwnum == 1);
  CHECK (ebase.wprnum == 1);
  CHECK (ebase.wpws[0] == RAM);
  CHECK (ebase.wprs[0] == RAM);

  CHECK (sim_clear_watchpoint (RAM, 4, 4) == 1);
  CHECK (ebase.wpwnum == 0);
  CHECK (ebase.wprnum == 0);
}

TEST_CASE ("Z4 rolls back the wrong half on a failure (suspected defect)")
{
  interf_fixture f;

  /* When one half of an access watchpoint cannot be inserted the packet
     fails, and the half that did go in has to be taken out again.  The write
     half is inserted first, but the rollback removes a read watchpoint, so
     a full read watchpoint table leaves a stray write watchpoint behind and
     drops an unrelated read entry.  Pinned as a suspected defect: the
     rollback should remove the write watchpoint.

     The read table is filled with the entry the rollback finds, so the
     search stays inside the array.  */
  ebase.wprnum = WPR_MAX;
  ebase.wprs[WPR_MAX - 1] = RAM;

  CHECK (sim_set_watchpoint (RAM, 4, 4) == 0);

  CHECK (ebase.wpwnum == 1); /* should be 0 */
  CHECK (ebase.wpws[0] == RAM);
  CHECK (ebase.wprnum == WPR_MAX - 1);

  ebase.wprnum = 0;
}

TEST_CASE ("Z4 fails outright when the write half cannot be inserted")
{
  interf_fixture f;

  /* Nothing is inserted, so nothing is left behind.  */
  ebase.wpwnum = WPW_MAX;

  CHECK (sim_set_watchpoint (RAM, 4, 4) == 0);
  CHECK (ebase.wprnum == 0);
  CHECK (ebase.wpwnum == WPW_MAX);

  ebase.wpwnum = 0;
}

TEST_CASE ("Z2 and Z3 are refused once their table is full")
{
  interf_fixture f;

  ebase.wpwnum = WPW_MAX;
  CHECK (sim_set_watchpoint (RAM, 4, 2) == 0);
  CHECK (ebase.wpwnum == WPW_MAX);
  ebase.wpwnum = 0;

  ebase.wprnum = WPR_MAX;
  CHECK (sim_set_watchpoint (RAM, 4, 3) == 0);
  CHECK (ebase.wprnum == WPR_MAX);
  ebase.wprnum = 0;
}

TEST_CASE ("z2 and z3 for an unknown address leave the tables alone")
{
  interf_fixture f;

  /* An empty table and a populated one that does not hold the address are
     both answered OK with nothing removed.  */
  CHECK (sim_clear_watchpoint (RAM, 4, 2) == 1);
  CHECK (sim_clear_watchpoint (RAM, 4, 3) == 1);

  REQUIRE (sim_set_watchpoint (RAM, 4, 4) == 1);
  CHECK (sim_clear_watchpoint (RAM + 8, 4, 2) == 1);
  CHECK (sim_clear_watchpoint (RAM + 8, 4, 3) == 1);
  CHECK (ebase.wpwnum == 1);
  CHECK (ebase.wprnum == 1);
}

TEST_CASE ("z2 and z3 keep the wrong size mask (suspected defect)")
{
  interf_fixture f;

  /* Removing an entry that is not the last one shifts the addresses down but
     leaves the mask array untouched, so every entry above the hole keeps the
     mask of its predecessor.  check_wpr and check_wpw pair wprs[i] with
     wprm[i], so the surviving watchpoint then covers the wrong region.
     Pinned as a suspected defect: the mask array has to be shifted with the
     addresses.  */
  REQUIRE (sim_set_watchpoint (RAM, 2, 3) == 1);     /* mask 1 */
  REQUIRE (sim_set_watchpoint (RAM + 8, 4, 3) == 1); /* mask 3 */
  REQUIRE (sim_set_watchpoint (RAM, 2, 2) == 1);
  REQUIRE (sim_set_watchpoint (RAM + 8, 4, 2) == 1);

  CHECK (sim_clear_watchpoint (RAM, 2, 3) == 1);
  CHECK (ebase.wprnum == 1);
  CHECK (ebase.wprs[0] == RAM + 8);
  CHECK (ebase.wprm[0] == 1); /* should be 3 */

  CHECK (sim_clear_watchpoint (RAM, 2, 2) == 1);
  CHECK (ebase.wpwnum == 1);
  CHECK (ebase.wpws[0] == RAM + 8);
  CHECK (ebase.wpwm[0] == 1); /* should be 3 */
}

TEST_CASE ("a zero length Z or z packet is a probe and always succeeds")
{
  interf_fixture f;

  /* GDB probes for watchpoint support by sending a Z packet of length zero.
     It must be accepted without inserting anything.  */
  for (int type = 0; type <= 4; type++)
    {
      CHECK (sim_set_watchpoint (RAM, 0, type) == 1);
      CHECK (sim_clear_watchpoint (RAM, 0, type) == 1);
    }

  CHECK (ebase.bptnum == 0);
  CHECK (ebase.wprnum == 0);
  CHECK (ebase.wpwnum == 0);
  CHECK (ram_word (RAM) == 0);
}

TEST_CASE ("an unknown Z or z packet type is refused")
{
  interf_fixture f;

  /* Only types 0 to 4 are defined by the protocol.  Anything else is
     answered E01 and changes nothing.  */
  CHECK (sim_set_watchpoint (RAM, 4, 5) == 0);
  CHECK (sim_clear_watchpoint (RAM, 4, 5) == 0);
  CHECK (ebase.bptnum == 0);
  CHECK (ebase.wprnum == 0);
  CHECK (ebase.wpwnum == 0);
}

TEST_CASE ("sim_can_use_hw_breakpoint refuses only hardware breakpoints")
{
  interf_fixture f;

  /* GDB asks per breakpoint class.  Class 2 is bp_hardware_breakpoint, which
     this target does not implement; the watchpoint classes are available.  */
  CHECK (sim_can_use_hw_breakpoint (0, 2, 1, 0) == 0);
  CHECK (sim_can_use_hw_breakpoint (0, 1, 1, 0) == 1);
  CHECK (sim_can_use_hw_breakpoint (0, 3, 1, 0) == 1);
}

TEST_CASE ("the watchpoint stop is reported with its address")
{
  interf_fixture f;

  /* remote.cc turns a watchpoint stop into a T05watch reply carrying
     ebase.wpaddress, and these two accessors are the same state.  */
  CHECK (sim_stopped_by_watchpoint (0) == 0);
  CHECK (sim_watchpoint_address (0) == 0);

  ebase.wphit = 1;
  ebase.wpaddress = RAM + 8;
  CHECK (sim_stopped_by_watchpoint (0) == 1);
  CHECK (sim_watchpoint_address (0) == (int) (RAM + 8));

  ebase.wphit = 0;
}

TEST_CASE ("sim_info prints the run statistics")
{
  interf_fixture f;

  /* The same report the perf command prints, for the cpu GDB is attached
     to.  */
  ebase.simtime = 1000;
  sregs[0].ninst = 500;

  std::string out;
  {
    stdout_capture capture;
    sim_info (0, 0);
    out = capture.str ();
  }

  CHECK (out.find ("Frequency") != std::string::npos);
  CHECK (out.find ("Instructions    : 500") != std::string::npos);
}

TEST_CASE ("sim_close shuts the board down")
{
  interf_fixture f;

  /* Detaching ends the simulation and gives the terminal back the flags SIS
     saved when it took it over.  */
  sim_close (0, 1);
  CHECK (counts.exit_sim == 1);
}

TEST_CASE ("sim_stop raises the interrupt flag the run loop polls")
{
  interf_fixture f;

  /* An interrupt request from GDB stops the run loop, which polls ctrl_c.  */
  ctrl_c = 0;
  CHECK (sim_stop (0) == 1);
  CHECK (ctrl_c == 1);
  ctrl_c = 0;
}

TEST_CASE ("verbose mode traces the breakpoint and watchpoint interface")
{
  interf_fixture f;

  /* -v traces what the stub does.  The breakpoint patches are traced at the
     second verbosity level, the rest at the first.  */
  std::string out;

  sis_verbose = 2;
  {
    stdout_capture capture;

    sim_create_inferior ();
    sim_set_watchpoint (RAM, 4, 1);
    sim_clear_watchpoint (RAM, 4, 1);
    sim_set_watchpoint (RAM, 4, 3);
    sim_clear_watchpoint (RAM, 4, 3);
    sim_set_watchpoint (RAM, 4, 2);
    sim_clear_watchpoint (RAM, 4, 2);
    sim_set_watchpoint (RAM, 4, 0);
    sim_clear_watchpoint (RAM, 4, 0);
    sim_stopped_by_watchpoint (0);
    sim_watchpoint_address (0);

    out = capture.str ();
  }
  sis_verbose = 0;

  CHECK (out.find ("sim_create_inferior") != std::string::npos);
  CHECK (out.find ("inserted hw breakpoint at a0000000") != std::string::npos);
  CHECK (out.find ("removed hw breakpoint at a0000000") != std::string::npos);
  CHECK (out.find ("inserted read watchpoint at a0000000") !=
	 std::string::npos);
  CHECK (out.find ("removed read watchpoint at a0000000") !=
	 std::string::npos);
  CHECK (out.find ("sim_insert_watchpoint_write: 0xa0000000 : 3") !=
	 std::string::npos);
  CHECK (out.find ("removed write watchpoint at a0000000") !=
	 std::string::npos);
  CHECK (out.find ("added breakpoint 1 at 0xa0000000") != std::string::npos);
  CHECK (out.find ("remove breakpoint 0 at 0xa0000000") != std::string::npos);
  CHECK (out.find ("sim_stopped_by_watchpoint 0") != std::string::npos);
  CHECK (out.find ("sim__watchpoint_address 0") != std::string::npos);
}
