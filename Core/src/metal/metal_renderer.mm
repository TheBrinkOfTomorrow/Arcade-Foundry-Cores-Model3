// The Metal renderer: see metal_renderer.h. R3DGpuMetal follows
// R3DGpuGL (Src/Graphics/New3D/R3DGpuGL.cpp) call by call, and
// Render2DMetal follows CRender2D (Src/Graphics/Render2D.cpp).
//
// OpenGL's coordinates are kept: viewports, scissor boxes and pixel
// positions are OpenGL's numbers, and the shaders flip y on the way out
// (R3DShadersMetal.h), so memory row 0 is OpenGL's row 0.
//
// One command buffer per frame; EndFrame() waits for it, so everything the
// emulator writes between frames (textures, vertices) is free to change.

// Supermodel's headers first: the system's <sys/param.h> defines a PVM
// macro, which is also one of Supermodel's CRTcolor names.
#include "af_debug.h"
#include "metal_renderer.h"
#include "R3DShadersMetal.h"
#include "Model3/TileGenBuffer.h"

#import <CoreVideo/CoreVideo.h>
#import <Metal/Metal.h>
#include <simd/simd.h>

#include <TargetConditionals.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <vector>

namespace attract::metal {

using New3D::FVertex;
using New3D::Layer;
using New3D::Mesh;
using New3D::Model;
using New3D::Viewport;

namespace {

#if TARGET_OS_OSX
constexpr MTLStorageMode kUploadStorage = MTLStorageModeManaged;
constexpr MTLResourceOptions kUploadOptions = MTLResourceStorageModeManaged;
#else
constexpr MTLStorageMode kUploadStorage = MTLStorageModeShared;
constexpr MTLResourceOptions kUploadOptions = MTLResourceStorageModeShared;
#endif

constexpr MTLPixelFormat kColourFormat = MTLPixelFormatRGBA8Unorm;
constexpr MTLPixelFormat kDepthFormat = MTLPixelFormatDepth32Float_Stencil8;

// The finished frame into a surface (Surface()): the target turned the right
// way up (its row 0 is the bottom, as OpenGL's) and opaque; the BGRA
// surface's pixel format puts the bytes in its order.
const char *const kSurfaceShader = R"(
#include <metal_stdlib>
using namespace metal;
vertex float4 surfaceVertex(uint id [[vertex_id]])
{
  const float2 p = float2((id << 1) & 2, id & 2);  // one triangle over the screen
  return float4(p * 2.0 - 1.0, 0.0, 1.0);
}
fragment float4 surfaceFragment(float4 pos [[position]], texture2d<float> target [[texture(0)]])
{
  const uint2 at = uint2(pos.xy);
  return float4(target.read(uint2(at.x, target.get_height() - 1 - at.y)).rgb, 1.0);
}
)";

// R3DUniforms in R3DShadersMetal.h
struct R3DUniforms
{
  simd_float4x4 projMat;
  simd_float4x4 modelMat;
  simd_float4 fogColour;
  simd_float4 lighting0;
  simd_float4 lighting1;
  simd_float4 spotEllipse;
  simd_float4 spotColor;
  simd_float4 spotFogColor;
  simd_float4 spotRange;
  float fogIntensity;
  float fogDensity;
  float fogStart;
  float fogAttenuation;
  float fogAmbient;
  float cota;
  float modelScale;
  float nodeAlpha;
  float microTextureMinLOD;
  float shininess;
  float specularValue;
  float layerDepthBias;
  simd_int4 baseTexInfo;
  simd_int2 textureWrapMode;
  int baseTexType;
  int texturePage;
  int microTextureID;
  int colourLayer;
  uint32_t flags;
  int pad;
};
static_assert(sizeof(R3DUniforms) == 336, "R3DUniforms must match the Metal shader");

enum : uint32_t
{
  F_TEXTURE_ENABLED = 1u << 0,
  F_MICRO_TEXTURE = 1u << 1,
  F_TEXTURE_INVERTED = 1u << 2,
  F_TEXTURE_ALPHA = 1u << 3,
  F_ALPHA_TEST = 1u << 4,
  F_DISCARD_ALPHA = 1u << 5,
  F_LIGHT_ENABLED = 1u << 6,
  F_SUN_CLAMP = 1u << 7,
  F_INTENSITY_CLAMP = 1u << 8,
  F_SPECULAR_ENABLED = 1u << 9,
  F_FIXED_SHADING = 1u << 10,
  F_SMOOTH_SHADING = 1u << 11,
  F_STEP15_LUMINOUS = 1u << 12,
  F_TRANSLATOR_MAP = 1u << 13,
};

struct FullUniforms
{
  simd_float4 offsetScale;
};

struct FogUniforms
{
  simd_float4 fogColour;
  simd_float4 spotFogColor;
  simd_float4 spotEllipse;
  float fogAttenuation;
  float fogAmbient;
  float pad[2];
};
static_assert(sizeof(FogUniforms) == 64, "FogUniforms must match the Metal shader");

simd_float4x4 Matrix(const float *m)
{
  simd_float4x4 r;
  std::memcpy(&r, m, sizeof(r));  // both column major
  return r;
}

simd_float4 Vec3(const float *v, float w = 0.0f) { return simd_make_float4(v[0], v[1], v[2], w); }

void SetFlag(uint32_t &flags, uint32_t flag, bool on)
{
  if (on)
    flags |= flag;
  else
    flags &= ~flag;
}

}  // namespace

class Context
{
public:
  id<MTLDevice> device;
  id<MTLCommandQueue> queue;
  id<MTLLibrary> library;
  id<MTLTexture> target;
  id<MTLBuffer> readback[2];
  int readbackIndex = 0;
  bool readbackValid[2] = {false, false};
  int width = 0;
  int height = 0;
  MTLScissorRect scissor{};
  id<MTLCommandBuffer> cmd;
  std::vector<std::function<void()>> completed;  // run once the frame is done

  // screen_surface (Surface()): frames drawn into IOSurface-backed BGRA
  // pixel buffers on the GPU instead of being copied to the CPU, so the
  // front end shows them without a copy (Lindbergh's iOS host does the same).
  // Six: the last three handed out, the newest finished, the one the frame in
  // flight draws into, and one to draw into next.
  static constexpr int kSurfaces = 6;
  std::mutex surfaceLock;          // the front end asks from its own thread
  bool surfacesOn = false;         // asked for: under surfaceLock
  bool surfacesTried = false, surfacesOk = false;
  CVMetalTextureCacheRef surfaceCache = nullptr;
  CVPixelBufferRef surface[kSurfaces] = {};
  CVMetalTextureRef surfaceTexture[kSurfaces] = {};
  id<MTLRenderPipelineState> surfacePipeline;
  int surfacePending = -1;         // drawn into by the frame in flight
  int surfaceReady = -1;           // the newest finished: under surfaceLock
  int surfaceHanded[3] = {-1, -1, -1};  // newest first: under surfaceLock

  // Asynchronous frames (the default; ATTRACT_M3_SYNC=1 turns it off): the
  // GPU draws frame N while the CPU runs the boards for frame N+1, and the
  // screen handed out is one frame old. Everything that writes to memory
  // the GPU reads calls WaitInFlight() first. Line-of-sight results reach
  // the game one frame later than in sync mode, as in Supermodel's own
  // multi-threaded rendering; parity checks run in sync mode.
  bool async = true;
  id<MTLCommandBuffer> inflight;
  std::vector<std::function<void()>> inflightDone;

  // ATTRACT_M3_TIMING=1: average times per frame on stderr, every 600 frames
  bool timing = false;
  int timedFrames = 0;
  double coreMs = 0, waitMs = 0, gpuMs = 0, intervalMs = 0;
  double frameWaitMs = 0;  // this frame's share of waitMs
  std::chrono::steady_clock::time_point frameStart, lastStart;

  ~Context()
  {
    // the renderers (and the callbacks' targets) are gone by now
    inflightDone.clear();
    if (inflight)
      [inflight waitUntilCompleted];
    for (int i = 0; i < kSurfaces; i++)
    {
      if (surfaceTexture[i])
        CFRelease(surfaceTexture[i]);
      if (surface[i])
        CVPixelBufferRelease(surface[i]);
    }
    if (surfaceCache)
      CFRelease(surfaceCache);
  }

  // The surfaces and their pipeline, made the first frame after they were
  // asked for. False (and frames copied as before) when this GPU can't.
  bool SetUpSurfaces()
  {
    if (surfacesTried)
      return surfacesOk;
    surfacesTried = true;
    NSError *error = nil;
    id<MTLLibrary> lib = [device newLibraryWithSource:[NSString stringWithUTF8String:kSurfaceShader]
                                              options:nil
                                                error:&error];
    if (!lib)
    {
      NSLog(@"Metal: surface shader: %@ (frames copied instead)", error);
      return false;
    }
    MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
    d.vertexFunction = [lib newFunctionWithName:@"surfaceVertex"];
    d.fragmentFunction = [lib newFunctionWithName:@"surfaceFragment"];
    d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    surfacePipeline = Pipeline(d);
    if (!surfacePipeline ||
        CVMetalTextureCacheCreate(kCFAllocatorDefault, nullptr, device, nullptr, &surfaceCache) != kCVReturnSuccess)
    {
      NSLog(@"Metal: no surfaces (frames copied instead)");
      return false;
    }
    NSDictionary *attrs = @{
      (id)kCVPixelBufferIOSurfacePropertiesKey: @{},
      (id)kCVPixelBufferMetalCompatibilityKey: @YES,
    };
    NSDictionary *textureAttrs = @{(id)kCVMetalTextureUsage: @(MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead)};
    for (int i = 0; i < kSurfaces; i++)
    {
      CVReturn r = CVPixelBufferCreate(kCFAllocatorDefault, size_t(width), size_t(height), kCVPixelFormatType_32BGRA,
                                       (__bridge CFDictionaryRef)attrs, &surface[i]);
      if (r == kCVReturnSuccess)
        r = CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault, surfaceCache, surface[i],
                                                      (__bridge CFDictionaryRef)textureAttrs, MTLPixelFormatBGRA8Unorm,
                                                      size_t(width), size_t(height), 0, &surfaceTexture[i]);
      if (r != kCVReturnSuccess)
      {
        NSLog(@"Metal: no surface %d (%d): frames copied instead", i, r);
        return false;
      }
    }
    surfacesOk = true;
    return true;
  }

  // A surface no one may still be reading: not handed out lately, not the
  // newest finished, not the frame in flight's.
  int FreeSurface()
  {
    std::lock_guard<std::mutex> lock(surfaceLock);
    for (int i = 0; i < kSurfaces; i++)
      if (i != surfaceReady && i != surfacePending && i != surfaceHanded[0] && i != surfaceHanded[1] &&
          i != surfaceHanded[2])
        return i;
    return -1;  // not reached: six surfaces, five held at most
  }

  // Waits for the frame the GPU is drawing, if any, and runs what was
  // waiting on its results.
  void WaitInFlight()
  {
    if (!inflight)
      return;
    using Clock = std::chrono::steady_clock;
    const Clock::time_point start = Clock::now();
    [inflight waitUntilCompleted];
    if (inflight.status == MTLCommandBufferStatusError)
      NSLog(@"Metal: frame failed: %@", inflight.error);
    if (timing)
    {
      frameWaitMs += std::chrono::duration<double, std::milli>(Clock::now() - start).count();
      gpuMs += (inflight.GPUEndTime - inflight.GPUStartTime) * 1000.0;
    }
    inflight = nil;
    std::vector<std::function<void()>> done;
    done.swap(inflightDone);
    for (auto &f : done)
      f();
  }

  id<MTLCommandBuffer> CommandBuffer()
  {
    if (!cmd)
      cmd = [queue commandBuffer];
    return cmd;
  }

  id<MTLFunction> Function(const char *name)
  {
    id<MTLFunction> f = [library newFunctionWithName:[NSString stringWithUTF8String:name]];
    if (!f)
      NSLog(@"Metal: no shader function %s", name);
    return f;
  }

  // The full-screen quads (fog, composite, 2D layers): one colour target
  // or New3D's three, with OpenGL's alpha blending or none.
  id<MTLRenderPipelineState> FullPipeline(const char *fragment, int colourTargets, bool blend, bool depth)
  {
    MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
    d.vertexFunction = Function("fullVertex");
    d.fragmentFunction = Function(fragment);
    for (int i = 0; i < colourTargets; i++)
    {
      d.colorAttachments[i].pixelFormat = kColourFormat;
      if (blend)
      {
        d.colorAttachments[i].blendingEnabled = YES;
        d.colorAttachments[i].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        d.colorAttachments[i].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        d.colorAttachments[i].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
        d.colorAttachments[i].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
      }
    }
    if (depth)
    {
      d.depthAttachmentPixelFormat = kDepthFormat;
      d.stencilAttachmentPixelFormat = kDepthFormat;
    }
    return Pipeline(d);
  }

  id<MTLRenderPipelineState> Pipeline(MTLRenderPipelineDescriptor *d)
  {
    NSError *error = nil;
    id<MTLRenderPipelineState> p = [device newRenderPipelineStateWithDescriptor:d error:&error];
    if (!p)
      NSLog(@"Metal: pipeline: %@", error);
    return p;
  }

  void SetScissor(id<MTLRenderCommandEncoder> enc) { [enc setScissorRect:scissor]; }
};

std::shared_ptr<Context> CreateContext(int width, int height, std::string *why)
{
  @autoreleasepool
  {
    auto ctx = std::make_shared<Context>();
    ctx->device = MTLCreateSystemDefaultDevice();
    if (!ctx->device)
    {
      *why = "Metal: no GPU";
      return nullptr;
    }
    ctx->queue = [ctx->device newCommandQueue];
    const char *timing = af_debug_env("ATTRACT_M3_TIMING");
    ctx->timing = timing && timing[0] == '1';

    MTLCompileOptions *options = [MTLCompileOptions new];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    options.fastMathEnabled = NO;  // the quad interpolation needs exact floats
#pragma clang diagnostic pop
    NSError *error = nil;
    ctx->library = [ctx->device newLibraryWithSource:[NSString stringWithUTF8String:kR3DShadersMetal]
                                             options:options
                                               error:&error];
    if (!ctx->library)
    {
      *why = std::string("Metal: shaders: ") + (error ? error.localizedDescription.UTF8String : "?");
      return nullptr;
    }

    ctx->width = width;
    ctx->height = height;
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:kColourFormat
                                                                                  width:width
                                                                                 height:height
                                                                              mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModePrivate;
    ctx->target = [ctx->device newTextureWithDescriptor:td];
    for (auto &b : ctx->readback)
      b = [ctx->device newBufferWithLength:size_t(width) * height * 4 options:MTLResourceStorageModeShared];
    const char *sync = af_debug_env("ATTRACT_M3_SYNC");
    ctx->async = !(sync && *sync && *sync != '0');

    // The scissor box Supermodel's ResizeGLScreen() leaves (2 pixels in on
    // each side at 496 x 384), as m3core.cpp sets it for OpenGL.
    const int correction = int((height / 384.0) * 2.0 + 0.5);
    ctx->scissor = MTLScissorRect{NSUInteger(correction), NSUInteger(correction),
                                  NSUInteger(width - correction * 2), NSUInteger(height - correction * 2)};
    return ctx;
  }
}

bool EndFrame(Context &ctx, uint8_t *rgba)
{
  @autoreleasepool
  {
    bool on;
    {
      std::lock_guard<std::mutex> lock(ctx.surfaceLock);
      on = ctx.surfacesOn;
    }
    if (on && ctx.SetUpSurfaces())
    {
      // the target into a free surface, on the GPU; no copy to the CPU
      const int into = ctx.FreeSurface();
      id<MTLCommandBuffer> cmd = ctx.CommandBuffer();
      MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
      pass.colorAttachments[0].texture = CVMetalTextureGetTexture(ctx.surfaceTexture[into]);
      pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
      pass.colorAttachments[0].storeAction = MTLStoreActionStore;
      id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:pass];
      [enc setRenderPipelineState:ctx.surfacePipeline];
      [enc setFragmentTexture:ctx.target atIndex:0];
      [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
      [enc endEncoding];

      ctx.WaitInFlight();  // the last frame (normally done already): its surface is finished
      {
        std::lock_guard<std::mutex> lock(ctx.surfaceLock);
        if (ctx.surfacePending >= 0)
          ctx.surfaceReady = ctx.surfacePending;
        ctx.surfacePending = into;
      }
      [cmd commit];
      ctx.cmd = nil;
      ctx.inflight = cmd;
      ctx.inflightDone.swap(ctx.completed);
      ctx.completed.clear();
      if (!ctx.async)
      {
        ctx.WaitInFlight();
        std::lock_guard<std::mutex> lock(ctx.surfaceLock);
        ctx.surfaceReady = into;
        ctx.surfacePending = -1;
      }
      return false;
    }

    const size_t bytes = size_t(ctx.width) * ctx.height * 4;
    const int cur = ctx.readbackIndex;
    id<MTLCommandBuffer> cmd = ctx.CommandBuffer();
    id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
    [blit copyFromTexture:ctx.target
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                      sourceSize:MTLSizeMake(ctx.width, ctx.height, 1)
                        toBuffer:ctx.readback[cur]
               destinationOffset:0
          destinationBytesPerRow:size_t(ctx.width) * 4
        destinationBytesPerImage:bytes];
    [blit endEncoding];

    ctx.WaitInFlight();  // the last frame (normally done already)
    [cmd commit];
    ctx.cmd = nil;
    ctx.inflight = cmd;
    ctx.inflightDone.swap(ctx.completed);
    ctx.completed.clear();
    ctx.readbackValid[cur] = true;
    int show = cur;
    if (ctx.async)
    {
      show = cur ^ 1;  // the last frame's picture; this one is still being drawn
      ctx.readbackIndex = cur ^ 1;
    }
    else
      ctx.WaitInFlight();

    if (ctx.readbackValid[show])
      std::memcpy(rgba, ctx.readback[show].contents, bytes);
    else
      std::memset(rgba, 0, bytes);
    return true;
  }
}

void *Surface(Context &ctx, int *width, int *height)
{
  std::lock_guard<std::mutex> lock(ctx.surfaceLock);
  ctx.surfacesOn = true;
  const int i = ctx.surfaceReady;
  if (i < 0)
    return nullptr;
  if (ctx.surfaceHanded[0] != i)
  {
    ctx.surfaceHanded[2] = ctx.surfaceHanded[1];
    ctx.surfaceHanded[1] = ctx.surfaceHanded[0];
    ctx.surfaceHanded[0] = i;
  }
  if (width)
    *width = ctx.width;
  if (height)
    *height = ctx.height;
  return ctx.surface[i];
}

bool CopySurface(Context &ctx, uint32_t *argb)
{
  std::lock_guard<std::mutex> lock(ctx.surfaceLock);
  const int i = ctx.surfaceReady;
  if (i < 0)
    return false;
  CVPixelBufferRef b = ctx.surface[i];
  if (CVPixelBufferLockBaseAddress(b, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess)
    return false;
  const uint8_t *base = static_cast<const uint8_t *>(CVPixelBufferGetBaseAddress(b));
  const size_t stride = CVPixelBufferGetBytesPerRow(b);
  for (int y = 0; y < ctx.height; y++)
  {
    // BGRA bytes are 0xAARRGGBB words on these little-endian CPUs
    const uint32_t *src = reinterpret_cast<const uint32_t *>(base + size_t(y) * stride);
    uint32_t *dst = argb + size_t(y) * ctx.width;
    for (int x = 0; x < ctx.width; x++)
      dst[x] = src[x] | 0xff000000u;
  }
  CVPixelBufferUnlockBaseAddress(b, kCVPixelBufferLock_ReadOnly);
  return true;
}

void FrameStart(Context &ctx)
{
  if (!ctx.timing)
    return;
  using Clock = std::chrono::steady_clock;
  const Clock::time_point now = Clock::now();
  if (ctx.lastStart != Clock::time_point())
    ctx.intervalMs += std::chrono::duration<double, std::milli>(now - ctx.lastStart).count();
  ctx.lastStart = now;
  ctx.frameStart = now;
  ctx.frameWaitMs = 0;
}

void FrameDone(Context &ctx)
{
  if (!ctx.timing || ctx.frameStart == std::chrono::steady_clock::time_point())
    return;
  using Clock = std::chrono::steady_clock;
  ctx.coreMs += std::chrono::duration<double, std::milli>(Clock::now() - ctx.frameStart).count();
  ctx.waitMs += ctx.frameWaitMs;
  if (++ctx.timedFrames == 600)
  {
    // core: the whole RunFrame, waiting for the GPU included; frame: the
    // time between RunFrame calls, set by the app's pacing
    fprintf(stderr, "metal: per frame (%s): core %.2f ms (waiting for GPU %.2f ms), GPU %.2f ms, frame %.2f ms\n",
            ctx.async ? "async" : "sync", ctx.coreMs / 600, ctx.waitMs / 600, ctx.gpuMs / 600,
            ctx.intervalMs / 600);
    ctx.timedFrames = 0;
    ctx.coreMs = ctx.waitMs = ctx.gpuMs = ctx.intervalMs = 0;
  }
}

// ---------------------------------------------------------------------------
// 2D layers (CRender2D)

namespace {

class Render2DMetal final : public IRender2D
{
public:
  explicit Render2DMetal(std::shared_ptr<Context> ctx) : m_ctx(std::move(ctx))
  {
    m_pipeline = m_ctx->FullPipeline("layer2DFragment", 1, true, false);
    for (auto &t : m_textures)
    {
      MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:kColourFormat
                                                                                    width:496
                                                                                   height:384
                                                                                mipmapped:NO];
      td.usage = MTLTextureUsageShaderRead;
      td.storageMode = kUploadStorage;
      t = [m_ctx->device newTextureWithDescriptor:td];
    }
  }

  Result Init(unsigned xOffset, unsigned yOffset, unsigned xRes, unsigned yRes, unsigned totalXRes, unsigned totalYRes, unsigned, UpscaleMode) override
  {
    // Setup2D(): glViewport(m_xOffset - m_correction, m_yOffset + m_correction, m_xPixels, m_yPixels)
    // over the whole target, as a clip space offset and scale
    const int correction = int(((yRes / 384.) * 2.) + 0.5);  // for some reason the 2d layer is 2 pixels off the 3D
    const float vx = float(int(xOffset) - correction), vy = float(int(yOffset) + correction);
    m_offsetScale = simd_make_float4((2.0f * vx + xRes) / totalXRes - 1.0f, (2.0f * vy + yRes) / totalYRes - 1.0f,
                                     float(xRes) / totalXRes, float(yRes) / totalYRes);
    return Result::OKAY;
  }

  void AttachDrawBuffers(std::shared_ptr<TileGenBuffer> bottom, std::shared_ptr<TileGenBuffer> top) override
  {
    m_drawBuffers[0] = bottom;
    m_drawBuffers[1] = top;
  }

  void BeginFrame() override {}
  void EndFrame() override {}

  void PreRenderFrame() override
  {
    m_ctx->WaitInFlight();
    for (int i = 0; i < 2; i++)
    {
      if (!m_drawBuffers[i])
        continue;  // we don't have a draw buffer yet
      [m_textures[i] replaceRegion:MTLRegionMake2D(0, 0, 496, 384)
                       mipmapLevel:0
                         withBytes:m_drawBuffers[i]->data
                       bytesPerRow:512 * 4];  // skip the non viewable data
    }
  }

  void RenderFrameBottom() override { Draw(0, true); }
  void RenderFrameTop() override { Draw(1, false); }

private:
  void Draw(int layer, bool clear)
  {
    @autoreleasepool
    {
      MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
      rp.colorAttachments[0].texture = m_ctx->target;
      rp.colorAttachments[0].loadAction = clear ? MTLLoadActionClear : MTLLoadActionLoad;  // the bottom layer clears everything, scissor or not
      rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
      rp.colorAttachments[0].storeAction = MTLStoreActionStore;
      id<MTLRenderCommandEncoder> enc = [m_ctx->CommandBuffer() renderCommandEncoderWithDescriptor:rp];
      [enc setViewport:MTLViewport{0, 0, double(m_ctx->width), double(m_ctx->height), 0, 1}];
      m_ctx->SetScissor(enc);
      [enc setRenderPipelineState:m_pipeline];
      FullUniforms u{m_offsetScale};
      [enc setVertexBytes:&u length:sizeof(u) atIndex:0];
      [enc setFragmentTexture:m_textures[layer] atIndex:0];
      [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
      [enc endEncoding];
    }
  }

  std::shared_ptr<Context> m_ctx;
  id<MTLRenderPipelineState> m_pipeline;
  id<MTLTexture> m_textures[2];
  std::shared_ptr<TileGenBuffer> m_drawBuffers[2];
  simd_float4 m_offsetScale = simd_make_float4(0, 0, 1, 1);
};

// ---------------------------------------------------------------------------
// New3D (R3DGpuGL, R3DShader, R3DFrameBuffers, R3DScrollFog, TextureBank)

class R3DGpuMetal final : public New3D::IR3DGpu
{
public:
  R3DGpuMetal(std::shared_ptr<Context> ctx, int maxVerts) : m_ctx(std::move(ctx))
  {
    id<MTLDevice> dev = m_ctx->device;

    m_vertices = [dev newBufferWithLength:size_t(maxVerts) * sizeof(FVertex) options:MTLResourceStorageModeShared];
    m_losResults = [dev newBufferWithLength:4 * 8 options:MTLResourceStorageModeShared];

    // TextureBank: 2048 x 1024, 16-bit texels, mipmaps down to 1 x 1
    for (auto &t : m_banks)
    {
      MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR16Uint
                                                                                    width:2048
                                                                                   height:1024
                                                                                mipmapped:YES];
      td.usage = MTLTextureUsageShaderRead;
      td.storageMode = kUploadStorage;
      t = [dev newTextureWithDescriptor:td];
    }

    // R3DShader: one pipeline for each layer, writing the buffers
    // R3DFrameBuffers::SetFBO() draws to
    for (int layer = 0; layer < 3; layer++)
    {
      MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
      d.vertexFunction = m_ctx->Function("r3dQuadVertex");
      d.fragmentFunction = m_ctx->Function("r3dQuadFragment");
      for (int i = 0; i < 3; i++)
      {
        d.colorAttachments[i].pixelFormat = kColourFormat;
        d.colorAttachments[i].writeMask = (layer == 0 || layer == i) ? MTLColorWriteMaskAll : MTLColorWriteMaskNone;
      }
      d.depthAttachmentPixelFormat = kDepthFormat;
      d.stencilAttachmentPixelFormat = kDepthFormat;
      m_layerPipelines[layer] = m_ctx->Pipeline(d);
    }
    m_fogPipeline = m_ctx->FullPipeline("scrollFogFragment", 3, false, true);
    m_basePipeline = m_ctx->FullPipeline("baseLayerFragment", 1, true, false);
    m_transPipeline = m_ctx->FullPipeline("transLayerFragment", 1, true, false);
    m_solidPipeline = m_ctx->FullPipeline("solidFragment", 1, false, false);

    // depth GEQUAL (opaque) or GREATER (transparent), each with the two
    // stencil setups R3DShader::SetMeshUniforms() makes
    for (int greater = 0; greater < 2; greater++)
    {
      for (int layered = 0; layered < 2; layered++)
      {
        MTLStencilDescriptor *s = [MTLStencilDescriptor new];
        s.stencilFailureOperation = MTLStencilOperationKeep;
        s.depthFailureOperation = MTLStencilOperationKeep;
        if (layered)
        {
          s.stencilCompareFunction = MTLCompareFunctionEqual;  // passes if the value is zero
          s.depthStencilPassOperation = MTLStencilOperationIncrementClamp;
          s.readMask = 0x7f;
          s.writeMask = 0x7f;
        }
        else
        {
          s.stencilCompareFunction = MTLCompareFunctionAlways;
          s.depthStencilPassOperation = MTLStencilOperationReplace;
          s.readMask = 0x80;
          s.writeMask = 0x80;
        }
        MTLDepthStencilDescriptor *d = [MTLDepthStencilDescriptor new];
        d.depthCompareFunction = greater ? MTLCompareFunctionGreater : MTLCompareFunctionGreaterEqual;
        d.depthWriteEnabled = YES;
        d.frontFaceStencil = s;
        d.backFaceStencil = s;
        m_depthStates[greater][layered] = [dev newDepthStencilStateWithDescriptor:d];
      }
    }
    MTLDepthStencilDescriptor *off = [MTLDepthStencilDescriptor new];
    off.depthCompareFunction = MTLCompareFunctionAlways;
    off.depthWriteEnabled = NO;
    m_noDepthState = [dev newDepthStencilStateWithDescriptor:off];

    std::memset(&m_u, 0, sizeof(m_u));
  }

  Result Init(int totalXRes, int totalYRes, unsigned) override
  {
    // R3DFrameBuffers::CreateFBO(): 3 colour buffers, and depth/stencil
    // twice (the second is StoreDepth()'s copy)
    m_width = totalXRes;
    m_height = totalYRes;
    id<MTLDevice> dev = m_ctx->device;
    for (auto &t : m_layers)
    {
      MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:kColourFormat
                                                                                    width:totalXRes
                                                                                   height:totalYRes
                                                                                mipmapped:NO];
      td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
      td.storageMode = MTLStorageModePrivate;
      t = [dev newTextureWithDescriptor:td];
    }
    for (auto &t : m_depth)
    {
      MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:kDepthFormat
                                                                                    width:totalXRes
                                                                                   height:totalYRes
                                                                                mipmapped:NO];
      td.usage = MTLTextureUsageRenderTarget;
      td.storageMode = MTLStorageModePrivate;
      t = [dev newTextureWithDescriptor:td];
    }
    return (m_layers[0] && m_depth[1]) ? Result::OKAY : Result::FAIL;
  }

  void AttachTextureRAM(const UINT16 *textureRAM) override { m_textureRAM = textureRAM; }

  int TextureLevels() const override { return 11; }  // as TextureBank counts them

  void UploadTextures(int page, int level, int x, int y, int width, int height) override
  {
    // The board uploads while the PowerPC runs, when the GPU may still be
    // drawing the last frame from these textures: queued, and copied from
    // texture RAM before the next frame is drawn. Texture RAM only changes
    // through uploads, so the textures are the same when the frame draws.
    m_queuedUploads.push_back({page, level, x, y, width, height});
  }

  void UploadQueued(int page, int level, int x, int y, int width, int height)
  {
    // TextureBank::UploadTextures(): the mipmaps sit in the texture sheet
    static constexpr int mipXBase[] = {0, 1024, 1536, 1792, 1920, 1984, 2016, 2032, 2040, 2044, 2046, 2047};
    static constexpr int mipYBase[] = {0, 512, 768, 896, 960, 992, 1008, 1016, 1020, 1022, 1023};
    if (!m_textureRAM || level < 0 || level > 10)
      return;
    const UINT16 *ram = m_textureRAM + size_t(page) * 2048 * 1024;
    int subX = x - mipXBase[level];
    int subY = y - mipYBase[level];
    // OpenGL drops an upload that goes outside the level; Metal must not see one
    const int levelW = std::max(2048 >> level, 1), levelH = std::max(1024 >> level, 1);
    if (subX < 0 || subY < 0 || width <= 0 || height <= 0 || subX + width > levelW || subY + height > levelH)
      return;
    [m_banks[page] replaceRegion:MTLRegionMake2D(subX, subY, width, height)
                     mipmapLevel:level
                       withBytes:ram + size_t(y) * 2048 + x
                     bytesPerRow:2048 * 2];
  }

  void UploadRamVerts(int ramBase, const FVertex *verts, size_t count) override
  {
    Ready();
    std::memcpy(static_cast<FVertex *>(m_vertices.contents) + ramBase, verts, count * sizeof(FVertex));
  }

  int RomVertsUploaded() const override { return m_romVerts; }

  void AppendRomVerts(const FVertex *verts, size_t count) override
  {
    Ready();
    // VBO::AppendData() refuses what does not fit
    if ((size_t(m_romVerts) + count) * sizeof(FVertex) >= m_vertices.length)
      return;
    std::memcpy(static_cast<FVertex *>(m_vertices.contents) + m_romVerts, verts, count * sizeof(FVertex));
    m_romVerts += int(count);
  }

  void ResetRom() override { m_romVerts = 0; }

  void WhiteFlash() override
  {
    // a scissored glClear() of the target to white
    @autoreleasepool
    {
      End();
      Ready();
      MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
      rp.colorAttachments[0].texture = m_ctx->target;
      rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
      rp.colorAttachments[0].storeAction = MTLStoreActionStore;
      id<MTLRenderCommandEncoder> enc = [m_ctx->CommandBuffer() renderCommandEncoderWithDescriptor:rp];
      [enc setViewport:MTLViewport{0, 0, double(m_ctx->width), double(m_ctx->height), 0, 1}];
      m_ctx->SetScissor(enc);
      [enc setRenderPipelineState:m_solidPipeline];
      FullUniforms fu{simd_make_float4(0, 0, 1, 1)};
      simd_float4 white = simd_make_float4(1, 1, 1, 1);
      [enc setVertexBytes:&fu length:sizeof(fu) atIndex:0];
      [enc setFragmentBytes:&white length:sizeof(white) atIndex:0];
      [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
      [enc endEncoding];
    }
  }

  void BeginFrame() override
  {
    // all 3 colour buffers cleared, then the fog drawn into them
    @autoreleasepool
    {
      End();
      Ready();
      m_losCount = 0;  // the last frame's results have been read
      // the fog writes no depth, and each priority clears it
      MTLRenderPassDescriptor *rp = LayerPass(MTLLoadActionClear, m_depth[0], MTLLoadActionDontCare, MTLStoreActionDontCare);
      m_enc = [m_ctx->CommandBuffer() renderCommandEncoderWithDescriptor:rp];
      m_ctx->SetScissor(m_enc);
    }
  }

  void DrawFog(const Viewport &vp, const float rgba[4], float attenuation, float ambient, const float spotRGB[3], const float spotEllipse[4]) override
  {
    [m_enc setViewport:MTLViewport{double(vp.x), double(vp.y), double(vp.width), double(vp.height), 0, 1}];
    [m_enc setRenderPipelineState:m_fogPipeline];
    [m_enc setDepthStencilState:m_noDepthState];
    FullUniforms fu{simd_make_float4(0, 0, 1, 1)};
    FogUniforms u{};
    u.fogColour = simd_make_float4(rgba[0], rgba[1], rgba[2], rgba[3]);
    u.spotFogColor = Vec3(spotRGB);
    u.spotEllipse = simd_make_float4(spotEllipse[0], spotEllipse[1], spotEllipse[2], spotEllipse[3]);
    u.fogAttenuation = attenuation;
    u.fogAmbient = ambient;
    [m_enc setVertexBytes:&fu length:sizeof(fu) atIndex:0];
    [m_enc setFragmentBytes:&u length:sizeof(u) atIndex:0];
    [m_enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
  }

  void BeginPriority() override
  {
    End();
    m_clearDepth[0] = true;  // glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT) to 0
    m_dirtyMesh = true;      // R3DShader::Start()
  }

  // A layer's render pass starts with its first mesh: on the Apple TV's
  // GPU every pass loads and stores all four targets, and many layers draw
  // nothing. A depth clear or StoreDepth()'s copy waits until then too.
  void BeginLayer(Layer layer) override
  {
    End();
    switch (layer)
    {
    case Layer::colour:
      m_layer = 0;
      m_depthIndex = 0;
      m_trans1Drew = false;
      break;
    case Layer::trans1:
      // StoreDepth(): trans1 draws on the original, trans2 on a copy made
      // before trans1 draws (what RestoreDepth() gives OpenGL)
      m_layer = 1;
      m_depthIndex = 0;
      break;
    case Layer::trans2:
      // the copy, or the original when trans1 left it as it was
      m_layer = 2;
      m_depthIndex = m_trans1Drew ? 1 : 0;
      break;
    default:
      return;
    }
    m_currentDepth = m_depth[m_depthIndex];
    SetFlag(m_u.flags, F_DISCARD_ALPHA, m_layer == 0);
    m_u.colourLayer = m_layer;
  }

  void BeginScene() override
  {
    if (m_enc)
    {
      [m_enc setFragmentTexture:m_banks[0] atIndex:0];
      [m_enc setFragmentTexture:m_banks[1] atIndex:1];
    }
  }

  void SetViewport(const Viewport &vp) override
  {
    m_viewport = MTLViewport{double(vp.x), double(vp.y), double(vp.width), double(vp.height), 0, 1};
    if (m_enc)
      [m_enc setViewport:m_viewport];

    // R3DShader::SetViewportUniforms()
    m_u.fogDensity = vp.fogDensity;
    m_u.fogStart = vp.fogStart;
    m_u.fogColour = Vec3(vp.fogColour);
    m_u.fogAttenuation = vp.fogAttenuation;
    m_u.fogAmbient = vp.fogAmbient;
    m_u.lighting0 = Vec3(&vp.lightingParams[0]);
    m_u.lighting1 = Vec3(&vp.lightingParams[3]);
    SetFlag(m_u.flags, F_SUN_CLAMP, vp.sunClamp);
    SetFlag(m_u.flags, F_INTENSITY_CLAMP, vp.intensityClamp);
    m_u.spotEllipse = simd_make_float4(vp.spotEllipse[0], vp.spotEllipse[1], vp.spotEllipse[2], vp.spotEllipse[3]);
    m_u.spotRange = simd_make_float4(vp.spotRange[0], vp.spotRange[1], 0, 0);
    m_u.spotColor = Vec3(vp.spotColor);
    m_u.spotFogColor = Vec3(vp.spotFogColor);
    m_u.projMat = Matrix(vp.projectionMatrix);
    SetFlag(m_u.flags, F_STEP15_LUMINOUS, vp.step15Luminous);
    m_u.cota = vp.cota;
  }

  void SetModel(const Model &model) override
  {
    // R3DShader::SetModelStates()
    m_u.modelScale = model.scale;
    m_u.nodeAlpha = model.alpha;
    m_transX = model.textureOffsetX;
    m_transY = model.textureOffsetY;
    m_transPage = model.page;
    m_u.modelMat = Matrix(model.modelMat);
  }

  void DrawMesh(const Mesh &m) override
  {
    // R3DShader::SetMeshUniforms(): the uniforms are always the mesh's own
    // values; the stencil setup is a state that carries over, as in OpenGL
    OpenLayerPass();
    SetFlag(m_u.flags, F_TEXTURE_ENABLED, m.textured);
    SetFlag(m_u.flags, F_MICRO_TEXTURE, m.microTexture);
    m_u.texturePage = m.page ^ m_transPage;
    m_u.microTextureMinLOD = m.microTextureMinLOD;
    m_u.microTextureID = m.microTextureID;
    m_u.baseTexInfo = simd_make_int4(m.x + m_transX, m.y + m_transY, m.width, m.height);
    m_u.baseTexType = m.format;
    SetFlag(m_u.flags, F_TEXTURE_INVERTED, m.inverted);
    SetFlag(m_u.flags, F_ALPHA_TEST, m.alphaTest);
    SetFlag(m_u.flags, F_TEXTURE_ALPHA, m.textureAlpha);
    m_u.fogIntensity = m.fogIntensity;
    SetFlag(m_u.flags, F_LIGHT_ENABLED, m.lighting);
    m_u.shininess = m.shininess;
    SetFlag(m_u.flags, F_SPECULAR_ENABLED, m.specular);
    m_u.specularValue = m.specularValue;
    SetFlag(m_u.flags, F_FIXED_SHADING, m.fixedShading);
    SetFlag(m_u.flags, F_SMOOTH_SHADING, m.smoothShading);
    SetFlag(m_u.flags, F_TRANSLATOR_MAP, m.translatorMap);
    m_u.textureWrapMode = simd_make_int2(m.wrapModeU, m.wrapModeV);

    if (m_dirtyMesh || m.noLosReturn != m_noLosReturn)
    {
      m_noLosReturn = m.noLosReturn;
      m_stencilLayered = false;
      m_stencilRef = uint32_t(m_noLosReturn) << 7;
    }
    if (m_dirtyMesh || m.layered != m_layered)
    {
      m_layered = m.layered;
      m_stencilLayered = m_layered;
      m_stencilRef = m_layered ? 0 : uint32_t(m_noLosReturn) << 7;
      m_u.layerDepthBias = m_layered ? 1.0f + 1.0f / 512.0f : 1.0f;
    }
    m_dirtyMesh = false;

    [m_enc setDepthStencilState:m_depthStates[m_layer == 0 ? 0 : 1][m_stencilLayered ? 1 : 0]];
    [m_enc setStencilReferenceValue:m_stencilRef];
    [m_enc setVertexBytes:&m_u length:sizeof(m_u) atIndex:1];
    [m_enc setFragmentBytes:&m_u length:sizeof(m_u) atIndex:1];
    // 6 vertices (2 triangles) for each quad of 4
    [m_enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:NSUInteger(m.vboOffset / 4) * 6 vertexCount:NSUInteger(m.vertexCount / 4) * 6];
  }

  void ReadLos(int x, int y, std::function<void(float depth, UINT8 stencil)> done) override
  {
    @autoreleasepool
    {
      End();
      if (x < 0 || y < 0 || x >= m_width || y >= m_height || m_losCount >= 4 || !m_currentDepth)
      {
        done(0.0f, 0);
        return;
      }
      if (m_clearDepth[m_depthIndex])
      {
        // nothing drawn since the clear: what the blit would read
        m_losCount++;
        done(0.0f, 0);
        return;
      }
      const size_t slot = size_t(m_losCount++) * 8;
      id<MTLBlitCommandEncoder> blit = [m_ctx->CommandBuffer() blitCommandEncoder];
      [blit copyFromTexture:m_currentDepth
                       sourceSlice:0
                       sourceLevel:0
                      sourceOrigin:MTLOriginMake(x, y, 0)
                        sourceSize:MTLSizeMake(1, 1, 1)
                          toBuffer:m_losResults
                 destinationOffset:slot
            destinationBytesPerRow:4
          destinationBytesPerImage:4
                           options:MTLBlitOptionDepthFromDepthStencil];
      [blit copyFromTexture:m_currentDepth
                       sourceSlice:0
                       sourceLevel:0
                      sourceOrigin:MTLOriginMake(x, y, 0)
                        sourceSize:MTLSizeMake(1, 1, 1)
                          toBuffer:m_losResults
                 destinationOffset:slot + 4
            destinationBytesPerRow:1
          destinationBytesPerImage:1
                           options:MTLBlitOptionStencilFromDepthStencil];
      [blit endEncoding];
      id<MTLBuffer> results = m_losResults;
      m_ctx->completed.push_back([this, results, slot, done]() {
        const uint8_t *p = static_cast<const uint8_t *>(results.contents) + slot;
        float depth;
        std::memcpy(&depth, p, 4);
        done(depth, p[4]);
      });
    }
  }

  void EndPriority() override { End(); }

  void Composite() override
  {
    // R3DFrameBuffers::Draw(): the opaque layer, then the two transparent
    // ones, blended onto the target (the 2D bottom layer is there)
    @autoreleasepool
    {
      End();
      MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
      rp.colorAttachments[0].texture = m_ctx->target;
      rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
      rp.colorAttachments[0].storeAction = MTLStoreActionStore;
      id<MTLRenderCommandEncoder> enc = [m_ctx->CommandBuffer() renderCommandEncoderWithDescriptor:rp];
      [enc setViewport:MTLViewport{0, 0, double(m_width), double(m_height), 0, 1}];
      m_ctx->SetScissor(enc);
      FullUniforms fu{simd_make_float4(0, 0, 1, 1)};
      [enc setVertexBytes:&fu length:sizeof(fu) atIndex:0];
      [enc setRenderPipelineState:m_basePipeline];
      [enc setFragmentTexture:m_layers[0] atIndex:0];
      [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
      [enc setRenderPipelineState:m_transPipeline];
      [enc setFragmentTexture:m_layers[1] atIndex:0];
      [enc setFragmentTexture:m_layers[2] atIndex:1];
      [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
      [enc endEncoding];
    }
  }

  // Stereo: an eye draws into its own target and layer buffers (swapped in
  // for the normal ones), in a command buffer of its own that is waited for.
  bool BeginEye(int width, int height, int inset, const UINT8 *background) override
  {
    @autoreleasepool
    {
      End();
      if (width != m_eyeWidth || height != m_eyeHeight)
      {
        id<MTLDevice> dev = m_ctx->device;
        auto texture = [&](MTLPixelFormat format, MTLTextureUsage usage) {
          MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                                                        width:width
                                                                                       height:height
                                                                                    mipmapped:NO];
          td.usage = usage;
          td.storageMode = MTLStorageModePrivate;
          return [dev newTextureWithDescriptor:td];
        };
        for (auto &t : m_eyeLayers)
          t = texture(kColourFormat, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
        for (auto &t : m_eyeDepth)
          t = texture(kDepthFormat, MTLTextureUsageRenderTarget);
        m_eyeTarget = texture(kColourFormat, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
        m_eyePixels = [dev newBufferWithLength:size_t(width) * height * 4 options:MTLResourceStorageModeShared];
        if (!m_eyeLayers[2] || !m_eyeDepth[1] || !m_eyeTarget || !m_eyePixels)
        {
          m_eyeWidth = m_eyeHeight = 0;
          return false;
        }
        m_eyeWidth = width;
        m_eyeHeight = height;
      }
      std::memcpy(m_eyePixels.contents, background, size_t(width) * height * 4);
      id<MTLBlitCommandEncoder> blit = [m_ctx->CommandBuffer() blitCommandEncoder];
      [blit copyFromBuffer:m_eyePixels
                 sourceOffset:0
            sourceBytesPerRow:size_t(width) * 4
          sourceBytesPerImage:size_t(width) * height * 4
                   sourceSize:MTLSizeMake(width, height, 1)
                    toTexture:m_eyeTarget
             destinationSlice:0
             destinationLevel:0
            destinationOrigin:MTLOriginMake(0, 0, 0)];
      [blit endEncoding];

      SwapEye();
      m_savedSize[0] = m_width;
      m_savedSize[1] = m_height;
      m_width = width;
      m_height = height;
      m_savedScissor = m_ctx->scissor;
      m_ctx->scissor = MTLScissorRect{NSUInteger(inset), NSUInteger(inset), NSUInteger(width - inset * 2),
                                      NSUInteger(height - inset * 2)};
      return true;
    }
  }

  void EndEye(UINT8 *rgba) override
  {
    @autoreleasepool
    {
      End();
      const size_t bytes = size_t(m_width) * m_height * 4;
      id<MTLCommandBuffer> cmd = m_ctx->CommandBuffer();
      id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
      [blit copyFromTexture:m_ctx->target
                       sourceSlice:0
                       sourceLevel:0
                      sourceOrigin:MTLOriginMake(0, 0, 0)
                        sourceSize:MTLSizeMake(m_width, m_height, 1)
                          toBuffer:m_eyePixels
                 destinationOffset:0
            destinationBytesPerRow:size_t(m_width) * 4
          destinationBytesPerImage:bytes];
      [blit endEncoding];
      [cmd commit];
      m_ctx->cmd = nil;
      [cmd waitUntilCompleted];
      if (cmd.status == MTLCommandBufferStatusError)
        NSLog(@"Metal: eye failed: %@", cmd.error);
      std::memcpy(rgba, m_eyePixels.contents, bytes);

      SwapEye();
      m_width = m_savedSize[0];
      m_height = m_savedSize[1];
      m_ctx->scissor = m_savedScissor;
    }
  }

private:
  void OpenLayerPass()
  {
    if (m_enc)
      return;
    @autoreleasepool
    {
      if (m_layer == 1 && !m_trans1Drew)
      {
        m_trans1Drew = true;
        if (m_clearDepth[0])
          m_clearDepth[1] = true;  // a copy of a cleared buffer
        else
        {
          id<MTLBlitCommandEncoder> blit = [m_ctx->CommandBuffer() blitCommandEncoder];
          [blit copyFromTexture:m_depth[0] toTexture:m_depth[1]];
          [blit endEncoding];
          m_clearDepth[1] = false;
        }
      }
      MTLRenderPassDescriptor *rp = LayerPass(MTLLoadActionLoad, m_depth[m_depthIndex],
                                              m_clearDepth[m_depthIndex] ? MTLLoadActionClear : MTLLoadActionLoad,
                                              MTLStoreActionStore);
      m_clearDepth[m_depthIndex] = false;
      m_enc = [m_ctx->CommandBuffer() renderCommandEncoderWithDescriptor:rp];
      m_ctx->SetScissor(m_enc);
      [m_enc setViewport:m_viewport];
      [m_enc setRenderPipelineState:m_layerPipelines[m_layer]];
      [m_enc setVertexBuffer:m_vertices offset:0 atIndex:0];
      [m_enc setFragmentTexture:m_banks[0] atIndex:0];
      [m_enc setFragmentTexture:m_banks[1] atIndex:1];
    }
  }

  // Before anything the GPU reads is written: the last frame is finished,
  // then the queued texture uploads are copied in.
  void Ready()
  {
    m_ctx->WaitInFlight();
    for (const QueuedUpload &u : m_queuedUploads)
      UploadQueued(u.page, u.level, u.x, u.y, u.width, u.height);
    m_queuedUploads.clear();
  }

  MTLRenderPassDescriptor *LayerPass(MTLLoadAction colourLoad, id<MTLTexture> depth, MTLLoadAction depthLoad, MTLStoreAction depthStore)
  {
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    for (int i = 0; i < 3; i++)
    {
      rp.colorAttachments[i].texture = m_layers[i];
      rp.colorAttachments[i].loadAction = colourLoad;
      rp.colorAttachments[i].clearColor = MTLClearColorMake(0, 0, 0, 0);
      rp.colorAttachments[i].storeAction = MTLStoreActionStore;
    }
    rp.depthAttachment.texture = depth;
    rp.depthAttachment.loadAction = depthLoad;
    rp.depthAttachment.clearDepth = 0.0;
    rp.depthAttachment.storeAction = depthStore;
    rp.stencilAttachment.texture = depth;
    rp.stencilAttachment.loadAction = depthLoad;
    rp.stencilAttachment.clearStencil = 0;
    rp.stencilAttachment.storeAction = depthStore;
    return rp;
  }

  void SwapEye()
  {
    for (int i = 0; i < 3; i++)
      std::swap(m_layers[i], m_eyeLayers[i]);
    for (int i = 0; i < 2; i++)
      std::swap(m_depth[i], m_eyeDepth[i]);
    id<MTLTexture> target = m_ctx->target;
    m_ctx->target = m_eyeTarget;
    m_eyeTarget = target;
  }

  void End()
  {
    if (m_enc)
    {
      [m_enc endEncoding];
      m_enc = nil;
    }
  }

  std::shared_ptr<Context> m_ctx;
  const UINT16 *m_textureRAM = nullptr;
  id<MTLTexture> m_banks[2];
  struct QueuedUpload
  {
    int page, level, x, y, width, height;
  };
  std::vector<QueuedUpload> m_queuedUploads;
  id<MTLBuffer> m_vertices;
  int m_romVerts = 0;
  id<MTLTexture> m_layers[3];
  id<MTLTexture> m_depth[2];
  id<MTLTexture> m_currentDepth;
  int m_width = 0, m_height = 0;

  id<MTLRenderPipelineState> m_layerPipelines[3];
  id<MTLRenderPipelineState> m_fogPipeline, m_basePipeline, m_transPipeline, m_solidPipeline;
  id<MTLDepthStencilState> m_depthStates[2][2];
  id<MTLDepthStencilState> m_noDepthState;

  id<MTLRenderCommandEncoder> m_enc;
  int m_layer = 0;
  bool m_clearDepth[2] = {false, false};  // cleared, but not yet in the texture
  int m_depthIndex = 0;
  bool m_trans1Drew = false;
  MTLViewport m_viewport{0, 0, 1, 1, 0, 1};

  R3DUniforms m_u;
  int m_transX = 0, m_transY = 0, m_transPage = 0;
  bool m_dirtyMesh = true, m_noLosReturn = false, m_layered = false;
  bool m_stencilLayered = false;
  uint32_t m_stencilRef = 0;

  id<MTLBuffer> m_losResults;
  int m_losCount = 0;

  // stereo (BeginEye): the eye's buffers, swapped with the normal ones while it draws
  id<MTLTexture> m_eyeLayers[3];
  id<MTLTexture> m_eyeDepth[2];
  id<MTLTexture> m_eyeTarget;
  id<MTLBuffer> m_eyePixels;
  int m_eyeWidth = 0, m_eyeHeight = 0;
  int m_savedSize[2] = {0, 0};
  MTLScissorRect m_savedScissor{};
};

}  // namespace

std::unique_ptr<IRender2D> CreateRender2D(std::shared_ptr<Context> ctx)
{
  return std::make_unique<Render2DMetal>(std::move(ctx));
}

std::unique_ptr<New3D::IR3DGpu> CreateR3DGpu(std::shared_ptr<Context> ctx, int maxVerts)
{
  return std::make_unique<R3DGpuMetal>(std::move(ctx), maxVerts);
}

}  // namespace attract::metal
