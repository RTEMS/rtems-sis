/* SPDX-License-Identifier: GPL-3.0-or-later */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* The host side of an emulated UART: the stream a port is attached to, the
   descriptors under it, and the terminal settings to put back on exit.

   Every board with a UART needs the same thing, so this is shared by
   erc32.cc, leon2.cc and grlib.cc rather than written once per board.  What
   a port is attached to comes from the -uart1 and -uart2 options, or from
   the console when the board puts the port there.  */

#ifndef SIS_UARTPORT_H
#define SIS_UARTPORT_H

#include "config.h"

#include <stdio.h>
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif

/* One host port.  A board defines one of these per emulated UART and passes
   it to the calls below; nothing else touches the members.  The descriptors
   start at -1 so that a port left unattached is not mistaken for one on
   stdin, which is descriptor 0.  */
struct uart_port
{
  const char *name; /* "A" or "B", for the progress output */
  FILE *fin;
  FILE *fout;
  int fd;  /* the device, -1 unless -uart<n> named one */
  int ifd; /* the descriptor read from, -1 while unattached */
  int ofd; /* the descriptor written to, -1 while unattached */
  int open;
#ifdef HAVE_TERMIOS_H
  struct termios raw;	/* what the emulated UART needs */
  struct termios saved; /* what the terminal had before */
#endif
};

#define UART_PORT_INIT { NULL, NULL, NULL, -1, -1, -1, 0 }

/* Attach PORT, called NAME in the progress output, to DEVICE, or to the
   console when DEVICE is empty and CONSOLE is set.  A board which leaves a
   port unattached passes an empty DEVICE and a clear CONSOLE.  */
void uart_port_open (struct uart_port *port, const char *name,
		     const char *device, int console);

/* Put the terminal into the mode the emulated UART needs, and take it back
   out.  Both are no-ops for a port which is not on the console.  */
void uart_port_raw (struct uart_port *port);
void uart_port_restore (struct uart_port *port);

/* Release the device, if the port has one of its own.  */
void uart_port_close (struct uart_port *port);

/* Read what the host has for the port, without blocking.  Returns zero when
   there is nothing, and when the options say the UART takes no input.  */
int uart_port_read (struct uart_port *port, char *buf, int len);

#endif
