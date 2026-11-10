#pragma once

#include <QPointer>
#include <QQuickItem>

namespace sailfish
{
// A phone-owned viewport for the application-owned map item. Visual parenting leaves the backend's QObject
// ownership intact, so destroying the phone page releases only its presentation.
class MapSurface : public QQuickItem
{
  Q_OBJECT
  Q_PROPERTY(QQuickItem * map READ map WRITE setMap NOTIFY mapChanged)

public:
  explicit MapSurface(QQuickItem * parent = nullptr) : QQuickItem(parent) {}
  ~MapSurface() override;
  QQuickItem * map() const { return m_map; }
  void setMap(QQuickItem * map);

signals:
  void mapChanged();

protected:
  void geometryChanged(QRectF const & newGeometry, QRectF const & oldGeometry) override;

private:
  QPointer<QQuickItem> m_map;
};
}  // namespace sailfish
