/* SPDX-License-Identifier: GPL-3.0-or-later */
/* This file is part of SIS (SPARC/RISCV instruction simulator)

   Copyright (C) 2026 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include "config.h"
#include "sisio.h"

#include <stdio.h>

#ifdef _WIN32

#include <winsock2.h>
#include <windows.h>

static DWORD conmode;
static int conmode_saved = 0;

int
sis_uart_open (const char *dev)
{
  printf ("Warning, UART devices are not supported on Windows (%s)\n", dev);
  return -1;
}

/* The console is the only UART input Windows offers.  A console handle is
   signalled by key releases and window events as well as key presses, so
   the records are inspected rather than waited on, and ReadFile is entered
   only once a real character is queued.  */
int
sis_uart_read (int fd, char *buf, int len)
{
  HANDLE h;
  INPUT_RECORD rec[32];
  DWORD navail, nread, i, ready;

  if (fd != 0 || len <= 0)
    return 0;

  h = GetStdHandle (STD_INPUT_HANDLE);
  if (h == INVALID_HANDLE_VALUE)
    return 0;

  if (GetFileType (h) != FILE_TYPE_CHAR)
    {
      /* Redirected from a file or a pipe.  A pipe must be polled, a file
	 always has data.  */
      if (GetFileType (h) == FILE_TYPE_PIPE)
	{
	  DWORD pending = 0;
	  if (!PeekNamedPipe (h, NULL, 0, NULL, &pending, NULL) || !pending)
	    return 0;
	}
      if (!ReadFile (h, buf, len, &nread, NULL))
	return 0;
      return (int) nread;
    }

  if (!GetNumberOfConsoleInputEvents (h, &navail) || navail == 0)
    return 0;

  if (navail > sizeof (rec) / sizeof (rec[0]))
    navail = sizeof (rec) / sizeof (rec[0]);
  if (!PeekConsoleInput (h, rec, navail, &nread))
    return 0;

  ready = 0;
  for (i = 0; i < nread; i++)
    if (rec[i].EventType == KEY_EVENT && rec[i].Event.KeyEvent.bKeyDown &&
	rec[i].Event.KeyEvent.uChar.AsciiChar != 0)
      ready++;

  if (ready == 0)
    {
      /* Only releases and resizes are queued.  Drop them so they do not
	 keep the queue non-empty for ever.  */
      ReadConsoleInput (h, rec, nread, &nread);
      return 0;
    }

  if ((DWORD) len > ready)
    len = (int) ready;
  if (!ReadFile (h, buf, len, &nread, NULL))
    return 0;
  return (int) nread;
}

void
sis_console_raw (int on)
{
  HANDLE h = GetStdHandle (STD_INPUT_HANDLE);

  if (h == INVALID_HANDLE_VALUE || GetFileType (h) != FILE_TYPE_CHAR)
    return;

  if (on)
    {
      if (!conmode_saved && GetConsoleMode (h, &conmode))
	conmode_saved = 1;
      if (conmode_saved)
	SetConsoleMode (h, conmode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
    }
  else if (conmode_saved)
    SetConsoleMode (h, conmode);
}

int
sis_socket_read (int fd, char *buf, int len)
{
  return recv ((SOCKET) fd, buf, len, 0);
}

void
sis_socket_close (int fd)
{
  closesocket ((SOCKET) fd);
}

#else /* !_WIN32 */

#include <fcntl.h>
#include <unistd.h>

#ifndef O_NONBLOCK
#define O_NONBLOCK 0
#endif

int
sis_uart_open (const char *dev)
{
  return open (dev, O_RDWR | O_NONBLOCK);
}

int
sis_uart_read (int fd, char *buf, int len)
{
  int n = read (fd, buf, len);

  return n < 0 ? 0 : n;
}

void
sis_console_raw (int on)
{
  /* The boards drive termios directly.  */
  (void) on;
}

int
sis_socket_read (int fd, char *buf, int len)
{
  return read (fd, buf, len);
}

void
sis_socket_close (int fd)
{
  close (fd);
}

#endif /* !_WIN32 */
