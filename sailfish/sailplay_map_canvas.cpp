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
    m_sourceViewport = {};
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
  m_sourceViewport = {};
  m_lastRequest.invalidate();
  m_expiry.stop();
}

QVariantMap SailplayMapCanvas::Frame(QSize const & size)
{
  if (size != m_size)
  {
    if (m_touching)
      m_map.CarTouch(QStringLiteral("cancel"), 0, 0);
    m_touching = false;
    m_image.clear();
    m_sourceViewport = {};
  }
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
      // The shared phone view can have a different aspect ratio. Keep the car
      // viewport and its touch mapping together while the phone stays open.
      qreal const scale = qMax(qreal(m_size.width()) / image.width(), qreal(m_size.height()) / image.height());
      QSize const cropSize(qBound(1, qRound(m_size.width() / scale), image.width()),
                           qBound(1, qRound(m_size.height() / scale), image.height()));
      QRect const crop((image.width() - cropSize.width()) / 2, (image.height() - cropSize.height()) / 2,
                       cropSize.width(), cropSize.height());
      QRectF const viewport(qreal(crop.x()) / image.width(), qreal(crop.y()) / image.height(),
                            qreal(crop.width()) / image.width(), qreal(crop.height()) / image.height());
      image = image.copy(crop).scaled(m_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
      QByteArray encoded;
      QBuffer buffer(&encoded);
      buffer.open(QIODevice::WriteOnly);
      if (!image.save(&buffer, "JPEG", 85) || encoded.size() > 4 * 1024 * 1024)
        throw std::runtime_error("Map image encoding failed");
      m_frame = number;
      m_image = encoded;
      m_imageSize = image.size();
      m_sourceViewport = viewport;
    }
  }
  auto const * routing = m_map.routing();
  QVariantMap const state = {{"place", m_map.placePage()->open() ? m_map.placePage()->title().left(1024) : QString()},
                             {"bookmarked", m_map.placePage()->open() && m_map.placePage()->isBookmark()},
                             {"building", routing->building()},
                             {"built", routing->built()},
                             {"canStart", routing->canStart()},
                             {"disclaimerAccepted", routing->disclaimerAccepted()},
                             {"summary", routing->summary().left(1024)},
                             {"waitingForPosition", routing->waitingForPosition()},
                             {"error", routing->errorTitle().left(1024)}};
  return {{"image", m_image}, {"width", m_imageSize.width()}, {"height", m_imageSize.height()}, {"state", state}};
}

void SailplayMapCanvas::Touch(QString const & phase, double x, double y)
{
  if (!m_lastRequest.isValid() || m_sourceViewport.isEmpty())
    return;
  if (phase == QStringLiteral("press"))
    m_touching = true;
  if (m_touching)
    m_map.CarTouch(phase, m_sourceViewport.x() + x * m_sourceViewport.width(),
                   m_sourceViewport.y() + y * m_sourceViewport.height());
  if (phase == QStringLiteral("release") || phase == QStringLiteral("cancel"))
    m_touching = false;
}
}  // namespace sailfish
