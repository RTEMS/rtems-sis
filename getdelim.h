/* SPDX-License-Identifier: GPL-3.0-or-later */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* A local getdelim(), since not every host provides the POSIX one.  It is
   written as a template on an allocator policy so that the two allocation
   failure paths are injected rather than reached through malloc(): the
   simulator instantiates it on HostAlloc, a test instantiates it on an
   allocator that fails on demand.

   The allocator must provide:

     void *Malloc (size_t);          the first line buffer
     void *Realloc (void *, size_t); the buffer grown by one chunk

   The contract follows POSIX getdelim(): the line is stored in *lineptr,
   which is allocated on the first call and grown as needed, *n holds its
   size, and the delimiter is kept in the returned line.  It differs from
   POSIX in one way the caller relies on: a partial final line with no
   delimiter returns -1 rather than the byte count, so end of file and a
   truncated last line are not distinguished.  */

#ifndef SIS_GETDELIM_H
#define SIS_GETDELIM_H

#include <concepts>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/types.h>

namespace sis
{

/* What the line reader requires of its allocator.  */
template <class A>
concept LineAlloc = requires (size_t size, void *ptr) {
  { A::Malloc (size) } -> std::same_as<void *>;
  { A::Realloc (ptr, size) } -> std::same_as<void *>;
};

/* The buffer starts at this size and grows by it.  */
constexpr size_t line_size = 128;

/* The allocator the simulator runs on.  */
struct HostAlloc
{
  static void *
  Malloc (size_t size)
  {
    return malloc (size);
  }

  static void *
  Realloc (void *ptr, size_t size)
  {
    return realloc (ptr, size);
  }
};

template <LineAlloc Alloc>
ssize_t
GetDelim (char **lineptr, size_t *n, int delim, FILE *stream)
{
  size_t indx = 0;
  int c;

  /* Sanity checks.  */
  if (lineptr == NULL || n == NULL || stream == NULL)
    return -1;

  /* Allocate the line the first time.  */
  if (*lineptr == NULL)
    {
      *lineptr = (char *) Alloc::Malloc (line_size);
      if (*lineptr == NULL)
	return -1;
      *n = line_size;
    }

  /* Clear the line.  */
  memset (*lineptr, '\0', *n);

  while ((c = getc (stream)) != EOF)
    {
      /* Check if more memory is needed.  */
      if (indx >= *n)
	{
	  char *grown = (char *) Alloc::Realloc (*lineptr, *n + line_size);
	  if (grown == NULL)
	    return -1;
	  *lineptr = grown;
	  /* Clear the rest of the line.  */
	  memset (*lineptr + *n, '\0', line_size);
	  *n += line_size;
	}

      /* Push the result in the line.  */
      (*lineptr)[indx++] = c;

      /* Bail out.  */
      if (c == delim)
	break;
    }
  return (c == EOF) ? -1 : (ssize_t) indx;
}

template <LineAlloc Alloc>
ssize_t
GetLine (char **lineptr, size_t *n, FILE *stream)
{
  return GetDelim<Alloc> (lineptr, n, '\n', stream);
}

}

#endif
