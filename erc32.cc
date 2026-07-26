/* SPDX-License-Identifier: GPL-3.0-or-later */
/* This file is part of SIS (SPARC instruction simulator)

   Copyright (C) 1995-2017 Free Software Foundation, Inc.
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

#define ROM_START 0
#define ROM_SIZE  0x01000000
#define RAM_START 0x02000000
#define RAM_SIZE  0x01000000

#include "config.h"
#include <errno.h>
#ifndef _WIN32
#include <sys/types.h>
#endif
#include <stdio.h>
#include <string.h>
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif
#ifndef _WIN32
#include <sys/file.h>
#endif
#ifndef _WIN32
#include <unistd.h>
#endif
#include "sis.h"
#include "sisio.h"

#include "erc32_cfg.h"
#include "erc32_error.h"
#include "erc32_mec.h"
#include "erc32_timer.h"

/* MEC registers */
#define MEC_START 0x01f80000
#define MEC_END	  0x01f80100

/* Memory exception waitstates */
#define MEM_EX_WS 1

/* ERC32 always adds one waitstate during RAM std */
#define STD_WS 1

#define MEC_WS 0 /* Waitstates per MEC access (0 ws) */
#define MOK    0

/* MEC register addresses */

#define MEC_MCR	   0x000
#define MEC_SFR	   0x004
#define MEC_PWDR   0x008
#define MEC_MEMCFG 0x010
#define MEC_IOCR   0x014
#define MEC_WCR	   0x018

#define MEC_SSA1	0x020
#define MEC_SEA1	0x024
#define MEC_SSA2	0x028
#define MEC_SEA2	0x02C
#define MEC_ISR		0x044
#define MEC_IPR		0x048
#define MEC_IMR		0x04C
#define MEC_ICR		0x050
#define MEC_IFR		0x054
#define MEC_WDOG	0x060
#define MEC_TRAPD	0x064
#define MEC_RTC_COUNTER 0x080
#define MEC_RTC_RELOAD	0x080
#define MEC_RTC_SCALER	0x084
#define MEC_GPT_COUNTER 0x088
#define MEC_GPT_RELOAD	0x088
#define MEC_GPT_SCALER	0x08C
#define MEC_TIMER_CTRL	0x098
#define MEC_SFSR	0x0A0
#define MEC_FFAR	0x0A4
#define MEC_ERSR	0x0B0
#define MEC_TCR		0x0D0

#define MEC_UARTA     0x0E0
#define MEC_UARTB     0x0E4
#define MEC_UART_CTRL 0x0E8
#define SIM_LOAD      0x0F0

/* Size of UART buffers (bytes) */
#define UARTBUF 1024

/* Number of simulator ticks between flushing the UARTS. 	 */
/* For good performance, keep above 1000			 */
#define UART_FLUSH_TIME 3000

/* New uart defines */
#define UART_TX_TIME 1000
#define UART_RX_TIME 1000
#define UARTA_DR     0x1
#define UARTA_SRE    0x2
#define UARTA_HRE    0x4
#define UARTA_OR     0x40
#define UARTA_CLR    0x80
#define UARTB_DR     0x10000
#define UARTB_SRE    0x20000
#define UARTB_HRE    0x40000
#define UARTB_OR     0x400000
#define UARTB_CLR    0x800000

#define UART_DR	 0x100
#define UART_TSE 0x200
#define UART_THE 0x400

/* MEC registers */

static char fname[256];
static uint32 posted_irq;

/* UART support variables */

static int32 fd1, fd2; /* file descriptor for input file */
static int32 Ucontrol; /* UART status register */
static unsigned char aq[UARTBUF], bq[UARTBUF];
static int32 anum, aind = 0;
static int32 bnum, bind = 0;
static char wbufa[UARTBUF], wbufb[UARTBUF];
static unsigned wnuma;
static unsigned wnumb;
static FILE *f1in, *f1out, *f2in, *f2out;
#ifdef HAVE_TERMIOS_H
static struct termios ioc1, ioc2, iocold1, iocold2;
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0
#endif

static int f1open = 0, f2open = 0;

static char uarta_sreg, uarta_hreg, uartb_sreg, uartb_hreg;
static uint32 uart_stat_reg;
static uint32 uarta_data, uartb_data;

/* Forward declarations */

static void mecparerror (void);
static void close_port (void);
static void mec_reset (void);
static void mec_intack (int32 level, int32 cpu);
static void mec_irq (int32 level);
static void set_sfsr (uint32 fault, uint32 addr, uint32 asi, uint32 read);
static int32 mec_read (uint32 addr, uint32 asi, uint32 *data);
static int mec_write (uint32 addr, uint32 data);
static void port_init (void);
static uint32 read_uart (uint32 addr);
static void write_uart (uint32 addr, uint32 data);
static void flush_uart (void);
#ifndef FAST_UART
static void uarta_tx (int32);
static void uartb_tx (int32);
static void uart_rx (int32 arg);
#endif
static void uart_intr (int32 arg);
static void uart_irq_start (void);
static void wdog_intr (int32 arg);
static void rtc_intr (int32 arg);
static void gpt_intr (int32 arg);
static char *get_mem_ptr (uint32 addr, uint32 size);
static void store_bytes (char *mem, uint32 waddr, uint32 *data, int sz,
			 int32 *ws);

/* The environment every MEC subsystem runs against in the real board: its
   dependencies are the simulator globals.  The methods are declared here and
   defined after the subsystems, because several of them reach back into a
   subsystem which cannot be constructed until this class is complete.  */
struct RealEnv
{
  bool Verbose ();
  int &Irl ();
  void ReportError ();
  void Irq (int level);
  void SysReset ();
  void SysHalt ();
  bool ErrorWriteEnabled ();
  bool Rom8 ();
  void SetErrorMask (uint32 mcr);
  void SoftwareReset ();
  void EnterPowerDown ();
  void StartPowerDownTiming ();
  void Log (const char *msg);
};

/* The RAM window of the ERC32 board.  The MEC decodes its size from a
   register, but not its position.  */
static constexpr erc32::MemoryGeometry ram_geometry = { .ram_start = RAM_START,
							.ram_end = RAM_END,
							.ram_mask = RAM_MASK };

static RealEnv real_env;
static erc32::Mec<RealEnv> mec (real_env);
static erc32::ErrorHandler<RealEnv> error_handler (real_env);
static erc32::Config<RealEnv> cfg (real_env, ram_geometry);

bool
RealEnv::Verbose ()
{
  return sis_verbose != 0;
}

int &
RealEnv::Irl ()
{
  return ext_irl[0];
}

void
RealEnv::ReportError ()
{
  mecparerror ();
}

void
RealEnv::Irq (int level)
{
  mec_irq (level);
}

void
RealEnv::SysReset ()
{
  sys_reset ();
}

void
RealEnv::SysHalt ()
{
  sys_halt ();
}

void
RealEnv::Log (const char *msg)
{
  fputs (msg, stdout);
}

bool
RealEnv::ErrorWriteEnabled ()
{
  return (mec.tcr () & 0x100000u) != 0;
}

bool
RealEnv::Rom8 ()
{
  return rom8 != 0;
}

void
RealEnv::SetErrorMask (uint32 mcr)
{
  error_handler.SetControl (mcr);
}

void
RealEnv::SoftwareReset ()
{
  sys_reset ();
  error_handler.SetResetCause (erc32::kResetSoftware);
}

void
RealEnv::EnterPowerDown ()
{
  sregs->pwd_mode = 1;
}

void
RealEnv::StartPowerDownTiming ()
{
  sregs->pwdstart = sregs->simtime;
}

/* The environment a timer runs against in the real board.  THUNK is the
   event queue callback which ticks that timer, so the template itself never
   names a C function.  */
template <void (*Thunk) (int32)> struct RealTimerEnv
{
  bool
  Verbose ()
  {
    return sis_verbose != 0;
  }
  uint64
  Now ()
  {
    return now ();
  }
  void
  Irq (int level)
  {
    mec_irq (level);
  }
  void
  ScheduleTick (uint64 delta)
  {
    event (Thunk, 0, delta);
  }
  void
  Log (const char *msg)
  {
    fputs (msg, stdout);
  }
};

/* The watchdog additionally issues the MEC's warm reset.  */
struct RealWatchdogEnv : public RealTimerEnv<wdog_intr>
{
  void
  WatchdogReset ()
  {
    sys_reset ();
    error_handler.SetResetCause (erc32::kResetWatchdog);
  }
};

static RealTimerEnv<rtc_intr> rtc_env;
static RealTimerEnv<gpt_intr> gpt_env;
static RealWatchdogEnv wdog_env;

static constexpr erc32::TimerSpec rtc_spec = { .scaler_mask = 0x0ff,
					       .level = erc32::kRtcLevel,
					       .ctrl_shift = 8,
					       .reload_at_zero_reset = true,
					       .name = "RTC" };

static constexpr erc32::TimerSpec gpt_spec = { .scaler_mask = 0x0ffff,
					       .level = erc32::kGptLevel,
					       .ctrl_shift = 0,
					       .reload_at_zero_reset = false,
					       .name = "GPT" };

static erc32::Timer<RealTimerEnv<rtc_intr>> rtc (rtc_env, rtc_spec);
static erc32::Timer<RealTimerEnv<gpt_intr>> gpt (gpt_env, gpt_spec);
static erc32::Watchdog<RealWatchdogEnv> wdog (wdog_env);

/* One-time init */

static void
init_sim ()
{
  port_init ();
  ebase.ramstart = RAM_START;
}

/* Power-on reset init */

static void
reset ()
{
  mec_reset ();
  uart_irq_start ();
  wdog.Start ();
  sregs[0].intack = mec_intack;
}

/* The error handler.  The logic is in erc32_error.h; these are the board's
   entry points into it.  */

static void
mecparerror ()
{
  error_handler.MecHwError ();
}

/* IU error mode manager */

static void
error_mode (uint32 pc)
{
  error_handler.IuErrorMode ();
}

/* Flush ports when simulator stops */

static void
sim_halt ()
{
#ifdef FAST_UART
  flush_uart ();
#endif
}

static void
close_port ()
{
  if (f1open && f1in != stdin)
    fclose (f1in);
  if (f2open && f2in != stdin)
    fclose (f2in);
}

static void
exit_sim ()
{
  close_port ();
}

static void
mec_reset ()
{
  error_handler.Reset ();
  mec.ResetInterrupts ();
  cfg.Reset ();

  posted_irq = 0;
  wnuma = wnumb = 0;
  anum = aind = bnum = bind = 0;

  uart_stat_reg = UARTA_SRE | UARTA_HRE | UARTB_SRE | UARTB_HRE;
  uarta_data = uartb_data = UART_THE | UART_TSE;

  rtc.Reset ();
  gpt.Reset ();
  wdog.Reset ();
}

static void
mec_intack (int32 level, int cpu)
{
  (void) cpu;
  mec.Intack (level);
}

static void
mec_irq (int32 level)
{
  mec.Irq (level);
}

static void
set_sfsr (uint32 fault, uint32 addr, uint32 asi, uint32 read)
{
  error_handler.SetFault (fault, addr, asi, read);
}

static int32
mec_read (uint32 addr, uint32 asi, uint32 *data)
{

  switch (addr & 0x0ff)
    {

    case MEC_MCR: /* 0x00 */
      *data = cfg.mcr ();
      break;

    case MEC_MEMCFG: /* 0x10 */
      *data = cfg.memcfg ();
      break;

    case MEC_IOCR: /* 0x14 */
      *data = cfg.iocr ();
      break;

    case MEC_SSA1: /* 0x20 */
      *data = cfg.ReadSegmentBase (0);
      break;
    case MEC_SEA1: /* 0x24 */
      *data = cfg.seg_end (0);
      break;
    case MEC_SSA2: /* 0x28 */
      *data = cfg.ReadSegmentBase (1);
      break;
    case MEC_SEA2: /* 0x2c */
      *data = cfg.seg_end (1);
      break;

    case MEC_ISR: /* 0x44 */
      *data = mec.isr ();
      break;

    case MEC_IPR: /* 0x48 */
      *data = mec.ipr ();
      break;

    case MEC_IMR: /* 0x4c */
      *data = mec.imr ();
      break;

    case MEC_IFR: /* 0x54 */
      *data = mec.ifr ();
      break;

    case MEC_RTC_COUNTER: /* 0x80 */
      *data = rtc.counter ();
      break;
    case MEC_RTC_SCALER: /* 0x84 */
      *data = rtc.ScalerRead ();
      break;

    case MEC_GPT_COUNTER: /* 0x88 */
      *data = gpt.counter ();
      break;

    case MEC_GPT_SCALER: /* 0x8c */
      *data = gpt.ScalerRead ();
      break;

    case MEC_SFSR: /* 0xA0 */
      *data = error_handler.sfsr ();
      break;

    case MEC_FFAR: /* 0xA4 */
      *data = error_handler.ffar ();
      break;

    case MEC_ERSR: /* 0xB0 */
      *data = error_handler.ersr ();
      break;

    case MEC_TCR: /* 0xD0 */
      *data = mec.tcr ();
      break;

    case MEC_UARTA: /* 0xE0 */
    case MEC_UARTB: /* 0xE4 */
      if (asi != 0xb)
	{
	  set_sfsr (erc32::kFaultMecRegister, addr, asi, 1);
	  return 1;
	}
      *data = read_uart (addr);
      break;

    case MEC_UART_CTRL: /* 0xE8 */

      *data = read_uart (addr);
      break;

    default:
      set_sfsr (erc32::kFaultMecRegister, addr, asi, 1);
      return 1;
      break;
    }
  return MOK;
}

static int
mec_write (uint32 addr, uint32 data)
{
  if (sis_verbose > 1)
    printf ("MEC write a: %08x, d: %08x\n", addr, data);
  switch (addr & 0x0ff)
    {

    case MEC_MCR: /* 0x00 */
      cfg.WriteMcr (data);
      break;

    case MEC_SFR: /* 0x04 */
      cfg.WriteSoftwareReset ();
      break;

    case MEC_IOCR: /* 0x14 */
      cfg.WriteIocr (data);
      break;

    case MEC_SSA1: /* 0x20 */
      cfg.WriteSegmentBase (0, data);
      break;
    case MEC_SEA1: /* 0x24 */
      cfg.WriteSegmentEnd (0, data);
      break;
    case MEC_SSA2: /* 0x28 */
      cfg.WriteSegmentBase (1, data);
      break;
    case MEC_SEA2: /* 0x2c */
      cfg.WriteSegmentEnd (1, data);
      break;

    case MEC_UARTA:
    case MEC_UARTB:
      if (data & 0xFFFFFF00)
	mecparerror ();
    case MEC_UART_CTRL:
      if (data & 0xFF00FF00)
	mecparerror ();
      write_uart (addr, data);
      break;

    case MEC_GPT_RELOAD:
      gpt.SetReload (data);
      break;

    case MEC_GPT_SCALER:
      if (data & 0xFFFF0000)
	mecparerror ();
      gpt.SetScaler (data);
      break;

    case MEC_TIMER_CTRL:
      if (data & 0xFFFFF0F0)
	mecparerror ();
      rtc.WriteControl (data);
      gpt.WriteControl (data);
      break;

    case MEC_RTC_RELOAD:
      rtc.SetReload (data);
      break;

    case MEC_RTC_SCALER:
      if (data & 0xFFFFFF00)
	mecparerror ();
      rtc.SetScaler (data);
      break;

    case MEC_SFSR: /* 0xA0 */
      error_handler.WriteSfsr (data);
      break;

    case MEC_ISR:
      mec.WriteIsr (data);
      break;

    case MEC_IMR: /* 0x4c */
      mec.WriteImr (data);
      break;

    case MEC_ICR: /* 0x50 */
      mec.WriteIcr (data);
      break;

    case MEC_IFR: /* 0x54 */
      mec.WriteIfr (data);
      break;

    case MEC_MEMCFG: /* 0x10 */
      cfg.WriteMemcfg (data);
      break;

    case MEC_WCR: /* 0x18 */
      cfg.WriteWcr (data);
      break;

    case MEC_ERSR: /* 0xB0 */
      error_handler.WriteErsr (data);
      break;

    case MEC_TCR: /* 0xD0 */
      mec.WriteTcr (data);
      break;

    case MEC_WDOG: /* 0x60 */
      wdog.WriteProgram (data);
      break;

    case MEC_TRAPD: /* 0x64 */
      wdog.WriteTrapDoor ();
      break;

    case MEC_PWDR: /* 0x08 */
      cfg.WritePowerDown ();
      break;

    default:
      set_sfsr (erc32::kFaultMecRegister, addr, 0xb, 0);
      return 1;
      break;
    }
  return MOK;
}

/* MEC UARTS */

static int ifd1 = -1, ifd2 = -1, ofd1 = -1, ofd2 = -1;

static void
init_stdio ()
{
  if (dumbio)
    return; /* do nothing */
#ifdef HAVE_TERMIOS_H
  if (ifd1 == 0 && f1open)
    {
      tcsetattr (0, TCSANOW, &ioc1);
      tcflush (ifd1, TCIFLUSH);
    }
  if (ifd2 == 0 && f1open)
    {
      tcsetattr (0, TCSANOW, &ioc2);
      tcflush (ifd2, TCIFLUSH);
    }
#endif
}

static void
restore_stdio ()
{
  if (dumbio)
    return; /* do nothing */
#ifdef HAVE_TERMIOS_H
  if (ifd1 == 0 && f1open && tty_setup)
    tcsetattr (0, TCSANOW, &iocold1);
  if (ifd2 == 0 && f2open && tty_setup)
    tcsetattr (0, TCSANOW, &iocold2);
#endif
}

#define DO_STDIO_READ(_fd_, _buf_, _len_)                                     \
  (dumbio || nouartrx ? (0) : sis_uart_read (_fd_, (char *) (_buf_), _len_))

static void
port_init ()
{

  if (uben)
    {
      f2in = stdin;
      f1in = NULL;
      f2out = stdout;
      f1out = NULL;
    }
  else
    {
      f1in = stdin;
      f2in = NULL;
      f1out = stdout;
      f2out = NULL;
    }
  if (uart_dev1[0] != 0)
    if ((fd1 = sis_uart_open (uart_dev1)) < 0)
      {
	printf ("Warning, couldn't open output device %s\n", uart_dev1);
      }
    else
      {
	if (sis_verbose)
	  printf ("serial port A on %s\n", uart_dev1);
	f1in = f1out = fdopen (fd1, "r+");
	setbuf (f1out, NULL);
	f1open = 1;
      }
  if (f1in)
    ifd1 = fileno (f1in);
  if (ifd1 == 0)
    {
      if (sis_verbose)
	printf ("serial port A on stdin/stdout\n");
      if (!dumbio)
	{
#ifdef HAVE_TERMIOS_H
	  tcgetattr (ifd1, &ioc1);
	  if (tty_setup)
	    {
	      iocold1 = ioc1;
	      ioc1.c_lflag &= ~(ICANON | ECHO);
	      ioc1.c_cc[VMIN] = 0;
	      ioc1.c_cc[VTIME] = 0;
	    }
#endif
	}
      f1open = 1;
    }

  if (f1out)
    {
      ofd1 = fileno (f1out);
      if (!dumbio && tty_setup && ofd1 == 1)
	setbuf (f1out, NULL);
    }

  if (uart_dev2[0] != 0)
    if ((fd2 = sis_uart_open (uart_dev2)) < 0)
      {
	printf ("Warning, couldn't open output device %s\n", uart_dev2);
      }
    else
      {
	if (sis_verbose)
	  printf ("serial port B on %s\n", uart_dev2);
	f2in = f2out = fdopen (fd2, "r+");
	setbuf (f2out, NULL);
	f2open = 1;
      }
  if (f2in)
    ifd2 = fileno (f2in);
  if (ifd2 == 0)
    {
      if (sis_verbose)
	printf ("serial port B on stdin/stdout\n");
      if (!dumbio)
	{
#ifdef HAVE_TERMIOS_H
	  tcgetattr (ifd2, &ioc2);
	  if (tty_setup)
	    {
	      iocold2 = ioc2;
	      ioc2.c_lflag &= ~(ICANON | ECHO);
	      ioc2.c_cc[VMIN] = 0;
	      ioc2.c_cc[VTIME] = 0;
	    }
#endif
	}
      f2open = 1;
    }

  if (f2out)
    {
      ofd2 = fileno (f2out);
      if (!dumbio && tty_setup && ofd2 == 1)
	setbuf (f2out, NULL);
    }

  wnuma = wnumb = 0;
}

static uint32
read_uart (uint32 addr)
{

  unsigned tmp;

  tmp = 0;
  switch (addr & 0xff)
    {

    case 0xE0: /* UART 1 */
#ifndef _WIN32
#ifdef FAST_UART

      if (aind < anum)
	{
	  if ((aind + 1) < anum)
	    mec_irq (4);
	  return (0x700 | (uint32) aq[aind++]);
	}
      else
	{
	  if (f1open)
	    anum = DO_STDIO_READ (ifd1, aq, UARTBUF);
	  else
	    anum = 0;
	  if (anum > 0)
	    {
	      aind = 0;
	      if ((aind + 1) < anum)
		mec_irq (4);
	      return (0x700 | (uint32) aq[aind++]);
	    }
	  else
	    {
	      return (0x600 | (uint32) aq[aind]);
	    }
	}
#else
      tmp = uarta_data;
      uarta_data &= ~UART_DR;
      uart_stat_reg &= ~UARTA_DR;
      return tmp;
#endif
#else
      return 0;
#endif
      break;

    case 0xE4: /* UART 2 */
#ifndef _WIN32
#ifdef FAST_UART
      if (bind < bnum)
	{
	  if ((bind + 1) < bnum)
	    mec_irq (5);
	  return (0x700 | (uint32) bq[bind++]);
	}
      else
	{
	  if (f2open)
	    bnum = DO_STDIO_READ (ifd2, bq, UARTBUF);
	  else
	    bnum = 0;
	  if (bnum > 0)
	    {
	      bind = 0;
	      if ((bind + 1) < bnum)
		mec_irq (5);
	      return (0x700 | (uint32) bq[bind++]);
	    }
	  else
	    {
	      return (0x600 | (uint32) bq[bind]);
	    }
	}
#else
      tmp = uartb_data;
      uartb_data &= ~UART_DR;
      uart_stat_reg &= ~UARTB_DR;
      return tmp;
#endif
#else
      return 0;
#endif
      break;

    case 0xE8: /* UART status register  */
#ifndef _WIN32
#ifdef FAST_UART

      Ucontrol = 0;
      if (aind < anum)
	{
	  Ucontrol |= 0x00000001;
	}
      else
	{
	  if (f1open)
	    anum = DO_STDIO_READ (ifd1, aq, UARTBUF);
	  else
	    anum = 0;
	  if (anum > 0)
	    {
	      Ucontrol |= 0x00000001;
	      aind = 0;
	      mec_irq (4);
	    }
	}
      if (bind < bnum)
	{
	  Ucontrol |= 0x00010000;
	}
      else
	{
	  if (f2open)
	    bnum = DO_STDIO_READ (ifd2, bq, UARTBUF);
	  else
	    bnum = 0;
	  if (bnum > 0)
	    {
	      Ucontrol |= 0x00010000;
	      bind = 0;
	      mec_irq (5);
	    }
	}

      Ucontrol |= 0x00060006;
      return Ucontrol;
#else
      return uart_stat_reg;
#endif
#else
      return 0x00060006;
#endif
      break;
    default:
      if (sis_verbose)
	printf ("Read from unimplemented MEC register (%x)\n", addr);
    }
  return 0;
}

static void
write_uart (uint32 addr, uint32 data)
{
  unsigned char c;

  c = (unsigned char) data;
  switch (addr & 0xff)
    {

    case 0xE0: /* UART A */
#ifdef FAST_UART
      if (f1open)
	{
	  if (wnuma < UARTBUF)
	    wbufa[wnuma++] = c;
	  else
	    {
	      while (wnuma)
		{
		  wnuma -= fwrite (wbufa, 1, wnuma, f1out);
		}
	      wbufa[wnuma++] = c;
	    }
	}
      mec_irq (4);
#else
      if (uart_stat_reg & UARTA_SRE)
	{
	  uarta_sreg = c;
	  uart_stat_reg &= ~UARTA_SRE;
	  event (uarta_tx, 0, UART_TX_TIME);
	}
      else
	{
	  uarta_hreg = c;
	  uart_stat_reg &= ~UARTA_HRE;
	}
#endif
      break;

    case 0xE4: /* UART B */
#ifdef FAST_UART
      if (f2open)
	{
	  if (wnumb < UARTBUF)
	    wbufb[wnumb++] = c;
	  else
	    {
	      while (wnumb)
		{
		  wnumb -= fwrite (wbufb, 1, wnumb, f2out);
		}
	      wbufb[wnumb++] = c;
	    }
	}
      mec_irq (5);
#else
      if (uart_stat_reg & UARTB_SRE)
	{
	  uartb_sreg = c;
	  uart_stat_reg &= ~UARTB_SRE;
	  event (uartb_tx, 0, UART_TX_TIME);
	}
      else
	{
	  uartb_hreg = c;
	  uart_stat_reg &= ~UARTB_HRE;
	}
#endif
      break;
    case 0xE8: /* UART status register */
#ifndef FAST_UART
      if (data & UARTA_CLR)
	{
	  uart_stat_reg &= 0xFFFF0000;
	  uart_stat_reg |= UARTA_SRE | UARTA_HRE;
	}
      if (data & UARTB_CLR)
	{
	  uart_stat_reg &= 0x0000FFFF;
	  uart_stat_reg |= UARTB_SRE | UARTB_HRE;
	}
#endif
      break;
    default:
      if (sis_verbose)
	printf ("Write to unimplemented MEC register (%x)\n", addr);
    }
}

static void
flush_uart ()
{
  while (wnuma && f1open)
    {
      wnuma -= fwrite (wbufa, 1, wnuma, f1out);
    }
  while (wnumb && f2open)
    {
      wnumb -= fwrite (wbufb, 1, wnumb, f2out);
    }
}

/* The interrupt-driven transmit and receive handlers are used only when the
   UART is not in fast mode.  Fast mode moves the data in read_uart,
   write_uart and uart_intr, so these are left out of the build.  */
#ifndef FAST_UART
static void
uarta_tx (int32 arg)
{
  (void) arg;
  while (f1open)
    {
      while (fwrite (&uarta_sreg, 1, 1, f1out) != 1)
	continue;
    }
  if (uart_stat_reg & UARTA_HRE)
    {
      uart_stat_reg |= UARTA_SRE;
    }
  else
    {
      uarta_sreg = uarta_hreg;
      uart_stat_reg |= UARTA_HRE;
      event (uarta_tx, 0, UART_TX_TIME);
    }
  mec_irq (4);
}

static void
uartb_tx (int32 arg)
{
  (void) arg;
  while (f2open)
    {
      while (fwrite (&uartb_sreg, 1, 1, f2out) != 1)
	continue;
    }
  if (uart_stat_reg & UARTB_HRE)
    {
      uart_stat_reg |= UARTB_SRE;
    }
  else
    {
      uartb_sreg = uartb_hreg;
      uart_stat_reg |= UARTB_HRE;
      event (uartb_tx, 0, UART_TX_TIME);
    }
  mec_irq (5);
}

static void
uart_rx (int32 arg)
{
  int32 rsize;
  char rxd;

  rsize = 0;
  if (f1open)
    rsize = DO_STDIO_READ (ifd1, &rxd, 1);
  else
    rsize = 0;
  if (rsize > 0)
    {
      uarta_data = UART_DR | rxd;
      if (uart_stat_reg & UARTA_HRE)
	uarta_data |= UART_THE;
      if (uart_stat_reg & UARTA_SRE)
	uarta_data |= UART_TSE;
      if (uart_stat_reg & UARTA_DR)
	{
	  uart_stat_reg |= UARTA_OR;
	  mec_irq (7); /* UART error interrupt */
	}
      uart_stat_reg |= UARTA_DR;
      mec_irq (4);
    }
  rsize = 0;
  if (f2open)
    rsize = DO_STDIO_READ (ifd2, &rxd, 1);
  else
    rsize = 0;
  if (rsize)
    {
      uartb_data = UART_DR | rxd;
      if (uart_stat_reg & UARTB_HRE)
	uartb_data |= UART_THE;
      if (uart_stat_reg & UARTB_SRE)
	uartb_data |= UART_TSE;
      if (uart_stat_reg & UARTB_DR)
	{
	  uart_stat_reg |= UARTB_OR;
	  mec_irq (7); /* UART error interrupt */
	}
      uart_stat_reg |= UARTB_DR;
      mec_irq (5);
    }
  event (uart_rx, 0, UART_RX_TIME);
}
#endif

static void
uart_intr (int32 arg)
{
  read_uart (0xE8); /* Check for UART interrupts every 1000 clk */
  flush_uart ();    /* Flush UART ports      */
  event (uart_intr, 0, UART_FLUSH_TIME);
}

static void
uart_irq_start ()
{
#ifdef FAST_UART
  event (uart_intr, 0, UART_FLUSH_TIME);
#else
#ifndef _WIN32
  event (uart_rx, 0, UART_RX_TIME);
#endif
#endif
}

/* Watch-dog and MEC timers.  The logic is in erc32_timer.h; these are the
   event queue callbacks which tick each one.  */

static void
wdog_intr (int32 arg)
{
  (void) arg;
  wdog.Tick ();
}

static void
rtc_intr (int32 arg)
{
  (void) arg;
  rtc.Tick ();
}

static void
gpt_intr (int32 arg)
{
  (void) arg;
  gpt.Tick ();
}

/* Store data in host byte order.  MEM points to the beginning of the
   emulated memory; WADDR contains the index the emulated memory,
   DATA points to words in host byte order to be stored.  SZ contains log(2)
   of the number of bytes to retrieve, and can be 0 (1 byte), 1 (one
   half-word), 2 (one word), or 3 (two words); WS should return the number of
   wait-states.  */

static void
store_bytes (char *mem, uint32 waddr, uint32 *data, int32 sz, int32 *ws)
{
  /* sz is the two-bit store size: 0 byte, 1 half-word, 2 word, 3 double, so
     the final case needs no test of its own.  */
  if (sz == 0)
    {
#ifdef HOST_LITTLE_ENDIAN
      waddr ^= 3;
#endif
      mem[waddr] = *data & 0x0ff;
      *ws = cfg.ram_write_ws () + 3;
    }
  else if (sz == 1)
    {
#ifdef HOST_LITTLE_ENDIAN
      waddr ^= 2;
#endif
      *((uint16 *) &mem[waddr]) = *data & 0x0ffff;
      *ws = cfg.ram_write_ws () + 3;
    }
  else if (sz == 2)
    {
      memcpy (&mem[waddr], data, 4);
      *ws = cfg.ram_write_ws ();
    }
  else
    {
      memcpy (&mem[waddr], data, 8);
      *ws = 2 * cfg.ram_write_ws () + STD_WS;
    }
}

/* Memory emulation */

static int
memory_iread (uint32 addr, uint32 *data, int32 *ws)
{
  uint32 asi;
  if ((addr >= cfg.ram_start ()) &&
      (addr < (cfg.ram_start () + cfg.ram_size ())))
    {
      memcpy (data, &ramb[addr & cfg.ram_mask ()], 4);
      *ws = cfg.ram_read_ws ();
      return 0;
    }
  else if (addr < cfg.rom_size ())
    {
      memcpy (data, &romb[addr], 4);
      *ws = cfg.rom_read_ws ();
      return 0;
    }

  if (sis_verbose)
    printf ("Memory exception at %x (illegal address)\n", addr);
  if (sregs->psr & 0x080)
    asi = 9;
  else
    asi = 8;
  set_sfsr (erc32::kFaultUnimplemented, addr, asi, 1);
  *ws = MEM_EX_WS;
  return 1;
}

static int
memory_read (uint32 addr, uint32 *data, int32 *ws)
{
  int32 mexc;
  int32 asi;

  if ((addr >= cfg.ram_start ()) &&
      (addr < (cfg.ram_start () + cfg.ram_size ())))
    {
      memcpy (data, &ramb[addr & cfg.ram_mask ()], 4);
      *ws = cfg.ram_read_ws ();
      return 0;
    }
  else if ((addr >= MEC_START) && (addr < MEC_END))
    {
      asi = (sregs->psr & 0x080) ? 11 : 10;
      mexc = mec_read (addr, asi, data);
      if (mexc)
	{
	  set_sfsr (erc32::kFaultMecRegister, addr, asi, 1);
	  *ws = MEM_EX_WS;
	}
      else
	{
	  *ws = 0;
	}
      return mexc;
    }
  else if (addr < cfg.rom_size ())
    {
      memcpy (data, &romb[addr], 4);
      *ws = cfg.rom_read_ws ();
      return 0;
    }

  if (sis_verbose)
    printf ("Memory exception at %x (illegal address)\n", addr);
  asi = (sregs->psr & 0x080) ? 11 : 10;
  set_sfsr (erc32::kFaultUnimplemented, addr, asi, 1);
  *ws = MEM_EX_WS;
  return 1;
}

static int
memory_write (uint32 addr, uint32 *data, int32 sz, int32 *ws)
{
  uint32 byte_addr;
  uint32 byte_mask;
  uint32 waddr;
  uint32 *ram;
  int32 mexc;
  int i;
  int wphit[2];
  int asi;

  if ((addr >= cfg.ram_start ()) &&
      (addr < (cfg.ram_start () + cfg.ram_size ())))
    {
      if (cfg.access_protect ())
	{

	  waddr = (addr & 0x7fffff) >> 2;
	  asi = (sregs->psr & 0x080) ? 11 : 10;
	  for (i = 0; i < 2; i++)
	    wphit[i] =
		(((asi == 0xa) && (cfg.seg_mode (i) & erc32::kSegUser)) ||
		 ((asi == 0xb) &&
		  (cfg.seg_mode (i) & erc32::kSegSupervisor))) &&
		((waddr >= cfg.seg_base (i)) &&
		 ((waddr | (sz == 3)) < cfg.seg_end (i)));

	  if (((cfg.block_protect ()) && (wphit[0] || wphit[1])) ||
	      ((!cfg.block_protect ()) && !((cfg.seg_mode (0) && wphit[0]) ||
					    (cfg.seg_mode (1) && wphit[1]))))
	    {
	      if (sis_verbose)
		printf ("Memory access protection error at 0x%08x\n", addr);
	      set_sfsr (erc32::kFaultProtection, addr, asi, 0);
	      *ws = MEM_EX_WS;
	      return 1;
	    }
	}
      waddr = addr & cfg.ram_mask ();
      store_bytes (ramb, waddr, data, sz, ws);
      return 0;
    }
  else if ((addr >= MEC_START) && (addr < MEC_END))
    {
      asi = (sregs->psr & 0x080) ? 11 : 10;
      if ((sz != 2) || (asi != 0xb))
	{
	  set_sfsr (erc32::kFaultMecRegister, addr, asi, 0);
	  *ws = MEM_EX_WS;
	  return 1;
	}
      mexc = mec_write (addr, *data);
      if (mexc)
	{
	  set_sfsr (erc32::kFaultMecRegister, addr, asi, 0);
	  *ws = MEM_EX_WS;
	}
      else
	{
	  *ws = 0;
	}
      return mexc;
    }
  else if ((addr < cfg.rom_size ()) && cfg.prom_write () && (wrp) &&
	   ((cfg.prom_40bit () && (sz > 1)) ||
	    (!cfg.prom_40bit () && (sz == 0))))
    {

      *ws = cfg.rom_write_ws () + 1;
      if (sz == 3)
	*ws += cfg.rom_write_ws () + STD_WS;
      store_bytes (romb, addr, data, sz, ws);
      return 0;
    }

  *ws = MEM_EX_WS;
  asi = (sregs->psr & 0x080) ? 11 : 10;
  set_sfsr (erc32::kFaultUnimplemented, addr, asi, 0);
  return 1;
}

static char *
get_mem_ptr (uint32 addr, uint32 size)
{
  if ((addr + size) < ROM_SIZE)
    {
      return &romb[addr];
    }
  else if ((addr >= cfg.ram_start ()) && ((addr + size) < cfg.ram_end ()))
    {
      return &ramb[addr & cfg.ram_mask ()];
    }

  return (char *) -1;
}

static int
sis_memory_write (uint32 addr, const char *data, uint32 length)
{
  char *mem;

  if ((mem = get_mem_ptr (addr, length)) == ((char *) -1))
    return 0;

  memcpy (mem, data, length);
  return length;
}

static int
sis_memory_read (uint32 addr, char *data, uint32 length)
{
  char *mem;
  int ws;
  unsigned int w4;

  if (length == 4)
    {
      memory_read (addr, &w4, &ws);
      memcpy (data, &w4, length);
      return 4;
    }

  if ((mem = get_mem_ptr (addr, length)) == ((char *) -1))
    return 0;

  memcpy (data, mem, length);
  return length;
}

static void
boot_init (void)
{
  mec_write (MEC_WCR, 0);			 /* zero waitstates */
  mec_write (MEC_TRAPD, 0);			 /* turn off watch-dog */
  mec_write (MEC_RTC_SCALER, ebase.freq - 1);	 /* generate 1 MHz RTC tick */
  /* Decode all of the ROM and RAM the board has, which is what the stack
     pointer set below assumes.  */
  mec_write (MEC_MEMCFG, (7 << 18) | (6 << 10)); /* 16 MB ROM, 16 MB RAM */
  sregs->wim = 2;
  sregs->psr = 0x110010e0;
  sregs->r[30] = RAM_END;
  sregs->r[14] = sregs->r[30] - 96 * 4;
  cfg.EnablePowerDown ();
}

const struct memsys erc32sys = {
  init_sim,	    reset,	     error_mode,   sim_halt,	exit_sim,
  init_stdio,	    restore_stdio,   memory_iread, memory_read, memory_write,
  sis_memory_write, sis_memory_read, boot_init
};
