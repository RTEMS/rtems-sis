/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Tests for func.cc: global simulator state, the event queue support
   functions (now/pwd_enter/rt_sync), breakpoint and watchpoint checking,
   the interactive command shell (exec_cmd) and the run loop (run_sim).

   tests/event.cc already covers event()/remove_event()/advance_time() and
   shows the fixture shape for driving func.cc directly; this file does not
   repeat those cases.

   The cases drive the shell against the flat memory of tests/cpumem.cc
   rather than a board, the way tests/sparc.cc drives the integer unit, so a
   case controls exactly what instruction stream and memory map the shell
   command sees.

   func.cc's global state (sregs[], ebase, sis_verbose, ncpu, cpu, cputype,
   archtype, last_load_addr, daddr, port, sim_run) is process-wide and
   shared with every other test file, so every case restores what it
   touches; see the func_fixture below.

   doc/commands.md is the source of truth for user visible shell behaviour;
   a case that pins behaviour the manual does not describe says so in its
   name.  Two discrepancies between the code and the manual turned up while
   writing these cases and are called out at their case instead of being
   silently worked around:

   - doc/commands.md documents "reset" as "equal to run 0"; the code's
     "reset" case only clears simtime/simstart and calls reset_all() plus
     reset_stat(), it does not set pc from last_load_addr, does not call
     ms->boot_init() and does not call run_sim() at all the way "run 0"
     does.
   - doc/commands.md documents the trace command as "trace [count]", but
     the shell only recognises abbreviations that are a prefix of its own
     internal "tra", so the full word "trace" does not match and falls
     through to "syntax error"; only "tra" itself (or a shorter prefix)
     works.
   - doc/commands.md documents a "sym" command; the code that would
     implement it is commented out, so "sym" always falls through to
     "syntax error".  */

#include "doctest.h"
#include "support.h"

#include "config.h"
#include "sis.h"

#include "cpumem.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <string>

using sis_tests::stdout_capture;

/* The coverage bitmap of func.cc.  No declaration in sis.h; nothing outside
   func.cc reads it.  */
extern unsigned char covram[];

namespace
{

/* sethi 0, %g0: the all-zero-bits encoding, and a true SPARC no-op.  */
const uint32 NOP = 0x01000000;

/* st %g1, [%g0 + simm13]: op=3 (memory), op3=4 (store word), rs1=%g0 so the
   effective address is exactly simm13.  */
uint32
store_g1 (int32 simm13)
{
  return (3u << 30) | (1u << 25) | (4u << 19) | (0u << 14) | (1u << 13) |
	 ((uint32) simm13 & 0x1fff);
}

/* Collects stderr the way support.h's stdout_capture collects stdout.
   batch()'s "couldn't open" message is the only func.cc output that goes
   to stderr rather than stdout, so this stays local to this file rather
   than growing support.h for one case.  */
class stderr_capture
{
public:
  stderr_capture () : file (tmpfile ()), saved (-1)
  {
    if (file == NULL)
      return;
    fflush (stderr);
    saved = SIS_DUP (SIS_FILENO (stderr));
    SIS_DUP2 (SIS_FILENO (file), SIS_FILENO (stderr));
  }

  ~stderr_capture ()
  {
    restore ();
    if (file != NULL)
      fclose (file);
  }

  std::string
  str ()
  {
    restore ();
    std::string text;
    char buffer[4096];
    size_t got;
    if (file == NULL)
      return text;
    rewind (file);
    while ((got = fread (buffer, 1, sizeof (buffer), file)) > 0)
      text.append (buffer, got);
    return text;
  }

private:
  void
  restore ()
  {
    if (saved < 0)
      return;
    fflush (stderr);
    SIS_DUP2 (saved, SIS_FILENO (stderr));
    SIS_CLOSE (saved);
    saved = -1;
  }

  FILE *file;
  int saved;
};

/* Fill every word from ADDR for COUNT words with NOP.  */
void
fill_nops (uint32 addr, uint32 count)
{
  for (uint32 i = 0; i < count; i++)
    sis_tests::flatmem_poke (addr + i * 4, NOP);
}

/* jmpl %g0 + 0, %g0: a call through a null pointer.  SPARC's dispatcher
   special-cases this as NULL_TRAP, "halt on null pointer" in sparc.cc,
   which run_sim_mp reports back as the distinct NULL_HIT rather than
   folding it into ERROR_MODE. op=2 (arith/control), op3=0x38 (JMPL).  */
const uint32 JMPL_NULL =
    (2u << 30) | (0u << 25) | (0x38u << 19) | (0u << 14) | (1u << 13) | 0u;

/* An event() callback that does nothing, for cases that only need a
   queued event to exist at a chosen time, not to observe it firing.  */
void
noop_event (int32 arg)
{
  (void) arg;
}

/* Sets ctrl_c the way a real Ctrl-C would (int_handler sets it to 1, not
   the 2 a tlimit timeout uses), from inside the event queue rather than
   from a signal, so a case can make run_sim stop for that reason at a
   chosen simulated time without sending itself a real signal.  */
void
set_ctrl_c (int32 arg)
{
  (void) arg;
  ctrl_c = 1;
}

/* Drives exec_cmd()/run_sim() against the flat memory, with no board.  */
struct func_fixture
{
  int saved_cputype;
  int saved_archtype;
  int saved_verbose;
  int saved_ncpu;
  int saved_cpu;
  int saved_delta;
  int saved_port;
  int saved_sim_run;
  uint32 saved_daddr;
  uint32 saved_last_load_addr;
  const struct memsys *saved_ms;
  const struct cpu_arch *saved_arch;
  struct estate saved_ebase;
  struct pstate saved_sregs[NCPU];

  func_fixture ()
      : saved_cputype (cputype), saved_archtype (archtype),
	saved_verbose (sis_verbose), saved_ncpu (ncpu), saved_cpu (cpu),
	saved_delta (delta), saved_port (port), saved_sim_run (sim_run),
	saved_daddr (daddr), saved_last_load_addr (last_load_addr),
	saved_ms (ms), saved_arch (arch), saved_ebase (ebase)
  {
    memcpy (saved_sregs, sregs, sizeof (saved_sregs));

    cputype = CPU_ERC32;
    archtype = CPU_SPARC;
    ms = &sis_tests::flatmem;
    arch = &sparc32;
    ebase.freq = 14;
    ebase.simtime = 0;
    ebase.simstart = 0;
    ebase.coven = 0;
    sis_tests::flatmem_clear ();
    reset_all ();
    init_bpt (sregs);
    sis_verbose = 0;
    ncpu = 1;
    cpu = 0;
    delta = 50;
    last_load_addr = 0;
    daddr = 0;
    sregs[0].bphit = 0;

    /* Traps enabled, supervisor mode, window zero: a store or a trap the
       cases below drive does not itself fault for lack of a trap table.  */
    for (int i = 0; i < NCPU; i++)
      sregs[i].psr = 0xF3000080 | 0x20;
  }

  ~func_fixture ()
  {
    /* history's storage is the one heap allocation a case here can make;
       free whatever is not the pointer this fixture started with before
       the snapshot below puts the old pointer value back.  */
    for (int i = 0; i < NCPU; i++)
      if (sregs[i].histbuf != saved_sregs[i].histbuf)
	free (sregs[i].histbuf);

    memcpy (sregs, saved_sregs, sizeof (saved_sregs));
    ebase = saved_ebase;
    ms = saved_ms;
    arch = saved_arch;
    cputype = saved_cputype;
    archtype = saved_archtype;
    sis_verbose = saved_verbose;
    ncpu = saved_ncpu;
    cpu = saved_cpu;
    delta = saved_delta;
    port = saved_port;
    sim_run = saved_sim_run;
    daddr = saved_daddr;
    last_load_addr = saved_last_load_addr;
  }
};

}

/* ---- init_bpt, check_bpt, check_wpr, check_wpw, driven directly ---- */

TEST_CASE_FIXTURE (func_fixture, "init_bpt clears every counter and buffer")
{
  ebase.bptnum = 3;
  ebase.wprnum = 2;
  ebase.wpwnum = 1;
  ebase.histlen = 7;
  ebase.tlimit = 123;
  sregs[0].histind = 5;
  sregs[0].histbuf = (struct histype *) calloc (7, sizeof (struct histype));

  init_bpt (sregs);

  CHECK (ebase.bptnum == 0);
  CHECK (ebase.wprnum == 0);
  CHECK (ebase.wpwnum == 0);
  CHECK (ebase.histlen == 0);
  CHECK (ebase.tlimit == 0);
  CHECK (sregs[0].histind == 0);
  CHECK (sregs[0].histbuf == NULL);
}

TEST_CASE_FIXTURE (func_fixture, "check_bpt clears a pending hit without "
				 "reporting it again")
{
  ebase.bpts[0] = 0x1000;
  ebase.bptnum = 1;
  sregs[0].pc = 0x1000;
  sregs[0].bphit = 1;

  CHECK (check_bpt (&sregs[0]) == 0);
  CHECK (sregs[0].bphit == 0);

  /* With bphit already clear, the same pc now reports the hit.  */
  CHECK (check_bpt (&sregs[0]) == BPT_HIT);
}

TEST_CASE_FIXTURE (func_fixture, "check_bpt misses a pc with no breakpoint")
{
  ebase.bpts[0] = 0x2000;
  ebase.bptnum = 1;
  sregs[0].pc = 0x3000;
  sregs[0].bphit = 0;

  CHECK (check_bpt (&sregs[0]) == 0);
}

TEST_CASE_FIXTURE (func_fixture,
		   "check_wpr reports the accessed address on a hit")
{
  ebase.wprs[0] = 0x4000;
  ebase.wprm[0] = 0;
  ebase.wprnum = 1;
  ebase.wphit = 0;

  CHECK (check_wpr (&sregs[0], 0x4000, 0) == WPT_HIT);
  CHECK (ebase.wpaddress == 0x4000);
  CHECK (ebase.wptype == 3);
}

TEST_CASE_FIXTURE (func_fixture,
		   "check_wpr defers to an already pending watchpoint hit")
{
  ebase.wprs[0] = 0x4000;
  ebase.wprm[0] = 0;
  ebase.wprnum = 1;
  ebase.wphit = 1;

  CHECK (check_wpr (&sregs[0], 0x4000, 0) == 0);
  /* wpaddress is still updated even though the hit is not reported.  */
  CHECK (ebase.wpaddress == 0x4000);
}

TEST_CASE_FIXTURE (func_fixture, "check_wpr misses an address outside "
				 "every read watchpoint")
{
  ebase.wprs[0] = 0x4000;
  ebase.wprm[0] = 0;
  ebase.wprnum = 1;
  ebase.wphit = 0;

  CHECK (check_wpr (&sregs[0], 0x5000, 0) == 0);
}

TEST_CASE_FIXTURE (func_fixture, "check_wpw reports the configured "
				 "watchpoint address, not the access "
				 "address (current behaviour)")
{
  /* check_wpr reports the address the access used; check_wpw instead
     reports ebase.wpws[i], the address the watchpoint was set at.  With no
     mask the two coincide on a hit, so this only shows up once a mask
     widens the watchpoint past a single word: a store anywhere in the
     masked region reports the same wpaddress rather than where the store
     actually landed.  Flagged in the report as worth a second look rather
     than asserted as a defect, since it may be an intentional difference
     between the two checks. */
  ebase.wpws[0] = 0x6000;
  ebase.wpwm[0] = 0x0f; /* covers 0x6000-0x600f */
  ebase.wpwnum = 1;
  ebase.wphit = 0;

  CHECK (check_wpw (&sregs[0], 0x6008, 0) == WPT_HIT);
  CHECK (ebase.wpaddress == 0x6000);
  CHECK (ebase.wptype == 2);
}

TEST_CASE_FIXTURE (func_fixture,
		   "check_wpw defers to an already pending watchpoint hit")
{
  ebase.wpws[0] = 0x6000;
  ebase.wpwm[0] = 0;
  ebase.wpwnum = 1;
  ebase.wphit = 1;

  CHECK (check_wpw (&sregs[0], 0x6000, 0) == 0);
}

TEST_CASE_FIXTURE (func_fixture, "check_wpw misses an address outside "
				 "every write watchpoint")
{
  ebase.wpws[0] = 0x6000;
  ebase.wpwm[0] = 0;
  ebase.wpwnum = 1;
  ebase.wphit = 0;

  CHECK (check_wpw (&sregs[0], 0x7000, 0) == 0);
}

/* ---- exec_cmd: breakpoints, doc/commands.md "+bp"/"bp"/"delete" ---- */

TEST_CASE_FIXTURE (func_fixture, "exec_cmd on a null command is a no-op")
{
  CHECK (exec_cmd (NULL) == OK);
}

TEST_CASE_FIXTURE (func_fixture, "exec_cmd on an empty command is a no-op")
{
  CHECK (exec_cmd ("") == OK);
}

TEST_CASE_FIXTURE (func_fixture, "exec_cmd rejects an unknown command")
{
  stdout_capture capture;

  exec_cmd ("frobnicate");

  CHECK (capture.str ().find ("syntax error") != std::string::npos);
}

TEST_CASE_FIXTURE (func_fixture, "'bp' with no argument lists nothing set")
{
  stdout_capture capture;

  exec_cmd ("bp");

  CHECK (capture.str ().empty ());
}

TEST_CASE_FIXTURE (func_fixture, "'+bp' adds a breakpoint at an address")
{
  stdout_capture capture;

  exec_cmd ("+bp 0x1000");

  std::string text = capture.str ();
  CHECK (text.find ("added breakpoint 1 at 0x00001000") != std::string::npos);
  REQUIRE (ebase.bptnum == 1);
  CHECK (ebase.bpts[0] == 0x1000);
}

TEST_CASE_FIXTURE (func_fixture, "'break' is the long form of '+bp'")
{
  exec_cmd ("break 0x2000");

  REQUIRE (ebase.bptnum == 1);
  CHECK (ebase.bpts[0] == 0x2000);
}

TEST_CASE_FIXTURE (func_fixture, "'+bp 0' names no address and adds nothing")
{
  exec_cmd ("+bp 0");

  CHECK (ebase.bptnum == 0);
}

TEST_CASE_FIXTURE (
    func_fixture,
    "'+bp' with a non-numeric address only exercises the isdigit check "
    "(current behaviour: the address is left unset rather than defaulting "
    "to zero, so whether a breakpoint is added depends on unrelated stack "
    "content; not asserted on for that reason)")
{
  exec_cmd ("+bp abc");

  /* Nothing to assert: only reaching here without crashing matters. */
}

TEST_CASE_FIXTURE (
    func_fixture,
    "'+bp' silently does nothing once sim_set_watchpoint refuses it "
    "(interf.cc's hardware breakpoint pool is out of room; note it is "
    "gated on ebase.wprnum, the *read watchpoint* count, not bptnum, "
    "which looks like an unrelated bug in interf.cc, out of scope here)")
{
  ebase.wprnum = BPT_MAX;

  stdout_capture capture;
  exec_cmd ("+bp 0x1000");

  CHECK (capture.str ().find ("added") == std::string::npos);
  CHECK (ebase.bptnum == 0);

  ebase.wprnum = 0;
}

TEST_CASE_FIXTURE (func_fixture, "'+bp' with no argument lists the "
				 "breakpoints instead of adding one")
{
  exec_cmd ("+bp 0x1000");

  stdout_capture capture;
  exec_cmd ("+bp");

  CHECK (capture.str ().find ("1 : 0x00001000") != std::string::npos);
}

TEST_CASE_FIXTURE (func_fixture, "'bp' lists every breakpoint set")
{
  exec_cmd ("+bp 0x1000");
  exec_cmd ("+bp 0x2000");

  stdout_capture capture;
  exec_cmd ("bp");

  std::string text = capture.str ();
  CHECK (text.find ("1 : 0x00001000") != std::string::npos);
  CHECK (text.find ("2 : 0x00002000") != std::string::npos);
}

TEST_CASE_FIXTURE (func_fixture, "'-bp' deletes a breakpoint by number "
				 "and shifts the rest down")
{
  exec_cmd ("+bp 0x1000");
  exec_cmd ("+bp 0x2000");

  stdout_capture capture;
  exec_cmd ("-bp 1");

  CHECK (capture.str ().find ("deleted breakpoint 1 at 0x00001000") !=
	 std::string::npos);
  REQUIRE (ebase.bptnum == 1);
  CHECK (ebase.bpts[0] == 0x2000);
}

TEST_CASE_FIXTURE (func_fixture, "'delete' is the long form of '-bp'")
{
  exec_cmd ("+bp 0x1000");

  exec_cmd ("delete 1");

  CHECK (ebase.bptnum == 0);
}

TEST_CASE_FIXTURE (func_fixture,
		   "'-bp' with an out of range number changes nothing")
{
  exec_cmd ("+bp 0x1000");

  exec_cmd ("-bp 5");
  exec_cmd ("-bp 0");

  CHECK (ebase.bptnum == 1);
}

TEST_CASE_FIXTURE (func_fixture, "'-bp' with no argument changes nothing")
{
  exec_cmd ("+bp 0x1000");

  exec_cmd ("-bp");

  CHECK (ebase.bptnum == 1);
}

/* ---- exec_cmd: watchpoints, doc/commands.md "wp"/"+wpr"/"+wpw" ---- */

TEST_CASE_FIXTURE (func_fixture, "'wp' lists read and write watchpoints")
{
  exec_cmd ("+wpr 0x100");
  exec_cmd ("+wpw 0x200");

  stdout_capture capture;
  exec_cmd ("wp");

  std::string text = capture.str ();
  CHECK (text.find ("1 : 0x00000100 (read)") != std::string::npos);
  CHECK (text.find ("1 : 0x00000200 (write)") != std::string::npos);
}

TEST_CASE_FIXTURE (func_fixture, "'+wpr'/'rwatch' add a read watchpoint")
{
  stdout_capture capture;
  exec_cmd ("+wpr 0x103");

  CHECK (capture.str ().find ("added read watchpoint 1") != std::string::npos);
  REQUIRE (ebase.wprnum == 1);
  /* The address is masked to a word, as sim_set_watchpoint's caller does
     for every watchpoint kind.  */
  CHECK (ebase.wprs[0] == 0x100);

  exec_cmd ("rwatch 0x200");
  CHECK (ebase.wprnum == 2);
}

TEST_CASE_FIXTURE (func_fixture, "'-wpr' deletes a read watchpoint")
{
  exec_cmd ("+wpr 0x100");
  exec_cmd ("+wpr 0x200");

  stdout_capture capture;
  exec_cmd ("-wpr 1");

  CHECK (capture.str ().find ("deleted read watchpoint 1") !=
	 std::string::npos);
  REQUIRE (ebase.wprnum == 1);
  CHECK (ebase.wprs[0] == 0x200);
}

TEST_CASE_FIXTURE (func_fixture,
		   "'-wpr' with an out of range number changes nothing")
{
  exec_cmd ("+wpr 0x100");

  exec_cmd ("-wpr 9"); /* too high */
  exec_cmd ("-wpr 0"); /* VAL("0")-1 == -1, too low */
  exec_cmd ("-wpr");   /* no argument at all */

  CHECK (ebase.wprnum == 1);
}

TEST_CASE_FIXTURE (func_fixture,
		   "'+wpr' with no argument does nothing (current "
		   "behaviour: unlike '+bp'/'+wpw', it has no listing "
		   "fallback for a missing address)")
{
  stdout_capture capture;
  exec_cmd ("+wpr");

  CHECK (capture.str ().empty ());
  CHECK (ebase.wprnum == 0);
}

TEST_CASE_FIXTURE (
    func_fixture,
    "'+wpr' silently does nothing once WPR_MAX read watchpoints are set")
{
  ebase.wprnum = WPR_MAX;

  stdout_capture capture;
  exec_cmd ("+wpr 0x100");

  CHECK (capture.str ().find ("added") == std::string::npos);

  ebase.wprnum = 0;
}

TEST_CASE_FIXTURE (
    func_fixture,
    "'+wpw' silently does nothing once WPW_MAX write watchpoints are set")
{
  ebase.wpwnum = WPW_MAX;

  stdout_capture capture;
  exec_cmd ("+wpw 0x100");

  CHECK (capture.str ().find ("added") == std::string::npos);

  ebase.wpwnum = 0;
}

TEST_CASE_FIXTURE (func_fixture, "'+wpw'/'watch' add a write watchpoint")
{
  stdout_capture capture;
  exec_cmd ("+wpw 0x100");

  CHECK (capture.str ().find ("added write watchpoint 1") !=
	 std::string::npos);
  REQUIRE (ebase.wpwnum == 1);

  exec_cmd ("watch 0x200");
  CHECK (ebase.wpwnum == 2);
}

TEST_CASE_FIXTURE (func_fixture,
		   "'+wpw' with no argument lists the write watchpoints")
{
  exec_cmd ("+wpw 0x100");

  stdout_capture capture;
  exec_cmd ("+wpw");

  CHECK (capture.str ().find ("1 : 0x00000100 (write)") != std::string::npos);
}

TEST_CASE_FIXTURE (func_fixture, "'-wpw' deletes a write watchpoint")
{
  exec_cmd ("+wpw 0x100");
  exec_cmd ("+wpw 0x200");

  stdout_capture capture;
  exec_cmd ("-wpw 1");

  CHECK (capture.str ().find ("deleted write watchpoint 1") !=
	 std::string::npos);
  REQUIRE (ebase.wpwnum == 1);
  CHECK (ebase.wpws[0] == 0x200);
}

TEST_CASE_FIXTURE (func_fixture,
		   "'-wpw' with an out of range number changes nothing")
{
  exec_cmd ("+wpw 0x100");

  exec_cmd ("-wpw 9"); /* too high */
  exec_cmd ("-wpw 0"); /* VAL("0")-1 == -1, too low */
  exec_cmd ("-wpw");   /* no argument at all */

  CHECK (ebase.wpwnum == 1);
}

/* ---- exec_cmd: simple informational and setting commands ---- */

TEST_CASE_FIXTURE (func_fixture, "'debug' with an argument sets the level "
				 "and reports it")
{
  stdout_capture capture;
  exec_cmd ("debug 3");

  CHECK (sis_verbose == 3);
  CHECK (capture.str ().find ("Debug level = 3") != std::string::npos);
}

TEST_CASE_FIXTURE (func_fixture, "'debug' with no argument only reports "
				 "the current level (current behaviour, "
				 "undocumented command)")
{
  sis_verbose = 2;

  stdout_capture capture;
  exec_cmd ("debug");

  CHECK (sis_verbose == 2);
  CHECK (capture.str ().find ("Debug level = 2") != std::string::npos);
}

TEST_CASE_FIXTURE (func_fixture, "'echo' with a message prints it")
{
  stdout_capture capture;
  exec_cmd ("echo hello there");

  CHECK (capture.str ().find ("hello there") != std::string::npos);
}

TEST_CASE_FIXTURE (func_fixture, "'echo' with no message prints nothing")
{
  stdout_capture capture;
  exec_cmd ("echo");

  CHECK (capture.str ().empty ());
}

TEST_CASE_FIXTURE (func_fixture, "'help' prints the shell command help")
{
  stdout_capture capture;
  exec_cmd ("help");

  CHECK (!capture.str ().empty ());
}

TEST_CASE_FIXTURE (func_fixture, "'float' prints the FPU registers")
{
  stdout_capture capture;
  exec_cmd ("float");

  CHECK (!capture.str ().empty ());
}

TEST_CASE_FIXTURE (func_fixture, "'csr' prints the special registers")
{
  stdout_capture capture;
  exec_cmd ("csr");

  CHECK (!capture.str ().empty ());
}

TEST_CASE_FIXTURE (func_fixture, "'reg' with no argument displays the "
				 "registers")
{
  stdout_capture capture;
  exec_cmd ("reg");

  CHECK (!capture.str ().empty ());
}

TEST_CASE_FIXTURE (func_fixture, "'reg name value' sets a register")
{
  exec_cmd ("reg pc 4096");

  CHECK (sregs[0].pc == 4096);
}

TEST_CASE_FIXTURE (func_fixture,
		   "'cpu' with no argument reports the active cpu")
{
  stdout_capture capture;
  exec_cmd ("cpu");

  CHECK (capture.str ().find ("active cpu: 0") != std::string::npos);
}

TEST_CASE_FIXTURE (func_fixture, "'cpu n' selects the active cpu")
{
  exec_cmd ("cpu 2");

  CHECK (cpu == 2);
  cpu = 0; /* back in range before the fixture touches sregs[cpu] again */
}

TEST_CASE_FIXTURE (func_fixture,
		   "'cpu' above the last cpu clamps to the last one")
{
  /* sregs[] holds NCPU of them, so the highest index a command may leave
     behind is NCPU - 1.  Clamping to NCPU itself named a register block
     one past the end which every later command then indexed.  */
  exec_cmd ("cpu 99");

  CHECK (cpu == NCPU - 1);
  cpu = 0;
}

TEST_CASE_FIXTURE (func_fixture,
		   "'ncpu' with no argument reports the online cpu count")
{
  stdout_capture capture;
  exec_cmd ("ncpu");

  CHECK (capture.str ().find ("number of online cpus: 1") !=
	 std::string::npos);
}

TEST_CASE_FIXTURE (func_fixture, "'ncpu n' sets the online cpu count, "
				 "clamped to NCPU")
{
  exec_cmd ("ncpu 2");
  CHECK (ncpu == 2);

  exec_cmd ("ncpu 99");
  CHECK (ncpu == NCPU);
}

TEST_CASE_FIXTURE (func_fixture, "'perf' displays the statistics")
{
  stdout_capture capture;
  exec_cmd ("perf");

  CHECK (capture.str ().find ("Frequency") != std::string::npos);
}

TEST_CASE_FIXTURE (func_fixture,
		   "show_stat divides by a nonzero wall time even when no "
		   "run has actually taken any (show_stat's own "
		   "1E-6 floor for ebase.tottime)")
{
  ebase.tottime = 0.0;

  stdout_capture capture;
  exec_cmd ("perf");

  CHECK (!capture.str ().empty ());
}

TEST_CASE_FIXTURE (func_fixture,
		   "show_stat uses the wall time already accumulated, "
		   "when a run has taken one")
{
  ebase.tottime = 2.5;

  stdout_capture capture;
  exec_cmd ("perf");

  CHECK (!capture.str ().empty ());
}

TEST_CASE_FIXTURE (func_fixture,
		   "show_stat folds a core's power-down time into its "
		   "statistics before reporting")
{
  sregs[0].simtime = 1000;
  sregs[0].pwd_mode = 1;
  sregs[0].pwdstart = 100;
  sregs[0].pwdtime = 0;

  stdout_capture capture;
  exec_cmd ("perf");

  CHECK (sregs[0].pwdtime == 1000 - 100);
  CHECK (!capture.str ().empty ());

  sregs[0].pwd_mode = 0;
}

TEST_CASE_FIXTURE (func_fixture, "'perf reset' resets the statistics")
{
  ebase.simtime = 1000;
  sregs[0].ninst = 42;

  exec_cmd ("perf reset");

  CHECK (sregs[0].ninst == 0);
  CHECK (ebase.simstart == ebase.simtime);
}

TEST_CASE_FIXTURE (func_fixture,
		   "'perf' with an argument other than 'reset' just "
		   "displays, the same as no argument")
{
  sregs[0].ninst = 3;

  stdout_capture capture;
  exec_cmd ("perf foo");

  CHECK (sregs[0].ninst == 3);
  CHECK (capture.str ().find ("Frequency") != std::string::npos);
}

TEST_CASE_FIXTURE (func_fixture, "'quit' returns QUIT without touching "
				 "any state")
{
  CHECK (exec_cmd ("quit") == QUIT);
}

TEST_CASE_FIXTURE (func_fixture, "'reset' clears the simulated time and "
				 "the statistics, doc/commands.md \"reset\" "
				 "(current behaviour: unlike \"run 0\" as "
				 "documented, it does not set pc from the "
				 "loaded file, does not call boot_init and "
				 "does not call run_sim at all)")
{
  ebase.simtime = 12345;
  sregs[0].ninst = 7;
  last_load_addr = 0x1000;
  sregs[0].pc = 0x9999;

  exec_cmd ("reset");

  CHECK (ebase.simtime == 0);
  CHECK (sregs[0].ninst == 0);
  /* pc came from init_regs() inside reset_all(), not from last_load_addr:
     it is 0, not 0x1000. */
  CHECK (sregs[0].pc == 0);
}

TEST_CASE_FIXTURE (func_fixture,
		   "'shell' with no command runs nothing (current "
		   "behaviour, undocumented command)")
{
  /* Only the "no argument" branch is exercised: running an actual host
     shell from a test is not worth the portability cost, and the syntax
     is otherwise a straight system() call. */
  CHECK (exec_cmd ("shell") == OK);
}

TEST_CASE_FIXTURE (func_fixture,
		   "'shell command' runs it through the host shell "
		   "(current behaviour, undocumented command, POSIX only)")
{
  /* "true"/"false" are portable enough on the POSIX hosts this runs on
     to exercise system()'s return value both ways without depending on
     anything else being installed. */
  CHECK (exec_cmd ("shell true") == OK);
  CHECK (exec_cmd ("shell false") == OK);
}

/* ---- exec_cmd: 'mem'/'wmem'/'disas', doc/commands.md ---- */

TEST_CASE_FIXTURE (func_fixture, "'wmem'/'mem' round-trip a word")
{
  exec_cmd ("wmem 0x40 0xdeadbeef");

  stdout_capture capture;
  exec_cmd ("mem 0x40 16");

  CHECK (capture.str ().find ("deadbeef") != std::string::npos);
  CHECK (daddr == 0x40 + 16);
}

TEST_CASE_FIXTURE (func_fixture, "'wmem' with no data argument writes nothing")
{
  /* The value written used to be whatever the local held, so a bare wmem
     put an uninitialised word into memory at the last address displayed.  */
  daddr = 0x44;
  ms->sis_memory_write (daddr, (char *) "\xaa\xaa\xaa\xaa", 4);

  exec_cmd ("wmem");

  uint32 word = 0;
  ms->sis_memory_read (daddr, (char *) &word, 4);
  CHECK (daddr == 0x44);
  CHECK (word == 0xaaaaaaaa);

  /* An address on its own names where a later write goes, and still
     writes nothing by itself.  */
  exec_cmd ("wmem 0x48");
  CHECK (daddr == 0x48);
}

TEST_CASE_FIXTURE (func_fixture, "'mem' with no argument continues from "
				 "the last displayed address")
{
  daddr = 0x80;

  stdout_capture capture;
  exec_cmd ("mem");

  CHECK (!capture.str ().empty ());
  CHECK (daddr == 0x80 + 64); /* the default length */
}

TEST_CASE_FIXTURE (func_fixture,
		   "'mem' renders a printable byte as itself in the ASCII "
		   "column, and a non-printable one as a dot")
{
  sis_tests::flatmem_poke (0xc0, 0x41424344); /* "ABCD", printable */
  sis_tests::flatmem_poke (0xc4, 0x00010203); /* control bytes */

  stdout_capture capture;
  exec_cmd ("mem 0xc0 16");

  std::string text = capture.str ();
  CHECK (text.find ('A') != std::string::npos);
  CHECK (text.find ('.') != std::string::npos);
}

TEST_CASE_FIXTURE (func_fixture, "'disas' disassembles at an address")
{
  fill_nops (0x200, 4);

  stdout_capture capture;
  exec_cmd ("disas 0x200 2");

  CHECK (capture.str ().find ("nop") != std::string::npos);
  CHECK (daddr == 0x200 + 8);
}

TEST_CASE_FIXTURE (func_fixture, "'disas' with no address continues from "
				 "the last one, default length 16")
{
  fill_nops (0, 20);
  daddr = 0;

  exec_cmd ("disas");

  CHECK (daddr == 16 * 4);
}

TEST_CASE_FIXTURE (func_fixture,
		   "dis_mem decodes a RISC-V compressed instruction as a "
		   "halfword")
{
  int saved_cputype = cputype;
  int saved_archtype = archtype;
  const struct cpu_arch *saved_arch = arch;

  cputype = CPU_RISCV;
  archtype = CPU_RISCV;
  arch = &riscv;
  /* c.nop: a compressed instruction, low two bits not 0b11.  */
  sis_tests::flatmem_poke (0x300, 0x00000001);

  stdout_capture capture;
  uint32 next = dis_mem (0x300, 1);

  cputype = saved_cputype;
  archtype = saved_archtype;
  arch = saved_arch;

  CHECK (next == 0x302);
  CHECK (capture.str ().find ("0001") != std::string::npos);
}

TEST_CASE_FIXTURE (
    func_fixture,
    "dis_mem decodes a RISC-V full width instruction as a word, the "
    "same as any other architecture, when the low two bits are 0b11")
{
  int saved_cputype = cputype;
  int saved_archtype = archtype;
  const struct cpu_arch *saved_arch = arch;

  cputype = CPU_RISCV;
  archtype = CPU_RISCV;
  arch = &riscv;
  /* addi zero, zero, 0: a full width instruction, low two bits 0b11. */
  sis_tests::flatmem_poke (0x310, 0x00000013);

  uint32 next = dis_mem (0x310, 1);

  cputype = saved_cputype;
  archtype = saved_archtype;
  arch = saved_arch;

  CHECK (next == 0x314);
}

/* ---- exec_cmd: 'load', doc/commands.md ---- */

TEST_CASE_FIXTURE (func_fixture, "'load' with no file reports the error")
{
  stdout_capture capture;
  exec_cmd ("load");

  CHECK (capture.str ().find ("load: no file specified") != std::string::npos);
}

TEST_CASE_FIXTURE (func_fixture,
		   "'load' with one missing file reports it once")
{
  stdout_capture capture;
  exec_cmd ("load /nonexistent-a");

  std::string text = capture.str ();
  size_t pos = text.find ("file not found");
  CHECK (pos != std::string::npos);
  CHECK (text.find ("file not found", pos + 1) == std::string::npos);
}

TEST_CASE_FIXTURE (func_fixture,
		   "'load' with several files loads every one of them")
{
  stdout_capture capture;
  exec_cmd ("load /nonexistent-a /nonexistent-b");

  std::string text = capture.str ();
  size_t first = text.find ("file not found");
  REQUIRE (first != std::string::npos);
  CHECK (text.find ("file not found", first + 1) != std::string::npos);
}

/* ---- exec_cmd: 'history', doc/commands.md "hist" ---- */

TEST_CASE_FIXTURE (func_fixture, "'history n' sizes the trace buffer and "
				 "reports the new length")
{
  stdout_capture capture;
  exec_cmd ("history 4");

  CHECK (capture.str ().find ("trace history length = 4") !=
	 std::string::npos);
  CHECK (ebase.histlen == 4);
  REQUIRE (sregs[0].histbuf != NULL);
  CHECK (sregs[0].histind == 0);

  /* A second call frees the first buffer before allocating the new one. */
  exec_cmd ("history 2");
  CHECK (ebase.histlen == 2);
}

TEST_CASE_FIXTURE (func_fixture, "'history 0' disables the trace buffer, "
				 "as doc/commands.md describes")
{
  exec_cmd ("history 4");

  exec_cmd ("history 0");

  CHECK (ebase.histlen == 0);
}

TEST_CASE_FIXTURE (func_fixture,
		   "'history' with no argument displays the trace buffer, "
		   "wrapping the index once it runs off the end")
{
  /* One step, not two: with a length-2 buffer, a single entry leaves
     histind at 1, so the display loop's starting index is already past
     zero and has to wrap once to print both slots. */
  exec_cmd ("history 2");
  fill_nops (0, 4);
  exec_cmd ("step");

  stdout_capture capture;
  exec_cmd ("history");

  CHECK (!capture.str ().empty ());

  exec_cmd ("history 0");
}

/* ---- exec_cmd: 'batch', doc/commands.md ---- */

TEST_CASE_FIXTURE (func_fixture, "'batch' with no file reports the error")
{
  stdout_capture capture;
  exec_cmd ("batch");

  CHECK (capture.str ().find ("no file specified") != std::string::npos);
}

TEST_CASE_FIXTURE (func_fixture,
		   "'batch' on a file that cannot be opened reports it "
		   "to stderr")
{
  stderr_capture capture;
  exec_cmd ("batch /nonexistent/sis-funcq-batch");

  CHECK (capture.str ().find ("couldn't open batch file") !=
	 std::string::npos);
}

TEST_CASE_FIXTURE (func_fixture,
		   "'batch' runs every newline terminated command in the "
		   "file, growing the line buffer for a long one, and "
		   "silently drops a final line with no trailing newline "
		   "(current behaviour, doc/commands.md does not say a "
		   "batch file needs one)")
{
  std::string path = "/tmp/sis-funcq-batch-test";
  FILE *fp = fopen (path.c_str (), "w");
  REQUIRE (fp != NULL);
  fputs ("debug 5\n", fp);
  /* A line long enough to force mygetdelim's buffer past its initial 128
     bytes at least once. */
  fputs ("echo ", fp);
  for (int i = 0; i < 200; i++)
    fputc ('a', fp);
  fputc ('\n', fp);
  /* No trailing newline: dropped by design of the current code. */
  fputs ("debug 9", fp);
  fclose (fp);

  stdout_capture capture;
  exec_cmd (("batch " + path).c_str ());
  std::string text = capture.str ();

  remove (path.c_str ());

  CHECK (sis_verbose == 5);
  CHECK (text.find (std::string (200, 'a')) != std::string::npos);
}

/* ---- exec_cmd: run loop entry points, doc/commands.md ---- */

TEST_CASE_FIXTURE (func_fixture, "'go address' sets pc for every online "
				 "cpu and resumes")
{
  fill_nops (0x400, 4);
  ncpu = 1;

  stdout_capture capture;
  exec_cmd ("go 0x400 2");

  CHECK (capture.str ().find ("resuming at 0x00000400") != std::string::npos);
  CHECK (sregs[0].pc == 0x400 + 2 * 4);
  CHECK (daddr == sregs[0].pc);
}

TEST_CASE_FIXTURE (func_fixture,
		   "'go' with no address uses the last loaded address")
{
  fill_nops (0x500, 1);
  last_load_addr = 0x500;
  /* Bounds the run: "go" with neither address nor count has no way to
     pass a count (the first token after "go" is always taken as the
     address), so without a breakpoint this would run unbounded. */
  exec_cmd ("+bp 0x500");

  exec_cmd ("go");

  CHECK (sregs[0].pc == 0x500);
  CHECK (sregs[0].bphit != 0);
}

TEST_CASE_FIXTURE (
    func_fixture,
    "'go' with no count runs until the next breakpoint, not forever")
{
  fill_nops (0x600, 1);
  exec_cmd ("+bp 0x600");

  exec_cmd ("go 0x600");

  CHECK (sregs[0].pc == 0x600);
  CHECK (sregs[0].bphit != 0);
}

TEST_CASE_FIXTURE (func_fixture,
		   "'go' skips boot_init once simtime is no longer zero")
{
  fill_nops (0, 4);
  exec_cmd ("step"); /* advances ebase.simtime past zero */
  REQUIRE (ebase.simtime != 0);

  fill_nops (0x700, 1);
  exec_cmd ("+bp 0x700");
  /* Nothing to assert on boot_init itself (the flat memory's is a no-op);
     this only has to run clean through the false branch. */
  exec_cmd ("go 0x700");

  CHECK (sregs[0].pc == 0x700);
}

TEST_CASE_FIXTURE (func_fixture, "'cont' resumes from the current pc")
{
  fill_nops (0, 4);
  sregs[0].pc = 0;
  sregs[0].npc = 4;

  exec_cmd ("cont 2");

  CHECK (sregs[0].pc == 8);
}

TEST_CASE_FIXTURE (func_fixture,
		   "'cont' with no count stops at the next breakpoint")
{
  /* The breakpoint is at 4, not 0: "+bp 0" is silently ignored, see the
     "'+bp 0' names no address and adds nothing" case above. */
  fill_nops (0, 2);
  sregs[0].pc = 0;
  sregs[0].npc = 4;
  exec_cmd ("+bp 4");

  exec_cmd ("cont");

  CHECK (sregs[0].pc == 4);
  CHECK (sregs[0].bphit != 0);
}

TEST_CASE_FIXTURE (func_fixture, "'run' resets, then runs from pc 0 when "
				 "no file has been loaded")
{
  fill_nops (0, 4);
  ebase.simtime = 999;

  exec_cmd ("run 2");

  CHECK (ebase.simstart == 0);
  CHECK (sregs[0].pc == 8);
}

TEST_CASE_FIXTURE (func_fixture, "'run' sets pc from the last loaded address")
{
  fill_nops (0x800, 2);
  last_load_addr = 0x800;

  exec_cmd ("run 1");

  CHECK (sregs[0].pc == 0x800 + 4);
}

TEST_CASE_FIXTURE (func_fixture, "'run' with no count stops at the next "
				 "breakpoint rather than running forever")
{
  /* The breakpoint is at 4, not 0: "+bp 0" is silently ignored, see the
     "'+bp 0' names no address and adds nothing" case above. */
  fill_nops (0, 2);
  exec_cmd ("+bp 4");

  exec_cmd ("run");

  CHECK (sregs[0].pc == 4);
  CHECK (sregs[0].bphit != 0);
}

TEST_CASE_FIXTURE (func_fixture, "'step' executes exactly one instruction")
{
  fill_nops (0, 2);
  sregs[0].pc = 0;
  sregs[0].npc = 4;

  exec_cmd ("step");

  CHECK (sregs[0].pc == 4);
}

TEST_CASE_FIXTURE (
    func_fixture,
    "an awake core checks for a pending interrupt before fetching, and "
    "still dispatches normally once the check reports nothing pending "
    "(the interrupt line is masked by psr's PIL field, the same masked, "
    "never-taken setup as the power-down case above, for the same "
    "reason: an interrupt that is actually taken never decrements "
    "icount without a board to lower the line again)")
{
  fill_nops (0, 2);
  sregs[0].pc = 0;
  sregs[0].npc = 4;
  sregs[0].psr |= 0x0f00; /* PSR_PIL = 15 */
  ext_irl[0] = 1;

  exec_cmd ("step");

  CHECK (sregs[0].pc == 4);

  ext_irl[0] = 0;
}

TEST_CASE_FIXTURE (func_fixture,
		   "a core already in error mode runs no instructions at all")
{
  fill_nops (0, 2);
  sregs[0].err_mode = ERROR_MODE;

  int stat = exec_cmd ("step");

  CHECK (stat == ERROR_MODE);
  CHECK (sregs[0].pc == 0); /* nothing dispatched */

  sregs[0].err_mode = 0;
}

TEST_CASE_FIXTURE (
    func_fixture,
    "an access exception with traps disabled drops the core into error "
    "mode instead of trying to run a trap handler that is not there")
{
  sregs[0].psr &= ~0x20;		 /* clear PSR_ET: traps disabled */
  sregs[0].pc = sis_tests::FLATMEM_SIZE; /* one word past the window */
  sregs[0].npc = sis_tests::FLATMEM_SIZE + 4;

  int stat = exec_cmd ("step");

  CHECK (stat == ERROR_MODE);
  CHECK (sregs[0].err_mode != 0);
}

TEST_CASE_FIXTURE (
    func_fixture,
    "a core in power-down mode idles until a timed-out run gives up on "
    "it, rather than looping without ever consuming the instruction "
    "count (current behaviour: run_sim_un's power-down branch never "
    "decrements icount, so only ctrl_c, here from the tlimit timeout, "
    "ends the loop)")
{
  sregs[0].pwd_mode = 1;
  sregs[0].hold = 0;
  ebase.tlimit = 50; /* run_sim() arms sim_timeout at this many cycles */
  /* An interrupt line asserted below the current mask (psr's PIL field):
     check_interrupts() gets called and returns 0, so this does not wake
     the core or change the outcome, but it exercises the "is there a
     pending interrupt to even look at" check while power-down mode
     keeps its own end-to-end behaviour safely bounded by the timeout
     above. A masked, never-taken interrupt is used deliberately: an
     interrupt that actually fires never decrements icount either (see
     run_sim_un), so without a board to eventually lower the line this
     would hang the test the same way. */
  sregs[0].psr |= 0x0f00; /* PSR_PIL = 15: mask everything below 15 */
  ext_irl[0] = 1;

  int stat = exec_cmd ("cont 5");

  CHECK (stat == CTRL_C);
  CHECK (ctrl_c == 2);

  sregs[0].pwd_mode = 0;
  ebase.tlimit = 0;
  ext_irl[0] = 0;
}

TEST_CASE_FIXTURE (
    func_fixture,
    "with the trace buffer enabled but no breakpoint and no disassembly, "
    "'cont' still runs through the debug bookkeeping path and wraps the "
    "trace index")
{
  exec_cmd ("history 2");
  fill_nops (0, 4);

  exec_cmd ("cont 3");

  CHECK (sregs[0].pc == 12);

  exec_cmd ("history 0");
}

TEST_CASE_FIXTURE (
    func_fixture,
    "with a breakpoint further ahead, 'cont' dispatches the instructions "
    "before it normally and only stops once it is reached")
{
  fill_nops (0, 3);
  exec_cmd ("+bp 8");

  exec_cmd ("cont 100");

  CHECK (sregs[0].pc == 8);
  CHECK (sregs[0].bphit != 0);
}

TEST_CASE_FIXTURE (func_fixture,
		   "'tra'/'trace' prints and steps, doc/commands.md")
{
  fill_nops (0, 4);
  sregs[0].pc = 0;
  sregs[0].npc = 4;

  stdout_capture capture;
  exec_cmd ("tra 2");

  CHECK (sregs[0].pc == 8);
  CHECK (!capture.str ().empty ());
}

TEST_CASE_FIXTURE (func_fixture,
		   "the full word 'trace' does not match, only 'tra' does "
		   "(current behaviour, contradicts doc/commands.md which "
		   "documents the command as \"trace [count]\")")
{
  stdout_capture capture;
  exec_cmd ("trace 1");

  CHECK (capture.str ().find ("syntax error") != std::string::npos);
}

TEST_CASE_FIXTURE (func_fixture,
		   "'sym' always reports a syntax error (current "
		   "behaviour, contradicts doc/commands.md which "
		   "documents a \"sym\" command; the implementation is "
		   "commented out)")
{
  stdout_capture capture;
  exec_cmd ("sym");

  CHECK (capture.str ().find ("syntax error") != std::string::npos);
}

TEST_CASE_FIXTURE (func_fixture,
		   "'tra' with no count stops at the next breakpoint")
{
  /* The breakpoint is at 4, not 0: "+bp 0" is silently ignored, see the
     "'+bp 0' names no address and adds nothing" case above. */
  fill_nops (0, 2);
  exec_cmd ("+bp 4");

  exec_cmd ("tra");

  CHECK (sregs[0].pc == 4);
  CHECK (sregs[0].bphit != 0);
}

TEST_CASE_FIXTURE (
    func_fixture,
    "'tlimit' sets a time limit in the given unit (undocumented "
    "command; 'tcont'/'tgo'/'trun' below use the same limcalc helper)")
{
  sis_verbose = 1;

  stdout_capture capture;
  exec_cmd ("tlimit 10 ms");

  CHECK (ebase.tlimit == (uint64) (10 * 1000 * 14) + ebase.simtime);
  CHECK (capture.str ().find ("simulation limit") != std::string::npos);
}

TEST_CASE_FIXTURE (
    func_fixture,
    "'tlimit' with no argument at all sets the limit to limcalc's "
    "'unset' sentinel, UINT64_MAX (undocumented command)")
{
  ebase.tlimit = 123;

  exec_cmd ("tlimit");

  CHECK (ebase.tlimit == UINT64_MAX);
}

TEST_CASE_FIXTURE (func_fixture, "'tlimit' with an expression that does "
				 "not move time forward reports an error "
				 "(undocumented command)")
{
  ebase.simtime = 100;

  stdout_capture capture;
  exec_cmd ("tlimit 0 us");

  CHECK (capture.str ().find ("error in expression") != std::string::npos);
  CHECK (ebase.tlimit == 0);
}

TEST_CASE_FIXTURE (func_fixture,
		   "'tlimit' with no unit defaults to microseconds "
		   "(undocumented command)")
{
  exec_cmd ("tlimit 10");

  CHECK (ebase.tlimit == (uint64) (10 * 1 * 14) + ebase.simtime);
}

TEST_CASE_FIXTURE (func_fixture,
		   "'tlimit' accepts seconds as a unit (undocumented "
		   "command)")
{
  exec_cmd ("tlimit 1 s");

  CHECK (ebase.tlimit == (uint64) (1 * 1000000 * 14) + ebase.simtime);
}

TEST_CASE_FIXTURE (
    func_fixture,
    "'tlimit' prints nothing once the limit happens to equal (uint32)-1, "
    "the sentinel limcalc uses for 'no limit given' (current behaviour)")
{
  ebase.freq = 1;
  sis_verbose = 1;

  stdout_capture capture;
  exec_cmd ("tlimit 4294967295 us");

  CHECK (ebase.tlimit == (uint32) -1);
  CHECK (capture.str ().empty ());
}

TEST_CASE_FIXTURE (func_fixture,
		   "'tcont' runs bounded by a breakpoint, not by the time "
		   "limit alone (undocumented command)")
{
  /* The breakpoint is at 4, not 0: "+bp 0" is silently ignored, see the
     "'+bp 0' names no address and adds nothing" case above. */
  fill_nops (0, 2);
  exec_cmd ("+bp 4");

  exec_cmd ("tcont 10 ms");

  CHECK (sregs[0].pc == 4);
  CHECK (sregs[0].bphit != 0);
}

TEST_CASE_FIXTURE (func_fixture,
		   "'tgo address' sets pc, a time limit, and resumes "
		   "(undocumented command)")
{
  fill_nops (0x900, 1);
  exec_cmd ("+bp 0x900");

  stdout_capture capture;
  exec_cmd ("tgo 0x900 10 ms");

  CHECK (capture.str ().find ("resuming at 0x00000900") != std::string::npos);
  CHECK (sregs[0].pc == 0x900);
}

TEST_CASE_FIXTURE (func_fixture,
		   "'tgo' with no address uses the last loaded address "
		   "and sets no time limit (undocumented command)")
{
  fill_nops (0xa00, 1);
  last_load_addr = 0xa00;
  exec_cmd ("+bp 0xa00");
  ebase.tlimit = 0;

  exec_cmd ("tgo");

  CHECK (sregs[0].pc == 0xa00);
  CHECK (ebase.tlimit == 0);
}

TEST_CASE_FIXTURE (func_fixture,
		   "'trun' resets, sets a time limit and runs from pc 0 "
		   "when no file is loaded (undocumented command)")
{
  /* The breakpoint is at 4, not 0: "+bp 0" is silently ignored, see the
     "'+bp 0' names no address and adds nothing" case above. */
  fill_nops (0, 2);
  exec_cmd ("+bp 4");

  exec_cmd ("trun 10 ms");

  CHECK (sregs[0].pc == 4);
  CHECK (sregs[0].bphit != 0);
}

TEST_CASE_FIXTURE (
    func_fixture,
    "'trun' sets pc from the last loaded address, and calls boot_init "
    "since that pc is not zero (undocumented command)")
{
  fill_nops (0xb00, 2);
  last_load_addr = 0xb00;
  exec_cmd ("+bp 0xb04");

  exec_cmd ("trun 10 ms");

  CHECK (sregs[0].pc == 0xb04);
  CHECK (sregs[0].bphit != 0);
}

/* ---- run_sim: the write watchpoint hit path ---- */

TEST_CASE_FIXTURE (func_fixture,
		   "a store into a watched word stops the run and is not "
		   "counted, matching WPT_HIT in run_sim_un")
{
  sis_tests::flatmem_poke (0x1000, store_g1 (0x100));
  sregs[0].pc = 0x1000;
  sregs[0].npc = 0x1004;
  exec_cmd ("+wpw 0x100");

  stdout_capture capture;
  int stat = exec_cmd ("step");
  (void) capture;

  CHECK (stat == WPT_HIT);
  CHECK (ebase.wphit == WPT_HIT);
  /* The instruction that hit the watchpoint stays the current one. */
  CHECK (sregs[0].pc == 0x1000);
}

TEST_CASE_FIXTURE (
    func_fixture,
    "a watchpoint hit backs the trace buffer index off the entry logged "
    "for the trapping store")
{
  exec_cmd ("history 4");
  sis_tests::flatmem_poke (0x1000, store_g1 (0x100));
  sregs[0].pc = 0x1000;
  sregs[0].npc = 0x1004;
  exec_cmd ("+wpw 0x100");

  exec_cmd ("step");

  CHECK (sregs[0].histind == 0);

  exec_cmd ("history 0");
}

TEST_CASE_FIXTURE (
    func_fixture,
    "backing the trace index off a one-entry buffer wraps to the last "
    "slot instead of going negative")
{
  /* With a single slot, logging the trapping store's entry wraps histind
     back to 0 immediately; the watchpoint handler's own decrement then
     underflows that 0, which has to wrap to histlen - 1 rather than
     stay an out of range value. */
  exec_cmd ("history 1");
  sis_tests::flatmem_poke (0x1000, store_g1 (0x100));
  sregs[0].pc = 0x1000;
  sregs[0].npc = 0x1004;
  exec_cmd ("+wpw 0x100");

  exec_cmd ("step");

  CHECK (sregs[0].histind == 0);

  exec_cmd ("history 0");
}

/* ---- exec_cmd: multi-cpu run_sim_mp path ---- */

TEST_CASE_FIXTURE (
    func_fixture,
    "with more than one cpu online, 'cont' steps every core that is not "
    "in power-down mode")
{
  /* A secondary core resets into power-down (waiting to be released, the
     way real SMP hardware starts), so it has to be taken out of it here
     to show run_sim_mp actually stepping it; "run" would only reset it
     back to power-down, so this drives the run loop with "cont" instead
     of going through "run"'s reset_all(). */
  ncpu = 2;
  fill_nops (0, 4);
  REQUIRE (sregs[1].pwd_mode == 1);
  sregs[1].pwd_mode = 0;

  exec_cmd ("cont 2");

  CHECK (sregs[0].pc == 8);
  CHECK (sregs[1].pc == 8);
}

TEST_CASE_FIXTURE (
    func_fixture,
    "with more than one cpu online, an awake core checks for a pending "
    "interrupt before each fetch in run_sim_core too, masked the same "
    "way the single-cpu case above masks it")
{
  ncpu = 2;
  fill_nops (0, 4);
  sregs[1].pwd_mode = 0;
  sregs[0].psr |= 0x0f00; /* PSR_PIL = 15 */
  ext_irl[0] = 1;

  exec_cmd ("cont 2");

  CHECK (sregs[0].pc == 8);

  ext_irl[0] = 0;
}

TEST_CASE_FIXTURE (
    func_fixture,
    "with more than one cpu online, an idle secondary core still checks "
    "for a pending interrupt on every slice, masked the same way")
{
  ncpu = 2;
  fill_nops (0, 4);
  /* Core 1 is left in its default power-down: this exercises
     run_sim_core's own pwd_mode branch, distinct from run_sim_un's. */
  sregs[1].psr |= 0x0f00; /* PSR_PIL = 15 */
  ext_irl[1] = 1;

  exec_cmd ("cont 2");

  CHECK (sregs[0].pc == 8);
  CHECK (sregs[1].pc == 0); /* still idle */

  ext_irl[1] = 0;
}

TEST_CASE_FIXTURE (
    func_fixture,
    "with more than one cpu online, a core already in error mode stops "
    "the whole run immediately")
{
  ncpu = 2;
  fill_nops (0, 4);
  sregs[1].err_mode = 1;

  int stat = exec_cmd ("cont 4");

  CHECK (stat == ERROR_MODE);
}

TEST_CASE_FIXTURE (
    func_fixture,
    "with more than one cpu online, a breakpoint on one core stops the "
    "whole time slice")
{
  /* The breakpoint is at 4, not 0: "+bp 0" is silently ignored, see the
     "'+bp 0' names no address and adds nothing" case above. */
  ncpu = 2;
  fill_nops (0, 4);
  exec_cmd ("+bp 4");

  int stat = exec_cmd ("cont");

  CHECK (stat == BPT_HIT);
  CHECK (sregs[0].pc == 4);
  CHECK (sregs[0].bphit != 0);
}

TEST_CASE_FIXTURE (
    func_fixture,
    "with more than one cpu online and the trace buffer enabled but no "
    "breakpoint, run_sim_core still logs history and wraps the index")
{
  ncpu = 2;
  fill_nops (0, 4);
  exec_cmd ("history 2");

  exec_cmd ("cont 3");

  CHECK (sregs[0].pc == 12);

  exec_cmd ("history 0");
}

TEST_CASE_FIXTURE (
    func_fixture,
    "with more than one cpu online, 'tra' prints each instruction as "
    "run_sim_core dispatches it")
{
  ncpu = 2;
  fill_nops (0, 4);

  stdout_capture capture;
  exec_cmd ("tra 2");

  CHECK (!capture.str ().empty ());
}

TEST_CASE_FIXTURE (
    func_fixture,
    "with more than one cpu online, a store into a watched word stops "
    "the whole time slice through run_sim_core's own watchpoint check")
{
  ncpu = 2;
  sis_tests::flatmem_poke (0x1000, store_g1 (0x100));
  sregs[0].pc = 0x1000;
  sregs[0].npc = 0x1004;
  exec_cmd ("+wpw 0x100");

  int stat = exec_cmd ("cont 4");

  CHECK (stat == WPT_HIT);
  CHECK (sregs[0].pc == 0x1000);
}

TEST_CASE_FIXTURE (
    func_fixture,
    "with more than one cpu online, a watchpoint hit on a one-entry "
    "trace buffer wraps the index the same way the single-cpu run loop "
    "does")
{
  ncpu = 2;
  exec_cmd ("history 1");
  sis_tests::flatmem_poke (0x1000, store_g1 (0x100));
  sregs[0].pc = 0x1000;
  sregs[0].npc = 0x1004;
  exec_cmd ("+wpw 0x100");

  exec_cmd ("cont 4");

  CHECK (sregs[0].histind == 0);

  exec_cmd ("history 0");
}

TEST_CASE_FIXTURE (
    func_fixture,
    "with more than one cpu online, a watchpoint hit on a trace buffer "
    "with room to spare just backs the index up by one, no wrap needed")
{
  ncpu = 2;
  exec_cmd ("history 4");
  sis_tests::flatmem_poke (0x1000, store_g1 (0x100));
  sregs[0].pc = 0x1000;
  sregs[0].npc = 0x1004;
  exec_cmd ("+wpw 0x100");

  exec_cmd ("cont 4");

  CHECK (sregs[0].histind == 0);

  exec_cmd ("history 0");
}

TEST_CASE_FIXTURE (
    func_fixture,
    "with more than one cpu online, an access exception with traps "
    "disabled drops that core into power-down and error mode without "
    "stopping to run a trap handler that is not there")
{
  ncpu = 2;
  sregs[0].psr &= ~0x20; /* clear PSR_ET: traps disabled */
  sregs[0].pc = sis_tests::FLATMEM_SIZE;
  sregs[0].npc = sis_tests::FLATMEM_SIZE + 4;

  int stat = exec_cmd ("cont 4");

  CHECK (stat == ERROR_MODE);
  CHECK (sregs[0].err_mode != 0);
}

TEST_CASE_FIXTURE (
    func_fixture,
    "with more than one cpu online, a jmpl through a null pointer is "
    "reported as NULL_HIT rather than folded into ERROR_MODE (sparc.cc's "
    "\"halt on null pointer\" debug aid)")
{
  ncpu = 2;
  sis_tests::flatmem_poke (0, JMPL_NULL);
  sregs[0].pc = 0;
  sregs[0].npc = 4;

  int stat = exec_cmd ("cont 4");

  CHECK (stat == NULL_HIT);
}

TEST_CASE_FIXTURE (
    func_fixture,
    "with more than one cpu online, a queued event due before the time "
    "slice's own delta or count limit cuts the slice short")
{
  ncpu = 2;
  fill_nops (0, 4);
  delta = 1000; /* wide enough that the event below is the binding limit */
  event (noop_event, 0, 8);

  exec_cmd ("cont 1000");

  /* Only the queued event's time (8) is short enough to be the binding
     limit on the first pass through run_sim_mp's per-slice loop, next to
     the 1000 requested and the wide delta; the run then continues past
     it for further slices, so this only has to show the core made it
     past that first slice rather than getting stuck at cycle 0. */
  CHECK (sregs[0].pc >= 8);
}

TEST_CASE_FIXTURE (
    func_fixture,
    "with more than one cpu online, a timed-out run with nothing else to "
    "report returns CTRL_C")
{
  ncpu = 2;
  fill_nops (0, 4);
  ebase.tlimit = 4;

  int stat = exec_cmd ("cont 1000");

  CHECK (stat == CTRL_C);
  CHECK (ctrl_c == 2);

  ebase.tlimit = 0;
}

TEST_CASE_FIXTURE (
    func_fixture,
    "with more than one cpu online, 'step' (icount 1) still runs only "
    "the active cpu through run_sim_un, the same single-core path as "
    "when ncpu is 1 (current behaviour: run_sim dispatches on icount == "
    "1 before it looks at ncpu at all, so a single step never touches "
    "the other online cores)")
{
  ncpu = 2;
  fill_nops (0, 2);

  exec_cmd ("step");

  CHECK (sregs[0].pc == 4);
  CHECK (sregs[1].pc == 0); /* untouched: run_sim_un only ever sees cpu 0 */
}

TEST_CASE_FIXTURE (
    func_fixture,
    "a run that stops for a reason other than the tlimit timeout prints "
    "no time-out message, even when it still reports CTRL_C (current "
    "behaviour: the message is gated on ctrl_c == 2 specifically, the "
    "value only sim_timeout uses; nothing in the shipped code sets "
    "ctrl_c to 1 while a run is in progress other than a real SIGINT, "
    "so this drives it directly through the event queue instead)")
{
  sregs[0].pwd_mode = 1; /* never decrements icount on its own */
  event (set_ctrl_c, 0, 20);

  stdout_capture capture;
  int stat = exec_cmd ("cont 5");

  CHECK (stat == CTRL_C);
  CHECK (ctrl_c == 1);
  CHECK (capture.str ().find ("Time-out") == std::string::npos);

  sregs[0].pwd_mode = 0;
}

/* ---- global helpers: now(), pwd_enter(), sys_reset(), sys_halt() ---- */

TEST_CASE_FIXTURE (func_fixture, "now() reports the current simulated time")
{
  ebase.simtime = 555;

  CHECK (now () == 555);
}

TEST_CASE_FIXTURE (
    func_fixture,
    "event() reports an error and drops the request once the MAX_EVENT "
    "pool is exhausted, rather than corrupting the queue")
{
  /* init_event() already queued one entry, the end-of-time sentinel, so
     MAX_EVENT - 1 more fill every remaining slot. */
  for (int i = 0; i < MAX_EVENT - 1; i++)
    event (noop_event, i, 1000 + (uint64) i);

  stdout_capture capture;
  event (noop_event, -1, 1);

  CHECK (capture.str ().find ("too many events") != std::string::npos);
}

TEST_CASE_FIXTURE (func_fixture, "pwd_enter puts a core into power-down mode")
{
  sregs[0].simtime = 1000;
  sregs[0].hold = 3;
  int saved_delta = delta;
  delta = 7;

  pwd_enter (&sregs[0]);

  CHECK (sregs[0].pwd_mode == 1);
  CHECK (sregs[0].pwdstart == 1000);
  CHECK (sregs[0].hold == 3 + 7);

  delta = saved_delta;
}

TEST_CASE_FIXTURE (func_fixture, "sys_reset resets the registers and forces "
				 "the fake reset trap (it does not reset "
				 "the statistics; only reset_stat/'perf "
				 "reset' does that)")
{
  sregs[0].pc = 0x1234;
  sregs[0].ninst = 9;

  sys_reset ();

  CHECK (sregs[0].pc == 0);
  CHECK (sregs[0].ninst == 9);
  CHECK (sregs[0].trap == 256);
}

TEST_CASE_FIXTURE (func_fixture, "sys_halt forces the fake halt trap")
{
  sys_halt ();

  CHECK (sregs[0].trap == 257);
}

TEST_CASE_FIXTURE (
    func_fixture,
    "int_handler flags a SIGINT for the run loop to pick up, while a run "
    "is in progress (its other branch, taken when nothing is running, "
    "closes the gdb socket or exits the process outright, so it is not "
    "safe to drive from a unit test)")
{
  int saved_sim_run = sim_run;
  int saved_ctrl_c = ctrl_c;
  sim_run = 1;
  ctrl_c = 0;

  int_handler (SIGINT);

  CHECK (ctrl_c == 1);

  ctrl_c = saved_ctrl_c;
  sim_run = saved_sim_run;
}

TEST_CASE_FIXTURE (func_fixture,
		   "int_handler reports an unexpected signal instead of "
		   "crashing on it")
{
  stdout_capture capture;
  int_handler (SIGTERM);

  CHECK (capture.str ().find ("Signal handler error") != std::string::npos);
}

/* ---- get_time()/rt_sync() ---- */

TEST_CASE_FIXTURE (func_fixture, "get_time returns a plausible wall clock "
				 "reading")
{
  double before = get_time ();
  double after = get_time ();

  CHECK (before > 0.0);
  CHECK (after >= before);
}

TEST_CASE_FIXTURE (func_fixture,
		   "rt_sync does not sleep once real time has caught up")
{
  ebase.simstart = 0;
  ebase.simtime = 0; /* zero simulated time: realtime is zero */
  ebase.freq = 1;
  ebase.tottime = 0.0;
  ebase.starttime = get_time ();

  double before = get_time ();
  rt_sync ();
  double after = get_time ();

  /* No sleep: the call returns essentially immediately. */
  CHECK (after - before < 0.05);
}

TEST_CASE_FIXTURE (func_fixture,
		   "rt_sync sleeps to let real time catch up with a small "
		   "lag")
{
  ebase.simstart = 0;
  ebase.freq = 1;
  ebase.tottime = 0.0;
  ebase.starttime = get_time ();
  /* realtime = simtime / 1e6 / freq = 0.005 s, well past the 1ms
     threshold and short enough to keep the test fast. */
  ebase.simtime = 5000;

  double before = get_time ();
  rt_sync ();
  double after = get_time ();

  CHECK (after - before > 0.001);
}

TEST_CASE_FIXTURE (func_fixture,
		   "rt_sync clamps an oversized lag to a tenth of a second")
{
  ebase.simstart = 0;
  ebase.freq = 1;
  ebase.tottime = 0.0;
  ebase.starttime = get_time ();
  /* realtime = 2e6 / 1e6 / 1 = 2 s: past the 1 s clamp threshold. */
  ebase.simtime = 2000000;

  double before = get_time ();
  rt_sync ();
  double after = get_time ();

  double elapsed = after - before;
  CHECK (elapsed > 0.05);
  CHECK (elapsed < 0.5);
}

/* ---- coverage bitmap: cov_start/cov_exec/cov_bt/cov_bnt/cov_jmp/cov_save
   ---- */

TEST_CASE_FIXTURE (func_fixture, "cov_start marks a start and exec point")
{
  covram[0x9000 >> 2] = 0;

  cov_start (0x9000);

  CHECK ((covram[0x9000 >> 2] & 0x03) == 0x03);
  covram[0x9000 >> 2] = 0;
}

TEST_CASE_FIXTURE (func_fixture, "cov_exec marks an exec point")
{
  covram[0x9004 >> 2] = 0;

  cov_exec (0x9004);

  CHECK ((covram[0x9004 >> 2] & 0x01) != 0);
  covram[0x9004 >> 2] = 0;
}

TEST_CASE_FIXTURE (func_fixture, "cov_bt marks a taken branch and its target")
{
  covram[0x9008 >> 2] = 0;
  covram[0x900c >> 2] = 0;

  cov_bt (0x9008, 0x900c);

  CHECK ((covram[0x9008 >> 2] & 0x09) == 0x09);
  CHECK ((covram[0x900c >> 2] & 0x03) == 0x03);
  covram[0x9008 >> 2] = 0;
  covram[0x900c >> 2] = 0;
}

TEST_CASE_FIXTURE (func_fixture, "cov_bnt marks a not-taken branch")
{
  covram[0x9010 >> 2] = 0;

  cov_bnt (0x9010);

  CHECK ((covram[0x9010 >> 2] & 0x11) == 0x11);
  covram[0x9010 >> 2] = 0;
}

TEST_CASE_FIXTURE (func_fixture, "cov_jmp marks a jump and its target")
{
  covram[0x9014 >> 2] = 0;
  covram[0x9018 >> 2] = 0;

  cov_jmp (0x9014, 0x9018);

  CHECK ((covram[0x9014 >> 2] & 0x05) == 0x05);
  CHECK ((covram[0x9018 >> 2] & 0x03) == 0x03);
  covram[0x9014 >> 2] = 0;
  covram[0x9018 >> 2] = 0;
}

TEST_CASE_FIXTURE (func_fixture, "cov_save writes a .cov file for a "
				 "region it saw execution in")
{
  covram[0x9020 >> 2] = 0;
  cov_exec (0x9020);
  ebase.ramstart = 0;
  sregs[0].pc = 0;

  std::string base = "/tmp/sis-funcq-cov-test";
  std::string path = base + ".cov";
  remove (path.c_str ());

  stdout_capture capture;
  cov_save (const_cast<char *> (base.c_str ()));

  CHECK (capture.str ().find ("saved code coverage to") != std::string::npos);

  FILE *fp = fopen (path.c_str (), "r");
  REQUIRE (fp != NULL);
  fclose (fp);
  remove (path.c_str ());

  covram[0x9020 >> 2] = 0;
}

TEST_CASE_FIXTURE (
    func_fixture,
    "cov_save propagates execution across a straight run between a "
    "block start and the next jump/branch, resets state at that jump, "
    "and stops propagating at the current pc")
{
  /* Four consecutive words, word-index aligned to the 32-word chunk
     cov_save itself iterates in, so all four fall in one pass of its
     outer loop and i is known ahead of time (0xa000 >> 2 == 0x2800,
     already a multiple of 32). */
  const uint32 w0 = 0xa000, w1 = 0xa004, w2 = 0xa008, w3 = 0xa00c;
  covram[w0 >> 2] = 0;
  covram[w1 >> 2] = 0;
  covram[w2 >> 2] = 0;
  covram[w3 >> 2] = 0;

  cov_start (w0); /* block start: covram[w0] gets START|EXEC */
  /* w1 is left with no bits of its own: only the propagated state (from
     w0's START) should mark it executed. */
  cov_bnt (w2); /* a not-taken branch: covram[w2] gets BNT|EXEC,
		   and ends the propagation from w0. */
  /* w3 is left with no bits either, and is where sregs[0].pc points: it
     exercises the pc-match check instead of the state propagation. */
  ebase.ramstart = 0;
  sregs[0].pc = w3;

  std::string base = "/tmp/sis-funcq-cov-test2";
  std::string path = base + ".cov";
  remove (path.c_str ());

  cov_save (const_cast<char *> (base.c_str ()));
  remove (path.c_str ());

  CHECK ((covram[w1 >> 2] & 0x01) != 0); /* propagated EXEC */
  CHECK ((covram[w3 >> 2] & 0x01) == 0); /* not propagated: pc stopped it */

  covram[w0 >> 2] = 0;
  covram[w1 >> 2] = 0;
  covram[w2 >> 2] = 0;
  covram[w3 >> 2] = 0;
}

TEST_CASE_FIXTURE (func_fixture, "run_sim starts coverage collection when "
				 "coverage is enabled")
{
  fill_nops (0, 1);
  sregs[0].pc = 0;
  covram[0 >> 2] = 0;
  ebase.coven = 1;

  exec_cmd ("step");

  CHECK ((covram[0 >> 2] & 0x02) != 0); /* COV_START */

  covram[0 >> 2] = 0;
  ebase.coven = 0;
}
