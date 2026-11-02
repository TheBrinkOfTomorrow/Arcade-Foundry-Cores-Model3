/*
 * Arcade Foundry core: the core's own OpenGL context. Supermodel's renderers
 * need a current desktop GL 4.1 core context; the front end never sees it.
 * The core draws into an FBO and hands the finished screen over as pixels.
 */
#ifndef ATTRACT_GL_CONTEXT_H
#define ATTRACT_GL_CONTEXT_H

#include <string>

namespace attract {

class GLContext
{
public:
  // Creates the context and makes it current on this thread. On failure
  // returns false and says why in *err.
  bool Create(std::string *err);
  void MakeCurrent();
  // No context current on this thread (EGL; nothing on macOS).
  void Release();
  void Destroy();
  ~GLContext() { Destroy(); }

private:
  void *m_display = nullptr;  // EGLDisplay (EGL backend)
  void *m_surface = nullptr;  // EGLSurface (EGL backend)
  void *m_context = nullptr;  // EGLContext or CGLContextObj
};

// The core's context current for one call into the core and released after
// it, so the front end's own GL (SDL's renderer, on Linux) finds the thread
// free: with GLVND both share one thread's current context.
class GLCurrent
{
public:
  explicit GLCurrent(GLContext &gl) : m_gl(gl) { m_gl.MakeCurrent(); }
  ~GLCurrent() { m_gl.Release(); }

private:
  GLContext &m_gl;
};

}

#endif
