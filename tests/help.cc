/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Tests for help.cc, the command line usage and the interactive help.

   Both functions are straight line printf, so there is nothing to compute
   and nothing to get wrong except what they leave out. The cases therefore
   assert that every option and every shell command the manual documents is
   still mentioned. That catches a deletion, not an addition: a new flag that
   nobody adds here goes unnoticed.  */

#include "doctest.h"
#include "support.h"

#include "config.h"
#include "sis.h"

using sis_tests::stdout_capture;

TEST_CASE ("sis_usage lists the command line options")
{
  std::string text;

  {
    stdout_capture capture;
    sis_usage ();
    text = capture.str ();
  }

  REQUIRE (!text.empty ());
  CHECK (text.find ("Usage: sis [options] [files]") != std::string::npos);

  /* The boards, which double as the architecture selection.  */
  for (const char *board :
       { "-erc32", "-leon2", "-leon3", "-gr740", "-griscv", "-rv32" })
    {
      CAPTURE (board);
      CHECK (text.find (board) != std::string::npos);
    }

  /* The options documented in doc/invoking-sis.md.  */
  for (const char *option : { "-help", "-v", "-r", "-tlim", "-c", "-gdb",
			      "-port", "-cov", "-freq", "-d", "-rt", "-m" })
    {
      CAPTURE (option);
      CHECK (text.find (option) != std::string::npos);
    }
}

TEST_CASE ("gen_help lists the shell commands")
{
  std::string text;

  {
    stdout_capture capture;
    gen_help ();
    text = capture.str ();
  }

  REQUIRE (!text.empty ());

  /* The commands documented in doc/commands.md.  */
  for (const char *command :
       { "batch", "+bp",  "-bp",   "bp",  "cont", "cpu",  "deb",
	 "dis",	  "echo", "float", "go",  "hist", "load", "mem",
	 "quit",  "perf", "reg",   "run", "step", "tra" })
    {
      CAPTURE (command);
      CHECK (text.find (command) != std::string::npos);
    }

  CHECK (text.find ("Ctrl-C") != std::string::npos);
}
