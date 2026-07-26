/* SPDX-License-Identifier: GPL-3.0-or-later */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* One ERC32 MEC UART channel, written as a template on an environment policy
   so that its state and dependencies are injected rather than reached through
   globals.  The real board (erc32.cc) instantiates it on an environment that
   forwards to a host port; a test instantiates it on an environment holding
   its own byte streams, so a case drives a channel in isolation.

   The MEC has two channels which differ only in their interrupt level and in
   where their fields sit in the shared status register, so both are this one
   template with a different UartSpec.

   Modelled from the TSC693E Memory Controller User's Manual, Rev D, section
   3.15 and the register descriptions for 01F8 00E0, 01F8 00E4 and 01F8 00E8.
   Two deviations, both described in doc/erc32.md: a read of a RX and TX
   register returns the status bits the manual reserves, and the errors the
   status register can report cannot arise in the simulator.  */

#ifndef SIS_ERC32_UART_H
#define SIS_ERC32_UART_H

#include "sis.h"

#include <concepts>
#include <cstring>

namespace erc32
{

/* Interrupt levels from Table 5.  */
enum
{
  kUartALevel = 4,
  kUartBLevel = 5
};

/* Status register fields, relative to the channel's own field.  The manual
   resets both empty bits set, and the simulator holds them there: it has
   nowhere to queue a character that has not gone out yet.  */
enum
{
  kUartDataReady = 1u << 0,    /* DR */
  kUartSendEmpty = 1u << 1,    /* TSE */
  kUartHoldEmpty = 1u << 2,    /* THE */
  kUartFramingError = 1u << 4, /* FE, never set */
  kUartParityError = 1u << 5,  /* PE, never set */
  kUartOverrunError = 1u << 6, /* OE, never set */
  kUartStatusBLevel = 16       /* where channel B's field starts */
};

/* What a read of a RX and TX register reports beside the character.  The
   manual reserves bits 31-8 of that register; these mirror the status bits
   there so a program can read a character and see whether it was valid in
   one access.  */
enum
{
  kUartDataValid = 1u << 8,
  kUartDataSendEmpty = 1u << 9,
  kUartDataHoldEmpty = 1u << 10
};

/* How many bytes a channel buffers in each direction.  */
enum
{
  kUartBufSize = 1024
};

/* Everything which distinguishes one channel from the other.  */
struct UartSpec
{
  int level;
  unsigned status_shift;
  const char *name;
};

/* What a UART channel requires of its environment: a host port to move bytes
   through, and the interrupt controller.  */
template <class E>
concept UartEnv = requires (E e) {
  { e.Irq (0) };
  { e.PortOpen () } -> std::convertible_to<bool>;
  { e.PortRead ((char *) nullptr, 0) } -> std::convertible_to<int>;
  { e.PortWrite ((const char *) nullptr, 0) } -> std::convertible_to<int>;
};

template <UartEnv Env> class Uart
{
public:
  Uart (Env &env, const UartSpec &spec) : env_ (env), spec_ (spec)
  {
    Reset ();
  }

  /* Nothing received and nothing waiting to go out.  */
  void
  Reset ()
  {
    rnum_ = 0;
    rind_ = 0;
    wnum_ = 0;
  }

  /* Read the RX and TX register.  A character is delivered if one is
     buffered or the host has more; with another character behind it the
     channel interrupts, which is what keeps a driver reading.  */
  uint32
  ReadData ()
  {
    if (rind_ >= rnum_ && !Refill ())
      {
	/* Nothing arrived, so the register still holds the last character
	   delivered.  */
	return kUartDataSendEmpty | kUartDataHoldEmpty | Last ();
      }

    if (rind_ + 1 < rnum_)
      env_.Irq (spec_.level);

    return kUartDataValid | kUartDataSendEmpty | kUartDataHoldEmpty |
	   (uint32) rq_[rind_++];
  }

  /* Write the RX and TX register.  The character joins the transmit buffer,
     which is emptied to the host when it fills or when the channel is
     flushed, and the channel interrupts because the transmitter is free
     again at once.  */
  void
  WriteData (uint32 data)
  {
    if (env_.PortOpen ())
      {
	if (wnum_ >= kUartBufSize)
	  Drain ();
	/* A host which took nothing leaves the buffer full, and the
	   character is lost, as it is on a real overrun.  */
	if (wnum_ < kUartBufSize)
	  wbuf_[wnum_++] = (char) data;
      }
    env_.Irq (spec_.level);
  }

  /* This channel's contribution to the shared status register.  Reading it
     takes whatever the host has, so a driver polling the status sees input
     arrive without reading the data register first.  */
  uint32
  StatusBits ()
  {
    uint32 bits = kUartSendEmpty | kUartHoldEmpty;

    if (rind_ < rnum_)
      bits |= kUartDataReady;
    else if (Refill ())
      {
	bits |= kUartDataReady;
	env_.Irq (spec_.level);
      }

    return bits << spec_.status_shift;
  }

  /* Push everything buffered out to the host.  */
  void
  Flush ()
  {
    if (env_.PortOpen ())
      Drain ();
  }

  /* How many characters are waiting to be read, and to be written.  */
  int
  buffered () const
  {
    return rind_ < rnum_ ? rnum_ - rind_ : 0;
  }
  unsigned
  pending () const
  {
    return wnum_;
  }

private:
  /* Take whatever the host has ready.  Returns whether anything arrived.  A
     refill which brings nothing leaves the read index alone, so the last
     character delivered is still there to be read back.  */
  bool
  Refill ()
  {
    int n = env_.PortOpen () ? env_.PortRead ((char *) rq_, kUartBufSize) : 0;

    rnum_ = n > 0 ? n : 0;
    if (n <= 0)
      return false;

    rind_ = 0;
    return true;
  }

  /* The last character delivered, which the receive register holds until the
     next one arrives.  */
  uint32
  Last () const
  {
    return rind_ > 0 ? (uint32) rq_[rind_ - 1] : 0;
  }

  /* Hand the buffer to the host, from where the last write stopped.  A host
     which takes nothing more keeps what is left for the next attempt, rather
     than being asked again forever.  */
  void
  Drain ()
  {
    unsigned sent = 0;

    while (sent < wnum_)
      {
	int n = env_.PortWrite (wbuf_ + sent, (int) (wnum_ - sent));
	if (n <= 0)
	  break;
	sent += (unsigned) n;
      }

    wnum_ -= sent;
    if (wnum_ > 0)
      memmove (wbuf_, wbuf_ + sent, wnum_);
  }

  Env &env_;

  const UartSpec spec_;

  unsigned char rq_[kUartBufSize];
  int rnum_;
  int rind_;

  char wbuf_[kUartBufSize];
  unsigned wnum_;
};

} // namespace erc32

#endif
