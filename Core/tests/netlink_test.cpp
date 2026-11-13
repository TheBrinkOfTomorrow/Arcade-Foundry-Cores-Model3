/*
 * netlink_test: one cabinet's network board (Supermodel's CSimNetBoard) in a
 * process, driven the way a type-one game (Daytona USA 2, Scud Race, Sega
 * Rally 2) drives it, with no ROM. Start one per cabinet to make a ring on
 * this machine or a LAN; netlink_ring.sh does it. Built twice: on the cores'
 * plain sockets (m3netlink) and, when SDL2_net is there, on SDL_net as stock
 * Supermodel is (m3netlink_sdl), so a ring can mix the two.
 *
 *   m3netlink master|slave LISTEN_PORT NEXT_HOST:PORT FRAMES [TIMEOUT_MS]
 *
 * Each frame the cabinet writes (its listen port, frame) into its own
 * segment. Once the link is up it checks that every other segment holds
 * another cabinet's port, all different, and that their frames move. Prints
 * one line a second and a last line:
 *   RESULT up=<frames up> index=<machine index, 0 master> count=<machines> state=<ready|waiting|error>
 *          bad=<frames with a wrong segment> max_frame_ms=<longest frame while waiting>
 * TIMEOUT_MS sets LinkTimeout (the cores' 3000 by default, 0 = stock: wait
 * for ever).
 */
#include "Supermodel.h"
#include "Util/NewConfig.h"
#include "Network/NetServer.h"
#include "Model3/SimNetBoard.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

const char *StateName(int linkInfo)
{
  switch (linkInfo)
  {
  case 0: return "off";
  case 1: return "waiting";
  case 2: return "ready";
  default: return "error";
  }
}

}  // namespace

int main(int argc, char **argv)
{
  if (argc < 5)
  {
    std::fprintf(stderr, "usage: %s master|slave LISTEN_PORT NEXT_HOST:PORT FRAMES [TIMEOUT_MS]\n", argv[0]);
    return 2;
  }
  const bool master = std::strcmp(argv[1], "master") == 0;
  const unsigned listenPort = unsigned(std::atoi(argv[2]));
  const std::string next = argv[3];
  const long frames = std::atol(argv[4]);
  const unsigned timeoutMs = argc > 5 ? unsigned(std::atoi(argv[5])) : 3000;
  const size_t colon = next.rfind(':');

  SetLogger(std::make_shared<CConsoleErrorLogger>());

  Util::Config::Node config("Global");
  config.Set("Network", true);
  config.Set("SimulateNet", true);
  config.Set("PortIn", listenPort);
  config.Set("PortOut", unsigned(std::atoi(next.c_str() + colon + 1)));
  config.Set<std::string>("AddressOut", next.substr(0, colon));
  config.Set("LinkTimeout", timeoutMs);
  config.Set("LinkHandshakeTimeout", timeoutMs ? 30000u : 0u);

  auto server = std::make_shared<CNetServer>(uint16_t(listenPort), "m3netlink");

  Game game;
  game.name = "daytona2";
  game.netboard_present = true;
  std::vector<uint8_t> netRAM(0x10000), netBuffer(0x20000);
  CSimNetBoard board(config, server);
  board.GetGame(game);
  if (board.Init(netRAM.data(), netBuffer.data()) != Result::OKAY)
  {
    std::printf("RESULT init-failed\n");
    return 1;
  }
  // after Init, which registers the board's handlers (as link_start does)
  if (!server->Start())
  {
    std::printf("RESULT listen-failed\n");
    return 1;
  }

  // What the game's net board code sets up in the board's RAM: master flag,
  // RAM size and segment size (the game's values do not matter here).
  const uint16_t segment = 0x40;
  auto ram16 = [&](unsigned word, uint16_t v) { std::memcpy(&netRAM[word], &v, 2); };
  ram16(0x400, master ? 0 : 1);
  ram16(0x402, 0x2000);
  ram16(0x404, segment);

  board.WriteIORegister(0xc0, 1);  // the game starts the board
  long up = 0, bad = 0;
  int upIndex = -1, upCount = 0;  // as last seen up
  double maxWaitingMs = 0;
  auto lastPrint = std::chrono::steady_clock::now();
  std::vector<uint32_t> lastFrames;
  const auto period = std::chrono::microseconds(17384);  // 57.524 Hz
  auto due = std::chrono::steady_clock::now();
  for (long f = 0; f < frames; f++)
  {
    // the game's side: acknowledge init, write its own segment
    int index = 0, count = 0;
    if (board.LinkInfo(&index, &count) == 1)
      board.WriteIORegister(0x88, 0xf000);  // the game: init done (harmless later)
    uint32_t own[2] = { listenPort, uint32_t(f) };
    for (unsigned i = 0; i < sizeof(own); i++)
      board.WriteCommRAM8(0x100 + i, reinterpret_cast<uint8_t *>(own)[i]);

    const auto t0 = std::chrono::steady_clock::now();
    const int before = board.LinkInfo(nullptr, nullptr);
    board.RunFrame();
    const int after = board.LinkInfo(&index, &count);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (before != 2 && after != 2 && ms > maxWaitingMs)
      maxWaitingMs = ms;

    if (after == 2 && before == 2)
    {
      up++;
      upIndex = index - 1;
      upCount = count;
      // every segment after ours: another cabinet, all different
      std::set<uint32_t> ports = { listenPort };
      std::vector<uint32_t> seen;
      bool ok = true;
      for (int k = 1; k < count; k++)
      {
        uint32_t other[2];
        for (unsigned i = 0; i < sizeof(other); i++)
          reinterpret_cast<uint8_t *>(other)[i] = board.ReadCommRAM8(0x100 + k * segment + i);
        ok &= ports.insert(other[0]).second;
        seen.push_back(other[1]);
      }
      if (up > 2 && seen.size() == lastFrames.size())
        for (size_t k = 0; k < seen.size(); k++)
          ok &= seen[k] == lastFrames[k] + 1;
      lastFrames = seen;
      bad += up > 2 && !ok;
    }

    if (std::chrono::steady_clock::now() - lastPrint > std::chrono::seconds(1))
    {
      lastPrint = std::chrono::steady_clock::now();
      std::printf("%d frame %ld state %s up %ld\n", listenPort, f, StateName(after), up);
      std::fflush(stdout);
    }
    due += period;
    std::this_thread::sleep_until(due);
  }
  const int state = board.LinkInfo(nullptr, nullptr);
  std::printf("RESULT port=%u up=%ld index=%d count=%d state=%s bad=%ld max_frame_ms=%.1f\n", listenPort, up,
              upIndex, upCount, StateName(state), bad, maxWaitingMs);
  return 0;
}
