/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Tests for sisio.cc, the host I/O the standard library cannot express.

   Only the POSIX implementation is exercised. The Windows one is a different
   body of code behind #ifdef _WIN32 and is not compiled here, so it is
   absent from the coverage report rather than uncovered.

   These cases use real file descriptors, a real temporary file and a real
   pipe, so that the system calls the simulator depends on are the ones under
   test. Nothing here needs privileges.  */

#include "doctest.h"

#include "config.h"
#include "sis.h"
#include "sisio.h"

#ifndef _WIN32

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace
{

/* A temporary file that removes itself.  */
class temp_file
{
public:
  temp_file () : fd (-1)
  {
    strcpy (path, "/tmp/sis-test-XXXXXX");
    fd = mkstemp (path);
  }

  ~temp_file ()
  {
    if (fd >= 0)
      close (fd);
    unlink (path);
  }

  int fd;
  char path[64];
};

} /* namespace */

TEST_CASE ("sis_uart_open opens a device read write")
{
  temp_file temp;
  REQUIRE (temp.fd >= 0);
  REQUIRE (write (temp.fd, "uart", 4) == 4);

  int fd = sis_uart_open (temp.path);
  REQUIRE (fd >= 0);

  /* Opened O_RDWR, so both directions work.  */
  char buf[4] = { 0 };
  CHECK (read (fd, buf, sizeof (buf)) == 4);
  CHECK (memcmp (buf, "uart", 4) == 0);
  CHECK (write (fd, "x", 1) == 1);

  /* And O_NONBLOCK, which is what keeps the emulated UART from stalling
     the simulator.  */
  int flags = fcntl (fd, F_GETFL);
  REQUIRE (flags >= 0);
  CHECK ((flags & O_NONBLOCK) != 0);

  close (fd);
}

TEST_CASE ("sis_uart_open reports a missing device")
{
  CHECK (sis_uart_open ("/nonexistent/sis-test-device") < 0);
}

TEST_CASE ("sis_uart_read returns zero rather than an error")
{
  int fds[2];
  REQUIRE (pipe (fds) == 0);

  SUBCASE ("data available")
  {
    REQUIRE (write (fds[1], "abc", 3) == 3);

    char buf[8] = { 0 };
    CHECK (sis_uart_read (fds[0], buf, sizeof (buf)) == 3);
    CHECK (memcmp (buf, "abc", 3) == 0);
  }

  SUBCASE ("end of file")
  {
    close (fds[1]);
    fds[1] = -1;

    char buf[8];
    CHECK (sis_uart_read (fds[0], buf, sizeof (buf)) == 0);
  }

  SUBCASE ("would block reports no data, not an error")
  {
    REQUIRE (fcntl (fds[0], F_SETFL, O_NONBLOCK) == 0);

    char buf[8];
    CHECK (sis_uart_read (fds[0], buf, sizeof (buf)) == 0);
  }

  SUBCASE ("a bad descriptor reports no data, not an error")
  {
    char buf[8];
    CHECK (sis_uart_read (-1, buf, sizeof (buf)) == 0);
  }

  close (fds[0]);
  if (fds[1] >= 0)
    close (fds[1]);
}

TEST_CASE ("sis_console_raw is a no-op where the boards drive termios")
{
  sis_console_raw (1);
  sis_console_raw (0);
}

TEST_CASE ("sis_socket_read passes the read through")
{
  int fds[2];
  REQUIRE (pipe (fds) == 0);
  REQUIRE (write (fds[1], "gdb", 3) == 3);

  char buf[8] = { 0 };
  CHECK (sis_socket_read (fds[0], buf, sizeof (buf)) == 3);
  CHECK (memcmp (buf, "gdb", 3) == 0);

  close (fds[1]);
  CHECK (sis_socket_read (fds[0], buf, sizeof (buf)) == 0);

  close (fds[0]);

  /* Unlike sis_uart_read this one does not hide an error, because the GDB
     stub has to tell a closed connection from an idle one.  */
  CHECK (sis_socket_read (-1, buf, sizeof (buf)) < 0);
}

TEST_CASE ("sis_socket_close closes the descriptor")
{
  int fds[2];
  REQUIRE (pipe (fds) == 0);

  sis_socket_close (fds[0]);
  sis_socket_close (fds[1]);

  CHECK (fcntl (fds[0], F_GETFL) == -1);
  CHECK (errno == EBADF);
}

#endif /* !_WIN32 */
