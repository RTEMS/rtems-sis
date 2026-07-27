/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* A recording double for tap.cc.  See faketap.h.  */

#include "faketap.h"

namespace sis_tests
{

int faketap_init_calls = 0;
unsigned long faketap_init_mac = 0;
int faketap_init_result = 1;
std::vector<faketap_frame> faketap_writes;

void
faketap_reset ()
{
  faketap_init_calls = 0;
  faketap_init_mac = 0;
  faketap_writes.clear ();
}

}

int
sis_tap_init (long unsigned emac)
{
  sis_tests::faketap_init_calls++;
  sis_tests::faketap_init_mac = emac;
  return sis_tests::faketap_init_result;
}

int
sis_tap_write (unsigned char *buffer, int len)
{
  sis_tests::faketap_frame frame;

  frame.bytes.assign (buffer, buffer + len);
  sis_tests::faketap_writes.push_back (frame);
  return len;
}
