/* SPDX-License-Identifier: GPL-3.0-or-later */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* The ERC32 MEC error handler, written as a template on an environment policy
   so that its state and dependencies are injected rather than reached through
   globals.  The real board (erc32.cc) instantiates it on an environment that
   forwards to the simulator globals; a test instantiates it on an environment
   holding its own state, so a case drives the error handler in isolation.

   Modelled from the TSC693E Memory Controller User's Manual, Rev D, section
   3.17 (error handler) and the register descriptions for 01F8 00A0 (system
   fault status), 01F8 00A4 (failing address) and 01F8 00B0 (error and reset
   status).  The error mask and the reset-or-halt choice live in the MEC
   control register at 01F8 0000, which the board hands over with
   SetControl.  */

#ifndef SIS_ERC32_ERROR_H
#define SIS_ERC32_ERROR_H

#include "sis.h"

#include <concepts>
#include <cstdio>

namespace erc32
{

/* Error and Reset Status Register bits.  */
enum
{
  kErrIuErrorMode = 1u << 0,   /* IUEM */
  kErrIuHwError = 1u << 1,     /* IUHE */
  kErrIuCmpError = 1u << 2,    /* IUCMP */
  kErrFpuHwError = 1u << 3,    /* FPUHE, the MEC takes no action on it */
  kErrFpuCmpError = 1u << 4,   /* FPUCMP */
  kErrMecHwError = 1u << 5,    /* MECHE, an internal parity error */
  kErrSysAvailable = 1u << 12, /* SYSAV */
  kErrHalted = 1u << 13	       /* HLT */
};

/* Reset cause, ERSR bits 15-14.  A reset initialises every MEC register but
   this one, which is left holding the cause.  */
enum
{
  kResetSystem = 0u << 14,
  kResetSoftware = 1u << 14,
  kResetError = 2u << 14,
  kResetWatchdog = 3u << 14
};

/* The masked hardware error interrupt of Table 5.  A masked error raises this
   instead of halting or resetting the processor.  */
enum
{
  kMaskedErrorLevel = 1
};

/* Data fault types, SFSR bits 6-3.  Only the three the MEC model can report
   are named here.  */
enum
{
  kFaultProtection = 0x3,    /* access to a protected area */
  kFaultUnimplemented = 0x4, /* access to an unimplemented area */
  kFaultMecRegister = 0x6    /* MEC register access violation */
};

/* System fault status register fields.  */
enum
{
  kSfsrFaultShift = 3,		 /* SRFT, the data fault type */
  kSfsrDataFaultValid = 1u << 2, /* SDFV */
  kSfsrAtSupervisor = 1u << 12,	 /* AT, a supervisor rather than user access */
  kSfsrAtStore = 1u << 15,	 /* AT, a store rather than a load */
  kSfsrReset = 0x078		 /* SRFT all ones, everything else clear */
};

/* The ASIs of an IU data access.  An instruction fetch carries a different
   ASI and does not latch the failing address.  */
enum
{
  kAsiUserData = 0xa,
  kAsiSupervisorData = 0xb
};

/* An error source the MEC acts on: its bit in the ERSR, the position of its
   mask bit in the MEC control register, and the name the progress output
   gives it.  The reset-or-halt bit of a source always follows its mask
   bit.  */
struct ErrorSource
{
  uint32 ersr_bit;
  unsigned mask_shift;
  const char *name;
};

/* The error sources in the order the handler examines them.  FPUHE is absent
   because the manual gives the MEC no action for it.  */
inline constexpr ErrorSource kErrorSources[] = {
  { kErrIuErrorMode, 5, "IU in error mode" },
  { kErrIuCmpError, 9, "IU comparison error" },
  { kErrMecHwError, 13, "MEC hardware error" },
};

/* What the error handler requires of its environment.  */
template <class E>
concept ErrorEnv = requires (E e) {
  { e.Verbose () } -> std::convertible_to<bool>;
  { e.Log ("") };
  { e.Irq (0) };
  { e.SysReset () };
  { e.SysHalt () };
  { e.ErrorWriteEnabled () } -> std::convertible_to<bool>;
};

/* The MEC error handler: it latches errors in the error and reset status
   register, and for each unmasked error either halts or resets the processor
   as the MEC control register selects.  A masked error becomes an interrupt
   instead.  It also owns the system fault status and failing address
   registers, which record the cause of a memory exception.  */
template <ErrorEnv Env> class ErrorHandler
{
public:
  explicit ErrorHandler (Env &env) : env_ (env) { Reset (); }

  /* The reset state: no error latched, no fault recorded, and the fault type
     field all ones.  */
  void
  Reset ()
  {
    ersr_ = 0;
    sfsr_ = kSfsrReset;
    ffar_ = 0;
  }

  uint32
  ersr () const
  {
    return ersr_;
  }
  uint32
  sfsr () const
  {
    return sfsr_;
  }
  uint32
  ffar () const
  {
    return ffar_;
  }

  /* The error mask and the reset-or-halt choice are fields of the MEC control
     register, so the board hands the register over whenever it changes.  */
  void
  SetControl (uint32 mcr)
  {
    mcr_ = mcr;
  }

  /* The IU signalled error mode.  */
  void
  IuErrorMode ()
  {
    ersr_ |= kErrIuErrorMode;
    Decode ();
  }

  /* A parity error on an internal MEC register.  */
  void
  MecHwError ()
  {
    ersr_ |= kErrMecHwError;
    Decode ();
  }

  /* Record why the processor was reset.  The caller has just reset the rest
     of the MEC, and this register survives it holding the cause.  */
  void
  SetResetCause (uint32 cause)
  {
    ersr_ = cause;
  }

  /* Writing the status register clears it, whatever the data.  The reserved
     bits are an error.  */
  void
  WriteSfsr (uint32 data)
  {
    if (data & 0xffff0880u)
      MecHwError ();
    sfsr_ = kSfsrReset;
  }

  /* The error bits are writable for fault injection, which the error write
     enable bit of the test control register arms.  */
  void
  WriteErsr (uint32 data)
  {
    if (env_.ErrorWriteEnabled () && (data & 0xffffefc0u))
      MecHwError ();
    ersr_ = data & 0x103fu;
  }

  /* Latch a memory exception.  Only an IU data access updates the registers;
     an instruction fetch does not latch its address.  */
  void
  SetFault (uint32 fault, uint32 addr, uint32 asi, uint32 read)
  {
    if (asi != kAsiUserData && asi != kAsiSupervisorData)
      return;

    ffar_ = addr;
    sfsr_ = (fault << kSfsrFaultShift) | kSfsrDataFaultValid;
    if (!read)
      sfsr_ |= kSfsrAtStore;
    if (asi == kAsiSupervisorData)
      sfsr_ |= kSfsrAtSupervisor;
  }

private:
  /* Act on every latched error.  An unmasked error halts or resets the
     processor; a masked one raises the masked hardware error interrupt.  A
     reset clears the register, so a later source in the same pass sees the
     cleared state.  */
  void
  Decode ()
  {
    for (const ErrorSource &src : kErrorSources)
      {
	if (!(ersr_ & src.ersr_bit))
	  continue;

	if (mcr_ & (1u << src.mask_shift))
	  env_.Irq (kMaskedErrorLevel);
	else if (mcr_ & (2u << src.mask_shift))
	  {
	    env_.SysReset ();
	    ersr_ = kResetError;
	    Report ("reset", src.name);
	  }
	else
	  {
	    env_.SysHalt ();
	    ersr_ |= kErrHalted;
	    Report ("halt", src.name);
	  }
      }
  }

  void
  Report (const char *action, const char *name)
  {
    if (env_.Verbose ())
      {
	char msg[64];
	snprintf (msg, sizeof msg, "Error manager %s - %s\n", action, name);
	env_.Log (msg);
      }
  }

  Env &env_;

  uint32 ersr_;
  uint32 sfsr_;
  uint32 ffar_;
  uint32 mcr_ = 0;
};

} // namespace erc32

#endif
