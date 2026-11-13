/*
 * SDL_net's TCP calls on POSIX sockets (see sdlnet_posix.h).
 */
#include "sdlnet_posix.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

struct SDLNet_TCPSocketImpl
{
  int fd = -1;
  bool server = false;
  bool ready = false;
  IPaddress peer = {0, 0};
};

struct SDLNet_SocketSetImpl
{
  std::mutex mutex;
  std::vector<TCPsocket> sockets;
  size_t max = 0;
};

namespace
{
thread_local std::string g_error;

void SetError(const char *what)
{
  g_error = std::string(what) + ": " + std::strerror(errno);
}

constexpr int kConnectTimeoutMs = 1000;

void NoSigPipe(int fd)
{
#ifdef SO_NOSIGPIPE
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#else
  (void)fd;
#endif
}

void NoDelay(int fd)
{
  int one = 1;  // a frame's block at a time, now: no Nagle delay
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

TCPsocket Wrap(int fd, bool server, const sockaddr_in *peer)
{
  TCPsocket s = new SDLNet_TCPSocketImpl;
  s->fd = fd;
  s->server = server;
  if (peer)
  {
    s->peer.host = peer->sin_addr.s_addr;
    s->peer.port = peer->sin_port;
  }
  return s;
}
}  // namespace

int SDLNet_Init(void)
{
  return 0;
}

void SDLNet_Quit(void)
{
}

const char *SDLNet_GetError(void)
{
  return g_error.c_str();
}

int SDLNet_ResolveHost(IPaddress *address, const char *host, uint16_t port)
{
  address->port = htons(port);
  if (!host)
  {
    address->host = htonl(INADDR_ANY);
    return 0;
  }
  in_addr a;
  if (inet_pton(AF_INET, host, &a) == 1)
  {
    address->host = a.s_addr;
    return 0;
  }
  addrinfo hints = {};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo *res = nullptr;
  if (getaddrinfo(host, nullptr, &hints, &res) != 0 || !res)
  {
    g_error = std::string("unknown host ") + host;
    address->host = htonl(INADDR_NONE);
    return -1;
  }
  address->host = reinterpret_cast<sockaddr_in *>(res->ai_addr)->sin_addr.s_addr;
  freeaddrinfo(res);
  return 0;
}

TCPsocket SDLNet_TCP_Open(IPaddress *ip)
{
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
  {
    SetError("socket");
    return nullptr;
  }
  NoSigPipe(fd);
  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = ip->host;
  addr.sin_port = ip->port;
  if (ip->host == htonl(INADDR_ANY))
  {
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0 || listen(fd, 5) < 0)
    {
      SetError("listen");
      close(fd);
      return nullptr;
    }
    return Wrap(fd, true, nullptr);
  }
  // Connect with a 1 s timeout, then back to blocking as SDL_net is: a
  // blocking connect to a machine that is off waits for the system's
  // timeout (over a minute), and Destroy waits for the connect thread.
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  int rc = connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
  if (rc < 0 && errno == EINPROGRESS)
  {
    pollfd p = {fd, POLLOUT, 0};
    rc = poll(&p, 1, kConnectTimeoutMs);
    if (rc == 1)
    {
      int soError = 0;
      socklen_t len = sizeof(soError);
      getsockopt(fd, SOL_SOCKET, SO_ERROR, &soError, &len);
      errno = soError;
      rc = soError == 0 ? 0 : -1;
    }
    else
    {
      if (rc == 0)
        errno = ETIMEDOUT;
      rc = -1;
    }
  }
  if (rc < 0)
  {
    SetError("connect");
    close(fd);
    return nullptr;
  }
  fcntl(fd, F_SETFL, flags);
  NoDelay(fd);
  return Wrap(fd, false, &addr);
}

TCPsocket SDLNet_TCP_Accept(TCPsocket server)
{
  if (!server || !server->server)
    return nullptr;
  server->ready = false;
  sockaddr_in peer = {};
  socklen_t len = sizeof(peer);
  int fd = accept(server->fd, reinterpret_cast<sockaddr *>(&peer), &len);
  if (fd < 0)
  {
    SetError("accept");
    return nullptr;
  }
  NoSigPipe(fd);
  NoDelay(fd);
  return Wrap(fd, false, &peer);
}

IPaddress *SDLNet_TCP_GetPeerAddress(TCPsocket sock)
{
  if (!sock || sock->server)
    return nullptr;
  return &sock->peer;
}

int SDLNet_TCP_Send(TCPsocket sock, const void *data, int len)
{
  if (!sock || len < 0)
    return -1;
  const char *p = static_cast<const char *>(data);
  int sent = 0;
  while (sent < len)
  {
#ifdef MSG_NOSIGNAL
    ssize_t n = send(sock->fd, p + sent, size_t(len - sent), MSG_NOSIGNAL);
#else
    ssize_t n = send(sock->fd, p + sent, size_t(len - sent), 0);
#endif
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
    {
      SetError("send");
      break;
    }
    sent += int(n);
  }
  return sent;
}

int SDLNet_TCP_Recv(TCPsocket sock, void *data, int maxlen)
{
  if (!sock)
    return -1;
  sock->ready = false;
  ssize_t n;
  do
    n = recv(sock->fd, data, size_t(maxlen), 0);
  while (n < 0 && errno == EINTR);
  if (n < 0)
    SetError("recv");
  return int(n);
}

void SDLNet_TCP_Close(TCPsocket sock)
{
  if (!sock)
    return;
  if (sock->fd >= 0)
  {
    shutdown(sock->fd, SHUT_RDWR);
    close(sock->fd);
  }
  delete sock;
}

SDLNet_SocketSet SDLNet_AllocSocketSet(int maxsockets)
{
  SDLNet_SocketSet set = new SDLNet_SocketSetImpl;
  set->max = size_t(std::max(maxsockets, 1));
  return set;
}

void SDLNet_FreeSocketSet(SDLNet_SocketSet set)
{
  delete set;
}

int SDLNet_TCP_AddSocket(SDLNet_SocketSet set, TCPsocket sock)
{
  std::lock_guard<std::mutex> lock(set->mutex);
  if (set->sockets.size() >= set->max)
  {
    g_error = "socket set is full";
    return -1;
  }
  set->sockets.push_back(sock);
  return int(set->sockets.size());
}

int SDLNet_TCP_DelSocket(SDLNet_SocketSet set, TCPsocket sock)
{
  std::lock_guard<std::mutex> lock(set->mutex);
  set->sockets.erase(std::remove(set->sockets.begin(), set->sockets.end(), sock), set->sockets.end());
  return int(set->sockets.size());
}

int SDLNet_CheckSockets(SDLNet_SocketSet set, uint32_t timeout)
{
  std::vector<TCPsocket> sockets;
  {
    std::lock_guard<std::mutex> lock(set->mutex);
    sockets = set->sockets;
  }
  std::vector<pollfd> fds(sockets.size());
  for (size_t i = 0; i < sockets.size(); i++)
    fds[i] = {sockets[i]->fd, POLLIN, 0};
  int n = poll(fds.data(), nfds_t(fds.size()), int(timeout));
  if (n < 0)
  {
    if (errno == EINTR)
      return 0;
    SetError("poll");
    return -1;
  }
  int ready = 0;
  for (size_t i = 0; i < sockets.size(); i++)
  {
    // A closed or broken socket reads as ready, so its Recv returns 0 and it is closed.
    sockets[i]->ready = (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) != 0;
    ready += sockets[i]->ready ? 1 : 0;
  }
  return ready;
}

int SDLNet_SocketReady(TCPsocket sock)
{
  return sock && sock->ready ? 1 : 0;
}
