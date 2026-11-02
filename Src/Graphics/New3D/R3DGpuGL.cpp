#include "R3DGpuGL.h"
#include <cstddef>

namespace New3D {

R3DGpuGL::R3DGpuGL(const Util::Config::Node& config, int maxVerts) :
	m_vao(0),
	m_r3dShader(config),
	m_r3dScrollFog(),
	m_aaTarget(0),
	m_frameBuffers(&m_r3dFrameBuffers)
{
	m_primType = config["QuadRendering"].ValueAs<bool>() ? GL_LINES_ADJACENCY : GL_TRIANGLES;

	m_r3dShader.LoadShader();
	glUseProgram(0);

	// setup up our vertex buffer memory

	glGenVertexArrays(1, &m_vao);
	glBindVertexArray(m_vao);
	m_vbo.Create(GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW, sizeof(FVertex) * maxVerts);
	m_vbo.Bind(true);

	glEnableVertexAttribArray(m_r3dShader.GetVertexAttribPos("inVertex"));
	glEnableVertexAttribArray(m_r3dShader.GetVertexAttribPos("inNormal"));
	glEnableVertexAttribArray(m_r3dShader.GetVertexAttribPos("inTexCoord"));
	glEnableVertexAttribArray(m_r3dShader.GetVertexAttribPos("inColour"));
	glEnableVertexAttribArray(m_r3dShader.GetVertexAttribPos("inFaceNormal"));
	glEnableVertexAttribArray(m_r3dShader.GetVertexAttribPos("inFixedShade"));
	glEnableVertexAttribArray(m_r3dShader.GetVertexAttribPos("inTextureNP"));

	// before draw, specify vertex and index arrays with their offsets, offsetof is maybe evil ..
	glVertexAttribPointer(m_r3dShader.GetVertexAttribPos("inVertex"), 4, GL_FLOAT, GL_FALSE, sizeof(FVertex), 0);
	glVertexAttribPointer(m_r3dShader.GetVertexAttribPos("inNormal"), 3, GL_FLOAT, GL_FALSE, sizeof(FVertex), (void*)offsetof(FVertex, base.normal));
	glVertexAttribPointer(m_r3dShader.GetVertexAttribPos("inTexCoord"), 2, GL_FLOAT, GL_FALSE, sizeof(FVertex), (void*)offsetof(FVertex, base.texcoords));
	glVertexAttribPointer(m_r3dShader.GetVertexAttribPos("inColour"), 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(FVertex), (void*)offsetof(FVertex, faceColour));
	glVertexAttribPointer(m_r3dShader.GetVertexAttribPos("inFaceNormal"), 3, GL_FLOAT, GL_FALSE, sizeof(FVertex), (void*)offsetof(FVertex, faceNormal));
	glVertexAttribPointer(m_r3dShader.GetVertexAttribPos("inFixedShade"), 1, GL_FLOAT, GL_FALSE, sizeof(FVertex), (void*)offsetof(FVertex, base.fixedShade));
	glVertexAttribPointer(m_r3dShader.GetVertexAttribPos("inTextureNP"), 1, GL_FLOAT, GL_FALSE, sizeof(FVertex), (void*)offsetof(FVertex, textureNP));

	glBindVertexArray(0);
	m_vbo.Bind(false);
}

R3DGpuGL::~R3DGpuGL()
{
	m_vbo.Destroy();
	if (m_vao) {
		glDeleteVertexArrays(1, &m_vao);
		m_vao = 0;
	}

	m_r3dShader.UnloadShader();

	m_eyeFrameBuffers.reset();
	if (m_eyeTarget) {
		glDeleteFramebuffers(1, &m_eyeTarget);
	}
	if (m_eyeTexture) {
		glDeleteTextures(1, &m_eyeTexture);
	}
}

Result R3DGpuGL::Init(int totalXRes, int totalYRes, unsigned aaTarget)
{
	m_aaTarget = aaTarget;

	m_r3dFrameBuffers.DestroyFBO();		// remove any old ones if created

	return m_r3dFrameBuffers.CreateFBO(totalXRes, totalYRes);
}

void R3DGpuGL::AttachTextureRAM(const UINT16* textureRAM)
{
	m_textureBank[0].AttachMemory(textureRAM);
	m_textureBank[1].AttachMemory(textureRAM + (2048*1024));
}

int R3DGpuGL::TextureLevels() const
{
	return m_textureBank[0].GetNumberOfLevels();
}

void R3DGpuGL::UploadTextures(int page, int level, int x, int y, int width, int height)
{
	m_textureBank[page].UploadTextures(level, x, y, width, height);
}

void R3DGpuGL::UploadRamVerts(int ramBase, const FVertex* verts, size_t count)
{
	m_vbo.Bind(true);
	m_vbo.BufferSubData(ramBase*sizeof(FVertex), count*sizeof(FVertex), verts);	// upload all the dynamic data to GPU in one go
}

int R3DGpuGL::RomVertsUploaded() const
{
	return m_vbo.GetSize() / (int)sizeof(FVertex);
}

void R3DGpuGL::AppendRomVerts(const FVertex* verts, size_t count)
{
	m_vbo.AppendData(count*sizeof(FVertex), verts);
}

void R3DGpuGL::ResetRom()
{
	m_vbo.Reset();
}

void R3DGpuGL::WhiteFlash()
{
	if (m_aaTarget) {
		glBindFramebuffer(GL_FRAMEBUFFER, m_aaTarget);		// if we have an AA target draw to it instead of the default back buffer
	}

	// clear screen to white
	glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	if (m_aaTarget) {
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
}

void R3DGpuGL::BeginFrame()
{
	m_frameBuffers->SetFBO(Layer::colour);		// colour will draw to all 3 buffers. For regular opaque pixels the transparent layers will be essentially masked
	glClear(GL_COLOR_BUFFER_BIT);
}

void R3DGpuGL::DrawFog(const Viewport& vp, const float rgba[4], float attenuation, float ambient, const float spotRGB[3], const float spotEllipse[4])
{
	glViewport(vp.x, vp.y, vp.width, vp.height);
	m_r3dScrollFog.DrawScrollFog(rgba, attenuation, ambient, spotRGB, spotEllipse);
}

void R3DGpuGL::BeginPriority()
{
	// SetRenderStates()
	m_vbo.Bind(true);
	glBindVertexArray(m_vao);

	m_r3dShader.SetShader(true);

	glDepthFunc		(GL_GEQUAL);
	glEnable		(GL_DEPTH_TEST);
	glDepthMask		(GL_TRUE);
	glActiveTexture	(GL_TEXTURE0);
	glDisable		(GL_CULL_FACE);					// we'll emulate this in the shader

	glEnable		(GL_STENCIL_TEST);
	glStencilMask	(0xFF);

	glBlendFunc		(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable		(GL_BLEND);

	m_frameBuffers->SetFBO(Layer::colour);

	glClearDepth(0.0);
	glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void R3DGpuGL::BeginLayer(Layer layer)
{
	switch (layer)
	{
	case Layer::colour:
		m_r3dShader.DiscardAlpha(true);
		m_r3dShader.SetLayer(Layer::colour);
		break;
	case Layer::trans1:
		glDepthFunc(GL_GREATER);

		m_r3dShader.DiscardAlpha(false);

		m_frameBuffers->StoreDepth();
		m_r3dShader.SetLayer(Layer::trans1);
		m_frameBuffers->SetFBO(Layer::trans1);
		break;
	case Layer::trans2:
		m_frameBuffers->RestoreDepth();
		m_r3dShader.SetLayer(Layer::trans2);
		m_frameBuffers->SetFBO(Layer::trans2);
		break;
	default:
		break;
	}
}

void R3DGpuGL::BeginScene()
{
	glActiveTexture(GL_TEXTURE0);
	m_textureBank[0].Bind();
	glActiveTexture(GL_TEXTURE1);
	m_textureBank[1].Bind();
	glActiveTexture(GL_TEXTURE0);
}

void R3DGpuGL::SetViewport(const Viewport& vp)
{
	glViewport(vp.x, vp.y, vp.width, vp.height);

	m_r3dShader.SetViewportUniforms(&vp);
}

void R3DGpuGL::SetModel(const Model& model)
{
	m_r3dShader.SetModelStates(&model);
}

void R3DGpuGL::DrawMesh(const Mesh& mesh)
{
	m_r3dShader.SetMeshUniforms(&mesh);
	glDrawArrays(m_primType, mesh.vboOffset, mesh.vertexCount);
}

void R3DGpuGL::ReadLos(int x, int y, std::function<void(float depth, UINT8 stencil)> done)
{
	float range[2];
	glReadPixels(x, y, 1, 1, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, range);
	UINT32 stencil;
	memcpy(&stencil, &range[1], sizeof(stencil));
	done(range[0], (UINT8)stencil);
}

void R3DGpuGL::EndPriority()
{
	// DisableRenderStates()
	m_vbo.Bind(false);
	glBindVertexArray(0);

	m_r3dShader.SetShader(false);

	glDisable(GL_STENCIL_TEST);
}

void R3DGpuGL::Composite()
{
	m_frameBuffers->SetFBO(Layer::none);

	if (m_aaTarget) {
		glBindFramebuffer(GL_FRAMEBUFFER, m_aaTarget);			// if we have an AA target draw to it instead of the default back buffer
	}

	m_frameBuffers->Draw();

	if (m_aaTarget) {
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
}

bool R3DGpuGL::BeginEye(int width, int height, int inset, const UINT8* background)
{
	if (width != m_eyeWidth || height != m_eyeHeight) {
		m_eyeFrameBuffers.reset();
		if (!m_eyeTexture) {
			glGenTextures(1, &m_eyeTexture);
			glGenFramebuffers(1, &m_eyeTarget);
		}
		glBindTexture(GL_TEXTURE_2D, m_eyeTexture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glBindFramebuffer(GL_FRAMEBUFFER, m_eyeTarget);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_eyeTexture, 0);
		const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		auto fb = std::make_unique<R3DFrameBuffers>();
		if (!complete || fb->CreateFBO(width, height) != Result::OKAY) {
			m_eyeWidth = m_eyeHeight = 0;
			return false;
		}
		m_eyeFrameBuffers = std::move(fb);
		m_eyeWidth = width;
		m_eyeHeight = height;
	}

	glBindTexture(GL_TEXTURE_2D, m_eyeTexture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, background);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGetIntegerv(GL_SCISSOR_BOX, m_savedScissor);
	glScissor(inset, inset, width - inset * 2, height - inset * 2);
	m_savedTarget = m_aaTarget;
	m_aaTarget = m_eyeTarget;
	m_frameBuffers = m_eyeFrameBuffers.get();
	return true;
}

void R3DGpuGL::EndEye(UINT8* rgba)
{
	glBindFramebuffer(GL_READ_FRAMEBUFFER, m_eyeTarget);
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, m_eyeWidth, m_eyeHeight, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

	m_frameBuffers = &m_r3dFrameBuffers;
	m_aaTarget = m_savedTarget;
	glScissor(m_savedScissor[0], m_savedScissor[1], m_savedScissor[2], m_savedScissor[3]);
}

} // New3D
