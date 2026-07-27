/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Tests for remote.cc, the GDB remote serial protocol server.

   Every expectation comes from the GDB Remote Serial Protocol, appendix
   "Remote Protocol" of the GDB manual.  The sections quoted in the case
   comments are "Overview" (packet framing and acknowledgement), "Packets"
   (the per-letter command definitions), "Stop Reply Packets" (S, T and W)
   and "General Query Packets" (q and v).  Nothing here is derived from
   reading remote.cc back to itself.

   The stub is driven over a real loopback TCP connection, because the
   framing, the acknowledgement and the connection teardown are the parts of
   the protocol most worth pinning and none of them exist below the socket.
   gdb_remote runs on its own thread and the case plays the client.  Every
   read is poll-timed, so a stub that stops answering fails the case instead
   of blocking the suite.

   Only the POSIX build is covered.  The Windows socket calls are a
   different body of code behind #ifdef _WIN32 and are not compiled here.  */

#include "doctest.h"

#include "config.h"
#include "sis.h"
#include "sisio.h"
#include "remote_socket.h"
#include "support.h"

#ifndef _WIN32

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

/* The access size a board's memory_write takes for a 32 bit word.  */
const int32 SZ_WORD = 2;

/* remote.cc exports these without a header.  */
extern int create_socket (int port);
extern int check_pkg (unsigned char *buf, int len);
extern void checksum (char *buf);
extern int new_socket;

namespace
{

/* Frame BODY as a packet.  Overview: "$packet-data#checksum", where the
   checksum is the modulo 256 sum of the packet data characters, sent as two
   hexadecimal digits.  */
std::string
pkt (const std::string &body)
{
  unsigned char sum = 0;
  char tail[8];

  for (char c : body)
    sum += (unsigned char) c;
  snprintf (tail, sizeof (tail), "#%02x", sum);

  return "$" + body + tail;
}

/* The acknowledgement the stub sent ahead of a reply.  Overview: the
   receiver of a packet answers '+' to accept it and '-' to reject it.  */
char
ack_of (const std::string &raw)
{
  return raw.empty () ? '?' : raw[0];
}

/* The packet data of a reply, checked against its own checksum.  Returns a
   marker instead of the body when the framing or the checksum is wrong, so
   that a single comparison in the case catches both.  */
std::string
body_of (const std::string &raw)
{
  size_t start = raw.find ('$');
  if (start == std::string::npos)
    return "<no packet>";

  size_t end = raw.find ('#', start);
  if (end == std::string::npos || end + 2 >= raw.size ())
    return "<unterminated>";

  std::string body = raw.substr (start + 1, end - start - 1);
  unsigned char sum = 0;
  for (char c : body)
    sum += (unsigned char) c;

  char expect[8];
  snprintf (expect, sizeof (expect), "%02x", sum);
  if (raw.substr (end + 1, 2) != expect)
    return "<bad checksum>";

  return body;
}

/* The stub reports a failed system call with perror, which writes to
   stderr and so escapes the stdout capture.  Send it nowhere while a
   failure is provoked on purpose.  */
class stderr_sink
{
public:
  stderr_sink () : saved (dup (2))
  {
    int null = open ("/dev/null", O_WRONLY);
    fflush (stderr);
    if (null >= 0)
      {
	dup2 (null, 2);
	close (null);
      }
  }

  ~stderr_sink ()
  {
    fflush (stderr);
    if (saved >= 0)
      {
	dup2 (saved, 2);
	close (saved);
      }
  }

  stderr_sink (const stderr_sink &) = delete;
  stderr_sink &operator= (const stderr_sink &) = delete;

private:
  int saved;
};

/* A sockets API for remote::Listener that holds its own state and fails
   where a case tells it to.  The defaults are the successful sequence.  */
struct listener_env
{
  int socket_fd = 3;
  int reuse = 0;
  int bind_result = 0;
  int listen_result = 0;
  int accept_fd = 4;
  int bound_port = -1;
  int closed = -1;
  int configured = -1;
  std::string failure;

  int
  Socket ()
  {
    return socket_fd;
  }

  int
  SetReuseAddr (int fd)
  {
    (void) fd;
    return reuse;
  }

  int
  Bind (int fd, int port)
  {
    (void) fd;
    bound_port = port;
    return bind_result;
  }

  int
  Listen (int fd)
  {
    (void) fd;
    return listen_result;
  }

  int
  Accept (int fd)
  {
    (void) fd;
    return accept_fd;
  }

  void
  Close (int fd)
  {
    closed = fd;
  }

  void
  Configure (int fd)
  {
    configured = fd;
  }

  void
  Fail (const char *what)
  {
    failure = what;
  }
};

/* A loopback port nothing is listening on.  Bind to port zero, note what
   the kernel handed out and give it back.  */
int
free_port ()
{
  struct sockaddr_in addr;
  socklen_t len = sizeof (addr);
  int fd = socket (AF_INET, SOCK_STREAM, 0);
  int port;

  if (fd < 0)
    return -1;

  memset (&addr, 0, sizeof (addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (bind (fd, (struct sockaddr *) &addr, sizeof (addr)) < 0 ||
      getsockname (fd, (struct sockaddr *) &addr, &len) < 0)
    {
      close (fd);
      return -1;
    }
  port = ntohs (addr.sin_port);
  close (fd);

  return port;
}

/* A listening socket held open on PORT, so that the stub's bind fails.  */
int
hold_port (int port)
{
  struct sockaddr_in addr;
  int fd = socket (AF_INET, SOCK_STREAM, 0);

  if (fd < 0)
    return -1;

  memset (&addr, 0, sizeof (addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl (INADDR_ANY);
  addr.sin_port = htons (port);
  if (bind (fd, (struct sockaddr *) &addr, sizeof (addr)) < 0 ||
      listen (fd, 1) < 0)
    {
      close (fd);
      return -1;
    }

  return fd;
}

/* Connect to the stub, retrying while it is still on its way to accept.  */
int
client_connect (int port)
{
  struct sockaddr_in addr;

  memset (&addr, 0, sizeof (addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
  addr.sin_port = htons (port);

  for (int i = 0; i < 500; i++)
    {
      int fd = socket (AF_INET, SOCK_STREAM, 0);
      if (fd < 0)
	return -1;
      if (connect (fd, (struct sockaddr *) &addr, sizeof (addr)) == 0)
	return fd;
      close (fd);
      std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }

  return -1;
}

/* One debug session: gdb_remote on a thread, the case as the client.

   The stub prints unconditionally, so stdout is captured for the whole life
   of the session and handed back as LOG.  Assertions therefore belong after
   finish(), which restores stdout first.  */
class gdb_session
{
public:
  explicit gdb_session (int verbose = 0)
      : log (), banner (), port (free_port ()), fd (-1), done (false),
	closed (false), capture (NULL)
  {
    if (port <= 0)
      return;

    sis_verbose = verbose;
    capture = new sis_tests::stdout_capture ();
    server = std::thread (
	[this]
	  {
	    gdb_remote (port);
	    done = true;
	  });
    fd = client_connect (port);
    if (fd >= 0)
      banner = read_some (2000);
  }

  ~gdb_session () { finish (); }

  gdb_session (const gdb_session &) = delete;
  gdb_session &operator= (const gdb_session &) = delete;

  /* Send DATA verbatim.  */
  void
  send_raw (const std::string &data)
  {
    if (fd >= 0)
      {
	ssize_t n = write (fd, data.data (), data.size ());
	(void) n;
      }
  }

  /* Drop the connection with a reset rather than a shutdown, so that the
     stub's next read fails instead of reporting end of file.  */
  void
  abort_connection ()
  {
    struct linger lg;

    if (fd < 0)
      return;

    lg.l_onoff = 1;
    lg.l_linger = 0;
    setsockopt (fd, SOL_SOCKET, SO_LINGER, (char *) &lg, sizeof (lg));
    close (fd);
    fd = -1;
  }

  /* Read whatever arrives within MS milliseconds.  */
  std::string
  read_some (int ms)
  {
    struct pollfd pfd;
    char buf[4096];
    std::string got;

    if (fd < 0)
      return got;

    pfd.fd = fd;
    pfd.events = POLLIN;
    if (poll (&pfd, 1, ms) > 0)
      {
	int n = read (fd, buf, sizeof (buf));
	if (n > 0)
	  got.append (buf, n);
      }

    return got;
  }

  /* Send PACKET and collect bytes until a complete reply packet is in
     hand, or the budget runs out.  */
  std::string
  request (const std::string &packet)
  {
    std::string got;

    send_raw (packet);
    for (int i = 0; i < 40; i++)
      {
	size_t hash = got.find ('#');
	if (got.find ('$') != std::string::npos && hash != std::string::npos &&
	    hash + 2 < got.size ())
	  break;
	got += read_some (100);
      }

    return got;
  }

  /* Close the connection, wait for the stub to return and put stdout
     back.  Idempotent.  */
  void
  finish ()
  {
    if (closed)
      return;
    closed = true;

    if (fd >= 0)
      {
	close (fd);
	fd = -1;
      }

    if (server.joinable ())
      {
	for (int i = 0; i < 1000 && !done; i++)
	  std::this_thread::sleep_for (std::chrono::milliseconds (10));
	if (done)
	  server.join ();
	else
	  server.detach ();
      }

    if (capture != NULL)
      {
	log = capture->str ();
	delete capture;
	capture = NULL;
      }

    CHECK (port > 0);
    CHECK (done);
  }

  /* Everything the stub printed, and the bytes it sent before the first
     request.  */
  std::string log;
  std::string banner;

private:
  int port;
  int fd;
  std::atomic<bool> done;
  bool closed;
  sis_tests::stdout_capture *capture;
  std::thread server;
};

/* Machine state for a session.  ERC32 is the board used because it is
   self-contained: it registers no GRLIB cores and so cannot disturb the
   shared bus model the other test files drive.  */
struct remote_fixture
{
  int saved_verbose;
  int saved_cputype;
  int saved_archtype;
  int saved_socket;
  const struct memsys *saved_ms;
  const struct cpu_arch *saved_arch;
  uint32 saved_load_addr;
  void (*saved_pipe) (int);

  remote_fixture ()
      : saved_verbose (sis_verbose), saved_cputype (cputype),
	saved_archtype (archtype), saved_socket (new_socket), saved_ms (ms),
	saved_arch (arch), saved_load_addr (last_load_addr)
  {
    /* A stub that writes to a socket the case has already closed would
       take SIGPIPE down the whole suite.  */
    saved_pipe = signal (SIGPIPE, SIG_IGN);

    cputype = CPU_ERC32;
    archtype = CPU_SPARC;
    ms = &erc32sys;
    arch = &sparc32;
    ebase.freq = 14;
    ebase.simtime = 0;
    ebase.simstart = 0;
    ebase.coven = 0;
    ebase.wphit = 0;
    ebase.wptype = 0;
    ebase.wpaddress = 0;
    reset_all ();
    init_bpt (sregs);
    ms->init_sim ();
    ms->boot_init (); /* decodes the memory map, so RAM is writable */
    sis_verbose = 0;
    simstat = OK;
    ctrl_c = 0;
    last_load_addr = 0;
    new_socket = 0;
    sregs[0].pc = RAM;
    sregs[0].npc = RAM + 4;
  }

  ~remote_fixture ()
  {
    sis_verbose = saved_verbose;
    cputype = saved_cputype;
    archtype = saved_archtype;
    new_socket = saved_socket;
    last_load_addr = saved_load_addr;
    ctrl_c = 0;
    ebase.wphit = 0;
    /* The event queue outlives the case otherwise: a continue leaves the
       socket poll event behind.  */
    reset_all ();
    init_bpt (sregs);
    ms = saved_ms;
    arch = saved_arch;
    signal (SIGPIPE, saved_pipe);
  }

  /* ERC32 RAM, well clear of the addresses the other test files use.  */
  static constexpr uint32 RAM = 0x02001000;

  void
  poke (uint32 addr, uint32 val)
  {
    uint32 d = val;
    int32 ws;
    ms->memory_write (addr, &d, SZ_WORD, &ws);
  }

  uint32
  peek (uint32 addr)
  {
    uint32 d = 0;
    int32 ws;
    ms->memory_read (addr, &d, &ws);
    return d;
  }
};

} /* namespace */

/* ------------------------------------------------------------------ */
/* Framing, below the socket                                          */
/* ------------------------------------------------------------------ */

TEST_CASE ("checksum appends the modulo 256 sum in hex")
{
  /* Overview: "the two digit checksum ... is computed as the modulo 256 sum
     of all characters between the leading '$' and the trailing '#'".  */
  char buf[16];

  strcpy (buf, "OK");
  checksum (buf);
  CHECK (std::string (buf) == "OK#9a"); /* 'O' 0x4f + 'K' 0x4b = 0x9a */

  /* An empty reply is a legal packet and its checksum is zero.  Overview:
     an empty response means the command is not supported.  */
  strcpy (buf, "");
  checksum (buf);
  CHECK (std::string (buf) == "#00");

  /* The sum wraps at 256 rather than saturating.  */
  strcpy (buf, "\x80\x81\x02");
  checksum (buf);
  CHECK (std::string (buf) == "\x80\x81\x02#03");
}

TEST_CASE ("check_pkg accepts a well formed packet")
{
  /* Overview: a packet is "$packet-data#checksum".  The offset returned is
     the first character of the packet data.  */
  unsigned char buf[32];

  strcpy ((char *) buf, "$g#67"); /* 'g' is 0x67 */
  CHECK (check_pkg (buf, 5) == 1);

  /* GDB acknowledges the previous reply and pipelines the next packet into
     the same segment, so the '$' need not be first.  */
  strcpy ((char *) buf, "+$g#67");
  CHECK (check_pkg (buf, 6) == 2);

  /* Overview: the checksum digits are hexadecimal, and hex digits are
     accepted in either case.  */
  strcpy ((char *) buf, "$OK#9a");
  CHECK (check_pkg (buf, 6) == 1);
  strcpy ((char *) buf, "$OK#9A");
  CHECK (check_pkg (buf, 6) == 1);
}

TEST_CASE ("check_pkg rejects what is not a packet")
{
  unsigned char buf[32];

  /* No packet start at all.  */
  strcpy ((char *) buf, "hello");
  CHECK (check_pkg (buf, 5) == -1);

  /* Started but never terminated.  */
  strcpy ((char *) buf, "$qSupported");
  CHECK (check_pkg (buf, 11) == -1);

  /* Terminated with no checksum digits behind the '#'.  */
  strcpy ((char *) buf, "$g#");
  CHECK (check_pkg (buf, 3) == -1);

  /* A checksum that does not match the data.  Overview: the receiver
     answers '-' and the sender retransmits.  */
  strcpy ((char *) buf, "$g#00");
  CHECK (check_pkg (buf, 5) == -1);

  /* A checksum that is not hexadecimal at all.  */
  strcpy ((char *) buf, "$g#zz");
  CHECK (check_pkg (buf, 5) == -1);
}

/* ------------------------------------------------------------------ */
/* The listening socket                                               */
/* ------------------------------------------------------------------ */

TEST_CASE_FIXTURE (remote_fixture,
		   "gdb_remote gives up when the port is taken")
{
  /* Nothing in the protocol covers this: it is the stub refusing to start
     when the port is already served.  gdb_remote must return rather than
     spin, and must leave no socket behind.  */
  int port = free_port ();
  REQUIRE (port > 0);
  int held = hold_port (port);
  REQUIRE (held >= 0);

  std::string log;
  {
    stderr_sink quiet;
    sis_tests::stdout_capture capture;
    gdb_remote (port);
    log = capture.str ();
  }
  close (held);

  CHECK (log.find ("listening on port") != std::string::npos);
  CHECK (log.find ("connected") == std::string::npos);
  CHECK (new_socket == 0);
  CHECK (sis_gdb_break == 0);
}

TEST_CASE ("the listener serves one connection")
{
  /* The successful sequence: create, allow the port to be rebound, bind,
     listen with a backlog of one, accept, then drop the listening socket
     and configure the connection.  */
  listener_env env;
  remote::Listener<listener_env> listener (env);
  int conn = -2;

  CHECK (listener.Open (1234, &conn) == 1);
  CHECK (conn == 4);
  CHECK (env.bound_port == 1234);
  CHECK (env.closed == 3);     /* the listening socket, not the connection */
  CHECK (env.configured == 4); /* keep-alive and no delay on the connection */
  CHECK (env.failure == "");
}

TEST_CASE ("the listener reports a failed socket call")
{
  /* POSIX: socket, bind, listen and accept report failure as -1, and
     setsockopt reports it as a non-zero return.  Each failure stops the
     sequence and is named.  */
  listener_env env;
  remote::Listener<listener_env> listener (env);
  int conn = -2;

  SUBCASE ("setsockopt")
  {
    env.reuse = -1;
    CHECK (listener.Open (1234, &conn) == 0);
    CHECK (env.failure == "setsockopt");
    /* Nothing was bound, so the port is still free.  */
    CHECK (env.bound_port == -1);
  }

  SUBCASE ("bind")
  {
    env.bind_result = -1;
    CHECK (listener.Open (1234, &conn) == 0);
    CHECK (env.failure == "bind failed");
  }

  SUBCASE ("listen")
  {
    env.listen_result = -1;
    CHECK (listener.Open (1234, &conn) == 0);
    CHECK (env.failure == "listen");
  }

  SUBCASE ("accept")
  {
    env.accept_fd = -1;
    CHECK (listener.Open (1234, &conn) == 0);
    CHECK (env.failure == "accept");
    /* The accept result is stored before it is checked, so the caller
       sees the failed descriptor.  */
    CHECK (conn == -1);
  }

  /* No failure path hands out a connection to configure.  */
  CHECK (env.configured == -1);
  CHECK (conn != 4);
}

TEST_CASE ("the listener mistakes descriptor 0 for a failure (suspected "
	   "defect)")
{
  /* POSIX: socket() returns -1 on failure, and 0 is a perfectly good
     descriptor.  The listener tests for 0, so it reports a real failure as
     success and a descriptor 0 as a failure.  */
  listener_env env;
  remote::Listener<listener_env> listener (env);
  int conn = -2;

  env.socket_fd = 0;
  CHECK (listener.Open (1234, &conn) == 0);
  CHECK (env.failure == "socket failed");
  CHECK (conn == -2);

  /* And the real failure sails through to bind.  */
  listener_env broken;
  remote::Listener<listener_env> unlucky (broken);
  broken.socket_fd = -1;
  broken.bind_result = -1;
  CHECK (unlucky.Open (1234, &conn) == 0);
  CHECK (broken.failure == "bind failed");
}

TEST_CASE ("create_socket mistakes descriptor 0 for a failure (suspected "
	   "defect)")
{
  /* socket() reports failure as -1, never as 0.  create_socket tests for
     0, so it reports success as failure whenever descriptor 0 is free, and
     would miss the real error.  Closing stdin makes 0 the lowest free
     descriptor and pins the behaviour.  */
  int port = free_port ();
  REQUIRE (port > 0);

  stderr_sink quiet;
  int saved = dup (0);
  REQUIRE (saved >= 0);
  close (0);

  /* Confirm 0 is the lowest free descriptor before calling.  If it is not,
     create_socket goes on to accept and blocks for ever.  */
  int probe = open ("/dev/null", O_RDONLY);
  int lowest = probe;
  if (probe >= 0)
    close (probe);
  if (lowest != 0)
    {
      dup2 (saved, 0);
      close (saved);
    }
  REQUIRE (lowest == 0);

  int res = create_socket (port);

  /* The listening socket really was created, on descriptor 0.  */
  CHECK (res == 0);
  close (0);
  dup2 (saved, 0);
  close (saved);
}

/* ------------------------------------------------------------------ */
/* Connection handling                                                */
/* ------------------------------------------------------------------ */

TEST_CASE_FIXTURE (remote_fixture, "the stub acknowledges a new connection")
{
  std::string banner, log;
  {
    gdb_session session;
    banner = session.banner;
    session.finish ();
    log = session.log;
  }

  /* Overview: the stub sends '+' once the connection is up, so that GDB
     knows acknowledgement is in use.  */
  CHECK (banner == "+");
  CHECK (log.find ("connected") != std::string::npos);

  /* The connection is gone once the client hangs up.  */
  CHECK (new_socket == 0);
  CHECK (sis_gdb_break == 0);
}

TEST_CASE_FIXTURE (remote_fixture, "a corrupt packet is answered with '-'")
{
  /* Overview: "if the receiver does not agree with the checksum it replies
     with '-'".  */
  std::string bad, junk, good;
  {
    gdb_session session;
    session.send_raw ("$!#00");
    bad = session.read_some (2000);
    session.send_raw ("garbage");
    junk = session.read_some (2000);
    good = session.request (pkt ("!"));
  }

  CHECK (bad == "-");
  CHECK (junk == "-");

  /* And the stub keeps the connection, so the retransmission works.  */
  CHECK (ack_of (good) == '+');
  CHECK (body_of (good) == "OK");
}

TEST_CASE_FIXTURE (remote_fixture, "'-' from the client retransmits the reply")
{
  /* Overview: a '-' from GDB means the last reply was not received
     correctly and must be sent again.  */
  std::string first, again;
  {
    gdb_session session;
    first = session.request (pkt ("!"));
    session.send_raw ("-");
    again = session.read_some (2000);
  }

  CHECK (body_of (first) == "OK");
  CHECK (again == pkt ("OK"));
}

TEST_CASE_FIXTURE (remote_fixture, "'+' from the client is not answered")
{
  /* Overview: '+' acknowledges the reply and carries no request, so
     nothing comes back.  */
  std::string quiet, next;
  {
    gdb_session session;
    (void) session.request (pkt ("!"));
    session.send_raw ("+");
    quiet = session.read_some (200);
    /* Nor is a stray byte that is neither an acknowledgement nor an
       interrupt.  */
    session.send_raw ("x");
    quiet += session.read_some (200);
    next = session.request (pkt ("!"));
  }

  CHECK (quiet == "");
  CHECK (body_of (next) == "OK");
}

TEST_CASE_FIXTURE (remote_fixture, "a bare ^C requests an interrupt")
{
  /* "Interrupts": GDB interrupts the target by sending the single byte
     0x03 outside a packet.  */
  std::string quiet;
  {
    gdb_session session;
    session.send_raw ("\x03");
    quiet = session.read_some (200);
    /* A second byte gets the read loop moving again so the case can
       finish.  */
    session.send_raw ("+");
    quiet += session.read_some (200);
  }

  CHECK (quiet == "");
  CHECK (ctrl_c == 1);
}

TEST_CASE_FIXTURE (remote_fixture, "a reset connection ends the session")
{
  /* Nothing in the protocol: a peer that vanishes must not leave the stub
     spinning on a dead descriptor.  */
  std::string reply;
  {
    gdb_session session;
    reply = session.request (pkt ("!"));
    session.abort_connection ();
  }

  CHECK (body_of (reply) == "OK");
  CHECK (new_socket == 0);
}

TEST_CASE_FIXTURE (remote_fixture, "verbose tracing logs both directions")
{
  /* Not protocol, but the -v -v packet trace the manual documents for the
     stub.  */
  std::string log, reply, again;
  {
    gdb_session session (2);
    reply = session.request (pkt ("!"));
    session.send_raw ("garbage");
    (void) session.read_some (500);
    /* A retransmission is traced as well.  */
    session.send_raw ("-");
    again = session.read_some (500);
    session.finish ();
    log = session.log;
  }

  CHECK (body_of (reply) == "OK");
  CHECK (again == pkt ("OK"));
  CHECK (log.find ("tx: +") != std::string::npos);
  CHECK (log.find ("tx: -") != std::string::npos);
  CHECK (log.find ("tx: $OK#9a") != std::string::npos);
  CHECK (log.find ("$!#21 (5)") != std::string::npos);
}

/* ------------------------------------------------------------------ */
/* Stop replies                                                       */
/* ------------------------------------------------------------------ */

TEST_CASE_FIXTURE (remote_fixture, "'?' reports the last stop reason")
{
  /* "Stop Reply Packets": '?' asks for the reason the target halted and is
     answered with a stop reply, "S AA" where AA is the signal number.  */
  std::string ok, seg, term, intr, trap;
  {
    gdb_session session;
    simstat = OK;
    ok = session.request (pkt ("?"));
    simstat = NULL_HIT;
    seg = session.request (pkt ("?"));
    simstat = ERROR_MODE;
    term = session.request (pkt ("?"));
    simstat = CTRL_C;
    intr = session.request (pkt ("?"));
    simstat = BPT_HIT;
    trap = session.request (pkt ("?"));
  }

  CHECK (body_of (ok) == "S00");
  CHECK (body_of (intr) == "S02"); /* SIGINT */
  CHECK (body_of (trap) == "S05"); /* SIGTRAP */

  /* Suspected defect: "S AA" is defined as a two digit hexadecimal signal
     number, but the reply is formatted with %02d.  A null pointer hit is
     SIGSEGV, 11, which must be sent as "S0b"; the stub sends "S11", which
     GDB reads as signal 0x11.  The same goes for the SIGTERM of error
     mode, 15, which must be "S0f".  */
  CHECK (body_of (seg) == "S11");
  CHECK (body_of (term) == "S15");
}

TEST_CASE_FIXTURE (remote_fixture,
		   "'?' reports an exit before the program ran")
{
  /* "Stop Reply Packets": "W AA" means the process exited with status AA.
     The stub reports it while the program still sits on its entry point
     with no simulated time spent, which is the state right after a load.  */
  std::string exited, ran, moved, noload;
  {
    gdb_session session;

    last_load_addr = sregs[0].pc;
    ebase.simtime = 0;
    exited = session.request (pkt ("?"));

    /* Once time has passed the program is running, not exited.  */
    ebase.simtime = 1;
    ran = session.request (pkt ("?"));

    /* So is a program stopped away from its entry point.  */
    ebase.simtime = 0;
    sregs[0].pc = last_load_addr + 4;
    moved = session.request (pkt ("?"));

    /* And with nothing loaded there is no entry point to compare with.  */
    sregs[0].pc = 0;
    last_load_addr = 0;
    noload = session.request (pkt ("?"));
  }

  CHECK (body_of (exited) == "W00");
  CHECK (body_of (ran) == "S00");
  CHECK (body_of (moved) == "S00");
  CHECK (body_of (noload) == "S00");
}

/* ------------------------------------------------------------------ */
/* Registers and memory                                               */
/* ------------------------------------------------------------------ */

TEST_CASE_FIXTURE (remote_fixture, "'g' returns the register block")
{
  /* "Packets", 'g': read general registers, answered with the register
     values in target byte order, hex encoded, in the order the target
     description defines.  For SPARC that order is g0-g7, o0-o7, l0-l7,
     i0-i7, then y, psr, wim, tbr, pc, npc, fsr and csr, 72 registers of
     four bytes each.  */
  std::string reply;
  {
    gdb_session session;
    sregs[0].g[1] = 0x12345678;
    sregs[0].pc = 0x02001000;
    sregs[0].npc = 0x02001004;
    sregs[0].y = 0xdeadbeef;
    reply = session.request (pkt ("g"));
  }

  std::string body = body_of (reply);
  REQUIRE (body.size () == 72 * 8);
  CHECK (body.substr (0 * 8, 8) == "00000000");	 /* g0 reads as zero */
  CHECK (body.substr (1 * 8, 8) == "12345678");	 /* g1, big endian */
  CHECK (body.substr (64 * 8, 8) == "deadbeef"); /* y */
  CHECK (body.substr (68 * 8, 8) == "02001000"); /* pc */
  CHECK (body.substr (69 * 8, 8) == "02001004"); /* npc */
}

TEST_CASE_FIXTURE (remote_fixture, "'m' reads memory")
{
  /* "Packets", 'm addr,length': read length addressable units from addr,
     answered with the bytes hex encoded.  */
  std::string lower, upper;
  {
    gdb_session session;
    poke (RAM, 0xcafe0102);
    poke (RAM + 4, 0x03040506);
    poke (RAM + 12, 0x11223344);
    lower = session.request (pkt ("m2001000,8"));
    /* Hexadecimal is accepted in either case throughout the protocol.  */
    upper = session.request (pkt ("m200100C,4"));
  }

  CHECK (body_of (lower) == "cafe010203040506");
  CHECK (body_of (upper) == "11223344");
}

TEST_CASE_FIXTURE (remote_fixture, "'M' writes memory")
{
  /* "Packets", 'M addr,length:XX...': write the hex encoded bytes,
     answered with "OK".  */
  std::string reply;
  uint32 word = 0;
  {
    gdb_session session;
    poke (RAM, 0);
    reply = session.request (pkt ("M2001000,4:deadbeef"));
    session.finish ();
    word = peek (RAM);
  }

  CHECK (body_of (reply) == "OK");
  CHECK (word == 0xdeadbeef);
}

TEST_CASE_FIXTURE (remote_fixture, "'P' writes one register")
{
  /* "Packets", 'P n...=r...': write register n with value r, in target
     byte order, answered with "OK".  SPARC is big endian, so the digits
     arrive most significant first.  */
  std::string reply;
  {
    gdb_session session;
    sregs[0].g[1] = 0;
    reply = session.request (pkt ("P1=12345678"));
    session.finish ();
  }

  CHECK (body_of (reply) == "OK");
  CHECK (sregs[0].g[1] == 0x12345678);
}

TEST_CASE_FIXTURE (remote_fixture, "'P' takes a RISC-V value little endian")
{
  /* Same packet, but RV32 is little endian, so the least significant byte
     comes first.  Register 1 is x1.  */
  std::string reply;
  {
    cputype = CPU_RISCV;
    archtype = CPU_RISCV;
    arch = &riscv;
    gdb_session session;
    sregs[0].r[1] = 0;
    reply = session.request (pkt ("P1=78563412"));
    session.finish ();
  }

  CHECK (body_of (reply) == "OK");
  CHECK (sregs[0].r[1] == 0x12345678);
}

/* ------------------------------------------------------------------ */
/* Execution                                                          */
/* ------------------------------------------------------------------ */

TEST_CASE_FIXTURE (remote_fixture, "'s' steps one instruction")
{
  /* "Packets", 's': step one instruction.  The stop reply is SIGTRAP,
     "S05".  'S' is the same with a signal, which the simulator has no use
     for.  */
  std::string step, sigstep;
  {
    gdb_session session;
    poke (RAM, 0x01000000); /* nop */
    poke (RAM + 4, 0x01000000);
    step = session.request (pkt ("s"));
    sigstep = session.request (pkt ("S05"));
    session.finish ();
  }

  CHECK (body_of (step) == "S05");
  CHECK (body_of (sigstep) == "S05");
  CHECK (sregs[0].pc == RAM + 8);
}

TEST_CASE_FIXTURE (remote_fixture, "'c' runs to a breakpoint")
{
  /* "Packets", 'c': continue.  'Z0,addr,kind' inserts a software
     breakpoint and is answered "OK"; running into it stops with SIGTRAP.  */
  std::string set, run;
  {
    gdb_session session;
    poke (RAM, 0x01000000); /* nop */
    poke (RAM + 4, 0x01000000);
    set = session.request (pkt ("Z0,2001004,4"));
    run = session.request (pkt ("c"));
    session.finish ();
  }

  CHECK (body_of (set) == "OK");
  CHECK (body_of (run) == "S05");
  CHECK (sregs[0].pc == RAM + 4);
}

TEST_CASE_FIXTURE (remote_fixture, "'c' reports a write watchpoint")
{
  /* "Stop Reply Packets": a watchpoint stop is "T AA watch:addr;", with
     the signal and the address of the watched location.  'Z2' inserts a
     write watchpoint.  */
  std::string set, run;
  {
    gdb_session session;
    sregs[0].g[1] = RAM + 0x100;
    poke (RAM, 0xc0206000); /* st %g0, [%g1] */
    poke (RAM + 4, 0x01000000);
    set = session.request (pkt ("Z2,2001100,4"));
    run = session.request (pkt ("c"));
    session.finish ();
  }

  CHECK (body_of (set) == "OK");
  CHECK (body_of (run) == "T05watch:2001100;");
}

TEST_CASE_FIXTURE (remote_fixture, "'c' reports a read watchpoint")
{
  /* "Stop Reply Packets": a read watchpoint stop is "T AA rwatch:addr;".
     'Z3' inserts one.  */
  std::string set, run;
  {
    gdb_session session;
    sregs[0].g[1] = RAM + 0x100;
    poke (RAM, 0xc4006000); /* ld [%g1], %g2 */
    poke (RAM + 4, 0x01000000);
    set = session.request (pkt ("Z3,2001100,4"));
    run = session.request (pkt ("c"));
    session.finish ();
  }

  CHECK (body_of (set) == "OK");
  CHECK (body_of (run) == "T05rwatch:2001100;");
}

TEST_CASE_FIXTURE (remote_fixture, "'C' restarts before continuing")
{
  /* "Packets", 'C sig': continue with a signal.  The simulator has no
     signals to deliver, so it restarts the program at its entry point and
     runs from there.  */
  std::string run;
  {
    gdb_session session;
    last_load_addr = RAM;
    poke (RAM, 0x01000000);
    poke (RAM + 4, 0x01000000);
    (void) session.request (pkt ("Z0,2001004,4"));
    sregs[0].pc = 0;
    run = session.request (pkt ("C05"));
    session.finish ();
  }

  CHECK (body_of (run) == "S05");
  CHECK (sregs[0].pc == RAM + 4);
}

TEST_CASE_FIXTURE (remote_fixture, "'c' stops in error mode")
{
  /* An unimplemented instruction traps, and with traps disabled by the
     second trap the processor enters error mode.  The stub reports that as
     SIGTERM, 15, which "Stop Reply Packets" writes "S0f".  */
  std::string run;
  {
    gdb_session session;
    poke (RAM, 0x00000000); /* unimp */
    run = session.request (pkt ("c"));
    session.finish ();
  }

  CHECK (body_of (run) == "S0f");
}

TEST_CASE_FIXTURE (remote_fixture, "'k' and 'R' restart the program")
{
  /* "Packets", 'k' kills the target and 'R' restarts it.  The simulator
     has one program, so both put it back on its entry point.  */
  std::string killed, restarted;
  {
    gdb_session session;
    last_load_addr = RAM;
    sregs[0].pc = 0;
    killed = session.request (pkt ("k"));
    session.finish ();
  }
  CHECK (body_of (killed) == "OK");
  CHECK (sregs[0].pc == RAM);

  {
    gdb_session session;
    last_load_addr = RAM + 4;
    sregs[0].pc = 0;
    restarted = session.request (pkt ("R0"));
    session.finish ();
  }
  CHECK (body_of (restarted) == "OK");
  CHECK (sregs[0].pc == RAM + 4);
}

/* ------------------------------------------------------------------ */
/* v packets                                                          */
/* ------------------------------------------------------------------ */

TEST_CASE_FIXTURE (remote_fixture, "vCont lists what it supports")
{
  /* "Packets", 'vCont?': the reply lists the supported vCont actions, or
     is empty when vCont is not supported at all.  This stub announces
     continue and step.  */
  std::string query;
  {
    gdb_session session;
    query = session.request (pkt ("vCont?"));
    session.finish ();
  }

  CHECK (body_of (query) == "vCont;c;s");
}

TEST_CASE_FIXTURE (remote_fixture,
		   "vCont ignores the action it announced (suspected defect)")
{
  /* "Packets", 'vCont[;action]...': the action follows the semicolon, so
     for "vCont;c" the action letter is at offset 6 of the packet data.
     The stub switches on offset 5, which is the semicolon itself, so no
     vCont action is ever recognised even though vCont? announces two.
     Both requests fall into the unknown action arm.

     That arm is a second defect.  An unsupported action is answered with
     the empty packet "$#00"; the stub builds it as "$#" and then runs the
     checksum routine over the '#' it just wrote, sending "$##23", a packet
     whose data is a single '#'.  */
  std::string cont, step, bad;
  {
    gdb_session session;
    poke (RAM, 0x01000000); /* nop */
    poke (RAM + 4, 0x01000000);
    cont = session.request (pkt ("vCont;c"));
    step = session.request (pkt ("vCont;s"));
    bad = session.request (pkt ("vCont;X"));
    session.finish ();
  }

  CHECK (cont == "+$##23");
  CHECK (step == "+$##23");
  CHECK (bad == "+$##23");

  /* The program never ran, because no action was recognised.  */
  CHECK (sregs[0].pc == RAM);
}

TEST_CASE_FIXTURE (remote_fixture,
		   "the vCont actions answer only misspelled (suspected "
		   "defect)")
{
  /* The continue and step arms of the vCont switch are reachable only by
     leaving out the semicolon the protocol requires, which puts the action
     letter at the offset the stub reads.  "vContc" and "vConts" are not
     protocol; they are the shape that reaches the code GDB cannot.  This
     case pins what those arms do so that the defect above is fixable
     without guessing at their behaviour.  */
  std::string step, cont;
  {
    gdb_session session;
    poke (RAM, 0x01000000); /* nop */
    poke (RAM + 4, 0x01000000);
    step = session.request (pkt ("vConts"));
    (void) session.request (pkt ("Z0,2001008,4"));
    cont = session.request (pkt ("vContc"));
    session.finish ();
  }

  /* Both stop with SIGTRAP, the step after one instruction and the
     continue at the breakpoint.  */
  CHECK (body_of (step) == "S05");
  CHECK (body_of (cont) == "S05");
  CHECK (sregs[0].pc == RAM + 8);
}

TEST_CASE_FIXTURE (remote_fixture, "vKill and vRun restart the program")
{
  /* "Packets", 'vKill;pid' kills a process and 'vRun;filename;args'
     restarts one.  vRun is answered with a stop reply for the new program,
     which has not run yet.  */
  std::string killed, ran, unknown;
  {
    gdb_session session;
    last_load_addr = RAM;
    sregs[0].pc = 0;
    killed = session.request (pkt ("vKill;1"));
    sregs[0].pc = 0;
    ran = session.request (pkt ("vRun;"));
    /* An unrecognised v packet gets the empty reply that means
       unsupported.  */
    unknown = session.request (pkt ("vMustReplyEmpty"));
    session.finish ();
  }

  CHECK (body_of (killed) == "OK");
  CHECK (body_of (ran) == "S00");
  CHECK (body_of (unknown) == "");
  CHECK (sregs[0].pc == RAM);
}

/* ------------------------------------------------------------------ */
/* Breakpoints and watchpoints                                        */
/* ------------------------------------------------------------------ */

TEST_CASE_FIXTURE (remote_fixture, "'z' removes what 'Z' inserted")
{
  /* "Packets", 'z type,addr,kind': remove a breakpoint or watchpoint,
     answered "OK" on success and "E NN" on failure.  */
  std::string set, clear, again;
  {
    gdb_session session;
    poke (RAM, 0x01000000);
    set = session.request (pkt ("Z0,2001000,4"));
    clear = session.request (pkt ("z0,2001000,4"));
    /* Removing it twice fails: it is no longer there.  */
    again = session.request (pkt ("z0,2001000,4"));
    session.finish ();
  }

  CHECK (body_of (set) == "OK");
  CHECK (body_of (clear) == "OK");
  CHECK (body_of (again) == "E01");
  /* The saved instruction is back in memory.  */
  CHECK (peek (RAM) == 0x01000000);
}

TEST_CASE_FIXTURE (remote_fixture, "an unknown breakpoint type is refused")
{
  /* "Packets": types 0 to 4 are defined.  GDB probes for support, and the
     protocol asks for an empty reply when a type is not supported.

     Suspected defect: the stub answers "E01" instead of an empty packet,
     which tells GDB the breakpoint failed rather than that the type is
     unknown.  */
  std::string reply;
  {
    gdb_session session;
    reply = session.request (pkt ("Z9,2001000,4"));
    session.finish ();
  }

  CHECK (body_of (reply) == "E01");
}

/* ------------------------------------------------------------------ */
/* Queries and the rest                                               */
/* ------------------------------------------------------------------ */

TEST_CASE_FIXTURE (remote_fixture, "thread queries report a single thread")
{
  /* "General Query Packets": qfThreadInfo starts a thread list and
     qsThreadInfo continues it; 'l' ends the list.  qAttached answers '0'
     when the target was created by the stub rather than attached to.  */
  std::string first, next, attached, unknown;
  {
    gdb_session session;
    first = session.request (pkt ("qfThreadInfo"));
    next = session.request (pkt ("qsThreadInfo"));
    attached = session.request (pkt ("qAttached"));
    /* An unimplemented query gets the empty reply.  */
    unknown = session.request (pkt ("qSupported:multiprocess+"));
    session.finish ();
  }

  CHECK (body_of (first) == "l");
  CHECK (body_of (next) == "l");
  CHECK (body_of (attached) == "0");
  CHECK (body_of (unknown) == "");
}

TEST_CASE_FIXTURE (remote_fixture, "qRcmd runs a simulator command")
{
  /* "General Query Packets", 'qRcmd,command': the command is hex encoded
     ASCII and is passed to the target's command interpreter.  "OK" means
     it ran.  */
  std::string reply, log;
  {
    gdb_session session;
    /* "echo remote" */
    reply = session.request (pkt ("qRcmd,6563686f2072656d6f7465"));
    session.finish ();
    log = session.log;
  }

  CHECK (body_of (reply) == "OK");
  CHECK (log.find ("remote") != std::string::npos);
}

TEST_CASE_FIXTURE (remote_fixture, "'H' selects a thread")
{
  /* "Packets", 'H op thread-id': set the thread for the following
     operations, answered "OK".  'Hc' is the deprecated form for step and
     continue, superseded by vCont, and gets the empty reply that means
     unsupported.  */
  std::string general, cont;
  {
    gdb_session session;
    general = session.request (pkt ("Hg0"));
    cont = session.request (pkt ("Hc-1"));
    session.finish ();
  }

  CHECK (body_of (general) == "OK");
  CHECK (body_of (cont) == "");
}

TEST_CASE_FIXTURE (remote_fixture, "'!' enables extended mode")
{
  /* "Packets", '!': enable extended mode, answered "OK" when the stub
     supports it.  */
  std::string reply;
  {
    gdb_session session;
    reply = session.request (pkt ("!"));
    session.finish ();
  }

  CHECK (body_of (reply) == "OK");
}

TEST_CASE_FIXTURE (remote_fixture, "an unknown packet gets the empty reply")
{
  /* Overview: "the reply to an unknown packet is the empty packet", which
     is how GDB probes for optional features.  */
  std::string reply, log;
  {
    gdb_session session;
    reply = session.request (pkt ("Ydeadbeef,0:"));
    session.finish ();
    log = session.log;
  }

  CHECK (ack_of (reply) == '+');
  CHECK (body_of (reply) == "");
  CHECK (log.find ("Ydeadbeef,0:") != std::string::npos);
}

TEST_CASE_FIXTURE (remote_fixture, "'D' detaches and closes the connection")
{
  /* "Packets", 'D': detach from the target, answered "OK", after which
     the stub stops serving the connection.  */
  std::string reply, after;
  {
    gdb_session session;
    reply = session.request (pkt ("D"));
    /* The acknowledgement of that reply is what ends the session.  */
    session.send_raw ("+");
    after = session.read_some (2000);
    session.finish ();
  }

  CHECK (body_of (reply) == "OK");
  CHECK (after == "");
  CHECK (sis_gdb_break == 0);
}

TEST_CASE_FIXTURE (remote_fixture,
		   "a packet after 'D' is acknowledged but not served")
{
  /* Once detached the stub answers no more packets: the '+' that
     acknowledges receipt is the last thing it sends.  */
  std::string reply, after;
  {
    gdb_session session;
    reply = session.request (pkt ("D"));
    session.send_raw (pkt ("!"));
    after = session.read_some (2000);
    session.finish ();
  }

  CHECK (body_of (reply) == "OK");
  CHECK (after == "+");
}

/* ------------------------------------------------------------------ */
/* The break poll                                                     */
/* ------------------------------------------------------------------ */

TEST_CASE_FIXTURE (remote_fixture, "socket_poll raises ctrl_c on pending data")
{
  /* The stub polls the connection while the program runs, so that a ^C
     from GDB is noticed without stopping at every instruction.
     "Interrupts": the interrupt request is a byte on the connection.  */
  int fds[2];
  REQUIRE (pipe (fds) == 0);
  new_socket = fds[0];

  ctrl_c = 0;
  socket_poll (0);
  CHECK (ctrl_c == 0);

  REQUIRE (write (fds[1], "\x03", 1) == 1);
  socket_poll (0);
  CHECK (ctrl_c == 1);

  ctrl_c = 0;
  close (fds[0]);
  close (fds[1]);
  new_socket = 0;
}

#endif /* !_WIN32 */
