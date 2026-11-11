#include "qt/qt_common/qtoglcontext.hpp"

#include "base/assert.hpp"

#include "drape/gl_functions.hpp"

#include <bit>
#include <memory>

namespace qt
{
namespace common
{
QtRenderOGLContext::QtRenderOGLContext(QOpenGLContext * rootContext, QOffscreenSurface * surface, bool exactSize)
  : m_surface(surface)
  , m_ctx(std::make_unique<QOpenGLContext>())
  , m_exactSize(exactSize)
  , m_isContextAvailable(false)
{
  m_ctx->setFormat(rootContext->format());
  m_ctx->setShareContext(rootContext);
  m_ctx->create();
  CHECK(m_ctx->isValid(), ());
}

void QtRenderOGLContext::Present()
{
  GLFunctions::glFinish();

  std::lock_guard<std::mutex> lock(m_frameMutex);
  std::swap(m_frontFrame, m_backFrame);
  m_frameUpdated = true;
  ++m_frameNumber;
  if (m_onPresent)
    m_onPresent();
}

void QtRenderOGLContext::SetPresentListener(std::function<void()> fn)
{
  std::lock_guard<std::mutex> lock(m_frameMutex);
  m_onPresent = std::move(fn);
}

void QtRenderOGLContext::MakeCurrent()
{
  CHECK(m_ctx->makeCurrent(m_surface), ());
  m_isContextAvailable = true;
}

void QtRenderOGLContext::DoneCurrent()
{
  m_isContextAvailable = false;
  m_ctx->doneCurrent();
}

void QtRenderOGLContext::SetFramebuffer(ref_ptr<dp::BaseFramebuffer> framebuffer)
{
  if (framebuffer)
    framebuffer->Bind();
  else if (m_backFrame != nullptr)
    m_backFrame->bind();
}

void QtRenderOGLContext::Resize(uint32_t w, uint32_t h)
{
  CHECK(m_isContextAvailable, ());

  // This function can't be called inside BeginRendering - EndRendering.
  std::lock_guard lock(m_frameMutex);

  auto const nw = static_cast<int>(m_exactSize ? w : std::bit_ceil(w));
  auto const nh = static_cast<int>(m_exactSize ? h : std::bit_ceil(h));

  bool const fits = m_exactSize ? nw == m_width && nh == m_height : nw <= m_width && nh <= m_height;
  if (fits && m_backFrame != nullptr)
  {
    m_frameRect = QRectF(0.0, 0.0, w / static_cast<float>(m_width), h / static_cast<float>(m_height));
    return;
  }

  m_width = nw;
  m_height = nh;
  m_frameRect = QRectF(0.0, 0.0, w / static_cast<float>(m_width), h / static_cast<float>(m_height));

  m_backFrame = std::make_unique<QOpenGLFramebufferObject>(QSize(m_width, m_height), QOpenGLFramebufferObject::Depth);
  m_frontFrame = std::make_unique<QOpenGLFramebufferObject>(QSize(m_width, m_height), QOpenGLFramebufferObject::Depth);
  m_acquiredFrame =
      std::make_unique<QOpenGLFramebufferObject>(QSize(m_width, m_height), QOpenGLFramebufferObject::Depth);
}

bool QtRenderOGLContext::AcquireFrame()
{
  if (!m_isContextAvailable)
    return false;

  std::lock_guard lock(m_frameMutex);
  // Render current acquired frame.
  if (!m_frameUpdated)
    return true;

  // Update acquired frame.
  m_acquiredFrameRect = m_frameRect;
  std::swap(m_acquiredFrame, m_frontFrame);
  m_frameUpdated = false;

  return true;
}

QRectF const & QtRenderOGLContext::GetTexRect() const
{
  return m_acquiredFrameRect;
}

GLuint QtRenderOGLContext::GetTextureHandle() const
{
  if (!m_acquiredFrame)
    return 0;

  CHECK(!m_acquiredFrame->isBound(), ());
  return m_acquiredFrame->texture();
}

QImage QtRenderOGLContext::CaptureFrame(QOpenGLFunctions & functions)
{
  std::lock_guard lock(m_frameMutex);
  auto const & frame = m_frameUpdated ? m_frontFrame : m_acquiredFrame;
  QRectF const & rect = m_frameUpdated ? m_frameRect : m_acquiredFrameRect;
  int const width = qRound(rect.width() * m_width);
  int const height = qRound(rect.height() * m_height);
  if (m_frameNumber == 0 || !frame || width <= 0 || height <= 0 || width > 4096 || height > 4096)
    return {};

  // Framebuffer names belong to a context. Attach the shared texture to a
  // readback-context framebuffer while keeping the renderer's frame stable.
  GLuint framebuffer = 0;
  GLint previous = 0;
  functions.glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
  functions.glGenFramebuffers(1, &framebuffer);
  functions.glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  functions.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, frame->texture(), 0);
  QImage image;
  if (functions.glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
  {
    image = QImage(width, height, QImage::Format_RGBA8888);
    functions.glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
    if (functions.glGetError() != GL_NO_ERROR)
      image = {};
  }
  functions.glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
  functions.glDeleteFramebuffers(1, &framebuffer);
  return image.mirrored();
}

QtUploadOGLContext::QtUploadOGLContext(QOpenGLContext * rootContext, QOffscreenSurface * surface)
  : m_surface(surface)
  , m_ctx(std::make_unique<QOpenGLContext>())
{
  m_ctx->setFormat(rootContext->format());
  m_ctx->setShareContext(rootContext);
  m_ctx->create();
  CHECK(m_ctx->isValid(), ());
}

void QtUploadOGLContext::MakeCurrent()
{
  m_ctx->makeCurrent(m_surface);
}

void QtUploadOGLContext::DoneCurrent()
{
  m_ctx->doneCurrent();
}

void QtUploadOGLContext::Present()
{
  CHECK(false, ());
}

void QtUploadOGLContext::SetFramebuffer(ref_ptr<dp::BaseFramebuffer>)
{
  CHECK(false, ());
}
}  // namespace common
}  // namespace qt
