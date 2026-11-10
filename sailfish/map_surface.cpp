#include "sailfish/map_surface.hpp"

namespace sailfish
{
MapSurface::~MapSurface()
{
  if (m_map && m_map->parentItem() == this)
    m_map->setParentItem(nullptr);
}

void MapSurface::setMap(QQuickItem * map)
{
  if (map == m_map)
    return;
  if (m_map && m_map->parentItem() == this)
    m_map->setParentItem(nullptr);
  m_map = map;
  if (map)
  {
    map->setParentItem(this);
    map->setSize(QSizeF(width(), height()));
  }
  emit mapChanged();
}

void MapSurface::geometryChanged(QRectF const & newGeometry, QRectF const & oldGeometry)
{
  QQuickItem::geometryChanged(newGeometry, oldGeometry);
  if (m_map && m_map->parentItem() == this)
    m_map->setSize(newGeometry.size());
}
}  // namespace sailfish
