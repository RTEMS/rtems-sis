/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* A recording double for the host tap device of tap.cc.

   greth.cc reaches the host network only through sis_tap_init and
   sis_tap_write (declared in sis.h, implemented in tap.cc against
   /dev/net/tun).  Defining both here again gives the linker a second
   definition to satisfy greth.o's references with: archive linking is
   lazy, so as long as this translation unit is pulled into the test
   binary before tap.o is ever needed, tap.o never gets extracted from
   libsis.a and no real tap device is opened.  A case can drive the whole
   file through the recorded calls below with no root privilege and no
   host networking.  */

#ifndef SIS_TESTS_FAKETAP_H
#define SIS_TESTS_FAKETAP_H

#include <vector>

namespace sis_tests
{

/* One frame handed to sis_tap_write, in the order it was written.  */
struct faketap_frame
{
  std::vector<unsigned char> bytes;
};

/* How many times sis_tap_init was called, and the mac argument of the
   most recent call.  greth_write only calls sis_tap_init on the first
   CTRL_RE 0->1 transition of the whole process, so only a case that is
   first to make that transition sees this move.  */
extern int faketap_init_calls;
extern unsigned long faketap_init_mac;

/* What sis_tap_init returns.  greth_write ignores the value, but a case
   can still pin it.  */
extern int faketap_init_result;

/* Every frame sis_tap_write was handed, in order.  */
extern std::vector<faketap_frame> faketap_writes;

/* Clears the recorded calls.  Cannot undo a latched sis_tap_init call or
   an armed greth_tx event: both are process-global state inside greth.cc
   with no reset entry point, by design (see greth.cc).  */
void faketap_reset ();

}

#endif
