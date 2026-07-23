/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Helpers shared by the test files.  */

#ifndef SIS_TESTS_SUPPORT_H
#define SIS_TESTS_SUPPORT_H

#include <stdio.h>
#include <string>

#ifdef _WIN32
#include <io.h>
#define SIS_DUP	   _dup
#define SIS_DUP2   _dup2
#define SIS_CLOSE  _close
#define SIS_FILENO _fileno
#else
#include <unistd.h>
#define SIS_DUP	   dup
#define SIS_DUP2   dup2
#define SIS_CLOSE  close
#define SIS_FILENO fileno
#endif

namespace sis_tests
{

/* Collect everything the simulator prints while this object is alive.

   The simulator writes through printf rather than a stream a test could
   swap, so the capture works on the file descriptor: stdout is redirected
   to a temporary file and put back in the destructor.  Keep the scope
   tight, since a failing assertion inside it reports into the file.  */
class stdout_capture
{
public:
  stdout_capture () : file (tmpfile ()), saved (-1)
  {
    if (file == NULL)
      return;

    fflush (stdout);
    saved = SIS_DUP (SIS_FILENO (stdout));
    SIS_DUP2 (SIS_FILENO (file), SIS_FILENO (stdout));
  }

  ~stdout_capture ()
  {
    restore ();
    if (file != NULL)
      fclose (file);
  }

  stdout_capture (const stdout_capture &) = delete;
  stdout_capture &operator= (const stdout_capture &) = delete;

  /* Everything written so far.  Restores stdout first, so it is safe to
     assert on the result.  */
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

    fflush (stdout);
    SIS_DUP2 (saved, SIS_FILENO (stdout));
    SIS_CLOSE (saved);
    saved = -1;
  }

  FILE *file;
  int saved;
};

} /* namespace sis_tests */

#endif
