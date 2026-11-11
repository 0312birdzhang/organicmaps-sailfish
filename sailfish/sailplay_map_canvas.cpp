#include "sailfish/sailplay_map_canvas.hpp"

#include "sailfish/map_item.hpp"
#include "sailfish/phone_presentation.hpp"
#include "sailfish/place_page.hpp"
#include "sailfish/routing.hpp"

#include <QBuffer>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QQuickWindow>

#include <stdexcept>

namespace sailfish
{
SailplayMapCanvas::SailplayMapCanvas(MapItem & map, PhonePresentation & phone, QObject * parent)
  : QObject(parent)
  , m_map(map)
  , m_phone(phone)
{
  m_expiry.setInterval(1000);
  connect(&m_expiry, &QTimer::timeout, this, [this]
  {
    if (m_lastRequest.isValid() && m_lastRequest.elapsed() > 2500)
      Release();
  });
  connect(&m_phone, &PhonePresentation::changed, this, [this]
  {
    if (m_touching)
      m_map.CarTouch(QStringLiteral("cancel"), 0, 0);
    m_touching = false;
    m_image.clear();
    if (m_lastRequest.isValid() && m_lastRequest.elapsed() <= 2500)
      Attach();
  });
}

SailplayMapCanvas::~SailplayMapCanvas()
{
  Release();
}

void SailplayMapCanvas::Attach()
{
  m_map.SetCarViewActive(true);
  if (m_phone.isOpen())
  {
    m_window.reset();
    return;
  }
  if (!m_window)
  {
    m_window = std::make_unique<QQuickWindow>();
    m_window->setSurfaceType(QSurface::OpenGLSurface);
  }
  m_window->setGeometry(0, 0, m_size.width(), m_size.height());
  m_map.setViewportBottomInset(0);
  m_map.setLeftInset(0);
  m_map.setRightInset(0);
  m_map.setBottomWidgetsOffset(0);
  m_map.setTopWidgetsOffset(0);
  if (QSizeF(m_map.width(), m_map.height()) != QSizeF(m_size))
    m_map.setSize(m_size);
  if (m_map.parentItem() != m_window->contentItem())
    m_map.setParentItem(m_window->contentItem());
}

void SailplayMapCanvas::Release()
{
  if (m_touching)
    m_map.CarTouch(QStringLiteral("cancel"), 0, 0);
  m_touching = false;
  m_map.SetCarViewActive(false);
  if (m_window && m_map.window() == m_window.get())
    m_map.setParentItem(nullptr);
  m_window.reset();
  m_context.reset();
  m_surface.reset();
  m_image.clear();
  m_imageSize = {};
  m_lastRequest.invalidate();
  m_expiry.stop();
}

QVariantMap SailplayMapCanvas::Frame(QSize const & size)
{
  if (size != m_size)
    m_image.clear();
  m_size = size;
  m_lastRequest.restart();
  m_expiry.start();
  Attach();
  if (!m_context)
  {
    auto * shared = QOpenGLContext::globalShareContext();
    if (!shared)
      throw std::runtime_error("Map OpenGL sharing is unavailable");
    auto surface = std::make_unique<QOffscreenSurface>(shared->screen());
    surface->setFormat(shared->format());
    surface->create();
    auto context = std::make_unique<QOpenGLContext>();
    context->setFormat(shared->format());
    context->setShareContext(shared);
    if (!context->create() || !surface->isValid())
      throw std::runtime_error("Map readback context creation failed");
    m_surface = std::move(surface);
    m_context = std::move(context);
  }
  uint64_t const number = m_map.CarFrameNumber();
  if (number != m_frame || m_image.isEmpty())
  {
    if (!m_context->makeCurrent(m_surface.get()))
      throw std::runtime_error("Map readback context binding failed");
    QImage image = m_map.CaptureCarFrame(*m_context->functions());
    m_context->doneCurrent();
    if (!image.isNull())
    {
      if (image.width() > 1920 || image.height() > 1920)
        image = image.scaled(1920, 1920, Qt::KeepAspectRatio, Qt::SmoothTransformation);
      QByteArray encoded;
      QBuffer buffer(&encoded);
      buffer.open(QIODevice::WriteOnly);
      if (!image.save(&buffer, "JPEG", 85) || encoded.size() > 4 * 1024 * 1024)
        throw std::runtime_error("Map image encoding failed");
      m_frame = number;
      m_image = encoded;
      m_imageSize = image.size();
    }
  }
  auto const * routing = m_map.routing();
  QVariantMap const state = {{"place", m_map.placePage()->open() ? m_map.placePage()->title().left(1024) : QString()},
                             {"building", routing->building()},
                             {"built", routing->built()},
                             {"canStart", routing->canStart()},
                             {"disclaimerAccepted", routing->disclaimerAccepted()},
                             {"summary", routing->summary().left(1024)},
                             {"error", routing->errorTitle().left(1024)}};
  return {{"image", m_image}, {"width", m_imageSize.width()}, {"height", m_imageSize.height()}, {"state", state}};
}

void SailplayMapCanvas::Touch(QString const & phase, double x, double y)
{
  if (!m_lastRequest.isValid())
    return;
  if (phase == QStringLiteral("press"))
    m_touching = true;
  if (m_touching)
    m_map.CarTouch(phase, x, y);
  if (phase == QStringLiteral("release") || phase == QStringLiteral("cancel"))
    m_touching = false;
}
}  // namespace sailfish
