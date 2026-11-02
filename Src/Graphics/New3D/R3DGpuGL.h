#ifndef _R3DGPUGL_H_
#define _R3DGPUGL_H_

// CNew3D's OpenGL renderer: the GL calls New3D.cpp made itself, moved here
// unchanged and in the same order.

#include <GL/glew.h>
#include <memory>
#include "R3DGpu.h"
#include "R3DShader.h"
#include "VBO.h"
#include "R3DScrollFog.h"
#include "R3DFrameBuffers.h"
#include "TextureBank.h"

namespace New3D {

class R3DGpuGL final : public IR3DGpu
{
public:
	R3DGpuGL(const Util::Config::Node& config, int maxVerts);
	~R3DGpuGL();

	Result	Init				(int totalXRes, int totalYRes, unsigned aaTarget) override;

	void	AttachTextureRAM	(const UINT16* textureRAM) override;
	int		TextureLevels		() const override;
	void	UploadTextures		(int page, int level, int x, int y, int width, int height) override;

	void	UploadRamVerts		(int ramBase, const FVertex* verts, size_t count) override;
	int		RomVertsUploaded	() const override;
	void	AppendRomVerts		(const FVertex* verts, size_t count) override;
	void	ResetRom			() override;

	void	WhiteFlash			() override;
	void	BeginFrame			() override;
	void	DrawFog				(const Viewport& vp, const float rgba[4], float attenuation, float ambient, const float spotRGB[3], const float spotEllipse[4]) override;

	void	BeginPriority		() override;
	void	BeginLayer			(Layer layer) override;
	void	BeginScene			() override;
	void	SetViewport			(const Viewport& vp) override;
	void	SetModel			(const Model& model) override;
	void	DrawMesh			(const Mesh& mesh) override;
	void	ReadLos				(int x, int y, std::function<void(float depth, UINT8 stencil)> done) override;
	void	EndPriority			() override;

	void	Composite			() override;

	bool	BeginEye			(int width, int height, int inset, const UINT8* background) override;
	void	EndEye				(UINT8* rgba) override;

private:
	GLenum m_primType;
	TextureBank m_textureBank[2];
	GLuint m_vao;
	VBO m_vbo;								// large VBO to hold our poly data, start of VBO is ROM data, ram polys follow
	R3DShader m_r3dShader;
	R3DScrollFog m_r3dScrollFog;
	R3DFrameBuffers m_r3dFrameBuffers;
	GLuint m_aaTarget;						// optional, maybe zero

	// stereo: an eye draws into its own buffers and target, then these go back
	R3DFrameBuffers* m_frameBuffers;		// m_r3dFrameBuffers, or m_eyeFrameBuffers while an eye draws
	std::unique_ptr<R3DFrameBuffers> m_eyeFrameBuffers;
	GLuint m_eyeTarget = 0, m_eyeTexture = 0;
	int m_eyeWidth = 0, m_eyeHeight = 0;
	GLuint m_savedTarget = 0;
	GLint m_savedScissor[4] = { 0, 0, 0, 0 };
};

} // New3D

#endif
