/*
 * Arcade Foundry core: the core's OpenGL context.
 *
 * macOS: a CGL context with no drawable (any thread, no window, no Cocoa).
 * Elsewhere: an EGL context on a small pbuffer (no window, no SDL).
 */
#include "gl_context.h"

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#else
#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif

namespace attract {

#if defined(__APPLE__)

bool GLContext::Create(std::string *err)
{
  CGLPixelFormatAttribute attrs[] = {
    kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute) kCGLOGLPVersion_GL4_Core,
    kCGLPFAColorSize, (CGLPixelFormatAttribute) 24,
    kCGLPFAAlphaSize, (CGLPixelFormatAttribute) 8,
    kCGLPFADepthSize, (CGLPixelFormatAttribute) 24,
    kCGLPFAStencilSize, (CGLPixelFormatAttribute) 8,
    kCGLPFAAccelerated,
    kCGLPFAAllowOfflineRenderers,
    (CGLPixelFormatAttribute) 0
  };
  CGLPixelFormatObj pix = nullptr;
  GLint npix = 0;
  if (CGLChoosePixelFormat(attrs, &pix, &npix) != kCGLNoError || !pix)
  {
    *err = "no OpenGL 4.1 pixel format";
    return false;
  }
  CGLContextObj ctx = nullptr;
  CGLError e = CGLCreateContext(pix, nullptr, &ctx);
  CGLReleasePixelFormat(pix);
  if (e != kCGLNoError || !ctx)
  {
    *err = std::string("CGLCreateContext: ") + CGLErrorString(e);
    return false;
  }
  m_context = ctx;
  MakeCurrent();
  return true;
}

void GLContext::MakeCurrent()
{
  if (m_context)
    CGLSetCurrentContext((CGLContextObj) m_context);
}

void GLContext::Release()
{
}

void GLContext::Destroy()
{
  if (m_context)
  {
    if (CGLGetCurrentContext() == (CGLContextObj) m_context)
      CGLSetCurrentContext(nullptr);
    CGLDestroyContext((CGLContextObj) m_context);
    m_context = nullptr;
  }
}

#else

// EGL, not a hidden SDL window: SDL2's video in the same process as the
// Linux app's SDL3 left the app's window blank (sound, no picture), whatever
// SDL3 drew with. A 16 x 16 pbuffer is all the context needs, as the core
// draws into its FBO.
bool GLContext::Create(std::string *err)
{
  EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  EGLint major = 0, minor = 0;
  if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor))
  {
    *err = "EGL: no display";
    return false;
  }
  if (!eglBindAPI(EGL_OPENGL_API))
  {
    *err = "EGL: no desktop OpenGL";
    eglTerminate(display);
    return false;
  }
  const EGLint configAttribs[] = {
    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
    EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
    EGL_NONE,
  };
  EGLConfig config = nullptr;
  EGLint configs = 0;
  if (!eglChooseConfig(display, configAttribs, &config, 1, &configs) || configs < 1)
  {
    *err = "EGL: no RGBA8 config with depth and stencil";
    eglTerminate(display);
    return false;
  }
  const EGLint surfaceAttribs[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
  EGLSurface surface = eglCreatePbufferSurface(display, config, surfaceAttribs);
  const EGLint contextAttribs[] = {
    EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 1,
    EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
    EGL_NONE,
  };
  EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
  if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT)
  {
    *err = "EGL: no OpenGL 4.1 context";
    if (context != EGL_NO_CONTEXT)
      eglDestroyContext(display, context);
    if (surface != EGL_NO_SURFACE)
      eglDestroySurface(display, surface);
    eglTerminate(display);
    return false;
  }
  m_display = display;
  m_surface = surface;
  m_context = context;
  MakeCurrent();
  return true;
}

void GLContext::MakeCurrent()
{
  if (m_context)
    eglMakeCurrent((EGLDisplay) m_display, (EGLSurface) m_surface, (EGLSurface) m_surface, (EGLContext) m_context);
}

void GLContext::Release()
{
  if (m_context)
    eglMakeCurrent((EGLDisplay) m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

void GLContext::Destroy()
{
  if (!m_display)
    return;
  EGLDisplay display = (EGLDisplay) m_display;
  if (eglGetCurrentContext() == (EGLContext) m_context)
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  if (m_context)
    eglDestroyContext(display, (EGLContext) m_context);
  if (m_surface)
    eglDestroySurface(display, (EGLSurface) m_surface);
  // The display is left initialised: EGL's default display is one for the
  // whole process, and the front end may open the next game's core on it.
  m_context = m_surface = m_display = nullptr;
}

#endif

}
