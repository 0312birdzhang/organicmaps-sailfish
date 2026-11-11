#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QSize>
#include <QTimer>
#include <QVariantMap>

#include <memory>

class QOffscreenSurface;
class QOpenGLContext;
class QQuickWindow;

namespace sailfish
{
class MapItem;
class PhonePresentation;

class SailplayMapCanvas : public QObject
{
public:
  SailplayMapCanvas(MapItem & map, PhonePresentation & phone, QObject * parent = nullptr);
  ~SailplayMapCanvas() override;
  QVariantMap Frame(QSize const & size);
  void Touch(QString const & phase, double x, double y);

private:
  void Attach();
  void Release();
  MapItem & m_map;
  PhonePresentation & m_phone;
  QTimer m_expiry;
  QElapsedTimer m_lastRequest;
  QSize m_size;
  std::unique_ptr<QQuickWindow> m_window;
  std::unique_ptr<QOffscreenSurface> m_surface;
  std::unique_ptr<QOpenGLContext> m_context;
  uint64_t m_frame = 0;
  QByteArray m_image;
  QSize m_imageSize;
  bool m_touching = false;
};
}  // namespace sailfish
