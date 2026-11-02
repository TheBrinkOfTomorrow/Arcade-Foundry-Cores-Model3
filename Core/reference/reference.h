/*
 * The reference build: stock Supermodel with hooks that play the cores'
 * input scripts and record what it shows and plays, so a core's run can be
 * compared with Supermodel's frame for frame. Included by Src/OSD/SDL/Main.cpp
 * and Audio.cpp; does nothing unless ATTRACT_REF_FRAMES is set.
 *
 *   ATTRACT_REF_FRAMES=N        run N frames, then quit
 *   ATTRACT_REF_SCRIPT=FILE     input script (the cores' format, as m3run)
 *   ATTRACT_REF_CRC=FILE        "frame crc32" per screen (0xAARRGGBB words)
 *   ATTRACT_REF_SHOTS=N,N,...   frames to write as PPM to ATTRACT_REF_SHOT_DIR
 *   ATTRACT_REF_WAV=FILE        the sound, mixed to stereo as the core does
 *   ATTRACT_REF_NVRAM=FILE      start from this .nv (the core's format), not blank
 *   ATTRACT_RTC_START=SECONDS   the board's clock at frame 0, then counted in
 *                               frames, as the cores count it
 *
 * Run it as the cores run: supermodel ROM.zip -no-threads -true-hz
 * -no-throttle -no-vsync -window -res=496,384.
 */
#ifndef ATTRACT_REFERENCE_H
#define ATTRACT_REFERENCE_H

#include "Inputs/Inputs.h"
#include "Game.h"
#include "Model3/RTC72421.h"

#include <GL/glew.h>
#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace AttractReference {

struct Line
{
  unsigned long from, to;
  std::string name;
  unsigned value;
};

struct State
{
  bool active = false;
  unsigned long frames = 0;
  unsigned long frame = 0;
  std::vector<Line> lines;
  FILE *crc = nullptr;
  FILE *wav = nullptr;
  uint32_t wavFrames = 0;
  std::set<unsigned long> shots;
  std::string shotDir = ".";
  std::vector<uint8_t> pixels;
  std::vector<uint32_t> words;
};

inline State &S()
{
  static State s;
  return s;
}

// True for a reference run (set before the machine starts, unlike S().active).
inline bool Active()
{
  return std::getenv("ATTRACT_REF_FRAMES") != nullptr;
}

// A reference run starts from a blank board, or from this NVRAM file (the
// core's own .nv; never written back).
inline const char *Nvram()
{
  return Active() ? std::getenv("ATTRACT_REF_NVRAM") : nullptr;
}

inline void WavHeader(FILE *f, uint32_t frames)
{
  auto u32 = [f](uint32_t v) { fwrite(&v, 4, 1, f); };
  auto u16 = [f](uint16_t v) { fwrite(&v, 2, 1, f); };
  fwrite("RIFF", 1, 4, f); u32(36 + frames * 4); fwrite("WAVEfmt ", 1, 8, f);
  u32(16); u16(1); u16(2); u32(44100); u32(44100 * 4); u16(4); u16(16);
  fwrite("data", 1, 4, f); u32(frames * 4);
}

inline void Begin()
{
  State &s = S();
  const char *n = std::getenv("ATTRACT_REF_FRAMES");
  if (!n)
    return;
  s.active = true;
  s.frames = std::strtoul(n, nullptr, 10);
  if (const char *p = std::getenv("ATTRACT_REF_SCRIPT"))
  {
    std::ifstream f(p);
    std::string line;
    while (std::getline(f, line))
    {
      if (line.empty() || line[0] == '#' || line.compare(0, 6, "frames") == 0)
        continue;
      std::istringstream in(line);
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
        if (eq != std::string::npos)
          s.lines.push_back({ from, to, kv.substr(0, eq), unsigned(std::stoul(kv.substr(eq + 1), nullptr, 0)) });
      }
    }
  }
  if (std::getenv("ATTRACT_RTC_START"))
    g_RTCTimeSource = [] {
      return time_t(std::strtoll(std::getenv("ATTRACT_RTC_START"), nullptr, 10)) + time_t(double(S().frame) / 57.524);
    };
  if (const char *p = std::getenv("ATTRACT_REF_CRC"))
    s.crc = fopen(p, "w");
  if (const char *p = std::getenv("ATTRACT_REF_WAV"))
  {
    s.wav = fopen(p, "wb");
    if (s.wav)
      WavHeader(s.wav, 0);
  }
  if (const char *p = std::getenv("ATTRACT_REF_SHOT_DIR"))
    s.shotDir = p;
  if (const char *p = std::getenv("ATTRACT_REF_SHOTS"))
  {
    std::stringstream ss(p);
    std::string v;
    while (std::getline(ss, v, ','))
      s.shots.insert(std::stoul(v));
  }
}

// After the input system's poll: the script's values for this frame replace
// whatever the keyboard and joysticks said.
inline void ApplyInputs(CInputs *in)
{
  State &s = S();
  if (!s.active)
    return;
  std::map<std::string, unsigned> v;
  for (const Line &l : s.lines)
    if (s.frame >= l.from && s.frame <= l.to)
      v[l.name] = l.value;
  auto set = [&v](const std::shared_ptr<CSwitchInput> &i, const char *name) {
    if (i)
      i->value = v.count(name) && v[name] ? 1 : 0;
  };
  set(in->coin[0], "coin");
  set(in->coin[1], "coin_b");
  set(in->start[0], "start");
  set(in->start[1], "start2");
  set(in->test[0], "test");
  set(in->service[0], "service");
  set(in->test[1], "test_b");
  set(in->service[1], "service_b");
  static const char *const names[2][8] = {
    { "up", "down", "left", "right", "punch", "kick", "guard", "escape" },
    { "up2", "down2", "left2", "right2", "punch2", "kick2", "guard2", "escape2" },
  };
  for (int p = 0; p < 2; p++)
  {
    set(in->up[p], names[p][0]);
    set(in->down[p], names[p][1]);
    set(in->left[p], names[p][2]);
    set(in->right[p], names[p][3]);
    set(in->punch[p], names[p][4]);
    set(in->kick[p], names[p][5]);
    set(in->guard[p], names[p][6]);
    set(in->escape[p], names[p][7]);
  }
  // Driving games: steer, accel, brake in the board's units (0 .. 0xff, the
  // wheel centred at 0x80), gear 0 .. 4.
  auto analog = [&v](CInput *i, const char *name, UINT16 off) {
    if (i)
      i->value = v.count(name) ? UINT16(v[name]) : off;
  };
  analog(in->steering.get(), "steer", 0x80);
  analog(in->accelerator.get(), "accel", 0);
  analog(in->brake.get(), "brake", 0);
  analog(in->gearShift4.get(), "gear", 0);
  set(in->gearShiftUp, "shiftup");
  set(in->gearShiftDown, "shiftdown");
  set(in->vr[0], "view1");
  set(in->vr[1], "view2");
  set(in->vr[2], "view3");
  set(in->vr[3], "view4");
  set(in->viewChange, "viewchange");
  set(in->handBrake, "handbrake");
  set(in->musicSelect, "musicselect");
  set(in->twinJoyUp1, "up");
  set(in->twinJoyDown1, "down");
  set(in->twinJoyLeft1, "left");
  set(in->twinJoyRight1, "right");
  set(in->twinJoyUp2, "up2");
  set(in->twinJoyDown2, "down2");
  set(in->twinJoyLeft2, "left2");
  set(in->twinJoyRight2, "right2");
  set(in->twinJoyShot1, "shot1");
  set(in->twinJoyShot2, "shot2");
  set(in->twinJoyTurbo1, "turbo1");
  set(in->twinJoyTurbo2, "turbo2");
  set(in->beat, "beat");
  set(in->charge, "charge");
  set(in->shift, "shift");
  set(in->jump, "jump");
  set(in->shortPass[0], "shortpass");
  set(in->longPass[0], "longpass");
  set(in->shoot[0], "shoot");
  set(in->shortPass[1], "shortpass2");
  set(in->longPass[1], "longpass2");
  set(in->shoot[1], "shoot2");
  analog(in->analogJoyX.get(), "stickx", 0x80);
  analog(in->analogJoyY.get(), "sticky", 0x80);
  set(in->analogJoyTrigger1, "trigger1");
  set(in->analogJoyTrigger2, "trigger2");
  set(in->analogJoyEvent1, "event1");
  set(in->analogJoyEvent2, "event2");
  // Analog guns: gunx/guny (gunx2/guny2) 0 .. 0xff across and down the
  // screen; the board's Y runs the other way.
  static const char *const gunNames[2][4] = {
    { "gunx", "guny", "lefttrigger", "righttrigger" },
    { "gunx2", "guny2", "lefttrigger2", "righttrigger2" },
  };
  for (int p = 0; p < 2; p++)
  {
    if (in->analogGunX[p])
      in->analogGunX[p]->value = v.count(gunNames[p][0]) ? UINT16(v[gunNames[p][0]]) : 0;
    if (in->analogGunY[p])
      in->analogGunY[p]->value = UINT16(255 - (v.count(gunNames[p][1]) ? v[gunNames[p][1]] : 0));
    set(in->analogTriggerLeft[p], gunNames[p][2]);
    set(in->analogTriggerRight[p], gunNames[p][3]);
  }
  if (in->analogTriggerLeft[0] && v.count("trigger1") && v["trigger1"])
    in->analogTriggerLeft[0]->value = 1;
  if (in->analogTriggerRight[0] && v.count("trigger2") && v["trigger2"])
    in->analogTriggerRight[0]->value = 1;
  // Magical Truck Adventure: lever/lever2 in the board's units (0xff forward ..
  // 0x80 .. 0 back), pedal/pedal2 the pedals.
  analog(in->magicalLever1.get(), "lever", 0x80);
  analog(in->magicalLever2.get(), "lever2", 0x80);
  set(in->magicalPedal1, "pedal");
  set(in->magicalPedal2, "pedal2");
  // Sega Bass Fishing: rodx/rody, reel and tension in the board's units, the
  // lure stick from up/down/left/right, cast and select.
  analog(in->fishingRodX.get(), "rodx", 0x80);
  analog(in->fishingRodY.get(), "rody", 0x80);
  analog(in->fishingReel.get(), "reel", 0);
  analog(in->fishingTension.get(), "tension", 0);
  if (in->fishingStickX)
    in->fishingStickX->value = v.count("left") && v["left"] ? 0x00 : v.count("right") && v["right"] ? 0xff : 0x80;
  if (in->fishingStickY)
    in->fishingStickY->value = v.count("up") && v["up"] ? 0x00 : v.count("down") && v["down"] ? 0xff : 0x80;
  set(in->fishingCast, "cast");
  set(in->fishingSelect, "select");
}

// Before the swap: the frame as shown, from the back buffer.
inline void Capture(int width, int height)
{
  State &s = S();
  if (!s.active)
    return;
  s.pixels.resize(size_t(width) * height * 4);
  s.words.resize(size_t(width) * height);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  glReadBuffer(GL_BACK);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, s.pixels.data());
  for (int y = 0; y < height; y++)
  {
    const uint8_t *src = &s.pixels[size_t(height - 1 - y) * width * 4];
    uint32_t *dst = &s.words[size_t(y) * width];
    for (int x = 0; x < width; x++, src += 4)
      dst[x] = 0xff000000u | uint32_t(src[0]) << 16 | uint32_t(src[1]) << 8 | src[2];
  }
  if (s.crc)
    fprintf(s.crc, "%lu %08lx\n", s.frame, crc32(0, reinterpret_cast<const Bytef *>(s.words.data()), uInt(s.words.size() * 4)));
  if (s.shots.count(s.frame))
  {
    std::string path = s.shotDir + "/frame_" + std::to_string(s.frame) + ".ppm";
    if (FILE *p = fopen(path.c_str(), "wb"))
    {
      fprintf(p, "P6\n%d %d\n255\n", width, height);
      for (uint32_t w : s.words)
      {
        unsigned char rgb[3] = { uint8_t(w >> 16), uint8_t(w >> 8), uint8_t(w) };
        fwrite(rgb, 1, 3, p);
      }
      fclose(p);
    }
  }
}

// The sound board's frame, before Supermodel's rate control: mixed to
// stereo as the core mixes it.
inline void Audio(unsigned n, const float *fl, const float *fr, const float *rl, const float *rr, bool flip)
{
  State &s = S();
  if (!s.active || !s.wav)
    return;
  for (unsigned i = 0; i < n; i++)
  {
    float l = std::clamp((fl[i] + rl[i]) * 0.5f / 32768.0f, -1.0f, 1.0f);
    float r = std::clamp((fr[i] + rr[i]) * 0.5f / 32768.0f, -1.0f, 1.0f);
    if (flip)
      std::swap(l, r);
    int16_t pcm[2] = { int16_t(std::clamp(l * 32767.0f, -32768.0f, 32767.0f)), int16_t(std::clamp(r * 32767.0f, -32768.0f, 32767.0f)) };
    fwrite(pcm, 2, 2, s.wav);
    s.wavFrames++;
  }
}

// After each frame: true once the run is over.
inline bool EndFrame()
{
  State &s = S();
  if (!s.active)
    return false;
  s.frame++;
  if (s.frame < s.frames)
    return false;
  if (s.crc)
    fclose(s.crc);
  if (s.wav)
  {
    fseek(s.wav, 0, SEEK_SET);
    WavHeader(s.wav, s.wavFrames);
    fclose(s.wav);
  }
  s.crc = s.wav = nullptr;
  return true;
}

}  // namespace AttractReference

#endif
