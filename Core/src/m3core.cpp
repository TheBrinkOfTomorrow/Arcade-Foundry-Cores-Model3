/*
 * Arcade Foundry core for one Model 3 game: Supermodel's CModel3 behind the
 * afcore C interface (Core/include/afcore.h) that the Arcade Foundry: Insert Coin front end
 * loads. One ROM set per build (CMake M3CORE_ROMSET); the library exports
 * only afcore_api_<romset>().
 *
 * Supermodel keeps the PowerPC, the sound board and the logger in globals,
 * so there is one core per process image: each game is its own library, and
 * only one instance of a library runs at a time.
 *
 * The board runs on the calling thread (Supermodel's MultiThreaded off), so a
 * run is the same every time for the same inputs. The screen is drawn by
 * Supermodel's New3D and 2D renderers into an FBO in the core's own GL
 * context and read back as pixels; with M3CORE_METAL (Apple), New3D draws
 * through the Metal renderer (metal/metal_renderer.h) instead and there is
 * no OpenGL at all.
 */
#include "afcore.h"
#include "core_versions.h"

#include "Supermodel.h"
#include "GameLoader.h"
#include "BlockFile.h"
#include "Model3/Model3.h"
#include "Inputs/Inputs.h"
#include "Graphics/New3D/New3D.h"
#include "Model3/TileGenBuffer.h"
#ifdef M3CORE_METAL
#include "metal/metal_renderer.h"
#else
#include "Graphics/Render2D.h"
#include "Graphics/SuperAA.h"
#include "gl_context.h"
#endif
#include "OSD/Audio.h"
#include "OSD/Video.h"
#include "OSD/Logger.h"
#include "Util/NewConfig.h"

#include "null_input_system.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#ifndef M3CORE_ROMSET
#error "M3CORE_ROMSET is the ROM set this core is built for"
#endif
#define M3_STR2(x) #x
#define M3_STR(x) M3_STR2(x)
#define M3_CAT2(a, b) a##b
#define M3_CAT(a, b) M3_CAT2(a, b)

#ifndef M3CORE_VERSION
#define M3CORE_VERSION M3CORE_VERSION_OF(M3CORE_ROMSET)
#endif

extern const char *const g_attract_games_xml;  // Config/Games.xml, built in (games_xml.cpp, generated)

namespace {

constexpr int kWidth = 496;
constexpr int kHeight = 384;
constexpr double kFrameHz = 57.524;  // 16 MHz / (656 x 424), the board's own rate
constexpr double kAudioHz = 44100.0;
constexpr int kNvramFileVersion = 0;  // Supermodel's own .nv format, so saves move between the two

// Keeps the last error for create()'s err; prints everything to stderr when
// ATTRACT_M3_LOG is set.
class CoreLogger : public CLogger
{
public:
  std::string lastError;
  bool verbose = std::getenv("ATTRACT_M3_LOG") != nullptr;

  void DebugLog(const char *, va_list) override {}
  void InfoLog(const char *fmt, va_list vl) override
  {
    if (verbose)
    {
      vfprintf(stderr, fmt, vl);
      fputc('\n', stderr);
    }
  }
  void ErrorLog(const char *fmt, va_list vl) override
  {
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, vl);
    lastError = buf;
    if (verbose)
      fprintf(stderr, "error: %s\n", buf);
  }
};

// The 2D renderer, passed through, keeping the tile generator's last two
// layers (bottom and top) for render_eyes, which draws them itself.
class LayerTap final : public IRender2D
{
public:
  explicit LayerTap(std::unique_ptr<IRender2D> inner) : m_inner(std::move(inner)) {}

  Result Init(unsigned xOffset, unsigned yOffset, unsigned xRes, unsigned yRes, unsigned totalXRes, unsigned totalYRes,
              unsigned aaTarget, UpscaleMode upscaleMode) override
  {
    return m_inner->Init(xOffset, yOffset, xRes, yRes, totalXRes, totalYRes, aaTarget, upscaleMode);
  }
  void BeginFrame() override { m_inner->BeginFrame(); }
  void PreRenderFrame() override { m_inner->PreRenderFrame(); }
  void RenderFrameBottom() override { m_inner->RenderFrameBottom(); }
  void RenderFrameTop() override { m_inner->RenderFrameTop(); }
  void EndFrame() override { m_inner->EndFrame(); }
  void AttachRegisters(const uint32_t *regPtr) override { m_inner->AttachRegisters(regPtr); }
  void AttachVRAM(const uint8_t *vramPtr) override { m_inner->AttachVRAM(vramPtr); }
  void AttachDrawBuffers(std::shared_ptr<TileGenBuffer> bottom, std::shared_ptr<TileGenBuffer> top) override
  {
    m_layers[0] = bottom;
    m_layers[1] = top;
    m_inner->AttachDrawBuffers(std::move(bottom), std::move(top));
  }

  // 0 bottom, 1 top: 512 words a line (496 seen), RGBA bytes, line 0 at the top
  const TileGenBuffer *Layer(int i) const { return m_layers[i].get(); }

private:
  std::unique_ptr<IRender2D> m_inner;
  std::shared_ptr<TileGenBuffer> m_layers[2];
};

struct AudioQueue
{
  std::vector<float> samples;  // interleaved stereo, -1 .. 1
  size_t read = 0;
};

}  // namespace

struct afcore
{
#ifdef M3CORE_METAL
  std::shared_ptr<attract::metal::Context> metal;
#else
  attract::GLContext gl;
#endif
  std::shared_ptr<CoreLogger> logger;
  Util::Config::Node config{"Global"};
  Game game;
  std::shared_ptr<CInputSystem> inputSystem;
  std::unique_ptr<CInputs> inputs;
  std::unique_ptr<CModel3> model3;
#ifndef M3CORE_METAL
  std::unique_ptr<SuperAA> superAA;
#endif
  std::unique_ptr<IRender2D> render2D;
  LayerTap *layers = nullptr;  // render2D
  std::unique_ptr<New3D::CNew3D> render3D;
  std::string saveDir;
  std::vector<uint32_t> screen;
  std::vector<uint8_t> readback;
  AudioQueue audio;
  std::string error;
  bool firstBoot = false;  // no save: the first-boot settings are still to make
  unsigned long frame = 0;
  time_t rtcStart = 0;

  // stereo (render_eyes)
  std::vector<uint32_t> eye[2];
  int eyeWidth[2] = {0, 0}, eyeHeight[2] = {0, 0};
  std::vector<uint32_t> hud;
  std::vector<uint8_t> eyeBackground, eyePixels;
  int headIdle = -1;  // frames since the last head-tracked render_eyes, -1 with the frustum closed
};

// The OSD layer Supermodel's machine calls into (OSD/Audio.h, OSD/Video.h).
// One core per process image, so the current core is a global.
static afcore *s_core = nullptr;
static Game::AudioTypes s_audioType = Game::STEREO_LR;

void SetAudioCallback(AudioCallbackFPtr, void *) {}
void SetAudioEnabled(bool) {}
void SetAudioType(Game::AudioTypes type) { s_audioType = type; }
Result OpenAudio(const Util::Config::Node &) { return Result::OKAY; }
void CloseAudio() {}

// As Supermodel's SDL Audio.cpp mixes for a stereo host: each side is the
// mean of its front and rear channels, swapped for the right-to-left types.
bool OutputAudio(unsigned numSamples, const float *fl, const float *fr, const float *rl, const float *rr, bool flipStereo)
{
  if (!s_core)
    return false;
  switch (s_audioType)
  {
  case Game::STEREO_RL:
  case Game::QUAD_1_FRL_2_RRL:
  case Game::QUAD_1_RRL_2_FRL:
    flipStereo = !flipStereo;
    break;
  default:
    break;
  }
  AudioQueue &q = s_core->audio;
  if (q.read > 0 && q.read == q.samples.size())
  {
    q.samples.clear();
    q.read = 0;
  }
  // A front end that stops reading should not grow this forever: keep 2 s.
  const size_t cap = size_t(kAudioHz) * 2 * 2;
  if (q.samples.size() - q.read > cap)
  {
    q.samples.erase(q.samples.begin(), q.samples.begin() + (q.samples.size() - cap));
    q.read = 0;
  }
  constexpr float kScale = 1.0f / 32768.0f;
  for (unsigned i = 0; i < numSamples; i++)
  {
    float l = std::clamp((fl[i] + rl[i]) * 0.5f * kScale, -1.0f, 1.0f);
    float r = std::clamp((fr[i] + rr[i]) * 0.5f * kScale, -1.0f, 1.0f);
    if (flipStereo)
      std::swap(l, r);
    q.samples.push_back(l);
    q.samples.push_back(r);
  }
  return false;
}

bool BeginFrameVideo()
{
  return true;
}

// The frame is finished in the FBO (or the Metal target): read it back,
// flip it the right way up and turn RGBA bytes into 0xAARRGGBB words.
void EndFrameVideo()
{
#ifdef M3CORE_METAL
  if (!s_core || !s_core->metal)
    return;
  afcore *c = s_core;
  attract::metal::EndFrame(*c->metal, c->readback.data());
#else
  if (!s_core || !s_core->superAA)
    return;
  afcore *c = s_core;
  glBindFramebuffer(GL_READ_FRAMEBUFFER, c->superAA->GetTargetID());
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, kWidth, kHeight, GL_RGBA, GL_UNSIGNED_BYTE, c->readback.data());
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
#endif
  for (int y = 0; y < kHeight; y++)
  {
    const uint8_t *src = &c->readback[size_t(kHeight - 1 - y) * kWidth * 4];
    uint32_t *dst = &c->screen[size_t(y) * kWidth];
    for (int x = 0; x < kWidth; x++, src += 4)
      dst[x] = 0xff000000u | uint32_t(src[0]) << 16 | uint32_t(src[1]) << 8 | src[2];
  }
}

namespace {

void DefaultConfig(Util::Config::Node &config)
{
  config.Set("PowerPCFrequency", 0u);
  config.Set("MultiThreaded", false);
  config.Set("GPUMultiThreaded", false);
  config.Set("EmulateSound", true);
  config.Set("EmulateDSB", true);
  config.Set("SoundVolume", 100);
  config.Set("MusicVolume", 100);
  config.Set("Balance", 0.0f);
  config.Set("BalanceLeftRight", 0.0f);
  config.Set("BalanceFrontRear", 0.0f);
  config.Set("NbSoundChannels", 2);
  config.Set("FlipStereo", false);
  config.Set("ForceFeedback", false);
  config.Set("New3DEngine", true);
#ifdef M3CORE_METAL
  config.Set("QuadRendering", true);  // the Metal renderer draws quads only
#else
  // Quads go through a geometry shader, which Apple's OpenGL (a layer over
  // Metal) runs as a compute pass before every draw: Daytona USA 2 on an M2
  // Max runs at 18 frames/s with quads and 61 with triangles. So Apple's
  // OpenGL builds draw triangles, as Supermodel's -no-quad-rendering does.
  // ATTRACT_M3_QUADS=0 or 1 picks either way.
#ifdef __APPLE__
  bool quadRendering = false;
#else
  bool quadRendering = true;
#endif
  if (const char *quads = std::getenv("ATTRACT_M3_QUADS"))
    quadRendering = quads[0] != '0';
  config.Set("QuadRendering", quadRendering);
#endif
  config.Set("XResolution", kWidth);
  config.Set("YResolution", kHeight);
  config.Set("FullScreen", false);
  config.Set("BorderlessWindow", false);
  config.Set("Supersampling", 1);
  config.Set("CRTcolors", 0);
  config.Set("UpscaleMode", 2);  // Supermodel's default, so screens match the reference
  config.Set("WideScreen", false);
  config.Set("Stretch", false);
  config.Set("WideBackground", false);
  config.Set("VSync", false);
  config.Set("Throttle", false);
  config.Set("RefreshRate", float(kFrameHz));
  config.Set("ShowFrameRate", false);
  config.Set("Crosshairs", 0);
  config.Set<std::string>("CrosshairStyle", "vector");
  config.Set("NoWhiteFlash", false);
  config.Set("Network", false);
  config.Set("SimulateNet", true);
  config.Set("PortIn", 1970u);
  config.Set("PortOut", 1971u);
  config.Set<std::string>("AddressOut", "127.0.0.1");
  config.Set<std::string>("Outputs", "none");
  config.Set("DumpTextures", false);
  config.Set("DumpMemory", false);
  config.Set("MaxTexMaps", 0x8000u);
  config.Set("MaxTexMapExtent", 4u);
  config.Set("InputDigitalSensitivity", 25u);
  config.Set("InputDigitalDecaySpeed", 25u);
  config.Set<std::string>("InitStateFile", "");
}

std::string NvramPath(const afcore *c)
{
  return c->saveDir + "/" + c->game.name + ".nv";
}

bool LoadNvram(afcore *c)
{
  if (c->saveDir.empty())
    return false;
  CBlockFile nv;
  if (Result::OKAY != nv.Load(NvramPath(c)))
    return false;
  if (Result::OKAY != nv.FindBlock("Supermodel NVRAM State"))
    return false;
  int32_t version = -1;
  nv.Read(&version, sizeof(version));
  if (version != kNvramFileVersion)
    return false;
  c->model3->LoadNVRAM(&nv);
  nv.Close();
  return true;
}

uint16_t Crc16Xmodem(const uint16_t *words, size_t n)
{
  uint16_t crc = 0;
  for (size_t i = 0; i < n; i++)
    for (int byte = 1; byte >= 0; byte--)
    {
      crc ^= uint16_t((words[i] >> (byte * 8)) & 0xff) << 8;
      for (int bit = 0; bit < 8; bit++)
        crc = crc & 0x8000 ? uint16_t(crc << 1 ^ 0x1021) : uint16_t(crc << 1);
    }
  return crc;
}

// A board's first boot (no save yet): games that come up as one cabinet of
// a linked set, waiting for a network board the cores do not have, get the
// setting a single cabinet's operator makes in test mode. The game writes
// its default settings to the EEPROM during its first boot; then the core
// changes them and resets the board, once.
//
// Scud Race and Daytona USA 2 (AM2's racing games): words 0-2 "M3SEGA",
// word 3 a CRC-16/XMODEM of the settings (words 6-34, high byte first), and
// words 35-63 a copy of them. Link ID is word 6's low byte (Scud: 1 master,
// 0 slave, 2 single; Daytona 2 adds "live", but single is 2 there too);
// going to single in test mode also clears word 15's high byte.
void FirstBootSettings(afcore *c)
{
  const std::string &name = c->game.parent.empty() ? c->game.name : c->game.parent;
  if ((name != "scud" && name != "daytona2") || c->frame > 600)
  {
    c->firstBoot = false;
    return;
  }
  uint16_t *e = c->model3->EEPROMData();
  if (e[0] != 0x4d33 || e[1] != 0x5345 || e[2] != 0x4741 || Crc16Xmodem(e + 6, 29) != e[3] ||
      !std::equal(e + 6, e + 35, e + 35))
    return;  // not written yet
  c->firstBoot = false;
  if ((e[6] & 0xff) == 2)
    return;
  e[6] = uint16_t((e[6] & 0xff00) | 2);
  e[15] &= 0x00ff;
  std::copy(e + 6, e + 35, e + 35);
  e[3] = Crc16Xmodem(e + 6, 29);
  c->model3->Reset();
}

void SaveNvram(afcore *c)
{
  if (c->saveDir.empty() || !c->model3)
    return;
  CBlockFile nv;
  if (Result::OKAY != nv.Create(NvramPath(c), "Supermodel NVRAM State", "Supermodel Version " SUPERMODEL_VERSION))
    return;
  int32_t version = kNvramFileVersion;
  nv.Write(&version, sizeof(version));
  nv.Write(c->game.name);
  c->model3->SaveNVRAM(&nv);
  nv.Close();
}

void SetErr(char *err, size_t errSize, const std::string &msg)
{
  if (err && errSize)
    snprintf(err, errSize, "%s", msg.c_str());
}

void Destroy(afcore *c);

afcore *Create(const char *romPath, const char *saveDir, char *err, size_t errSize)
{
  if (s_core)
  {
    SetErr(err, errSize, "this core is already running a game (one at a time)");
    return nullptr;
  }
  auto *c = new afcore;
  c->logger = std::make_shared<CoreLogger>();
  SetLogger(c->logger);
  c->saveDir = saveDir ? saveDir : "";
  DefaultConfig(c->config);

  std::string why;
#ifdef M3CORE_METAL
  c->metal = attract::metal::CreateContext(kWidth, kHeight, &why);
  if (!c->metal)
  {
    SetErr(err, errSize, why);
    delete c;
    return nullptr;
  }
#else
  if (!c->gl.Create(&why))
  {
    SetErr(err, errSize, why);
    delete c;
    return nullptr;
  }
  glewExperimental = GL_TRUE;
  const GLenum glew = glewInit();
#ifdef __APPLE__
  if (glew != GLEW_OK)
#else
  // The GL functions are loaded before glewInit() looks for a GLX display,
  // which the core's EGL context has none of
  if (glew != GLEW_OK && glew != GLEW_ERROR_NO_GLX_DISPLAY)
#endif
  {
    SetErr(err, errSize, "OpenGL: glewInit failed");
    delete c;
    return nullptr;
  }
  glGetError();  // GLEW can leave GL_INVALID_ENUM behind on a core profile
#endif

  GameLoader loader(g_attract_games_xml, GameLoader::FromText{});
  loader.SetPreferredGame(M3_STR(M3CORE_ROMSET));
  ROMSet romSet;
  if (loader.Load(&c->game, &romSet, romPath))
  {
    SetErr(err, errSize, c->logger->lastError.empty() ? std::string("cannot load ") + romPath : c->logger->lastError);
    delete c;
    return nullptr;
  }
  if (c->game.name != M3_STR(M3CORE_ROMSET))
  {
    SetErr(err, errSize, "this is the " M3_STR(M3CORE_ROMSET) " core; the ROM set is " + c->game.name);
    delete c;
    return nullptr;
  }

  c->inputSystem = std::make_shared<CNullInputSystem>();
  c->inputs = std::make_unique<CInputs>(c->inputSystem);
  if (!c->inputs->Initialize())
  {
    SetErr(err, errSize, "inputs");
    delete c;
    return nullptr;
  }

  s_core = c;
  c->screen.assign(size_t(kWidth) * kHeight, 0xff000000u);
  c->readback.assign(size_t(kWidth) * kHeight * 4, 0);
  SetAudioType(c->game.audio);

  // Star Wars Trilogy checks its feedback lever at boot and stops ("failed
  // feedback lever initialize") when the joystick board is off, as Supermodel
  // does without -force-feedback. With it on, the board's own program runs;
  // the forces it asks for go nowhere. Other games keep it off.
  if (c->game.driveboard_type == Game::DriveBoardType::DRIVE_BOARD_JOYSTICK)
    c->config.Set("ForceFeedback", true);
  c->model3 = std::make_unique<CModel3>(c->config, c->inputs.get(), nullptr, nullptr);
  if (Result::OKAY != c->model3->Init(c->game, romSet))
  {
    SetErr(err, errSize, c->logger->lastError.empty() ? "the board did not start" : c->logger->lastError);
    Destroy(c);
    return nullptr;
  }
  romSet = ROMSet();
  c->firstBoot = !LoadNvram(c);

  // The board's clock: the host's time when the core starts, then the
  // board's own frames, so a run depends only on when it starts.
  // ATTRACT_RTC_START (seconds since 1970) fixes the start for checks.
  const char *rtc = std::getenv("ATTRACT_RTC_START");
  c->rtcStart = rtc ? time_t(std::strtoll(rtc, nullptr, 10)) : std::time(nullptr);
  g_RTCTimeSource = [] { return s_core->rtcStart + time_t(double(s_core->frame) / kFrameHz); };

#ifdef M3CORE_METAL
  c->render2D = std::make_unique<LayerTap>(attract::metal::CreateRender2D(c->metal));
  c->render3D = std::make_unique<New3D::CNew3D>(c->config, c->game.name,
                                                attract::metal::CreateR3DGpu(c->metal, MAX_RAM_VERTS + MAX_ROM_VERTS));
  const unsigned target = 0;
#else
  c->superAA = std::make_unique<SuperAA>(1, CRTcolor::None);
  c->superAA->Init(kWidth, kHeight, true);
  c->render2D = std::make_unique<LayerTap>(std::make_unique<CRender2D>(c->config));
  c->render3D = std::make_unique<New3D::CNew3D>(c->config, c->game.name);
  const unsigned target = c->superAA->GetTargetID();
#endif
  c->layers = static_cast<LayerTap *>(c->render2D.get());
  if (Result::OKAY != c->render2D->Init(0, 0, kWidth, kHeight, kWidth, kHeight, target, UpscaleMode::Bilinear) ||
      Result::OKAY != c->render3D->Init(0, 0, kWidth, kHeight, kWidth, kHeight, target))
  {
    SetErr(err, errSize, c->logger->lastError.empty() ? "the renderer did not start" : c->logger->lastError);
    Destroy(c);
    return nullptr;
  }
#ifdef M3CORE_METAL
  c->model3->AttachRenderers(c->render2D.get(), c->render3D.get(), nullptr);
#else
  // The GL state Supermodel's ResizeGLScreen() leaves for a 496 x 384 window:
  // the renderers count on it, the scissor box above all (2 pixels in on each
  // side, for the 2D layer's offset).
  glBindFramebuffer(GL_FRAMEBUFFER, c->superAA->GetTargetID());
  glViewport(0, 0, kWidth, kHeight);
  glClearColor(0.0, 0.0, 0.0, 0.0);
  glClearDepth(1.0);
  glDepthFunc(GL_LESS);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  {
    const int correction = int((kHeight / 384.0) * 2.0 + 0.5);
    glEnable(GL_SCISSOR_TEST);
    glScissor(correction, correction, kWidth - correction * 2, kHeight - correction * 2);
  }

  c->model3->AttachRenderers(c->render2D.get(), c->render3D.get(), c->superAA.get());
#endif
  c->model3->Reset();
#ifndef M3CORE_METAL
  c->gl.Release();
#endif
  return c;
}

void Destroy(afcore *c)
{
  if (!c)
    return;
#ifndef M3CORE_METAL
  c->gl.MakeCurrent();
#endif
  SaveNvram(c);
  c->model3.reset();
  c->render3D.reset();
  c->render2D.reset();
#ifdef M3CORE_METAL
  c->metal.reset();
#else
  c->superAA.reset();
#endif
  c->inputs.reset();
  c->inputSystem.reset();
#ifndef M3CORE_METAL
  c->gl.Destroy();
#endif
  if (s_core == c)
    s_core = nullptr;
  SetLogger(nullptr);
  delete c;
}

void Set(const std::shared_ptr<CSwitchInput> &in, bool on)
{
  if (in)
    in->value = on ? 1 : 0;
}

// The front end's controls onto Supermodel's inputs. Fighting games: PUNCH,
// KICK, GUARD and the joystick as in the Model 2 fighters, BUTTON2 is
// Virtua Fighter 3's ESCAPE; buttons2 is player 2.
void ApplyInput(afcore *c, const afcore_input *in)
{
  afcore_input zero{};
  if (!in)
    in = &zero;
  uint32_t b = in->buttons;
  uint32_t b2 = in->size >= offsetof(afcore_input, buttons2) + sizeof(uint32_t) ? in->buttons2 : 0;
  CInputs &i = *c->inputs;
  Set(i.coin[0], b & AFCORE_COIN);
  Set(i.coin[1], false);
  Set(i.start[0], b & AFCORE_START);
  Set(i.start[1], b & AFCORE_START2);
  Set(i.test[0], b & AFCORE_TEST);
  Set(i.service[0], b & AFCORE_SERVICE);
  Set(i.test[1], false);
  Set(i.service[1], false);
  const uint32_t pads[2] = { b, b2 };
  for (int p = 0; p < 2; p++)
  {
    uint32_t pb = pads[p];
    Set(i.up[p], pb & AFCORE_UP);
    Set(i.down[p], pb & AFCORE_DOWN);
    Set(i.left[p], pb & AFCORE_LEFT);
    Set(i.right[p], pb & AFCORE_RIGHT);
    Set(i.punch[p], pb & AFCORE_PUNCH);
    Set(i.kick[p], pb & AFCORE_KICK);
    Set(i.guard[p], pb & AFCORE_GUARD);
    Set(i.escape[p], pb & AFCORE_BUTTON2);
  }
  // Driving games, in the board's units: the wheel 0 (left) .. 0x80 .. 0xff,
  // the pedals 0 .. 0xff, the shifter's gear 0 (neutral) .. 4.
  if (i.steering)
    i.steering->value = UINT16(std::clamp(std::lround(in->steer * 127.5f + 127.5f), 0L, 255L));
  if (i.accelerator)
    i.accelerator->value = UINT16(std::clamp(std::lround(in->accel * 255.0f), 0L, 255L));
  if (i.brake)
    i.brake->value = UINT16(std::clamp(std::lround(in->brake * 255.0f), 0L, 255L));
  if (i.gearShift4)
    i.gearShift4->value = std::clamp(in->gear, 0, 4);
  Set(i.gearShiftUp, b & AFCORE_SHIFT_UP);
  Set(i.gearShiftDown, b & AFCORE_SHIFT_DOWN);
  for (int v = 0; v < 4; v++)
    Set(i.vr[v], b & (AFCORE_VIEW1 << v));
  // The view button (Sega Rally 2, Dirt Devils, Harley-Davidson), Sega
  // Rally 2's handbrake and Harley-Davidson's music select.
  Set(i.viewChange, b & AFCORE_VIEW1);
  Set(i.handBrake, in->handbrake >= 0.5f);
  Set(i.musicSelect, b & AFCORE_VIEW2);
  // Virtual-On's twin sticks: the left stick is the joystick, the right one
  // player 2's (buttons2); each stick's shot trigger and turbo button.
  Set(i.twinJoyUp1, b & AFCORE_UP);
  Set(i.twinJoyDown1, b & AFCORE_DOWN);
  Set(i.twinJoyLeft1, b & AFCORE_LEFT);
  Set(i.twinJoyRight1, b & AFCORE_RIGHT);
  Set(i.twinJoyUp2, b2 & AFCORE_UP);
  Set(i.twinJoyDown2, b2 & AFCORE_DOWN);
  Set(i.twinJoyLeft2, b2 & AFCORE_LEFT);
  Set(i.twinJoyRight2, b2 & AFCORE_RIGHT);
  Set(i.twinJoyShot1, b & AFCORE_TRIGGER1);
  Set(i.twinJoyShot2, b & AFCORE_TRIGGER2);
  Set(i.twinJoyTurbo1, b & AFCORE_BUTTON2);
  Set(i.twinJoyTurbo2, b & AFCORE_BUTTON3);
  // Spikeout's buttons: beat, charge, shift and jump.
  Set(i.beat, b & AFCORE_PUNCH);
  Set(i.charge, b & AFCORE_KICK);
  Set(i.shift, b & AFCORE_GUARD);
  Set(i.jump, b & AFCORE_BUTTON2);
  // Virtua Striker's short pass, long pass and shoot, each player.
  for (int p = 0; p < 2; p++)
  {
    Set(i.shortPass[p], pads[p] & AFCORE_PUNCH);
    Set(i.longPass[p], pads[p] & AFCORE_KICK);
    Set(i.shoot[p], pads[p] & AFCORE_GUARD);
  }
  // Star Wars Trilogy's analog stick (as Star Wars Arcade's: steer across,
  // stick_y forward -1 .. back +1, in the board's 0 .. 0x80 .. 0xff), its
  // two triggers and two event buttons.
  float stickY = in->size >= offsetof(afcore_input, stick_y) + sizeof(float) ? in->stick_y : 0.0f;
  if (i.analogJoyX)
    i.analogJoyX->value = UINT16(std::clamp(std::lround(in->steer * 127.5f + 127.5f), 0L, 255L));
  if (i.analogJoyY)
    i.analogJoyY->value = UINT16(std::clamp(std::lround(stickY * 127.5f + 127.5f), 0L, 255L));
  Set(i.analogJoyTrigger1, b & AFCORE_TRIGGER1);
  Set(i.analogJoyTrigger2, b & AFCORE_TRIGGER2);
  Set(i.analogJoyEvent1, b & AFCORE_BUTTON2);
  Set(i.analogJoyEvent2, b & AFCORE_BUTTON3);
  // The analog guns (The Lost World, L.A. Machineguns, The Ocean Hunter):
  // gun_x/gun_y 0 .. 1 across and down the screen, in the board's units as
  // Supermodel's analog gun axes (X 0 left .. 0xff right, Y 0xff top .. 0
  // bottom); outside the screen, its edge (the guns have no off-screen
  // sense). Each gun's left and right triggers are TRIGGER1 and TRIGGER2, in
  // buttons (player 1) and buttons2 (player 2).
  for (int p = 0; p < 2; p++)
  {
    float gx = std::clamp(in->gun_x[p], 0.0f, 1.0f), gy = std::clamp(in->gun_y[p], 0.0f, 1.0f);
    if (i.analogGunX[p])
      i.analogGunX[p]->value = UINT16(std::lround(gx * 255.0f));
    if (i.analogGunY[p])
      i.analogGunY[p]->value = UINT16(255 - std::lround(gy * 255.0f));
    Set(i.analogTriggerLeft[p], pads[p] & AFCORE_TRIGGER1);
    Set(i.analogTriggerRight[p], pads[p] & AFCORE_TRIGGER2);
  }
  // Magical Truck Adventure: each player's lever, in the board's units (0xff
  // pushed forward .. 0x80 .. 0 pulled back), from UP and DOWN (0xff, 0) in
  // buttons and buttons2; player 1's also from stick_y when it is not 0 (-1
  // forward .. +1 back). Each player's pedal is PUNCH.
  float stickY1 = in->size >= offsetof(afcore_input, stick_y) + sizeof(float) ? in->stick_y : 0.0f;
  const std::shared_ptr<CAxisInput> levers[2] = { i.magicalLever1, i.magicalLever2 };
  for (int p = 0; p < 2; p++)
  {
    if (!levers[p])
      continue;
    UINT16 lever = (pads[p] & AFCORE_UP) ? 0xff : (pads[p] & AFCORE_DOWN) ? 0x00 : 0x80;
    if (p == 0 && stickY1 != 0.0f)
      lever = UINT16(std::clamp(std::lround(127.5f - stickY1 * 127.5f), 0L, 255L));
    levers[p]->value = lever;
  }
  Set(i.magicalPedal1, b & AFCORE_PUNCH);
  Set(i.magicalPedal2, b2 & AFCORE_PUNCH);
  // Sega Bass Fishing: the rod across (steer) and up/down (stick_y, -1 forward
  // .. +1 back), the reel's speed (accel) and the line's tension (brake), in
  // the board's 0 .. 0x80 .. 0xff; the lure stick from UP/DOWN/LEFT/RIGHT
  // (0, 0x80, 0xff); cast PUNCH, select KICK.
  if (i.fishingRodX)
    i.fishingRodX->value = UINT16(std::clamp(std::lround(in->steer * 127.5f + 127.5f), 0L, 255L));
  if (i.fishingRodY)
    i.fishingRodY->value = UINT16(std::clamp(std::lround(stickY1 * 127.5f + 127.5f), 0L, 255L));
  if (i.fishingReel)
    i.fishingReel->value = UINT16(std::clamp(std::lround(in->accel * 255.0f), 0L, 255L));
  if (i.fishingTension)
    i.fishingTension->value = UINT16(std::clamp(std::lround(in->brake * 255.0f), 0L, 255L));
  if (i.fishingStickX)
    i.fishingStickX->value = (b & AFCORE_LEFT) ? 0x00 : (b & AFCORE_RIGHT) ? 0xff : 0x80;
  if (i.fishingStickY)
    i.fishingStickY->value = (b & AFCORE_UP) ? 0x00 : (b & AFCORE_DOWN) ? 0xff : 0x80;
  Set(i.fishingCast, b & AFCORE_PUNCH);
  Set(i.fishingSelect, b & AFCORE_KICK);
}

// Frames without a head-tracked render_eyes before the frustum closes again.
constexpr int kHeadIdleFrames = 30;

void RunFrame(afcore *c, const afcore_input *in)
{
  if (!c->error.empty())
    return;
#ifdef M3CORE_METAL
  attract::metal::FrameStart(*c->metal);
#else
  attract::GLCurrent current(c->gl);
#endif
  ApplyInput(c, in);
  c->model3->RunFrame();
#ifdef M3CORE_METAL
  attract::metal::FrameDone(*c->metal);
#endif
  c->frame++;
  if (c->firstBoot)
    FirstBootSettings(c);
  if (c->headIdle >= 0 && ++c->headIdle > kHeadIdleFrames)  // head-tracked stereo stopped
  {
    c->render3D->SetOpenFrustum(false);
    c->headIdle = -1;
  }
}

const uint32_t *Screen(afcore *c, int *width, int *height)
{
  if (width)
    *width = kWidth;
  if (height)
    *height = kHeight;
  return c->screen.data();
}

double AudioRate(afcore *, int stream)
{
  return stream == 0 ? kAudioHz : 0.0;
}

size_t AudioRead(afcore *c, int stream, float *out, size_t maxFrames)
{
  if (stream != 0)
    return 0;
  AudioQueue &q = c->audio;
  size_t frames = std::min(maxFrames, (q.samples.size() - q.read) / 2);
  std::memcpy(out, &q.samples[q.read], frames * 2 * sizeof(float));
  q.read += frames * 2;
  return frames;
}

void Save(afcore *c)
{
  SaveNvram(c);
}

int LinkStart(afcore *, int, const char *, int, char *err, size_t errSize)
{
  SetErr(err, errSize, "link play is not in the Model 3 cores yet");
  return -1;
}

int LinkState(afcore *, int *id, int *count)
{
  if (id)
    *id = 0;
  if (count)
    *count = 0;
  return AFCORE_LINK_OFF;
}

const char *Error(afcore *c)
{
  return c->error.empty() ? nullptr : c->error.c_str();
}

// ---------------------------------------------------------------------------
// Stereo (render_eyes): the last frame's 3D drawn again for two eyes by
// New3D (CNew3D::RenderEye) from the display lists it built, the tile
// generator's layers placed by the core: the bottom one at infinity, the
// top one (HUD, text) at screen depth (pair) or apart (hud_screen, head).

// Metres per game unit. Scud Race's chase view: the player's car is about
// 1.7 units wide at about 2.1 units away (its size on screen and the pair's
// disparity), so about 1.1 m a unit for a car 1.9 m wide. 1 for every game,
// as in the Model 2 cores, until others are measured.
constexpr float kStereoScale = 1.0f;
constexpr int kMaxEyeWidth = 1024, kMaxEyeHeight = 512;
// The 2D layers sit 2 pixels up and left of the 3D, inside the scissor box
// 2 pixels in from each edge (CRender2D's Setup2D(), Render2DMetal::Init()).
constexpr int kLayerOffset = 2;

uint32_t LayerPixel(const TileGenBuffer *layer, int x, int y)
{
  x = std::clamp(x + kLayerOffset, 0, kWidth - 1);
  y = std::clamp(y + kLayerOffset, 0, kHeight - 1);
  return layer ? layer->data[size_t(y) * 512 + size_t(x)] : 0;  // RGBA bytes
}

// The bottom layer as the normal frame leaves it in the target: blended
// over transparent black (colour and alpha times alpha).
void PutBackground(uint8_t *out, uint32_t rgba)
{
  const uint32_t a = rgba >> 24;
  out[0] = uint8_t(((rgba & 0xff) * a + 127) / 255);
  out[1] = uint8_t((((rgba >> 8) & 0xff) * a + 127) / 255);
  out[2] = uint8_t((((rgba >> 16) & 0xff) * a + 127) / 255);
  out[3] = uint8_t((a * a + 127) / 255);
}

// afcore's eye view (camera to eye, metres, z forward) in New3D's terms:
// OpenGL axes (z back) and game units.
void EyeViewMatrix(const float *m, float scale, float *out)
{
  static constexpr float flip[4] = {1, 1, -1, 1};
  for (int col = 0; col < 4; col++)
    for (int row = 0; row < 4; row++)
      out[col * 4 + row] = flip[row] * m[col * 4 + row] * flip[col];
  for (int row = 0; row < 3; row++)
    out[12 + row] /= scale;
  out[3] = out[7] = out[11] = 0;
  out[15] = 1;
}

int RenderEyes(afcore *c, const afcore_stereo *in)
{
  if (!in || !c->error.empty() || !c->render3D || !c->layers)
    return 0;
  afcore_stereo s{};
  if (in->size > sizeof(uint32_t))
    std::memcpy(&s, in, std::min<size_t>(in->size, sizeof(s)));
  float tans[4];  // the game camera's frustum: left, right, bottom, top
  if (!c->render3D->SceneFrustum(tans) || !(tans[1] > tans[0]) || !(tans[3] > tans[2]))
    return 0;
  const bool head = s.mode == AFCORE_STEREO_HEAD;
  const float scale = s.world_scale > 0 ? s.world_scale : kStereoScale;
  const float ipd = s.ipd > 0 ? s.ipd : 0.063f;
  const float convergence = s.convergence > 0 ? s.convergence : 10.0f;
  if (head)
  {
    if (c->headIdle < 0)
      c->render3D->SetOpenFrustum(true);  // from the next frame
    c->headIdle = 0;
  }
#ifndef M3CORE_METAL
  attract::GLCurrent current(c->gl);
#endif
  const TileGenBuffer *bottom = c->layers->Layer(0), *top = c->layers->Layer(1);
  const int inset = kLayerOffset;

  c->hud.assign(size_t(kWidth) * kHeight, 0u);
  for (int y = inset; y < kHeight - inset; y++)
    for (int x = inset; x < kWidth - inset; x++)
    {
      const uint32_t p = LayerPixel(top, x, y);
      if (p >> 24)
        c->hud[size_t(y) * kWidth + x] = (p & 0xff000000u) | (p & 0xff) << 16 | (p & 0xff00) | (p >> 16 & 0xff);
    }

  for (int i = 0; i < 2; i++)
  {
    New3D::CNew3D::EyeView eye;
    eye.head = head;
    int w = kWidth, h = kHeight;
    if (head)
    {
      const afcore_eye &e = s.eye[i];
      if (!(e.tan_right > e.tan_left) || !(e.tan_up > e.tan_down))
        return 0;
      w = std::clamp(e.width, 16, kMaxEyeWidth);
      h = std::clamp(e.height, 16, kMaxEyeHeight);
      EyeViewMatrix(e.view, scale, eye.view);
      eye.tanLeft = e.tan_left, eye.tanRight = e.tan_right, eye.tanDown = e.tan_down, eye.tanUp = e.tan_up;
    }
    else
    {
      eye.offset = (i ? 0.5f : -0.5f) * ipd / scale;
      eye.convergence = std::max(convergence, 0.02f) / scale;
    }
    eye.width = w, eye.height = h;

    // The bottom layer at infinity, bottom row first.
    c->eyeBackground.assign(size_t(w) * h * 4, 0);
    if (head)
    {
      const float *m = s.eye[i].view;
      const float tl = s.eye[i].tan_left, tr = s.eye[i].tan_right, td = s.eye[i].tan_down, tu = s.eye[i].tan_up;
      for (int y = 0; y < h; y++)
      {
        const float ey = tu - (float(y) + 0.5f) / float(h) * (tu - td);
        uint8_t *row = &c->eyeBackground[size_t(h - 1 - y) * w * 4];
        for (int x = 0; x < w; x++)
        {
          // the pixel's direction back in the game camera's space, through the game's projection
          const float ex = tl + (float(x) + 0.5f) / float(w) * (tr - tl);
          const float X = m[0] * ex + m[1] * ey + m[2], Y = m[4] * ex + m[5] * ey + m[6], Z = m[8] * ex + m[9] * ey + m[10];
          int sx = kWidth / 2, sy = Y > 0 ? 0 : kHeight - 1;  // behind the game's camera: the nearer edge
          if (Z > 1e-4f)
          {
            sx = int(std::floor((X / Z - tans[0]) / (tans[1] - tans[0]) * kWidth));
            sy = int(std::floor((tans[3] - Y / Z) / (tans[3] - tans[2]) * kHeight));
          }
          PutBackground(row + size_t(x) * 4, LayerPixel(bottom, std::clamp(sx, inset, kWidth - 1 - inset),
                                                         std::clamp(sy, inset, kHeight - 1 - inset)));
        }
      }
    }
    else
    {
      // moved as far as the 3D at infinity: what the pair's projection adds
      const int shift = int(std::lround(kWidth * eye.offset / ((tans[1] - tans[0]) * eye.convergence)));
      for (int y = inset; y < h - inset; y++)
      {
        uint8_t *row = &c->eyeBackground[size_t(h - 1 - y) * w * 4];
        for (int x = inset; x < w - inset; x++)
          PutBackground(row + size_t(x) * 4, LayerPixel(bottom, std::clamp(x - shift, inset, kWidth - 1 - inset), y));
      }
    }

    c->eyePixels.resize(size_t(w) * h * 4);
    if (!c->render3D->RenderEye(eye, c->eyeBackground.data(), c->eyePixels.data()))
      return 0;

    // Top row first, 0xAARRGGBB; the pair's eyes get the top layer at
    // screen depth, blended as the normal frame blends it.
    std::vector<uint32_t> &out = c->eye[i];
    out.resize(size_t(w) * h);
    c->eyeWidth[i] = w, c->eyeHeight[i] = h;
    for (int y = 0; y < h; y++)
    {
      const uint8_t *src = &c->eyePixels[size_t(h - 1 - y) * w * 4];
      uint32_t *dst = &out[size_t(y) * w];
      for (int x = 0; x < w; x++, src += 4)
      {
        uint32_t r = src[0], g = src[1], b = src[2];
        if (!head && x >= inset && x < w - inset && y >= inset && y < h - inset)
        {
          const uint32_t p = LayerPixel(top, x, y), a = p >> 24;
          if (a)
          {
            r = ((p & 0xff) * a + r * (255 - a) + 127) / 255;
            g = ((p >> 8 & 0xff) * a + g * (255 - a) + 127) / 255;
            b = ((p >> 16 & 0xff) * a + b * (255 - a) + 127) / 255;
          }
        }
        dst[x] = 0xff000000u | r << 16 | g << 8 | b;
      }
    }
  }
  return 1;
}

const uint32_t *EyeScreen(afcore *c, int eye, int *width, int *height)
{
  eye &= 1;
  if (width)
    *width = c->eyeWidth[eye];
  if (height)
    *height = c->eyeHeight[eye];
  return c->eye[eye].empty() ? nullptr : c->eye[eye].data();
}

const uint32_t *HudScreen(afcore *c, int *width, int *height)
{
  if (width)
    *width = kWidth;
  if (height)
    *height = kHeight;
  return c->hud.empty() ? nullptr : c->hud.data();
}

}  // namespace

extern "C" AFCORE_EXPORT const afcore_api *M3_CAT(afcore_api_, M3CORE_ROMSET)(void)
{
  static const afcore_api api = {
    AFCORE_API_VERSION,
    sizeof(afcore_api),
    M3CORE_VERSION,
    M3_STR(M3CORE_ROMSET),
    M3CORE_TITLE,
    kFrameHz,
    kWidth,
    kHeight,
    Create,
    Destroy,
    RunFrame,
    Screen,
    AudioRate,
    AudioRead,
    Save,
    LinkStart,
    LinkState,
    Error,
    RenderEyes,
    EyeScreen,
    HudScreen,
    kStereoScale,
  };
  return &api;
}
