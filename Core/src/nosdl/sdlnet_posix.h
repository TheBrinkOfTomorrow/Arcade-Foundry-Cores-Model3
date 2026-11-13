/*
 * The part of SDL_net 2 that Src/Network (NetServer.cpp, TCPConnection.cpp)
 * uses, on plain POSIX sockets, for cores built without SDL: TCP over IPv4,
 * blocking sends, socket sets checked with poll(). Same names and meanings as
 * SDL_net, so Supermodel's network code builds unchanged.
 */
#ifndef INCLUDED_SDLNET_POSIX_H
#define INCLUDED_SDLNET_POSIX_H

#include <cstdint>

struct IPaddress
{
  uint32_t host;  // network byte order; INADDR_ANY (0) to listen
  uint16_t port;  // network byte order
};

struct SDLNet_TCPSocketImpl;
typedef SDLNet_TCPSocketImpl *TCPsocket;
struct SDLNet_SocketSetImpl;
typedef SDLNet_SocketSetImpl *SDLNet_SocketSet;

int SDLNet_Init(void);
void SDLNet_Quit(void);
const char *SDLNet_GetError(void);

int SDLNet_ResolveHost(IPaddress *address, const char *host, uint16_t port);
TCPsocket SDLNet_TCP_Open(IPaddress *ip);  // listens when ip->host is INADDR_ANY, else connects
TCPsocket SDLNet_TCP_Accept(TCPsocket server);
IPaddress *SDLNet_TCP_GetPeerAddress(TCPsocket sock);
int SDLNet_TCP_Send(TCPsocket sock, const void *data, int len);  // all of it, or fewer on error
int SDLNet_TCP_Recv(TCPsocket sock, void *data, int maxlen);     // > 0 bytes, or <= 0 when closed
void SDLNet_TCP_Close(TCPsocket sock);

SDLNet_SocketSet SDLNet_AllocSocketSet(int maxsockets);
void SDLNet_FreeSocketSet(SDLNet_SocketSet set);
int SDLNet_TCP_AddSocket(SDLNet_SocketSet set, TCPsocket sock);
int SDLNet_TCP_DelSocket(SDLNet_SocketSet set, TCPsocket sock);
int SDLNet_CheckSockets(SDLNet_SocketSet set, uint32_t timeout);  // sockets ready to read, or -1
int SDLNet_SocketReady(TCPsocket sock);

inline uint32_t SDL_SwapBE32(uint32_t x)
{
  const uint8_t *b = reinterpret_cast<const uint8_t *>(&x);
  return uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | b[3];
}

inline uint16_t SDL_SwapBE16(uint16_t x)
{
  const uint8_t *b = reinterpret_cast<const uint8_t *>(&x);
  return uint16_t(b[0] << 8 | b[1]);
}

#endif
