// The Metal renderer for Apple builds (M3CORE_METAL): Supermodel's New3D
// draws through R3DGpuMetal (an IR3DGpu), the tile generator's layers
// through Render2DMetal, both into one target texture the size of the
// screen. Plain C++ interface; the Objective-C++ is in metal_renderer.mm.
//
// The pixels land as OpenGL's would: row 0 is the bottom of the picture,
// so the core's readback code is the same for both.

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "Graphics/IRender2D.h"
#include "Graphics/New3D/R3DGpu.h"

namespace attract::metal {

class Context;

// The device, the command queue, the shaders and the target. nullptr and
// *why on failure.
std::shared_ptr<Context> CreateContext(int width, int height, std::string *why);

// Finishes the frame: submits it, waits for it and copies the target into
// rgba (width * height * 4 bytes, bottom row first). Once the front end has
// asked for surfaces (Surface()), the frame goes to one of them on the GPU
// instead and rgba is left alone: returns false then.
bool EndFrame(Context &ctx, uint8_t *rgba);

// afcore 5.1's screen_surface: the newest finished frame as an
// IOSurface-backed CVPixelBufferRef (32BGRA, top row first), or nullptr
// (none finished yet, or no surfaces on this GPU: use the copied screen).
// The first call turns surfaces on; from the next frame EndFrame stops
// copying to the CPU. The last three handed out are not drawn into again.
void *Surface(Context &ctx, int *width, int *height);

// The newest finished surface copied into argb (width * height words,
// 0xAARRGGBB, top row first) for the core's screen(); false when there is none.
bool CopySurface(Context &ctx, uint32_t *argb);

// ATTRACT_M3_TIMING=1: brackets the core's RunFrame, so the timing line
// shows the core's own time apart from the app's pacing.
void FrameStart(Context &ctx);
void FrameDone(Context &ctx);

std::unique_ptr<IRender2D> CreateRender2D(std::shared_ptr<Context> ctx);
std::unique_ptr<New3D::IR3DGpu> CreateR3DGpu(std::shared_ptr<Context> ctx, int maxVerts);

}  // namespace attract::metal
