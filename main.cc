/* SPDX-License-Identifier: GPL-3.0-or-later */
/* SPDX-FileCopyrightText: 1995-2017 Free Software Foundation, Inc. */

/* Entry point of the sis executable.  Everything the simulator does lives
   in libsis.a, so that the unit tests can link against it.  */

#include "sis.h"

int
main (int argc, char **argv)
{
  return sis_main (argc, argv);
}
