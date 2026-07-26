/* SPDX-License-Identifier: GPL-3.0-or-later */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* The ERC32 MEC configuration registers, written as a template on an
   environment policy so that their state and dependencies are injected rather
   than reached through globals.  The real board (erc32.cc) instantiates it on
   an environment that forwards to the simulator globals; a test instantiates
   it on an environment holding its own state, so a case drives the
   configuration in isolation.

   These registers describe the machine rather than doing anything: the sizes
   and waitstates of the memory areas, the write protection segments, and the
   modes the MEC control register enables.  The memory access path reads what
   they decode to.

   Modelled from the TSC693E Memory Controller User's Manual, Rev D, and the
   register descriptions for 01F8 0000 (MEC control), 01F8 0004 (software
   reset), 01F8 0008 (power down), 01F8 0010 (memory configuration), 01F8 0014
   (I/O configuration), 01F8 0018 (waitstate configuration) and 01F8 0020
   through 01F8 002C (access protection segments).  */

#ifndef SIS_ERC32_CFG_H
#define SIS_ERC32_CFG_H

#include "sis.h"

#include <concepts>
#include <cstdio>

namespace erc32
{

/* MEC control register bits.  Only the bits the simulator acts on are named;
   the rest are kept and read back.  */
enum
{
  kMcrPowerDown = 1u << 0,	/* PRD, power down mode allowed */
  kMcrSoftwareReset = 1u << 1,	/* SWR, software reset allowed */
  kMcrBlockProtect = 1u << 3,	/* BP, protect inside a segment instead of
				   outside it */
  kMcrHardwareError = 1u << 15, /* reserved in the manual; the simulator uses
				   it to inject a MEC hardware error */
  /* BTO, WDCS, DMAE, DST, UPE, UP and UCS set, the UART scaler at one, and
     every error unmasked and set to halt.  */
  kMcrReset = 0x01b50014u
};

/* Memory configuration register fields.  */
enum
{
  kMemcfgRamSizeShift = 10,	   /* RSIZ */
  kMemcfgPromWrite = 1u << 16,	   /* PWR, PROM write allowed */
  kMemcfgProm40Bit = 1u << 17,	   /* P8, a 40 bit rather than 8 bit PROM */
  kMemcfgPromSizeShift = 18,	   /* PSIZ */
  kMemcfgReserved = 0xc0e08000u,   /* bits 31-30, 23-21 and 15 */
  kMemcfgReset = kMemcfgPromWrite, /* PROM write enabled, both sizes minimal */
  kSizeFieldMask = 7u,

  /* The sizes RSIZ 000 and PSIZ 000 select; every further step doubles.  */
  kRamSizeMin = 256 * 1024,
  kPromSizeMin = 128 * 1024
};

/* I/O configuration register: bits 7-6 of every unit's field are unused.  */
enum
{
  kIocrReserved = 0xc0c0c0c0u
};

/* Access protection segment registers.  A segment address counts words from
   the start of the RAM, and the two mode bits enable the protection for user
   and for supervisor accesses.  */
enum
{
  kSegments = 2,
  kSegAddressMask = 0x7fffffu, /* SEGBASE and SEGEND, bits 22-0 */
  kSegModeShift = 23,	       /* UE and SE */
  kSegUser = 1u,	       /* UE */
  kSegSupervisor = 2u,	       /* SE */
  kSegBaseReserved = 0xfe000000u,
  kSegEndReserved = 0xff800000u
};

/* The RAM window of the board.  The MEC decodes the RAM size from a register
   but its position is fixed by the board, so it is handed over rather than
   derived.  */
struct MemoryGeometry
{
  uint32 ram_start;
  uint32 ram_end;
  uint32 ram_mask;
};

/* What the configuration registers require of their environment.  */
template <class E>
concept ConfigEnv = requires (E e) {
  { e.Verbose () } -> std::convertible_to<bool>;
  { e.Log ("") };
  { e.ReportError () };
  { e.Rom8 () } -> std::convertible_to<bool>;
  { e.SetErrorMask (uint32 (0)) };
  { e.SoftwareReset () };
  { e.EnterPowerDown () };
  { e.StartPowerDownTiming () };
};

template <ConfigEnv Env> class Config
{
public:
  Config (Env &env, const MemoryGeometry &geom) : env_ (env), geom_ (geom)
  {
    Reset ();
  }

  /* The reset state of the manual: the minimum memory sizes, the maximum
     waitstates, no I/O unit enabled and no segment protected.  */
  void
  Reset ()
  {
    for (int i = 0; i < kSegments; i++)
      seg_base_[i] = seg_end_[i] = seg_mode_[i] = 0;
    mcr_ = kMcrReset;
    iocr_ = 0;
    memcfg_ = kMemcfgReset;
    wcr_ = 0xffffffffu;
    UpdateAccessProtect ();

    DecodeMemcfg ();
    DecodeWcr ();
    DecodeMcr ();
  }

  uint32
  mcr () const
  {
    return mcr_;
  }
  uint32
  memcfg () const
  {
    return memcfg_;
  }
  uint32
  iocr () const
  {
    return iocr_;
  }

  /* The decoded memory map the access path works from.  */
  uint32
  ram_start () const
  {
    return geom_.ram_start;
  }
  uint32
  ram_end () const
  {
    return geom_.ram_end;
  }
  uint32
  ram_mask () const
  {
    return geom_.ram_mask;
  }
  uint32
  ram_size () const
  {
    return ram_size_;
  }
  uint32
  rom_size () const
  {
    return rom_size_;
  }

  uint32
  ram_read_ws () const
  {
    return ram_read_ws_;
  }
  uint32
  ram_write_ws () const
  {
    return ram_write_ws_;
  }
  uint32
  rom_read_ws () const
  {
    return rom_read_ws_;
  }
  uint32
  rom_write_ws () const
  {
    return rom_write_ws_;
  }

  bool
  prom_write () const
  {
    return (memcfg_ & kMemcfgPromWrite) != 0;
  }
  bool
  prom_40bit () const
  {
    return (memcfg_ & kMemcfgProm40Bit) != 0;
  }

  /* The write protection state the access path checks against.  */
  bool
  access_protect () const
  {
    return access_protect_;
  }
  bool
  block_protect () const
  {
    return block_protect_;
  }
  uint32
  seg_base (int seg) const
  {
    return seg_base_[seg];
  }
  uint32
  seg_end (int seg) const
  {
    return seg_end_[seg];
  }
  uint32
  seg_mode (int seg) const
  {
    return seg_mode_[seg];
  }

  /* Whether a RAM write at ADDR is protected.  A segment names a range of
     words counted from the start of the RAM and the modes it applies to.
     Normally everything outside every enabled segment is protected; the block
     protection bit of the control register inverts that, so a write inside a
     segment is the one that faults.  A double word write is caught when
     either of its two words is.

     The caller checks access_protect first, so this stays off the hot path
     while no segment is enabled.  */
  bool
  WriteProtected (uint32 addr, bool supervisor, int sz) const
  {
    if (!access_protect_)
      return false;

    uint32 mode = supervisor ? kSegSupervisor : kSegUser;
    uint32 waddr = (addr & 0x7fffffu) >> 2;
    bool hit[kSegments];

    for (int i = 0; i < kSegments; i++)
      hit[i] = (seg_mode_[i] & mode) && (waddr >= seg_base_[i]) &&
	       ((waddr | (sz == 3)) < seg_end_[i]);

    if (block_protect_)
      return hit[0] || hit[1];
    return !((seg_mode_[0] && hit[0]) || (seg_mode_[1] && hit[1]));
  }

  /* The read-back value of a segment base register: its address and its two
     mode bits.  */
  uint32
  ReadSegmentBase (int seg) const
  {
    return seg_base_[seg] | (seg_mode_[seg] << kSegModeShift);
  }

  void
  WriteMcr (uint32 data)
  {
    mcr_ = data;
    DecodeMcr ();
  }

  void
  WriteMemcfg (uint32 data)
  {
    if (data & kMemcfgReserved)
      env_.ReportError ();
    memcfg_ = data;
    DecodeMemcfg ();
  }

  void
  WriteWcr (uint32 data)
  {
    wcr_ = data;
    DecodeWcr ();
  }

  void
  WriteIocr (uint32 data)
  {
    iocr_ = data;
    if (iocr_ & kIocrReserved)
      env_.ReportError ();
  }

  /* A segment base register carries the start of the segment and the modes
     the protection applies to.  */
  void
  WriteSegmentBase (int seg, uint32 data)
  {
    if (data & kSegBaseReserved)
      env_.ReportError ();
    seg_base_[seg] = data & kSegAddressMask;
    seg_mode_[seg] = (data >> kSegModeShift) & (kSegUser | kSegSupervisor);
    UpdateAccessProtect ();
    if (env_.Verbose () && seg_mode_[seg])
      {
	char msg[80];
	snprintf (
	    msg, sizeof msg,
	    "Segment %d memory protection enabled (0x02%06x - 0x02%06x)\n",
	    seg + 1, seg_base_[seg] << 2, seg_end_[seg] << 2);
	env_.Log (msg);
      }
  }

  /* A segment end register carries the first address past the segment.  */
  void
  WriteSegmentEnd (int seg, uint32 data)
  {
    if (data & kSegEndReserved)
      env_.ReportError ();
    seg_end_[seg] = data & kSegAddressMask;
  }

  /* Writing the software reset register resets the processor, but only when
     the control register allows it.  */
  void
  WriteSoftwareReset ()
  {
    if (!(mcr_ & kMcrSoftwareReset))
      return;
    env_.SoftwareReset ();
    if (env_.Verbose ())
      env_.Log (" Software reset issued\n");
  }

  /* Writing the power down register enters power down mode, but only when the
     control register allows it.  */
  void
  WritePowerDown ()
  {
    if (mcr_ & kMcrPowerDown)
      env_.EnterPowerDown ();
    env_.StartPowerDownTiming ();
  }

  /* The simulator's boot loader allows power down without a register
     write.  */
  void
  EnablePowerDown ()
  {
    mcr_ |= kMcrPowerDown;
  }

private:
  /* The memory configuration register decodes into the sizes of the two
     memory areas.  The PROM width bit is read only and follows the board
     rather than the written data.  */
  void
  DecodeMemcfg ()
  {
    if (env_.Rom8 ())
      memcfg_ &= ~kMemcfgProm40Bit;
    else
      memcfg_ |= kMemcfgProm40Bit;

    ram_size_ = kRamSizeMin
		<< ((memcfg_ >> kMemcfgRamSizeShift) & kSizeFieldMask);
    rom_size_ = kPromSizeMin
		<< ((memcfg_ >> kMemcfgPromSizeShift) & kSizeFieldMask);

    if (env_.Verbose ())
      {
	char msg[80];
	snprintf (msg, sizeof msg,
		  "RAM start: 0x%x, RAM size: %d K, ROM size: %d K\n",
		  geom_.ram_start, ram_size_ >> 10, rom_size_ >> 10);
	env_.Log (msg);
      }
  }

  /* The write protection is on while either segment has a mode enabled.  */
  void
  UpdateAccessProtect ()
  {
    access_protect_ = seg_mode_[0] != 0 || seg_mode_[1] != 0;
  }

  /* The two PROM waitstate fields encode no waitstates twice, at zero and at
     one, so the count is one below the field from there on.  */
  static uint32
  PromWaitstates (uint32 field)
  {
    return field > 0 ? field - 1 : 0;
  }

  /* The waitstate configuration register holds one field per memory area.  An
     8 bit PROM needs four accesses for a word, so its read waitstates are
     four times the decoded value plus the access itself.  */
  void
  DecodeWcr ()
  {
    ram_read_ws_ = wcr_ & 3;
    ram_write_ws_ = (wcr_ >> 2) & 3;
    rom_read_ws_ = PromWaitstates ((wcr_ >> 4) & 0x0f);
    if (env_.Rom8 ())
      rom_read_ws_ = 5 + (4 * rom_read_ws_);
    rom_write_ws_ = PromWaitstates ((wcr_ >> 8) & 0x0f);

    if (env_.Verbose ())
      {
	char msg[96];
	snprintf (msg, sizeof msg,
		  "Waitstates = RAM read: %d, RAM write: %d, ROM read: %d, "
		  "ROM write: %d\n",
		  ram_read_ws_, ram_write_ws_, rom_read_ws_, rom_write_ws_);
	env_.Log (msg);
      }
  }

  /* The control register decodes into the write protection mode and the error
     mask the error handler works from.  */
  void
  DecodeMcr ()
  {
    block_protect_ = (mcr_ & kMcrBlockProtect) != 0;
    env_.SetErrorMask (mcr_);

    if (env_.Verbose () && access_protect_)
      env_.Log ("Memory block write protection enabled\n");
    if (mcr_ & kMcrHardwareError)
      env_.ReportError ();
    if (env_.Verbose () && (mcr_ & kMcrSoftwareReset))
      env_.Log ("Software reset enabled\n");
    if (env_.Verbose () && (mcr_ & kMcrPowerDown))
      env_.Log ("Power-down mode enabled\n");
  }

  Env &env_;

  const MemoryGeometry geom_;

  uint32 mcr_;
  uint32 memcfg_;
  uint32 wcr_;
  uint32 iocr_;

  uint32 seg_base_[kSegments];
  uint32 seg_end_[kSegments];
  uint32 seg_mode_[kSegments];

  uint32 ram_size_;
  uint32 rom_size_;
  uint32 ram_read_ws_;
  uint32 ram_write_ws_;
  uint32 rom_read_ws_;
  uint32 rom_write_ws_;
  bool access_protect_;
  bool block_protect_;
};

} // namespace erc32

#endif
