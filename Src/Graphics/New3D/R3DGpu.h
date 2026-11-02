#ifndef _R3DGPU_H_
#define _R3DGPU_H_

// The GPU side of CNew3D: everything it draws goes through this interface,
// so the scene walk in New3D.cpp is shared by the OpenGL renderer
// (R3DGpuGL) and the Arcade Foundry cores' Metal renderer.
//
// A frame is, in order:
//   UploadRamVerts / AppendRomVerts (or ResetRom)
//   BeginFrame, DrawFog (0-2 times)
//   for each priority: BeginPriority, then for colour, trans1, trans2:
//     BeginLayer, BeginScene, (SetViewport, (SetModel, DrawMesh...)...)
//     and ReadLos after trans2; EndPriority
//   Composite
// WhiteFlash replaces all of it when the 3D is blanked.
//
// Stereo (Arcade Foundry, CNew3D::RenderEye): BeginEye, then the frame's
// calls again from BeginFrame to Composite (no ReadLos), then EndEye.

#include <functional>
#include "Types.h"
#include "Model.h"

#define MAX_RAM_VERTS 300000
#define MAX_ROM_VERTS 1500000

namespace New3D {

class IR3DGpu
{
public:
	virtual ~IR3DGpu() {}

	virtual Result	Init				(int totalXRes, int totalYRes, unsigned aaTarget) = 0;

	// texture sheet: two 2048x1024 pages, 16-bit texels, with mipmaps
	virtual void	AttachTextureRAM	(const UINT16* textureRAM) = 0;
	virtual int		TextureLevels		() const = 0;
	virtual void	UploadTextures		(int page, int level, int x, int y, int width, int height) = 0;

	// vertex memory: the ROM models from the start, the RAM ones at ramBase
	virtual void	UploadRamVerts		(int ramBase, const FVertex* verts, size_t count) = 0;
	virtual int		RomVertsUploaded	() const = 0;
	virtual void	AppendRomVerts		(const FVertex* verts, size_t count) = 0;
	virtual void	ResetRom			() = 0;

	virtual void	WhiteFlash			() = 0;
	virtual void	BeginFrame			() = 0;
	virtual void	DrawFog				(const Viewport& vp, const float rgba[4], float attenuation, float ambient, const float spotRGB[3], const float spotEllipse[4]) = 0;

	virtual void	BeginPriority		() = 0;
	virtual void	BeginLayer			(Layer layer) = 0;
	virtual void	BeginScene			() = 0;
	virtual void	SetViewport			(const Viewport& vp) = 0;
	virtual void	SetModel			(const Model& model) = 0;
	virtual void	DrawMesh			(const Mesh& mesh) = 0;
	// depth (as stored) and stencil at a pixel; done may run later, but
	// before the next frame starts
	virtual void	ReadLos				(int x, int y, std::function<void(float depth, UINT8 stencil)> done) = 0;
	virtual void	EndPriority			() = 0;

	virtual void	Composite			() = 0;

	// Stereo: until EndEye, everything draws into the eye's own target,
	// width x height, with the scissor box `inset` pixels in from each edge
	// and `background` (RGBA bytes, bottom row first) already in it. False:
	// this renderer has no eyes.
	virtual bool	BeginEye			(int width, int height, int inset, const UINT8* background) { return false; }
	// The eye's picture, RGBA bytes, bottom row first; then back to the
	// normal target.
	virtual void	EndEye				(UINT8* rgba) {}
};

} // New3D

#endif
