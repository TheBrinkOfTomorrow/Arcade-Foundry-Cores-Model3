/*
 * The network board's TCP link (Network/NetServer.cpp, TCPConnection.cpp use
 * SDL_net) is not in cores built without SDL: link play is not in the Model 3
 * cores yet. The core never creates a server, so these only satisfy the
 * linker; a link would find no other cabinet.
 */
#include "Network/NetServer.h"

void CNetServer::Registration::Reset(void) { m_handlers.reset(); m_id = 0; }

CNetServer::Registration &CNetServer::Registration::operator=(Registration &&other) noexcept
{
  m_handlers = std::move(other.m_handlers);
  m_id = other.m_id;
  other.m_id = 0;
  return *this;
}

CNetServer::Registration::~Registration(void) {}

CNetServer::Registration CNetServer::RegisterHandler(uint32_t, MessageHandler)
{
  return Registration();
}

CNetServer::Registration CNetServer::RegisterDisconnectHandler(DisconnectHandler)
{
  return Registration();
}

std::shared_ptr<CTCPConnection> CNetServer::Connect(const std::string &, uint16_t)
{
  return nullptr;
}

bool CTCPConnection::Send(const void *, size_t) { return false; }
bool CTCPConnection::Send(std::vector<uint8_t>) { return false; }
void CTCPConnection::Close(void) {}
