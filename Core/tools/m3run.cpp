// m3run: runs a Model 3 core headless for a number of frames.
//
//   m3run ROMSET.zip FRAMES [--inputs SCRIPT] [--crc FILE] [--shot N,N,... --shot-dir DIR]
//         [--wav FILE] [--save DIR] [--stereo N,N,...] [--head N,N,...]
//         [--ipd M] [--scale M] [--yaw DEG] [--link-listen PORT --link-next HOST:PORT]
//         [--from-link 1] [--save-state-at N --state FILE] [--load-state FILE]
//
// --crc writes "frame crc32" for every screen (0xAARRGGBB words); --shot
// writes those frames as binary PPM; --wav the sound (44.1 kHz stereo,
// 16-bit). Input scripts are the cores' format: "<from>-<to> name=value ..."
// or "<at> name=value ...", "#" comments. Names: coin start start2 test
// service, and punch kick guard escape up down left right (a 2 suffix is
// player 2).
//
// --link-listen/--link-next: link play (afcore link_start): listen for the
// cabinet before this one, connect to the next. Which cabinet this is comes
// from the game's settings in --save. The link's state is printed when it
// changes and at the end. --from-link 1: the input script, --shot, --crc
// and FRAMES count from the frame after the link comes up, and no inputs go
// in before it. Cabinets started a few seconds apart come up at different
// frames of their own and then stay that far apart in lockstep, so a script
// in absolute frames presses each cabinet's buttons at a different moment
// of the linked game.
//
// --stereo writes those frames' two eyes (render_eyes, AFCORE_STEREO_PAIR,
// --ipd apart) side by side as frame_N_pair.ppm; --head both eyes of a
// headset-like view (AFCORE_STEREO_HEAD, 512 x 512 an eye, 90 degrees, the
// head turned --yaw degrees left, default 20) as frame_N_head.ppm, drawn for
// the 60 frames before too, as a headset front end would (the core opens
// its frustum). --scale: metres per game unit (0: the core's).
//
// --save-state-at N --state FILE: after frame N, write a save state
// (afcore save_state) to FILE and carry on. --load-state FILE: load it before
// the first frame; frames (the script, --crc, --shot) then count on from the
// state's frame, so a run from the state and the run that saved it can be
// compared frame by frame (Core/tests/save_state_check.sh).
#include "afcore.h"
#include "afcommon/state.h"

#include <zlib.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#define M3_CAT2(a, b) a##b
#define M3_CAT(a, b) M3_CAT2(a, b)

extern "C" const afcore_api *M3_CAT(afcore_api_, M3CORE_ROMSET)(void);

namespace {

std::set<unsigned long> FrameList(const std::string &v)
{
  std::set<unsigned long> frames;
  std::stringstream ss(v);
  std::string n;
  while (std::getline(ss, n, ','))
    frames.insert(std::stoul(n));
  return frames;
}

bool WritePpm(const std::string &path, const uint32_t *px, int w, int h)
{
  FILE *p = fopen(path.c_str(), "wb");
  if (!p)
    return false;
  fprintf(p, "P6\n%d %d\n255\n", w, h);
  for (int i = 0; i < w * h; i++)
  {
    unsigned char rgb[3] = { uint8_t(px[i] >> 16), uint8_t(px[i] >> 8), uint8_t(px[i]) };
    fwrite(rgb, 1, 3, p);
  }
  return fclose(p) == 0;
}

// render_eyes, then both eyes side by side (left eye on the left).
bool WriteEyes(const afcore_api *api, afcore *core, const afcore_stereo &st, const std::string &path)
{
  if (!api->render_eyes || !api->render_eyes(core, &st))
  {
    fprintf(stderr, "m3run: no 3D scene for %s\n", path.c_str());
    return false;
  }
  int w, h, w2, h2;
  const uint32_t *l = api->eye_screen(core, 0, &w, &h), *r = api->eye_screen(core, 1, &w2, &h2);
  if (!l || !r || w != w2 || h != h2)
    return false;
  std::vector<uint32_t> both(size_t(w) * 2 * h);
  for (int y = 0; y < h; y++)
  {
    std::memcpy(&both[size_t(y) * w * 2], l + size_t(y) * w, size_t(w) * 4);
    std::memcpy(&both[size_t(y) * w * 2 + w], r + size_t(y) * w, size_t(w) * 4);
  }
  return WritePpm(path, both.data(), w * 2, h);
}

}  // namespace

namespace {

struct Line
{
  unsigned long from, to;
  std::string name;
  unsigned value;
};

std::vector<Line> LoadScript(const std::string &path)
{
  std::vector<Line> lines;
  std::ifstream f(path);
  if (!f)
  {
    fprintf(stderr, "m3run: cannot open %s\n", path.c_str());
    exit(1);
  }
  std::string s;
  while (std::getline(f, s))
  {
    if (s.empty() || s[0] == '#' || s.compare(0, 6, "frames") == 0)
      continue;
    std::istringstream in(s);
    std::string range, kv;
    in >> range;
    auto dash = range.find('-');
    unsigned long from = std::stoul(range.substr(0, dash));
    unsigned long to = dash == std::string::npos ? from : std::stoul(range.substr(dash + 1));
    while (in >> kv)
    {
      if (kv[0] == '#')
        break;
      auto eq = kv.find('=');
      if (eq == std::string::npos)
        continue;
      lines.push_back({ from, to, kv.substr(0, eq), unsigned(std::stoul(kv.substr(eq + 1), nullptr, 0)) });
    }
  }
  return lines;
}

const std::map<std::string, uint32_t> kButtons = {
  { "coin", AFCORE_COIN }, { "start", AFCORE_START }, { "start2", AFCORE_START2 }, { "test", AFCORE_TEST },
  { "service", AFCORE_SERVICE }, { "punch", AFCORE_PUNCH }, { "kick", AFCORE_KICK }, { "guard", AFCORE_GUARD },
  { "escape", AFCORE_BUTTON2 }, { "up", AFCORE_UP }, { "down", AFCORE_DOWN }, { "left", AFCORE_LEFT },
  { "right", AFCORE_RIGHT }, { "view1", AFCORE_VIEW1 }, { "view2", AFCORE_VIEW2 }, { "view3", AFCORE_VIEW3 },
  { "view4", AFCORE_VIEW4 }, { "shiftup", AFCORE_SHIFT_UP }, { "shiftdown", AFCORE_SHIFT_DOWN },
  { "viewchange", AFCORE_VIEW1 }, { "musicselect", AFCORE_VIEW2 },
  { "shot1", AFCORE_TRIGGER1 }, { "shot2", AFCORE_TRIGGER2 }, { "turbo1", AFCORE_BUTTON2 }, { "turbo2", AFCORE_BUTTON3 },
  { "beat", AFCORE_PUNCH }, { "charge", AFCORE_KICK }, { "shift", AFCORE_GUARD }, { "jump", AFCORE_BUTTON2 },
  { "shortpass", AFCORE_PUNCH }, { "longpass", AFCORE_KICK }, { "shoot", AFCORE_GUARD },
  { "trigger1", AFCORE_TRIGGER1 }, { "trigger2", AFCORE_TRIGGER2 }, { "event1", AFCORE_BUTTON2 }, { "event2", AFCORE_BUTTON3 },
  { "lefttrigger", AFCORE_TRIGGER1 }, { "righttrigger", AFCORE_TRIGGER2 }, { "pedal", AFCORE_PUNCH },
  { "cast", AFCORE_PUNCH }, { "select", AFCORE_KICK },
};

afcore_input InputAt(const std::vector<Line> &lines, unsigned long frame)
{
  afcore_input in{};
  in.size = sizeof(in);
  for (const Line &l : lines)
  {
    if (frame < l.from || frame > l.to)
      continue;
    // Analog controls in the board's units, as the reference takes them.
    if (l.name == "steer") { in.steer = (float(l.value) - 127.5f) / 127.5f; continue; }
    if (l.name == "accel") { in.accel = float(l.value) / 255.0f; continue; }
    if (l.name == "brake") { in.brake = float(l.value) / 255.0f; continue; }
    if (l.name == "gear") { in.gear = int(l.value); continue; }
    if (l.name == "handbrake") { in.handbrake = l.value ? 1.0f : 0.0f; continue; }
    if (l.name == "rodx") { in.steer = (float(l.value) - 127.5f) / 127.5f; continue; }
    if (l.name == "rody") { in.stick_y = (float(l.value) - 127.5f) / 127.5f; continue; }
    if (l.name == "reel") { in.accel = float(l.value) / 255.0f; continue; }
    if (l.name == "tension") { in.brake = float(l.value) / 255.0f; continue; }
    if (l.name == "stickx") { in.steer = (float(l.value) - 127.5f) / 127.5f; continue; }
    if (l.name == "sticky") { in.stick_y = (float(l.value) - 127.5f) / 127.5f; continue; }
    // Analog guns, 0 .. 0xff across and down the screen.
    if (l.name == "gunx" || l.name == "gunx2") { in.gun_x[l.name.back() == '2'] = float(l.value) / 255.0f; continue; }
    if (l.name == "guny" || l.name == "guny2") { in.gun_y[l.name.back() == '2'] = float(l.value) / 255.0f; continue; }
    // Magical Truck Adventure's levers in the board's units: 0xff forward (UP),
    // 0 back (DOWN), else centred (0x80).
    if (l.name == "lever" || l.name == "lever2")
    {
      uint32_t &pb = l.name == "lever" ? in.buttons : in.buttons2;
      if (l.value >= 0xc0) pb |= AFCORE_UP;
      else if (l.value <= 0x40) pb |= AFCORE_DOWN;
      continue;
    }
    if (!l.value)
      continue;
    std::string n = l.name;
    // A trailing 2 is player 2's control, unless the name is a control of its
    // own (start2, view2).
    bool p2 = !kButtons.count(n) && n.size() > 1 && n.back() == '2';
    if (p2)
      n.pop_back();
    auto it = kButtons.find(n);
    if (it == kButtons.end())
      continue;
    (p2 ? in.buttons2 : in.buttons) |= it->second;
  }
  return in;
}

void WriteWavHeader(FILE *f, uint32_t frames)
{
  auto u32 = [f](uint32_t v) { fwrite(&v, 4, 1, f); };
  auto u16 = [f](uint16_t v) { fwrite(&v, 2, 1, f); };
  fwrite("RIFF", 1, 4, f); u32(36 + frames * 4); fwrite("WAVEfmt ", 1, 8, f);
  u32(16); u16(1); u16(2); u32(44100); u32(44100 * 4); u16(4); u16(16);
  fwrite("data", 1, 4, f); u32(frames * 4);
}

}  // namespace

int main(int argc, char **argv)
{
  if (argc < 3)
  {
    fprintf(stderr, "usage: m3run ROMSET.zip FRAMES [--inputs SCRIPT] [--crc FILE] [--shot N,N --shot-dir DIR] [--wav FILE] [--save DIR]\n");
    return 2;
  }
  std::string rom = argv[1];
  unsigned long frames = std::stoul(argv[2]);
  std::string script, crcPath, shotDir = ".", wavPath, saveDir, linkNext;
  int linkListen = 0;
  bool fromLink = false;
  std::string statePath, loadPath;
  long saveAt = -1;
  std::set<unsigned long> shots, pairShots, headShots;
  float ipd = 0, scale = 0, yaw = 20;
  for (int i = 3; i + 1 < argc; i += 2)
  {
    std::string a = argv[i], v = argv[i + 1];
    if (a == "--inputs") script = v;
    else if (a == "--crc") crcPath = v;
    else if (a == "--shot-dir") shotDir = v;
    else if (a == "--wav") wavPath = v;
    else if (a == "--save") saveDir = v;
    else if (a == "--shot") shots = FrameList(v);
    else if (a == "--stereo") pairShots = FrameList(v);
    else if (a == "--head") headShots = FrameList(v);
    else if (a == "--ipd") ipd = std::stof(v);
    else if (a == "--scale") scale = std::stof(v);
    else if (a == "--yaw") yaw = std::stof(v);
    else if (a == "--link-listen") linkListen = std::stoi(v);
    else if (a == "--link-next") linkNext = v;
    else if (a == "--from-link") fromLink = (v != "0");
    else if (a == "--save-state-at") saveAt = std::stol(v);
    else if (a == "--state") statePath = v;
    else if (a == "--load-state") loadPath = v;
    else
    {
      fprintf(stderr, "m3run: unknown option %s\n", a.c_str());
      return 2;
    }
  }
  std::vector<Line> lines;
  if (!script.empty())
    lines = LoadScript(script);

  const afcore_api *api = M3_CAT(afcore_api_, M3CORE_ROMSET)();
  char err[512] = "";
  // A fixed start for the board's clock, as refrun.sh gives the reference.
  if (!std::getenv("ATTRACT_RTC_START"))
    setenv("ATTRACT_RTC_START", "1767225600", 1);  // 2026-01-01 00:00 UTC
  // Frame checks compare each frame's picture, so the Metal renderer hands
  // out the frame just run rather than the last one (ATTRACT_M3_SYNC=0 for
  // the app's asynchronous frames).
  if (!std::getenv("ATTRACT_M3_SYNC"))
    setenv("ATTRACT_M3_SYNC", "1", 1);
  afcore *core = api->create(rom.c_str(), saveDir.empty() ? nullptr : saveDir.c_str(), err, sizeof(err));
  if (!core)
  {
    fprintf(stderr, "m3run: %s\n", err);
    return 1;
  }
  if (fromLink && linkListen <= 0)
  {
    fprintf(stderr, "m3run: --from-link needs --link-listen\n");
    return 2;
  }
  if (linkListen > 0 || !linkNext.empty())
  {
    if (api->link_start(core, linkListen, linkNext.c_str(), 1, err, sizeof(err)) != 0)
    {
      fprintf(stderr, "m3run: link: %s\n", err);
      api->destroy(core);
      return 1;
    }
  }
  if (saveAt >= 0 && (statePath.empty() || !api->save_state))
  {
    fprintf(stderr, "m3run: --save-state-at needs --state and a core with save states\n");
    return 2;
  }
  unsigned long offset = 0;  // the frame the loaded state was saved after
  if (!loadPath.empty())
  {
    std::ifstream f(loadPath, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    afc_state_info info;
    if (!api->load_state || afc_state_info_read(bytes.data(), bytes.size(), &info, err, sizeof(err)) != 0 ||
        api->load_state(core, bytes.data(), bytes.size(), err, sizeof(err)) != 0)
    {
      fprintf(stderr, "m3run: --load-state %s: %s\n", loadPath.c_str(), api->load_state ? err : "the core has no save states");
      api->destroy(core);
      return 1;
    }
    offset = (unsigned long)info.frame;
    fprintf(stderr, "m3run: loaded %s (frame %lu)\n", loadPath.c_str(), offset);
  }
  static const char *const kLinkStates[] = {"off", "looking for the other cabinets", "up", "lost"};
  int lastLink = -1;
  FILE *crc = crcPath.empty() ? nullptr : fopen(crcPath.c_str(), "w");
  FILE *wav = wavPath.empty() ? nullptr : fopen(wavPath.c_str(), "wb");
  uint32_t wavFrames = 0;
  if (wav)
    WriteWavHeader(wav, 0);
  std::vector<float> audio(8192 * 2);
  std::vector<int16_t> pcm(8192 * 2);

  afcore_stereo pair{}, head{};
  pair.size = head.size = sizeof(afcore_stereo);
  pair.mode = AFCORE_STEREO_PAIR;
  pair.world_scale = head.world_scale = scale;
  pair.ipd = ipd;
  head.mode = AFCORE_STEREO_HEAD;
  {
    const float a = yaw * 3.14159265f / 180.0f, c = std::cos(a), s = std::sin(a);
    for (int e = 0; e < 2; e++)
    {
      // camera to eye: the head turned left by yaw, this eye 31.5 mm to its side
      float *m = head.eye[e].view;
      std::memset(m, 0, 16 * sizeof(float));
      m[0] = c, m[2] = -s, m[5] = 1, m[8] = s, m[10] = c, m[15] = 1;
      m[12] = e ? -0.0315f : 0.0315f;
      head.eye[e].tan_left = -1, head.eye[e].tan_right = 1, head.eye[e].tan_down = -1, head.eye[e].tan_up = 1;
      head.eye[e].width = 512, head.eye[e].height = 512;
    }
  }

  auto t0 = std::chrono::steady_clock::now();
  // With --from-link, frames before the link is up don't count; give up if
  // it isn't up within 10 minutes of game time.
  const unsigned long kMaxWait = 36000;
  bool counting = !fromLink;
  unsigned long base = 0, ran = 0;
  for (unsigned long f = 0; counting ? f - base < frames : f < kMaxWait; f++, ran++)
  {
    afcore_input in{};
    in.size = sizeof(in);
    if (counting)
      in = InputAt(lines, offset + f - base);
    api->run_frame(core, &in);
    if (const char *e = api->error(core))
    {
      fprintf(stderr, "m3run: frame %lu: %s\n", f, e);
      break;
    }
    if (linkListen > 0)
    {
      int id = 0, count = 0, state = api->link_state(core, &id, &count);
      if (state != lastLink)
      {
        if (state == AFCORE_LINK_UP)
        {
          fprintf(stderr, "m3run: frame %lu: link up, cabinet %d of %d\n", f, id, count);
          if (!counting)
          {
            counting = true;
            base = f + 1;
            fprintf(stderr, "m3run: frames from here count from 0 (--from-link)\n");
          }
        }
        else
          fprintf(stderr, "m3run: frame %lu: link %s\n", f, kLinkStates[state & 3]);
        lastLink = state;
      }
    }
    int w, h;
    const uint32_t *px = api->screen(core, &w, &h);
    if (counting && f >= base)
    {
      const unsigned long g = offset + f - base;  // the frame as the script and shots count it
      if (crc)
        fprintf(crc, "%lu %08lx\n", g, crc32(0, reinterpret_cast<const Bytef *>(px), uInt(w * h * 4)));
      if (shots.count(g))
        WritePpm(shotDir + "/frame_" + std::to_string(g) + ".ppm", px, w, h);
      if (pairShots.count(g))
        WriteEyes(api, core, pair, shotDir + "/frame_" + std::to_string(g) + "_pair.ppm");
      if (headShots.count(g))
        WriteEyes(api, core, head, shotDir + "/frame_" + std::to_string(g) + "_head.ppm");
      else if (api->render_eyes && headShots.lower_bound(g) != headShots.end() && *headShots.lower_bound(g) - g <= 60)
        api->render_eyes(core, &head);
      if (saveAt >= 0 && g == (unsigned long)saveAt)
      {
        std::vector<char> state(api->state_size(core));
        size_t n = api->save_state(core, state.data(), state.size(), err, sizeof(err));
        if (!n)
          fprintf(stderr, "m3run: frame %lu: save state: %s\n", g, err);
        else
        {
          std::ofstream(statePath, std::ios::binary).write(state.data(), std::streamsize(n));
          fprintf(stderr, "m3run: frame %lu: saved %s (%zu bytes)\n", g, statePath.c_str(), n);
        }
      }
    }
    size_t n;
    while ((n = api->audio_read(core, 0, audio.data(), audio.size() / 2)) > 0)
    {
      if (!wav)
        continue;
      for (size_t i = 0; i < n * 2; i++)
      {
        float s = audio[i] * 32767.0f;
        pcm[i] = int16_t(s > 32767.0f ? 32767 : s < -32768.0f ? -32768 : s);
      }
      fwrite(pcm.data(), 2, n * 2, wav);
      wavFrames += uint32_t(n);
    }
  }
  double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  if (fromLink && !counting)
    fprintf(stderr, "m3run: --from-link: the link did not come up in %lu frames\n", kMaxWait);
  fprintf(stderr, "m3run: %lu frames in %.1f s (%.1f frames/s)\n", ran, secs, ran / secs);
  if (crc)
    fclose(crc);
  if (wav)
  {
    fseek(wav, 0, SEEK_SET);
    WriteWavHeader(wav, wavFrames);
    fclose(wav);
  }
  api->destroy(core);
  return 0;
}
