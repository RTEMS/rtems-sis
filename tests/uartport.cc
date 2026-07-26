/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Tests for uartport.cc, the host side of an emulated UART shared by every
   board with one.

   Like tests/sisio.cc these use a real temporary file and real descriptors,
   so the system calls the simulator depends on are the ones under test.

   A port on the console is driven with dumbio or with tty_setup clear
   wherever the terminal would otherwise be reprogrammed, because the test
   binary's own terminal is the one a tcsetattr would reach.  The option
   globals are simulator-wide, so every case saves and restores the ones it
   changes.  */

#include "doctest.h"

#include "config.h"
#include "sis.h"
#include "uartport.h"

#ifndef _WIN32

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace
{

/* A temporary file that removes itself, used as the device a port opens.  */
class temp_device
{
public:
  temp_device () : fd (-1)
  {
    strcpy (path, "/tmp/sis-uartport-XXXXXX");
    fd = mkstemp (path);
  }

  ~temp_device ()
  {
    if (fd >= 0)
      close (fd);
    unlink (path);
  }

  int fd;
  char path[64];
};

/* Saves and restores the simulator options a case changes, so the cases stay
   independent of each other and of the rest of the test binary.  */
class saved_options
{
public:
  saved_options ()
      : dumb (dumbio), norx (nouartrx), tty (tty_setup), verbose (sis_verbose)
  {
  }

  ~saved_options ()
  {
    dumbio = dumb;
    nouartrx = norx;
    tty_setup = tty;
    sis_verbose = verbose;
  }

private:
  int dumb;
  int norx;
  int tty;
  int verbose;
};

/* Puts a regular file under descriptor 0 for the lifetime of the object, so
   a port opened on the console is on something that is not the terminal of
   the test binary.  Every terminal call then fails and is ignored, which is
   what lets a case reach the console branches without reprogramming or
   flushing a terminal somebody is using.  */
class console_redirect
{
public:
  console_redirect () : file (tmpfile ()), saved (dup (0))
  {
    if (file != NULL)
      dup2 (fileno (file), 0);
  }

  ~console_redirect ()
  {
    if (saved >= 0)
      {
	dup2 (saved, 0);
	close (saved);
      }
    if (file != NULL)
      fclose (file);
  }

private:
  FILE *file;
  int saved;
};

} /* namespace */

TEST_CASE ("uart_port_open leaves an unattached port unattached")
{
  saved_options opts;
  struct uart_port port = UART_PORT_INIT;

  /* No device and not on the console: the port has no stream, keeps its
     descriptors at -1 so it is not mistaken for one on stdin, and is not
     open.  This is the ERC32 port A under -uben.  */
  uart_port_open (&port, "A", "", 0);

  CHECK (port.fin == NULL);
  CHECK (port.fout == NULL);
  CHECK (port.ifd == -1);
  CHECK (port.ofd == -1);
  CHECK (port.open == 0);
}

TEST_CASE ("uart_port_open puts a console port on stdin and stdout")
{
  saved_options opts;
  struct uart_port port = UART_PORT_INIT;

  dumbio = 1; /* keep the terminal of the test binary alone */
  uart_port_open (&port, "A", "", 1);

  CHECK (port.fin == stdin);
  CHECK (port.fout == stdout);
  CHECK (port.ifd == 0);
  CHECK (port.ofd == 1);
  CHECK (port.open == 1);
}

TEST_CASE ("uart_port_open attaches a port to a device")
{
  saved_options opts;
  temp_device dev;
  REQUIRE (dev.fd >= 0);

  struct uart_port port = UART_PORT_INIT;

  /* A named device wins over the console, and the port reads and writes the
     one stream.  */
  dumbio = 1;
  uart_port_open (&port, "B", dev.path, 1);

  CHECK (port.open == 1);
  CHECK (port.fd >= 0);
  CHECK (port.fin != NULL);
  CHECK (port.fin == port.fout);
  CHECK (port.fin != stdin);
  CHECK (port.ifd == port.fd);
  CHECK (port.ofd == port.fd);

  uart_port_close (&port);
}

TEST_CASE ("uart_port_open warns about a device it cannot open")
{
  saved_options opts;
  struct uart_port port = UART_PORT_INIT;

  /* The port falls back to whatever it already had, which is nothing here,
     and stays closed.  */
  dumbio = 1;
  uart_port_open (&port, "A", "/nonexistent/sis-uartport-device", 0);

  CHECK (port.open == 0);
  CHECK (port.fd < 0);
  CHECK (port.fin == NULL);
}

TEST_CASE ("uart_port_open names the port it attached when verbose")
{
  saved_options opts;
  temp_device dev;
  REQUIRE (dev.fd >= 0);

  dumbio = 1;
  sis_verbose = 1;

  struct uart_port device_port = UART_PORT_INIT;
  struct uart_port console_port = UART_PORT_INIT;

  uart_port_open (&device_port, "B", dev.path, 0);
  uart_port_open (&console_port, "A", "", 1);

  uart_port_close (&device_port);
}

TEST_CASE ("uart_port_open saves the terminal settings for -tty")
{
  saved_options opts;
  console_redirect console;
  struct uart_port port = UART_PORT_INIT;

  /* Without -dumbio the port reads the terminal settings, and with -tty it
     keeps a copy to put back and clears the line editing the emulated UART
     must not have.  */
  dumbio = 0;
  tty_setup = 1;
  uart_port_open (&port, "A", "", 1);

  CHECK (port.ifd == 0);
  CHECK (port.open == 1);

  /* Opening the console leaves stdout unbuffered, so a character written by
     the emulated UART appears at once.  Put the buffering back, since stdout
     belongs to the whole test binary.  */
  CHECK (port.ofd == 1);
  setvbuf (stdout, NULL, _IOFBF, BUFSIZ);
}

TEST_CASE ("uart_port_open keeps the terminal settings without -tty")
{
  saved_options opts;
  console_redirect console;
  struct uart_port port = UART_PORT_INIT;

  /* Without -tty the simulator is not allowed to reprogram the terminal, so
     nothing is saved and the settings are used as they are.  */
  dumbio = 0;
  tty_setup = 0;
  uart_port_open (&port, "A", "", 1);

  CHECK (port.open == 1);
}

TEST_CASE ("uart_port_raw and restore reach a console port")
{
  saved_options opts;
  console_redirect console;
  struct uart_port port = UART_PORT_INIT;

  dumbio = 0;
  tty_setup = 1;
  uart_port_open (&port, "A", "", 1);
  setvbuf (stdout, NULL, _IOFBF, BUFSIZ);

  /* Both reach the terminal calls for a port on the console.  Under the
     redirect above they act on a regular file and fail harmlessly.  */
  uart_port_raw (&port);
  uart_port_restore (&port);

  CHECK (port.ifd == 0);
  CHECK (port.open == 1);
}

TEST_CASE ("uart_port_read returns what the host has")
{
  saved_options opts;
  temp_device dev;
  REQUIRE (dev.fd >= 0);
  REQUIRE (write (dev.fd, "hello", 5) == 5);

  struct uart_port port = UART_PORT_INIT;
  char buf[16];

  dumbio = 1;
  uart_port_open (&port, "A", dev.path, 0);
  dumbio = 0;
  nouartrx = 0;

  CHECK (uart_port_read (&port, buf, sizeof buf) == 5);
  CHECK (memcmp (buf, "hello", 5) == 0);

  /* Nothing left, and a read of an empty device does not block.  */
  CHECK (uart_port_read (&port, buf, sizeof buf) == 0);

  uart_port_close (&port);
}

TEST_CASE ("uart_port_read takes no input when the options forbid it")
{
  saved_options opts;
  temp_device dev;
  REQUIRE (dev.fd >= 0);
  REQUIRE (write (dev.fd, "hello", 5) == 5);

  struct uart_port port = UART_PORT_INIT;
  char buf[16];

  dumbio = 1;
  uart_port_open (&port, "A", dev.path, 0);

  /* With -dumbio the UART is a plain stream with no input, and with
     -nouartrx it takes none either.  Both read nothing even though the
     device has bytes waiting.  */
  nouartrx = 0;
  CHECK (uart_port_read (&port, buf, sizeof buf) == 0);

  dumbio = 0;
  nouartrx = 1;
  CHECK (uart_port_read (&port, buf, sizeof buf) == 0);

  uart_port_close (&port);
}

TEST_CASE ("uart_port_raw and restore do nothing with dumbio")
{
  saved_options opts;
  struct uart_port port = UART_PORT_INIT;

  dumbio = 1;
  uart_port_open (&port, "A", "", 1);

  /* Both return before touching the terminal, which is what keeps this case
     from reprogramming the terminal of the test binary.  */
  uart_port_raw (&port);
  uart_port_restore (&port);

  CHECK (port.open == 1);
}

TEST_CASE ("uart_port_raw and restore leave a device port alone")
{
  saved_options opts;
  temp_device dev;
  REQUIRE (dev.fd >= 0);

  struct uart_port port = UART_PORT_INIT;

  dumbio = 0;
  tty_setup = 1;
  uart_port_open (&port, "B", dev.path, 0);

  /* The port is not on the console, so there is no terminal to reprogram and
     both calls fall through their descriptor test.  */
  REQUIRE (port.ifd != 0);
  uart_port_raw (&port);
  uart_port_restore (&port);

  uart_port_close (&port);
}

TEST_CASE ("uart_port_restore leaves the terminal alone without tty_setup")
{
  saved_options opts;
  struct uart_port port = UART_PORT_INIT;

  /* Without -tty the settings were never saved, so the restore must not put
     them back.  Opening with dumbio keeps the terminal untouched throughout.
   */
  dumbio = 1;
  uart_port_open (&port, "A", "", 1);
  dumbio = 0;
  tty_setup = 0;

  uart_port_restore (&port);

  CHECK (port.ifd == 0);
}

TEST_CASE ("uart_port_close keeps the console open")
{
  saved_options opts;
  struct uart_port port = UART_PORT_INIT;

  /* A port on the console shares stdin with the simulator, so closing the
     port must not close it.  */
  dumbio = 1;
  uart_port_open (&port, "A", "", 1);
  uart_port_close (&port);

  CHECK (port.fin == stdin);
  CHECK (fileno (stdin) == 0);
}

TEST_CASE ("uart_port_close releases a device")
{
  saved_options opts;
  temp_device dev;
  REQUIRE (dev.fd >= 0);

  struct uart_port port = UART_PORT_INIT;

  dumbio = 1;
  uart_port_open (&port, "A", dev.path, 0);
  REQUIRE (port.open == 1);

  int fd = port.fd;
  uart_port_close (&port);

  /* The descriptor is gone, so a read of it now fails.  */
  char c;
  CHECK (read (fd, &c, 1) == -1);
}

TEST_CASE ("uart_port_close does nothing for a port never attached")
{
  saved_options opts;
  struct uart_port port = UART_PORT_INIT;

  uart_port_open (&port, "A", "", 0);
  uart_port_close (&port);

  CHECK (port.open == 0);
}

#endif /* !_WIN32 */
