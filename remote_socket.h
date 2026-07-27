/* SPDX-License-Identifier: GPL-3.0-or-later */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* The listening socket of the GDB stub, written as a template on an
   environment policy so that the system calls it makes are injected rather
   than called directly.  remote.cc instantiates it on an environment that
   forwards to the sockets API; a test instantiates it on an environment that
   reports failures on demand, which is the only way to reach the error paths
   of a call sequence that otherwise never fails on a loopback connection.

   The environment must provide:

     int Socket ();               create a stream socket
     int SetReuseAddr (int fd);   allow the port to be rebound, zero on success
     int Bind (int fd, int port); bind to the port, negative on failure
     int Listen (int fd);         listen with a backlog of one
     int Accept (int fd);         accept one connection, negative on failure
     void Close (int fd);         close the listening socket
     void Configure (int fd);     keep-alive, no delay and descriptor owner
     void Fail (const char *);    report a failed call, was perror  */

#ifndef SIS_REMOTE_SOCKET_H
#define SIS_REMOTE_SOCKET_H

#include "sis.h"

#include <concepts>

namespace remote
{

/* What the listener requires of its environment.  */
template <class E>
concept ListenerEnv = requires (E e, int fd, int port) {
  { e.Socket () } -> std::same_as<int>;
  { e.SetReuseAddr (fd) } -> std::same_as<int>;
  { e.Bind (fd, port) } -> std::same_as<int>;
  { e.Listen (fd) } -> std::same_as<int>;
  { e.Accept (fd) } -> std::same_as<int>;
  { e.Close (fd) };
  { e.Configure (fd) };
  { e.Fail ("") };
};

template <ListenerEnv Env> class Listener
{
public:
  explicit Listener (Env &env) : env_ (env) {}

  /* Wait for one debugger to connect on PORT.  Stores the connected
     descriptor in *CONN and returns 1.  Returns 0 when a step failed, in
     which case *CONN is untouched unless the accept itself failed.  */
  int
  Open (int port, int *conn)
  {
    int fd = env_.Socket ();

    /* A failed socket() returns -1.  Zero is a valid descriptor and is what
       the kernel hands out whenever stdin is closed.  */
    if (fd < 0)
      {
	env_.Fail ("socket failed");
	return 0;
      }

    if (env_.SetReuseAddr (fd))
      {
	env_.Fail ("setsockopt");
	return 0;
      }

    if (env_.Bind (fd, port) < 0)
      {
	env_.Fail ("bind failed");
	return 0;
      }

    if (env_.Listen (fd) < 0)
      {
	env_.Fail ("listen");
	return 0;
      }

    *conn = env_.Accept (fd);
    if (*conn < 0)
      {
	env_.Fail ("accept");
	return 0;
      }

    env_.Close (fd);
    env_.Configure (*conn);

    return 1;
  }

private:
  Env &env_;
};

} /* namespace remote */

#endif /* SIS_REMOTE_SOCKET_H */
