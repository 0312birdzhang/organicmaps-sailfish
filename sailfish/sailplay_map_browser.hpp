#pragma once

#include "sailfish/bookmarks_model.hpp"
#include "sailfish/search_model.hpp"

#include <QVariantMap>

namespace sailfish
{
class MapItem;

class SailplayMapBrowser : public QObject
{
public:
  explicit SailplayMapBrowser(MapItem & map);
  QVariantMap Snapshot() const;
  void Invoke(QVariantMap const & parameters);

private:
  void Select(QString const & id);
  void SetPage(QString const & page);
  QVariantList Rows() const;

  MapItem & m_map;
  SearchModel m_search;
  BookmarkCategoriesModel m_groups;
  BookmarksModel m_bookmarks;
  QString m_page;
  uint64_t m_revision = 1;
  mutable uint64_t m_cachedRevision = 0;
  mutable QVariantList m_cachedRows;
  int m_offset = 0;
};
}  // namespace sailfish
