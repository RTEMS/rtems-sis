/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Tests for sis.cc, the simulator entry point: command line parsing, board
   and architecture selection, the start-up banner and the shell loop.

   sis_main owns the process.  It calls exit () for -help and for a bad
   option, and it drops into a prompt that reads host stdin.  Neither is
   safe in the shared test binary, so every case runs it in a fresh
   process: the parent execs this same test binary with a filter that
   selects the child runner case below, and the runner calls sis_main with
   the argument vector handed to it through the environment.  The child
   therefore starts from pristine globals no matter what the rest of the
   suite has done, the parent never inherits what sis_main sets up, and an
   exit () or a hang costs a subprocess rather than the suite.

   The expectations come from doc/invoking-sis.md (the option list),
   doc/emulated-systems.md, doc/erc32.md, doc/leon2.md, doc/leon3.md,
   doc/gr740.md and doc/riscv.md (the per board memory maps and
   peripherals) and help.cc's sis_usage (the documented default board).  */

#include "doctest.h"

#include "config.h"
#include "sis.h"

#ifdef __linux__

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <filesystem>
#include <string>
#include <vector>

namespace
{

/* The argument vector for the child, one argument per line.  */
const char ARGV_ENV[] = "SIS_TEST_MAIN_ARGV";

/* SPARC and RISC-V instruction words used by the synthetic executables.  */
const uint32 SPARC_NOP = 0x01000000;	  /* nop			*/
const uint32 SPARC_UNIMP = 0x00000000;	  /* unimp 0		*/
const uint32 SPARC_JMP_NULL = 0x81c02000; /* jmpl %g0 + 0, %g0	*/
const uint32 SPARC_LD_256 = 0xc2002100;	  /* ld [%g0 + 0x100], %g1 */
const uint32 SPARC_BA_SELF = 0x10800000;  /* ba .			*/
const uint32 RISCV_NOP = 0x00000013;	  /* addi zero, zero, 0	*/

/* e_machine values of the two architectures elf.cc recognises.  */
const int EM_SPARC_ = 2;
const int EM_RISCV_ = 243;

/* A file in the host temporary directory, removed when the object dies.  */
class temp_file
{
public:
  temp_file (const char *suffix, const std::string &content)
  {
    static int counter = 0;
    std::filesystem::path p = std::filesystem::temp_directory_path ();

    p /= "sis-test-" + std::to_string ((long) getpid ()) + "-" +
	 std::to_string (counter++) + suffix;
    path_ = p.string ();

    FILE *f = fopen (path_.c_str (), "wb");
    REQUIRE (f != NULL);
    if (!content.empty ())
      REQUIRE (fwrite (content.data (), content.size (), 1, f) == 1);
    fclose (f);
  }

  ~temp_file ()
  {
    std::error_code ec;
    std::filesystem::remove (path_, ec);
  }

  temp_file (const temp_file &) = delete;
  temp_file &operator= (const temp_file &) = delete;

  const std::string &
  path () const
  {
    return path_;
  }

private:
  std::string path_;
};

void
put32 (std::string &s, uint32 v, bool big)
{
  for (int i = 0; i < 4; i++)
    {
      int shift = big ? 24 - 8 * i : 8 * i;
      s.push_back ((char) ((v >> shift) & 0xff));
    }
}

void
put16 (std::string &s, uint32 v, bool big)
{
  for (int i = 0; i < 2; i++)
    {
      int shift = big ? 8 - 8 * i : 8 * i;
      s.push_back ((char) ((v >> shift) & 0xff));
    }
}

/* A minimal 32-bit ELF executable holding one allocated .text section at
   ENTRY.  elf.cc reads the machine and the entry point to pick the board
   and walks the section headers to load the image, so the file carries a
   section header table with .text and .shstrtab and one PT_LOAD program
   header.  */
std::string
make_elf (int machine, uint32 entry, const std::vector<uint32> &words)
{
  const bool big = machine == EM_SPARC_;
  const uint32 ehsize = 52;
  const uint32 phentsize = 32;
  const uint32 shentsize = 40;
  const std::string strtab (".text\0.shstrtab", 16);
  std::string text;
  std::string s;

  for (uint32 w : words)
    put32 (text, w, big);

  const uint32 phoff = ehsize;
  const uint32 textoff = phoff + phentsize;
  const uint32 stroff = textoff + (uint32) text.size ();
  const uint32 shoff = stroff + 1 + (uint32) strtab.size ();

  s.push_back ((char) 0x7f);
  s += "ELF";
  s.push_back (1);	     /* ELFCLASS32	*/
  s.push_back (big ? 2 : 1); /* data encoding */
  s.push_back (1);	     /* EV_CURRENT	*/
  s.append (9, '\0');
  put16 (s, 2, big); /* ET_EXEC	*/
  put16 (s, machine, big);
  put32 (s, 1, big); /* EV_CURRENT	*/
  put32 (s, entry, big);
  put32 (s, phoff, big);
  put32 (s, shoff, big);
  put32 (s, 0, big); /* e_flags	*/
  put16 (s, ehsize, big);
  put16 (s, phentsize, big);
  put16 (s, 1, big); /* e_phnum	*/
  put16 (s, shentsize, big);
  put16 (s, 3, big); /* e_shnum	*/
  put16 (s, 2, big); /* e_shstrndx	*/

  /* PT_LOAD covering .text.  */
  put32 (s, 1, big);
  put32 (s, textoff, big);
  put32 (s, entry, big);
  put32 (s, entry, big);
  put32 (s, (uint32) text.size (), big);
  put32 (s, (uint32) text.size (), big);
  put32 (s, 5, big);
  put32 (s, 4, big);

  s += text;
  s.push_back ('\0');
  s += strtab;

  /* Section 0, the unused SHN_UNDEF entry.  */
  for (int i = 0; i < 10; i++)
    put32 (s, 0, big);

  /* Section 1, .text: SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR.  */
  put32 (s, 1, big);
  put32 (s, 1, big);
  put32 (s, 6, big);
  put32 (s, entry, big);
  put32 (s, textoff, big);
  put32 (s, (uint32) text.size (), big);
  put32 (s, 0, big);
  put32 (s, 0, big);
  put32 (s, 4, big);
  put32 (s, 0, big);

  /* Section 2, .shstrtab: SHT_STRTAB.  */
  put32 (s, 7, big);
  put32 (s, 3, big);
  put32 (s, 0, big);
  put32 (s, 0, big);
  put32 (s, stroff, big);
  put32 (s, 1 + (uint32) strtab.size (), big);
  put32 (s, 0, big);
  put32 (s, 0, big);
  put32 (s, 1, big);
  put32 (s, 0, big);

  return s;
}

/* What one sis_main run produced.  */
struct sis_result
{
  int status;	      /* exit status of sis_main		*/
  bool timed_out;     /* the run had to be killed		*/
  std::string output; /* everything it wrote to stdout/stderr	*/

  bool
  has (const char *text) const
  {
    return output.find (text) != std::string::npos;
  }
};

/* Run sis_main with ARGS in a fresh process, feeding it INPUT on stdin.
   ARGS[0] is argv[0], which sis_main also uses to pick the architecture
   from the binary name.  */
sis_result
run_sis (const std::vector<std::string> &args, const std::string &input = "")
{
  std::string spec;
  sis_result result;

  for (size_t i = 0; i < args.size (); i++)
    {
      if (i > 0)
	spec += '\n';
      spec += args[i];
    }

  temp_file out ("-out.txt", "");
  temp_file in ("-in.txt", input);

  int outfd = open (out.path ().c_str (), O_RDWR);
  int infd = open (in.path ().c_str (), O_RDONLY);
  REQUIRE (outfd >= 0);
  REQUIRE (infd >= 0);

  fflush (NULL);

  pid_t pid = fork ();
  REQUIRE (pid >= 0);

  if (pid == 0)
    {
      char arg0[] = "sis-test";
      char arg1[] = "-tc=sis_main_child_runner";
      char arg2[] = "-ni";
      char *const child_argv[] = { arg0, arg1, arg2, NULL };

      setenv (ARGV_ENV, spec.c_str (), 1);
      dup2 (infd, 0);
      dup2 (outfd, 1);
      dup2 (outfd, 2);
      execv ("/proc/self/exe", child_argv);
      _exit (127);
    }

  close (infd);

  /* Wait with a deadline.  A defect that blocks sis_main must cost this
     subprocess, never the suite.  */
  int status = 0;
  int waited_ms = 0;
  pid_t done;

  while ((done = waitpid (pid, &status, WNOHANG)) == 0 && waited_ms < 30000)
    {
      struct timespec ts = { 0, 5000000 };

      nanosleep (&ts, NULL);
      waited_ms += 5;
    }

  result.timed_out = done == 0;
  if (result.timed_out)
    {
      kill (pid, SIGKILL);
      waitpid (pid, &status, 0);
    }

  result.status = WIFEXITED (status) ? WEXITSTATUS (status) : -1;

  lseek (outfd, 0, SEEK_SET);
  char buffer[4096];
  ssize_t got;
  while ((got = read (outfd, buffer, sizeof (buffer))) > 0)
    result.output.append (buffer, (size_t) got);
  close (outfd);

  REQUIRE_MESSAGE (!result.timed_out, "sis_main did not terminate");
  return result;
}

/* A run that ends through the prompt.  "quit" is the only way out of the
   shell that returns from sis_main.  */
sis_result
run_shell (const std::vector<std::string> &args, const std::string &commands)
{
  sis_result r = run_sis (args, commands + "quit\n");

  CHECK (r.status == 0);
  /* doc/invoking-sis.md lists the recognised options; none of the cases
     below passes anything else.  */
  CHECK_FALSE (r.has ("unknown option"));
  return r;
}

} /* namespace */

/* The child side of run_sis.  When the environment carries an argument
   vector this process exists only to run sis_main and report its result
   through the exit status; otherwise it is an ordinary no-op case in the
   suite.  */
TEST_CASE ("sis_main_child_runner")
{
  const char *spec = getenv (ARGV_ENV);

  if (spec == NULL)
    return;

  std::vector<std::string> args;
  std::string current;

  for (const char *p = spec;; p++)
    {
      if (*p == '\n' || *p == 0)
	{
	  args.push_back (current);
	  current.clear ();
	  if (*p == 0)
	    break;
	}
      else
	current.push_back (*p);
    }

  std::vector<char *> argv;
  for (std::string &a : args)
    argv.push_back (&a[0]);
  argv.push_back (NULL);

  exit (sis_main ((int) args.size (), argv.data ()));
}

TEST_CASE ("sis_main prints the banner and reaches the prompt")
{
  sis_result r = run_shell ({ "sis" }, "");

  /* The banner names the simulator and the bug address, so a user can
     tell which program is running.  */
  CHECK (r.has ("SIS - SPARC/RISCV instruction simulator"));
  CHECK (r.has ("jiri@gaisler.se"));
  /* help.cc's sis_usage: "-erc32 is set by default".  */
  CHECK (r.has ("ERC32 emulation enabled"));
}

TEST_CASE ("a command at the prompt runs and the shell keeps going")
{
  /* doc/commands.md: echo prints its argument.  The shell must survive an
     ordinary command and still accept quit afterwards.  */
  sis_result r = run_shell ({ "sis" }, "echo shell-is-alive\n");

  CHECK (r.has ("shell-is-alive"));
}

TEST_CASE ("quit ends the session and leaves the rest of the input unread")
{
  /* doc/commands.md: "quit  exit the simulator".  Anything queued behind
     it is never prompted for.  */
  sis_result r = run_sis ({ "sis" }, "quit\necho after-quit\n");

  CHECK (r.status == 0);
  CHECK_FALSE (r.has ("after-quit"));
}

TEST_CASE ("an empty command line is accepted at the prompt")
{
  sis_result r = run_shell ({ "sis" }, "\n");

  CHECK (r.has ("ERC32 emulation enabled"));
}

TEST_CASE ("end of input at the prompt ends the session")
{
  /* No commands at all: the prompt sees end of file and sis exits.  */
  sis_result r = run_sis ({ "sis" }, "");

  CHECK (r.status == 0);
  CHECK (r.has ("ERC32 emulation enabled"));
}

/* Board selection through the documented -<board> switches.  Each case
   checks the banner and then a property of the selected system that only
   that board has, so that picking the right name but the wrong memory
   system is still caught.  */

TEST_CASE ("-erc32 selects the ERC32 system")
{
  /* doc/invoking-sis.md: "-erc32  Emulate the SPARC V7 ERC32 processor".
     doc/erc32.md: RAM is 0x02000000 - 0x03000000.  */
  temp_file elf ("-erc32.elf",
		 make_elf (EM_SPARC_, 0x02000000, { SPARC_NOP }));
  sis_result r =
      run_shell ({ "sis", "-erc32", elf.path () }, "dis 0x02000000 1\n");

  CHECK (r.has ("ERC32 emulation enabled"));
  CHECK (r.has ("02000000:  01000000   nop"));
}

TEST_CASE ("-leon2 selects the LEON2 system")
{
  /* doc/invoking-sis.md: "-leon2  Emulate the SPARC V8 LEON2 processor".
     doc/leon2.md: RAM is 0x40000000 - 0x41000000 and the APB bus ends at
     0x80000100, so LEON3's GPTIMER at 0x80000300 is not there.  */
  temp_file elf ("-leon2.elf",
		 make_elf (EM_SPARC_, 0x40000000, { SPARC_NOP }));
  sis_result r = run_shell ({ "sis", "-leon2", elf.path () },
			    "dis 0x40000000 1\nmem 0x80000300 16\n");

  CHECK (r.has ("LEON2 emulation enabled"));
  CHECK (r.has ("40000000:  01000000   nop"));
  CHECK_FALSE (r.has ("00000142"));
}

TEST_CASE ("-leon3 selects the LEON3 system")
{
  /* doc/invoking-sis.md: "-leon3  Emulate the SPARC V8 LEON3 processor".
     doc/leon3.md: RAM is 0x40000000 - 0x41000000 and GPTIMER sits at
     0x80000300 with interrupts 8 and 9.  The GPTIMER configuration
     register at offset 8 reports two timers on interrupt 8, so it reads
     (8 << 3) | 2 = 0x142.  */
  temp_file elf ("-leon3.elf",
		 make_elf (EM_SPARC_, 0x40000000, { SPARC_NOP }));
  sis_result r = run_shell ({ "sis", "-leon3", elf.path () },
			    "dis 0x40000000 1\nmem 0x80000300 16\n");

  CHECK (r.has ("LEON3 emulation enabled"));
  CHECK (r.has ("40000000:  01000000   nop"));
  CHECK (r.has ("00000142"));
}

TEST_CASE ("-gr740 selects the GR740 system")
{
  /* doc/invoking-sis.md: "-gr740  Emulate a (limited) GR740 SOC device".
     doc/gr740.md: RAM is 0x00000000 - 0x04000000 and GPTIMER sits at
     0xFF908000 with interrupts 1 and 2, so its configuration register
     reads (1 << 3) | 2 = 0x10a.  */
  temp_file elf ("-gr740.elf",
		 make_elf (EM_SPARC_, 0x00000000, { SPARC_NOP }));
  sis_result r = run_shell ({ "sis", "-gr740", elf.path () },
			    "dis 0 1\nmem 0xff908000 16\n");

  CHECK (r.has ("GR740/LEON4 emulation enabled"));
  CHECK (r.has ("0:  01000000   nop"));
  CHECK (r.has ("0000010a"));
}

TEST_CASE ("-griscv selects RISC-V on the LEON3 system")
{
  /* doc/invoking-sis.md: "-griscv  Emulate a GRISCV (RISCV/GRLIB) SOC
     device".  doc/riscv.md: "The GRISCV SOC uses the same peripherals and
     memory maps as a SPARC LEON3 processor", so RAM is at 0x40000000 and
     GPTIMER at 0x80000300, but the instructions are RISC-V.  */
  temp_file elf ("-griscv.elf",
		 make_elf (EM_RISCV_, 0x40000000, { RISCV_NOP }));
  sis_result r = run_shell ({ "sis", "-griscv", elf.path () },
			    "dis 0x40000000 1\nmem 0x80000300 16\n");

  CHECK (r.has ("RISCV/GRLIB emulation enabled"));
  CHECK (r.has ("00000142"));
  /* A RISC-V decoder, not the SPARC one: 0x00000013 is addi zero, zero, 0.  */
  CHECK (r.has ("40000000:  00000013   li"));
}

TEST_CASE ("-rv32 selects the CLINT based RISC-V system")
{
  /* doc/invoking-sis.md: "-rv32  Emulate a RISC-V RV32IMACFD processor
     with CLINT module".  doc/riscv.md: RAM is 0x80000000 - 0x84000000 and
     "The DTB (device-tree table) is located at the end of ROM
     (0x20FF0000)"; a flattened device tree starts with the magic
     0xd00dfeed.  */
  temp_file elf ("-rv32.elf", make_elf (EM_RISCV_, 0x80000000, { RISCV_NOP }));
  sis_result r = run_shell ({ "sis", "-rv32", elf.path () },
			    "dis 0x80000000 1\nmem 0x20ff0000 16\n");

  CHECK (r.has ("RISCV/CLINT emulation enabled"));
  CHECK (r.has ("80000000:  00000013   li"));
  CHECK (r.has ("edfe0dd0"));
}

TEST_CASE ("the board switches report the core count and the time slice")
{
  /* doc/invoking-sis.md: "-m cores  Enable the number of cores (2 - 4) in
     a leon3 or RISC-V multi-processor system" and "-d clocks  Set the the
     number of clocks in each time-slice ... Default is 50".  The banner of
     every multi-processor capable board reports both.  */
  for (const char *board : { "-leon3", "-gr740", "-griscv", "-rv32" })
    {
      CAPTURE (board);
      sis_result r = run_shell ({ "sis", board }, "");

      CHECK (r.has ("1 cpus online, delta 50 clocks"));
    }
}

TEST_CASE ("the binary name selects the architecture")
{
  /* CLAUDE.md and the top of sis_main: a binary called riscv* emulates
     RISC-V, a binary called sparc* emulates SPARC.  With no board switch
     and no file the RISC-V default is the GRLIB SOC and the SPARC default
     is ERC32 (help.cc: "-erc32 is set by default").  */
  CHECK (run_shell ({ "riscv-sis" }, "").has ("RISCV/GRLIB emulation"));
  CHECK (run_shell ({ "sparc-sis" }, "").has ("ERC32 emulation enabled"));

  /* The name only fixes the architecture.  The board still comes from the
     executable: a SPARC image entered at 0x40000000 is a LEON3 image
     (doc/leon3.md).  */
  temp_file elf ("-leon3.elf",
		 make_elf (EM_SPARC_, 0x40000000, { SPARC_NOP }));
  CHECK (run_shell ({ "sparc-sis", elf.path () }, "")
	     .has ("LEON3 emulation enabled"));

  /* The name wins over the architecture of the executable.  An image whose
     entry point names no board leaves the architecture from the name to
     pick the default system, so the same file gives ERC32 under a sparc
     name and the RISC-V GRLIB SOC under a riscv name.  */
  temp_file odd_riscv ("-odd-riscv.elf",
		       make_elf (EM_RISCV_, 0x30000000, { RISCV_NOP }));
  temp_file odd_sparc ("-odd-sparc.elf",
		       make_elf (EM_SPARC_, 0x30000000, { SPARC_NOP }));

  CHECK (run_shell ({ "sparc-sis", odd_riscv.path () }, "")
	     .has ("ERC32 emulation enabled"));
  CHECK (run_shell ({ "riscv-sis", odd_sparc.path () }, "")
	     .has ("RISCV/GRLIB emulation enabled"));
}

TEST_CASE ("the loaded executable selects the board")
{
  /* doc/invoking-sis.md: "The executable file to be loaded must be an
     SPARC or RISCV ELF file."  With no board switch the ELF machine and
     entry point pick the system, matching the documented RAM base of each
     board: ERC32 0x02000000 (doc/erc32.md), LEON3 0x40000000
     (doc/leon3.md), GR740 0x00000000 (doc/gr740.md) and the CLINT RISC-V
     0x80000000 (doc/riscv.md).  */
  struct
  {
    int machine;
    uint32 entry;
    uint32 word;
    const char *banner;
  } cases[] = {
    { EM_SPARC_, 0x02000000, SPARC_NOP, "ERC32 emulation enabled" },
    { EM_SPARC_, 0x40000000, SPARC_NOP, "LEON3 emulation enabled" },
    { EM_SPARC_, 0x00000000, SPARC_NOP, "GR740/LEON4 emulation enabled" },
    { EM_RISCV_, 0x40000000, RISCV_NOP, "RISCV/GRLIB emulation enabled" },
    { EM_RISCV_, 0x80000000, RISCV_NOP, "RISCV/CLINT emulation enabled" },
  };

  for (auto &c : cases)
    {
      CAPTURE (c.banner);
      temp_file elf ("-auto.elf", make_elf (c.machine, c.entry, { c.word }));
      sis_result r = run_shell ({ "sis", elf.path () }, "");

      CHECK (r.has (c.banner));
    }
}

TEST_CASE ("an executable with an unknown entry point falls back to LEON3")
{
  /* Only the four documented entry points name a board.  An image that
     matches none of them still fixes the architecture, and the GRLIB SOC
     is what is left: LEON3 for SPARC, RISCV/GRLIB for RISC-V.  */
  temp_file sparc_elf ("-odd-sparc.elf",
		       make_elf (EM_SPARC_, 0x30000000, { SPARC_NOP }));
  temp_file riscv_elf ("-odd-riscv.elf",
		       make_elf (EM_RISCV_, 0x30000000, { RISCV_NOP }));

  CHECK (run_shell ({ "sis", sparc_elf.path () }, "")
	     .has ("LEON3 emulation enabled"));
  CHECK (run_shell ({ "sis", riscv_elf.path () }, "")
	     .has ("RISCV/GRLIB emulation enabled"));
}

TEST_CASE ("-riscv selects the architecture only")
{
  /* doc/invoking-sis.md: "-riscv  Select the RISC-V architecture and leave
     the board to the loaded ELF file."  With no file there is nothing to
     read a board from, so the GRLIB default stands.  */
  CHECK (run_shell ({ "sis", "-riscv" }, "").has ("RISCV/GRLIB emulation"));

  /* help.cc's sis_usage lists it alongside the processor switches.  */
  CHECK (run_shell ({ "sis", "-help" }, "").has ("[-riscv]"));

  /* Unlike -griscv and -rv32 it leaves the board to the executable.  */
  temp_file elf ("-rv32.elf", make_elf (EM_RISCV_, 0x80000000, { RISCV_NOP }));
  CHECK (run_shell ({ "sis", "-riscv", elf.path () }, "")
	     .has ("RISCV/CLINT emulation enabled"));
}

TEST_CASE ("-m sets the number of cores")
{
  /* doc/invoking-sis.md: "-m cores  Enable the number of cores (2 - 4) in
     a leon3 or RISC-V multi-processor system".  A count outside the
     supported range falls back to a single core.  */
  CHECK (run_shell ({ "sis", "-leon3", "-m", "2" }, "").has ("2 cpus online"));
  CHECK (run_shell ({ "sis", "-leon3", "-m", "4" }, "").has ("4 cpus online"));
  CHECK (run_shell ({ "sis", "-leon3", "-m", "8" }, "").has ("1 cpus online"));
  CHECK (run_shell ({ "sis", "-leon3", "-m", "0" }, "").has ("1 cpus online"));
  /* A missing count leaves the default in place.  */
  CHECK (run_shell ({ "sis", "-leon3", "-m" }, "").has ("1 cpus online"));
}

TEST_CASE ("-d sets the multi-processor time slice")
{
  /* doc/invoking-sis.md: "-d clocks  Set the the number of clocks in each
     time-slice for multi-processor simulation. Default is 50, set lower
     for higher accuracy."  */
  CHECK (
      run_shell ({ "sis", "-leon3", "-d", "25" }, "").has ("delta 25 clocks"));
  CHECK (
      run_shell ({ "sis", "-leon3", "-d", "0" }, "").has ("delta 50 clocks"));
  CHECK (run_shell ({ "sis", "-leon3", "-d" }, "").has ("delta 50 clocks"));
}

TEST_CASE ("-help prints the usage and exits successfully")
{
  /* doc/invoking-sis.md: "-help  Display a help message".  */
  sis_result r = run_sis ({ "sis", "-help" }, "");

  CHECK (r.status == 0);
  CHECK (r.has ("Usage: sis [options] [files]"));
}

TEST_CASE ("an unrecognised option is reported and fails")
{
  /* Only the options doc/invoking-sis.md lists are recognised.  */
  sis_result r = run_sis ({ "sis", "-nosuchoption" }, "");

  CHECK (r.status == 1);
  CHECK (r.has ("unknown option \"-nosuchoption\""));
  CHECK (r.has ("Usage: sis [options] [files]"));
}

TEST_CASE ("-c runs a batch file at start-up")
{
  /* doc/invoking-sis.md: "-c file  Read sis commands from file at
     startup."  */
  temp_file batch ("-batch.txt", "echo batch-file-ran\n");
  sis_result r = run_shell ({ "sis", "-c", batch.path () }, "");

  CHECK (r.has ("batch-file-ran"));

  /* A missing file name leaves the start-up batch out.  */
  CHECK_FALSE (run_shell ({ "sis", "-c" }, "").has ("batch-file-ran"));
}

TEST_CASE ("-c takes a batch file whose path is long")
{
  /* doc/invoking-sis.md puts no length on the file name, and the path
     comes straight from the command line.  The command was once built by
     copying it into a fixed 256 byte allocation, which a path this long
     ran past.  Only a sanitized build sees the overflow, so this case is a
     guard for that build rather than a check of its own.  */
  std::string suffix (225, 'b');

  temp_file batch (("-" + suffix + ".txt").c_str (), "echo long-path-ran\n");

  /* "batch " and the terminator take seven of the 256, so a path past 249
     is what ran off the end.  A single name cannot exceed 255 bytes, which
     is why this is as long as it is.  */
  REQUIRE (batch.path ().size () > 249);

  sis_result r = run_shell ({ "sis", "-c", batch.path () }, "");

  CHECK (r.has ("long-path-ran"));
}

TEST_CASE ("-r starts execution without the interactive shell")
{
  /* doc/invoking-sis.md: "-r  Start execution immediately without an
     interactive shell", and "-tlim value unit  Used together with -r to
     limit the amount of simulated time that the simulator runs for before
     exiting."  The program branches to itself, so only the time limit can
     end the run.  */
  temp_file elf ("-loop.elf", make_elf (EM_SPARC_, 0x02000000,
					{ SPARC_BA_SELF, SPARC_NOP }));

  for (const char *unit : { "us", "ms", "s" })
    {
      CAPTURE (unit);
      sis_result r =
	  run_sis ({ "sis", "-r", "-tlim", "1", unit, elf.path () }, "");

      CHECK (r.status == 0);
      CHECK (r.has ("Time-out limit reached"));
      /* The limit stops the run the way a Ctrl-C does, and the shell
	 reports the simulated time it stopped at.  */
      CHECK (r.has ("Interrupt!"));
      CHECK (r.has ("Stopped at time"));
    }

  /* Without the unit the limit is dropped, so the shell is entered as
     usual.  */
  CHECK_FALSE (
      run_shell ({ "sis", "-tlim", "1" }, "").has ("Time-out limit reached"));
}

TEST_CASE ("-tlim also limits a run started from the shell")
{
  temp_file elf ("-loop.elf", make_elf (EM_SPARC_, 0x02000000,
					{ SPARC_BA_SELF, SPARC_NOP }));
  sis_result r =
      run_shell ({ "sis", "-tlim", "1", "us", elf.path () }, "run\n");

  CHECK (r.has ("Time-out limit reached"));
}

TEST_CASE ("-gdb starts the gdb server on the selected port")
{
  /* doc/invoking-sis.md: "-gdb  Start a gdb server, listening on port
     1234. An alternative port can be specified with -port nn."  The test
     hands it a port it cannot bind, so the server reports the port and
     gives up instead of waiting for a debugger.  */
  sis_result r = run_shell ({ "sis", "-gdb", "-port", "1" }, "");

  CHECK (r.has ("gdb: listening on port 1"));

  /* A missing port number keeps the documented default.  */
  CHECK (run_shell ({ "sis", "-port" }, "").has ("ERC32 emulation enabled"));
}

TEST_CASE ("-cov writes a coverage file next to the executable")
{
  /* doc/invoking-sis.md: "-cov  Enable code coverage and write a coverage
     file at exit", and help.cc: "hello.exe will produce hello.exe.cov".  */
  temp_file elf ("-cov.elf", make_elf (EM_SPARC_, 0x02000000,
				       { SPARC_BA_SELF, SPARC_NOP }));
  std::string cov = elf.path () + ".cov";
  sis_result r =
      run_sis ({ "sis", "-cov", "-r", "-tlim", "1", "us", elf.path () }, "");

  CHECK (r.status == 0);
  CHECK (std::filesystem::exists (cov));

  std::error_code ec;
  std::filesystem::remove (cov, ec);
}

TEST_CASE ("-nfp disables the simulated FPU")
{
  /* doc/invoking-sis.md: "-nfp  Disable the simulated FPU, so each FPU
     instruction will generate an FPU disabled trap."  */
  CHECK (run_shell ({ "sis", "-nfp" }, "").has ("FPU disabled"));
  CHECK_FALSE (run_shell ({ "sis" }, "").has ("FPU disabled"));
}

TEST_CASE ("-freq sets the emulated cpu frequency")
{
  /* doc/invoking-sis.md: "-freq freq  Set frequency of emulated cpu. This
     is used by the 'perf' command to calculate the MIPS figure. The
     frequency must be an integer indicating the frequency in MHz."  Each
     board carries its own default, so the switch is exercised on all of
     them and the default is checked to be a real frequency.  */
  for (const char *board :
       { "-erc32", "-leon2", "-leon3", "-gr740", "-griscv", "-rv32" })
    {
      CAPTURE (board);

      CHECK (run_shell ({ "sis", board, "-freq", "100" }, "perf\n")
		 .has ("Frequency       : 100.0 MHz"));
      CHECK_FALSE (run_shell ({ "sis", board }, "perf\n")
		       .has ("Frequency       : 0.0 MHz"));
    }

  /* A missing frequency keeps the board default.  */
  CHECK_FALSE (run_shell ({ "sis", "-freq" }, "perf\n")
		   .has ("Frequency       : 0.0 MHz"));
}

TEST_CASE ("-v increases the diagnostic output")
{
  /* doc/invoking-sis.md: "-v  Increase the debug level with 1, to provide
     more diagnostic messages."  Loading an executable is silent by
     default and reports the architecture and the sections when verbose.  */
  temp_file elf ("-v.elf", make_elf (EM_SPARC_, 0x02000000, { SPARC_NOP }));

  CHECK_FALSE (
      run_shell ({ "sis", elf.path () }, "").has ("SPARC executable"));
  CHECK (
      run_shell ({ "sis", "-v", elf.path () }, "").has ("SPARC executable"));
}

TEST_CASE ("the remaining documented options are accepted")
{
  /* Every switch doc/invoking-sis.md lists has to be recognised.  These
     carry no start-up output of their own, so the check is that the
     simulator starts and does not reject them.  */
  const std::vector<std::vector<std::string>> runs = {
    { "sis", "-rt" },
    { "sis", "-ift" },
    { "sis", "-dumbio" },
    { "sis", "-nouartrx" },
    { "sis", "-wrp" },
    { "sis", "-rom8" },
    { "sis", "-uben" },
    { "sis", "-extirq", "5" },
    { "sis", "-bridge", "br0" },
    { "sis", "-uart1", "/dev/null" },
    { "sis", "-uart2", "/dev/null" },
    /* The same switches with the value left off must not upset the
       parser either.  */
    { "sis", "-extirq" },
    { "sis", "-bridge" },
    { "sis", "-uart1" },
    { "sis", "-uart2" },
  };

  for (const std::vector<std::string> &args : runs)
    {
      CAPTURE (args[1]);
      sis_result r = run_shell (args, "");

      CHECK (r.has ("ERC32 emulation enabled"));
    }
}

/* The shell reports why a run stopped.  Each stop reason gets a synthetic
   program that provokes it.  */

TEST_CASE ("the shell reports a breakpoint and a single step")
{
  /* doc/commands.md: "+bp <addr>  add a breakpoint at <addr>" and "step
     single step".  */
  temp_file elf ("-nops.elf",
		 make_elf (EM_SPARC_, 0x02000000,
			   { SPARC_NOP, SPARC_NOP, SPARC_NOP, SPARC_NOP }));
  sis_result r =
      run_shell ({ "sis", elf.path () }, "+bp 0x02000008\nrun\nstep\n");

  CHECK (r.has ("cpu 0 breakpoint at 0x02000008 reached"));
  /* The step that follows ends on its instruction count, not on an
     interrupt.  */
  CHECK (r.has ("Stopped at time"));
  CHECK_FALSE (r.has ("Interrupt!"));
}

TEST_CASE ("the shell reports error mode")
{
  /* An unimplemented instruction traps, and with traps disabled after
     reset the processor enters error mode.  */
  temp_file elf ("-trap.elf",
		 make_elf (EM_SPARC_, 0x02000000, { SPARC_UNIMP, SPARC_NOP }));
  sis_result r = run_shell ({ "sis", elf.path () }, "run\n");

  /* The report names the core and the trap type, and disassembles the
     instruction the core stopped on.  The file name of the executable is
     echoed by the loader, so the disassembly is matched with its address
     and opcode rather than by the mnemonic alone.  */
  CHECK (r.has ("cpu 0 in error mode (tt = 0x"));
  CHECK (r.has ("00000000   unimp"));
}

TEST_CASE ("the shell reports a watchpoint")
{
  /* doc/commands.md lists the watchpoint commands; +wpr stops the run on
     a read of the watched word.  The program loads from 0x100.  */
  temp_file elf ("-load.elf", make_elf (EM_SPARC_, 0x02000000,
					{ SPARC_LD_256, SPARC_NOP }));
  sis_result r = run_shell ({ "sis", elf.path () }, "+wpr 0x100\nrun\n");

  CHECK (r.has ("cpu 0 watchpoint at 0x00000100 reached"));
}

TEST_CASE ("the shell reports a null pointer jump")
{
  /* A jump to address zero halts the core.  The report comes from the
     multi-processor run loop, so the program runs on a two core LEON3
     (doc/leon3.md, doc/multi-processing.md).  */
  temp_file elf ("-jmpnull.elf", make_elf (EM_SPARC_, 0x40000000,
					   { SPARC_JMP_NULL, SPARC_NOP }));
  sis_result r =
      run_shell ({ "sis", "-leon3", "-m", "2", elf.path () }, "run\n");

  CHECK (r.has ("segmentation error, cpu 0 halted"));
  /* The instruction the core stopped on is disassembled after the
     report.  */
  CHECK (r.has ("81c02000   jmp"));
}

#endif /* __linux__ */
