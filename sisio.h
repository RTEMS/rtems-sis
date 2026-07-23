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

/* Host I/O that the standard library cannot express.  Everything here is
   non-blocking character I/O, which C++17 has no portable form of.  */

#ifndef SISIO_H
#define SISIO_H

/* Open DEV for the emulated UART, non-blocking.  Returns a descriptor, or
   -1 when the device cannot be opened.  Character devices are a POSIX only
   feature; on Windows this reports that and fails.  */
int sis_uart_open (const char *dev);

/* Read up to LEN bytes from FD without blocking.  Returns the number of
   bytes read, which is zero when none are available.  */
int sis_uart_read (int fd, char *buf, int len);

/* Put the console in character-at-a-time mode, or restore it.  On POSIX the
   boards drive termios themselves and this does nothing.  */
void sis_console_raw (int on);

/* Read from a connected socket.  A socket is not a file descriptor on
   Windows, so read() does not work on one.  */
int sis_socket_read (int fd, char *buf, int len);

/* Close a socket.  */
void sis_socket_close (int fd);

#endif /* SISIO_H */
