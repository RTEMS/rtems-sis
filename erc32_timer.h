/* SPDX-License-Identifier: GPL-3.0-or-later */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* The ERC32 MEC timers, written as templates on an environment policy so that
   their state and dependencies are injected rather than reached through
   globals.  The real board (erc32.cc) instantiates them on environments that
   forward to the simulator globals; a test instantiates them on environments
   holding their own state, so a case drives a timer in isolation.

   Modelled from the TSC693E Memory Controller User's Manual, Rev D, sections
   3.13 (general purpose and real time clock timers) and 3.14 (watch dog), and
   the register descriptions for 01F8 0060, 01F8 0064 and 01F8 0080 through
   01F8 0098.  */

#ifndef SIS_ERC32_TIMER_H
#define SIS_ERC32_TIMER_H

#include "sis.h"

#include <concepts>
#include <cstdio>

namespace erc32
{

/* Interrupt levels from Table 5 of the manual, trap type and default
   priority assignments.  */
enum
{
  kWatchdogLevel = 15,
  kRtcLevel = 13,
  kGptLevel = 12
};

/* What a MEC timer requires of its environment.  */
template <class E>
concept TimerEnv = requires (E e) {
  { e.Verbose () } -> std::convertible_to<bool>;
  { e.Now () } -> std::convertible_to<uint64>;
  { e.Irq (0) };
  { e.ScheduleTick (uint64 (0)) };
  { e.Log ("") };
};

/* Everything which distinguishes one MEC timer from the other: the width of
   its scaler, its interrupt level, where its field sits in the timer control
   register, and the reset value of its counter reload bit.  */
struct TimerSpec
{
  uint32 scaler_mask;
  int level;
  unsigned ctrl_shift;
  bool reload_at_zero_reset;
  const char *name;
};

/* One MEC timer: a down counting scaler driving a down counting 32 bit
   counter.  The real time clock and the general purpose timer share this
   logic and differ only by their TimerSpec.

   The scaler is modelled as an elapsed time delta rather than as a register
   that counts down every clock, so a tick is scheduled scaler + 1 clocks
   ahead and the scaler's visible value is derived from the time since.  */
template <TimerEnv Env> class Timer
{
public:
  Timer (Env &env, const TimerSpec &spec) : env_ (env), spec_ (spec)
  {
    Reset ();
  }

  /* The reset state: counter, reload and scaler all at their maximum, and
     the timer not running.  The manual states that after system reset the
     timer is not running and must be programmed as required.

     The counter reload bit is the one timer control register bit whose
     reset value is not zero: RTCCR resets set, GCR resets clear.  */
  void
  Reset ()
  {
    counter_ = 0xffffffffu;
    reload_ = 0xffffffffu;
    scaler_ = spec_.scaler_mask;
    scaler_start_ = 0;
    enabled_ = false;
    reload_at_zero_ = spec_.reload_at_zero_reset;
    scaler_enabled_ = false;
  }

  uint32
  counter () const
  {
    return counter_;
  }
  uint32
  reload () const
  {
    return reload_;
  }
  uint32
  scaler () const
  {
    return scaler_;
  }
  bool
  enabled () const
  {
    return enabled_;
  }
  bool
  reload_at_zero () const
  {
    return reload_at_zero_;
  }

  /* The scaler counts down between counter ticks, so while the timer runs
     its visible value is the programmed value less the time since the last
     tick.  A stopped timer reads back what was programmed.  */
  uint32
  ScalerRead ()
  {
    if (enabled_)
      return scaler_ - (uint32) (env_.Now () - scaler_start_);
    return scaler_;
  }

  /* The programmed scaler value, truncated to the timer's scaler width.  */
  void
  SetScaler (uint32 val)
  {
    scaler_ = val & spec_.scaler_mask;
  }

  void
  SetReload (uint32 val)
  {
    reload_ = val;
  }

  /* The timer control register carries both timers.  Relative to this
     timer's field: bit 0 reloads the counter at zero, bit 1 loads the
     counter from its programmed value, bit 2 enables counting.  The scaler
     load bit, bit 3, has no effect here: the prescaler is an elapsed time
     delta, so there is no scaler register to reload.  */
  void
  WriteControl (uint32 val)
  {
    uint32 bits = val >> spec_.ctrl_shift;

    reload_at_zero_ = (bits & 1) != 0;
    if (bits & 2)
      counter_ = reload_;
    scaler_enabled_ = (bits & 4) != 0;
    if (scaler_enabled_ && !enabled_)
      Start ();
  }

  /* Arm the timer and start the scaler.  */
  void
  Start ()
  {
    if (env_.Verbose ())
      {
	char msg[48];
	snprintf (msg, sizeof msg, "%s started (period %d)\n\r", spec_.name,
		  scaler_ + 1);
	env_.Log (msg);
      }
    env_.ScheduleTick (scaler_ + 1);
    scaler_start_ = env_.Now ();
    enabled_ = true;
  }

  /* One scaler period has elapsed.  At zero the timer interrupts and either
     reloads or stops; otherwise it counts down.  */
  void
  Tick ()
  {
    if (counter_ == 0)
      {
	env_.Irq (spec_.level);
	if (reload_at_zero_)
	  counter_ = reload_;
	else
	  scaler_enabled_ = false;
      }
    else
      counter_ -= 1;

    if (scaler_enabled_)
      {
	env_.ScheduleTick (scaler_ + 1);
	scaler_start_ = env_.Now ();
	enabled_ = true;
      }
    else
      {
	if (env_.Verbose ())
	  {
	    char msg[32];
	    snprintf (msg, sizeof msg, "%s stopped\n\r", spec_.name);
	    env_.Log (msg);
	  }
	enabled_ = false;
      }
  }

private:
  Env &env_;

  const TimerSpec spec_;

  uint32 counter_;
  uint32 reload_;
  uint32 scaler_;
  uint64 scaler_start_;
  bool enabled_;
  bool reload_at_zero_;
  bool scaler_enabled_;
};

/* What the watchdog requires of its environment.  WatchdogReset is the warm
   reset the MEC issues when the reset timeout elapses unacknowledged.  */
template <class E>
concept WatchdogEnv = requires (E e) {
  { e.Verbose () } -> std::convertible_to<bool>;
  { e.Irq (0) };
  { e.ScheduleTick (uint64 (0)) };
  { e.Log ("") };
  { e.WatchdogReset () };
};

/* The states of Figure 7 of the manual.  The watchdog runs from reset; a
   write to the trap door disables it, but only before it has been programmed
   and before it has elapsed, and a write to the program register re-arms it
   for good.  */
enum class WatchdogState
{
  Init,
  Enabled,
  Disabled,
  Stopped
};

/* The MEC watch dog: a scaler and a counter, an interrupt on timeout, and a
   processor reset if the timeout is not acknowledged within a further reset
   delay.  */
template <WatchdogEnv Env> class Watchdog
{
public:
  explicit Watchdog (Env &env) : env_ (env) { Reset (); }

  /* The reset state of the program register: scaler, counter and reset
     counter all at their maximum.  The manual has the watchdog enabled and
     running from reset, which the board does by calling Start.  */
  void
  Reset ()
  {
    scaler_ = 255;
    counter_ = 0xffff;
    rst_delay_ = 255;
    rston_ = false;
    state_ = WatchdogState::Init;
  }

  uint32
  counter () const
  {
    return counter_;
  }
  uint32
  scaler () const
  {
    return scaler_;
  }
  uint32
  rst_delay () const
  {
    return rst_delay_;
  }
  WatchdogState
  state () const
  {
    return state_;
  }

  void
  Start ()
  {
    env_.ScheduleTick (scaler_ + 1);
    if (env_.Verbose ())
      {
	char msg[64];
	snprintf (msg, sizeof msg,
		  "Watchdog started, scaler = %d, counter = %d\n", scaler_,
		  counter_);
	env_.Log (msg);
      }
  }

  /* The program and timeout acknowledge register: counter in bits 15-0,
     scaler in bits 23-16, reset counter in bits 31-24.  Writing it refreshes
     the watchdog and, once it has been disabled and stopped, starts it
     again.  */
  void
  WriteProgram (uint32 data)
  {
    scaler_ = (data >> 16) & 0x0ff;
    counter_ = data & 0x0ffff;
    rst_delay_ = data >> 24;
    rston_ = false;
    if (state_ == WatchdogState::Stopped)
      Start ();
    state_ = WatchdogState::Enabled;
  }

  /* The trap door: a write disables the watchdog, but only while it is still
     in its reset state.  The manual closes that window on two events, and
     Figure 7 gives the trap door no transition out of either: writing the
     program register, and the watchdog elapsing.  */
  void
  WriteTrapDoor ()
  {
    if (state_ == WatchdogState::Init)
      {
	state_ = WatchdogState::Disabled;
	if (env_.Verbose ())
	  env_.Log ("Watchdog disabled\n");
      }
  }

  /* One scaler period has elapsed.  A disabled watchdog stops here.  At zero
     the watchdog interrupts and starts the reset delay; if that elapses
     unacknowledged, the MEC resets the processor.  */
  void
  Tick ()
  {
    if (state_ == WatchdogState::Disabled)
      {
	state_ = WatchdogState::Stopped;
	return;
      }

    if (counter_)
      {
	counter_--;
	env_.ScheduleTick (scaler_ + 1);
      }
    else if (rston_)
      {
	env_.Log ("Watchdog reset!\n");
	env_.WatchdogReset ();
      }
    else
      {
	env_.Irq (kWatchdogLevel);
	rston_ = true;
	counter_ = rst_delay_;
	/* Having elapsed, the watchdog can no longer be disabled.  */
	state_ = WatchdogState::Enabled;
	env_.ScheduleTick (scaler_ + 1);
      }
  }

private:
  Env &env_;

  uint32 scaler_;
  uint32 counter_;
  uint32 rst_delay_;
  bool rston_;
  WatchdogState state_;
};

} // namespace erc32

#endif
