/* SPDX-License-Identifier: GPL-3.0-or-later */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* The host side of an emulated UART.  See uartport.h for what a port is and
   why this is shared rather than written once per board.  */

#include "uartport.h"

#include "sis.h"
#include "sisio.h"

#include <stdio.h>

void
uart_port_open (struct uart_port *port, const char *name, const char *device,
		int console)
{
  port->name = name;

  if (console)
    {
      port->fin = stdin;
      port->fout = stdout;
    }
  else
    {
      port->fin = NULL;
      port->fout = NULL;
    }

  if (device[0] != 0)
    {
      port->fd = sis_uart_open (device);
      if (port->fd < 0)
	printf ("Warning, couldn't open output device %s\n", device);
      else
	{
	  if (sis_verbose)
	    printf ("serial port %s on %s\n", name, device);
	  port->fin = port->fout = fdopen (port->fd, "r+");
	  setbuf (port->fout, NULL);
	  port->open = 1;
	}
    }

  if (port->fin)
    port->ifd = fileno (port->fin);

  if (port->ifd == 0)
    {
      if (sis_verbose)
	printf ("serial port %s on stdin/stdout\n", name);
      if (!dumbio)
	{
#ifdef HAVE_TERMIOS_H
	  tcgetattr (port->ifd, &port->raw);
	  if (tty_setup)
	    {
	      port->saved = port->raw;
	      port->raw.c_lflag &= ~(ICANON | ECHO);
	      port->raw.c_cc[VMIN] = 0;
	      port->raw.c_cc[VTIME] = 0;
	    }
#endif
	}
      port->open = 1;
    }

  if (port->fout)
    {
      port->ofd = fileno (port->fout);
      if (!dumbio && tty_setup && port->ofd == 1)
	setbuf (port->fout, NULL);
    }
}

void
uart_port_raw (struct uart_port *port)
{
  if (dumbio)
    return;

  /* On Windows the console is switched by hand; on POSIX this does nothing
     and the terminal settings below do the work.  */
  sis_console_raw (1);

#ifdef HAVE_TERMIOS_H
  /* Only a port on the console has a terminal.  Opening one is what sets the
     descriptor to zero, so there is nothing else to test.  */
  if (port->ifd == 0)
    {
      tcsetattr (0, TCSANOW, &port->raw);
      tcflush (port->ifd, TCIFLUSH);
    }
#else
  (void) port;
#endif
}

void
uart_port_restore (struct uart_port *port)
{
  if (dumbio)
    return;

  sis_console_raw (0);

#ifdef HAVE_TERMIOS_H
  /* Without -tty nothing was saved, so there is nothing to put back.  */
  if (port->ifd == 0 && tty_setup)
    tcsetattr (0, TCSANOW, &port->saved);
#else
  (void) port;
#endif
}

void
uart_port_close (struct uart_port *port)
{
  if (port->open && port->fin != stdin)
    fclose (port->fin);
}

int
uart_port_read (struct uart_port *port, char *buf, int len)
{
  if (dumbio || nouartrx)
    return 0;
  return sis_uart_read (port->ifd, buf, len);
}
