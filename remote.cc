/* SPDX-License-Identifier: GPL-3.0-or-later */
/* This file is part of SIS (SPARC/RISCV instruction simulator)

   Copyright (C) 2019 Free Software Foundation, Inc.
   Contributed by Jiri Gaisler, European Space Agency

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

/* This code based on socket example at
 * https://www.geeksforgeeks.org/socket-programming-cc/
 * and on sparc-stub.c from gdb.
 */

#ifndef _WIN32
#include <unistd.h>
#endif
#include <stdio.h>
#ifdef _WIN32
#include <winsock2.h>
#else
#define WSAPOLLFD struct pollfd
#define WSAPoll	  poll
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <poll.h>
#endif
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "sis.h"
#include "sisio.h"
#include "remote_socket.h"

#define EBREAK	0x00100073
#define CEBREAK 0x90002

#ifndef SIGTRAP
#define SIGTRAP 5
#endif
#ifndef _WIN32
#define closesocket close
#endif

int new_socket;
static char sendbuf[2048] = "$";
static const char hexchars[] = "0123456789abcdef";
static int detach = 0;

/* The sockets API, as the listener template sees it.  */

namespace
{

struct RealEnv
{
  int
  Socket ()
  {
    return socket (AF_INET, SOCK_STREAM, 0);
  }

  int
  SetReuseAddr (int fd)
  {
    int opt = 1;
    return setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, (char *) &opt,
		       sizeof (opt));
  }

  int
  Bind (int fd, int port)
  {
    struct sockaddr_in address;

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons (port);

    return bind (fd, (struct sockaddr *) &address, sizeof (address));
  }

  int
  Listen (int fd)
  {
    return listen (fd, 1);
  }

  int
  Accept (int fd)
  {
    struct sockaddr_in address;
    int addrlen = sizeof (address);

    return accept (fd, (struct sockaddr *) &address, (socklen_t *) &addrlen);
  }

  void
  Close (int fd)
  {
    closesocket (fd);
  }

  void
  Configure (int fd)
  {
    int opt = 1;
    struct protoent *proto;

    setsockopt (fd, SOL_SOCKET, SO_KEEPALIVE, (char *) &opt, sizeof (opt));
    proto = getprotobyname ("tcp");
    setsockopt (fd, proto->p_proto, TCP_NODELAY, (char *) &opt, sizeof (opt));
#ifndef _WIN32
    fcntl (fd, F_SETOWN, getpid ());
#endif
  }

  void
  Fail (const char *what)
  {
    perror (what);
  }
};

} /* namespace */

int
create_socket (int port)
{
  RealEnv env;
  remote::Listener<RealEnv> listener (env);

#ifdef _WIN32
  WORD wver;
  WSADATA wsaData;
  wver = MAKEWORD (2, 0);
  if (WSAStartup (wver, &wsaData))
    return 0;
#endif

  return listener.Open (port, &new_socket);
}

/* poll socket periodically to detect gdb break */
void
socket_poll (int32 arg)
{
  (void) arg;
  WSAPOLLFD fdarray = { 0 };
  int ret;

  fdarray.fd = new_socket;
  fdarray.events = POLLRDNORM;
  ret = WSAPoll (&fdarray, 1, 0);
  if (ret)
    ctrl_c = 1;
  event (socket_poll, 0, 10000000);
}

static int
hex (unsigned char ch)
{
  if (ch >= 'a' && ch <= 'f')
    return ch - 'a' + 10;
  if (ch >= '0' && ch <= '9')
    return ch - '0';
  if (ch >= 'A' && ch <= 'F')
    return ch - 'A' + 10;
  return -1;
}

/* Scan the hexadecimal number at *POS and return its value.  The scan stops
   at the first character that is not a hexadecimal digit, and *POS is left
   one past it: that character is the separator the protocol puts between a
   packet's fields.  A packet always ends in '#', so the scan cannot run past
   the data that was received.  */

static uint32
gethex (const char *buf, unsigned int *pos)
{
  uint32 val = 0;
  unsigned int i = *pos;
  int digit;

  while ((digit = hex (buf[i])) >= 0)
    {
      val = (val << 4) | digit;
      i++;
    }
  *pos = i + 1;

  return val;
}

/* The same, for a value sent least significant byte first, which is how a
   little endian target's register value arrives.  */

static uint32
gethex_le (const char *buf, unsigned int *pos)
{
  uint32 val = 0;
  unsigned int i = *pos;
  uint32 byte;

  while (hex (buf[i]) >= 0)
    {
      byte = hex (buf[i++]) << 4;
      byte |= hex (buf[i++]);
      val = (byte << 24) | (val >> 8);
    }
  *pos = i + 1;

  return val;
}

void
checksum (char *buf)
{
  unsigned char sum = 0;

  while (*buf)
    sum += *buf++;
  *buf++ = '#';
  *buf++ = hexchars[sum >> 4];
  *buf++ = hexchars[sum & 0x0f];
  *buf++ = 0;
}

int
check_pkg (unsigned char *buf, int len)
{
  int i, start = 0;
  unsigned char chksum = 0;
  unsigned char rxsum = 0;

  i = 0;
  while ((i < len) && (buf[i] != '$'))
    i++;
  if (i == len)
    return -1;
  i++;
  start = i;
  while ((i < len) && (buf[i] != '#'))
    {
      chksum = chksum + buf[i++];
    }
  if (i == len)
    return -1;
  i++;
  rxsum = (hex (buf[i]) << 4) | hex (buf[i + 1]);

  if ((i < len) && (chksum == rxsum))
    return start;
  return -1;
}

static void
int2hex (char *hexbuf, char *intbuf, int len)
{
  int i;
  for (i = 0; i < len; i++)
    {
      hexbuf[i * 2] = hexchars[(intbuf[i] >> 4) & 0x0f];
      hexbuf[i * 2 + 1] = hexchars[intbuf[i] & 0x0f];
    }
  hexbuf[len * 2] = 0;
}

static int
sim_stat ()
{
  int i;
  switch (simstat)
    {
    case OK:
      i = 0;
      break;
    case NULL_HIT:
      i = SIGSEGV;
      break;
    case ERROR_MODE:
      i = SIGTERM;
      break;
    case CTRL_C:
      i = SIGINT;
      break;
    default:
      i = SIGTRAP;
    }
  return i;
}

int
gdb_remote_exec (char *buf)
{
  char membuf[1024];
  unsigned int i, j, len, addr;
  int cont = 1;
  char *cptr, *mptr;
  char *txbuf = &sendbuf[1];

  switch (buf[0])
    {
    case 'H':
      if (buf[1] != 'c')
	strcpy (txbuf, "OK");
      break;
    case '?': /* last signal */
      if ((ebase.simtime == 0) && (sregs[cpu].pc == last_load_addr) &&
	  last_load_addr)
	sprintf (txbuf, "W%02x", sim_stat ());
      else
	sprintf (txbuf, "S%02x", sim_stat ());
      break;
    case 'D': /* detach */
      strcpy (txbuf, "OK");
      detach = 1;
      break;
    case 'g': /* get registers */
      len = arch->gdb_get_reg (membuf);
      int2hex (txbuf, membuf, len);
      break;
    case 'p': /* read one register */
      i = 1;
      addr = gethex (buf, &i);
      len = arch->gdb_get_regi (&sregs[cpu], addr, membuf);

      /* An empty reply is how the protocol says a register is not there.
	 The debugger falls back on the g packet, which is what it did
	 before this packet answered at all.  */
      int2hex (txbuf, membuf, len);
      break;
    case 'm': /* read memory */
      i = 1;
      addr = gethex (buf, &i);
      len = gethex (buf, &i);
      sim_read (addr, membuf, len);
      int2hex (txbuf, membuf, len);
      break;
    case 'M': /* write memory */
      i = 1;
      addr = gethex (buf, &i);
      len = gethex (buf, &i);
      j = 0;
      while (buf[i] != '#')
	{
	  membuf[j] = (hex (buf[i]) << 4) | hex (buf[i + 1]);
	  i += 2;
	  j += 1;
	}
      sim_write (addr, membuf, len);
      strcpy (txbuf, "OK");
      break;
    case 'P': /* write register */
      i = 1;
      addr = gethex (buf, &i);
      if (cputype == CPU_RISCV)
	len = gethex_le (buf, &i); /* value is in target order! */
      else
	len = gethex (buf, &i);
      arch->set_register (&sregs[cpu], NULL, len, addr);
      strcpy (txbuf, "OK");
      break;
    case 'C': /* continue execution */
      sim_create_inferior ();
    case 'c':
      sim_resume (0);
      i = sim_stat ();
      sprintf (txbuf, "S%02x", i);
      if ((i == SIGTRAP) && ebase.wphit)
	{
	  /* check_wpw and check_wpr are the only writers of wptype and set
	     2 for a write watchpoint and 3 for a read one.  */
	  if (ebase.wptype == 2)
	    sprintf (txbuf, "T%02xwatch:%x;", i, ebase.wpaddress);
	  else
	    sprintf (txbuf, "T%02xrwatch:%x;", i, ebase.wpaddress);
	}
      break;
    case 'k': /* kill */
    case 'R': /* restart */
      sim_create_inferior ();
      strcpy (txbuf, "OK");
      break;
    case 'v':
      if (strncmp (&buf[1], "Kill", 4) == 0)
	{ /* restart */
	  sim_create_inferior ();
	  strcpy (txbuf, "OK");
	}
      else if (strncmp (&buf[1], "Run;", 4) == 0)
	{ /* Restart */
	  sim_create_inferior ();
	  strcpy (txbuf, "S00");
	}
      else if (strncmp (&buf[1], "Cont", 4) == 0)
	{ /* continue/step */
	  /* Each action is introduced by a semicolon and may name a thread
	     after a colon.  The target has one thread, so the first action is
	     the one that applies.  An action the stub does not implement, and
	     a vCont with no action at all, get the empty packet.  */
	  char action = buf[5];

	  if (action == ';')
	    action = buf[6];
	  else if (action != '?')
	    action = 0;

	  switch (action)
	    {
	    case '?':
	      strcpy (txbuf, "vCont;c;s");
	      break;
	    case 'c':
	      sim_resume (0);
	      i = sim_stat ();
	      sprintf (txbuf, "S%02x", i);
	      break;
	    case 's':
	      sim_resume (1);
	      i = sim_stat ();
	      sprintf (txbuf, "S%02x", i);
	      break;
	    default:
	      break;
	    }
	}
      break;
    case 's':
    case 'S':
      sim_resume (1);
      i = sim_stat ();
      sprintf (txbuf, "S%02x", i);
      break;
    case 'Z': /* add break/watch point */
    case 'z': /* remove break/watch point */
      i = 3;
      addr = gethex (buf, &i);
      len = hex (buf[i]);
      if (buf[0] == 'Z')
	j = sim_set_watchpoint (addr, len, hex (buf[1]));
      else
	j = sim_clear_watchpoint (addr, len, hex (buf[1]));
      if (j)
	strcpy (txbuf, "OK");
      else
	strcpy (txbuf, "E01");
      break;
    case 'q': /* query */
      if (strncmp (&buf[1], "fThreadInfo", 11) == 0)
	{
	  strcpy (txbuf, "l");
	}
      else if (strncmp (&buf[1], "Attached", 8) == 0)
	{
	  strcpy (txbuf, "0");
	}
      else if (strncmp (&buf[1], "sThreadInfo", 11) == 0)
	{
	  strcpy (txbuf, "l");
	}
      else if (strncmp (&buf[1], "Rcmd", 4) == 0)
	{
	  cptr = &buf[6];
	  mptr = membuf;
	  while (*cptr != '#')
	    {
	      *mptr = hex (*cptr++) << 4;
	      *mptr++ |= hex (*cptr++);
	    }
	  *mptr = 0;
	  exec_cmd (membuf);
	  strcpy (txbuf, "OK");
	}
      break;
    case '!': /* extended protocl */
      strcpy (txbuf, "OK");
      break;
    default:
      printf ("%s\n", buf);
    }

  checksum (txbuf);
  return cont;
}

void
gdb_remote (int port)
{
  unsigned char buffer[2048];
  int cont = 1;
  int res, len = 0;
  char ack = '+';
  char nok = '-';

  sis_gdb_break = 1;
  detach = 0;

  printf ("gdb: listening on port %d ", port);
  while (cont)
    {
      if ((cont = create_socket (port)))
	{
	  send (new_socket, &ack, 1, 0);
	  printf ("connected\n");
	}
      while (cont)
	{
	  do
	    {
	      len = sis_socket_read (new_socket, (char *) buffer, 2048);
	      if (len < 0)
		len = 0;
	      buffer[len] = 0;
	      if (sis_verbose > 1)
		printf ("%s (%d)\n", buffer, len);
	      if (len == 1)
		if (buffer[0] == '-')
		  {
		    if (sis_verbose > 1)
		      printf ("tx: %s\n", sendbuf);
		    send (new_socket, sendbuf, strlen (sendbuf), 0);
		  }
		else if (buffer[0] == '+')
		  {
		    if (detach)
		      {
			cont = 0;
			break;
		      }
		  }
		else if (buffer[0] == 3)
		  {
		    ctrl_c = 1;
		  }
	      if (len <= 0)
		{
		  cont = 0;
		  break;
		}
	    }
	  while (len < 2);
	  if (!cont)
	    break;
	  res = check_pkg (buffer, len);
	  if (res > 0)
	    {
	      if (sis_verbose > 1)
		printf ("tx: +\n");
	      send (new_socket, &ack, 1, 0);
	      if (detach)
		{
		  cont = 0;
		}
	      else
		{
		  strcpy (sendbuf, "$");
		  cont = gdb_remote_exec ((char *) &buffer[res]);
		  if (sis_verbose > 1)
		    printf ("tx: %s\n", sendbuf);
		  send (new_socket, sendbuf, strlen (sendbuf), 0);
		}
	    }
	  else
	    {
	      if (sis_verbose > 1)
		printf ("tx: -\n");
	      send (new_socket, &nok, 1, 0);
	    }
	}
    }
  if (new_socket)
    {
      closesocket (new_socket);
    }
  new_socket = 0;
  sis_gdb_break = 0;
}
