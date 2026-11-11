#include "sailfish/sailplay_map_browser.hpp"

#include "sailfish/map_item.hpp"
#include "sailfish/place_page.hpp"
#include "sailfish/routing.hpp"

#include <QSet>
#include <QTextDocument>

#include <algorithm>
#include <stdexcept>

namespace sailfish
{
namespace
{
int constexpr kPageSize = 40;

QString Text(QString const & text)
{
  auto points = text.toUcs4();
  points.erase(std::remove(points.begin(), points.end(), 0), points.end());
  return QString::fromUcs4(points.constData(), std::min(512, points.size()));
}

QString PlainText(QVariant const & value)
{
  QTextDocument document;
  document.setHtml(value.toString());
  return Text(document.toPlainText());
}

void Fields(QVariantMap const & parameters, QStringList const & names)
{
  if (QSet<QString>::fromList(parameters.keys()) != QSet<QString>::fromList(names))
    throw std::invalid_argument("Unexpected map browsing parameters");
  for (auto const & name : names)
  {
    auto const value = parameters.value(name);
    if (value.type() != QVariant::String || value.toString().contains(QChar::Null) ||
        value.toString().toUcs4().size() > 256)
      throw std::invalid_argument("Map browsing parameters must be bounded strings");
  }
}
}  // namespace

SailplayMapBrowser::SailplayMapBrowser(MapItem & map) : m_map(map)
{
  for (auto * model : {static_cast<QAbstractItemModel *>(&m_search), static_cast<QAbstractItemModel *>(&m_groups),
                       static_cast<QAbstractItemModel *>(&m_bookmarks)})
    connect(model, &QAbstractItemModel::modelReset, this, [this] { ++m_revision; });
  connect(&m_search, &SearchModel::queryChanged, this, [this]
  {
    ++m_revision;
    m_offset = 0;
  });
  connect(&m_search, &SearchModel::historyChanged, this, [this] { ++m_revision; });
}

void SailplayMapBrowser::SetPage(QString const & page)
{
  m_page = page;
  m_offset = 0;
  ++m_revision;
}

QVariantList SailplayMapBrowser::Rows() const
{
  if (m_cachedRevision == m_revision)
    return m_cachedRows;
  QVariantList rows;
  auto const add = [&](int index, QString const & title, QString const & subtitle, QString const & kind,
                       QString const & value = QString())
  {
    rows.append(QVariantMap{{"id", QString::number(m_revision) + '/' + QString::number(index)},
                            {"title", Text(title)},
                            {"subtitle", Text(subtitle)},
                            {"kind", kind},
                            {"value", value},
                            {"index", index}});
  };
  if (m_page == "history")
  {
    auto const history = m_search.history();
    for (int row = 0; row < history.size(); ++row)
      add(row, history[row], {}, "query");
  }
  else if (m_page == "categories")
  {
    auto const categories = m_search.categories();
    for (int row = 0; row < categories.size(); ++row)
    {
      auto const category = categories[row].toMap();
      add(row, category.value("name").toString(), {}, "category");
    }
  }
  else if (m_page == "search")
  {
    if (m_search.searching() || m_search.resultsQuery() != m_search.query())
      return rows;
    for (int row = 0; row < m_search.rowCount(); ++row)
    {
      auto const index = m_search.index(row);
      QStringList detail;
      for (int role : {SearchModel::DescriptionRole, SearchModel::AddressRole, SearchModel::DistanceRole})
      {
        auto const text = PlainText(m_search.data(index, role));
        if (!text.isEmpty())
          detail.append(text);
      }
      add(row, PlainText(m_search.data(index, SearchModel::NameRole)), detail.join(" · "),
          m_search.data(index, SearchModel::SuggestRole).toBool() ? "suggestion" : "place");
    }
  }
  else if (m_page == "bookmark-groups")
  {
    for (int row = 0; row < m_groups.rowCount(); ++row)
    {
      auto const index = m_groups.index(row);
      int const count = m_groups.data(index, BookmarkCategoriesModel::BookmarksCountRole).toInt();
      if (count > 0)
        add(row, m_groups.data(index, BookmarkCategoriesModel::NameRole).toString(), QString::number(count), "group",
            m_groups.data(index, BookmarkCategoriesModel::IdRole).toString());
    }
  }
  else if (m_page == "bookmarks")
  {
    for (int row = 0; row < m_bookmarks.rowCount(); ++row)
    {
      auto const index = m_bookmarks.index(row);
      if (m_bookmarks.data(index, BookmarksModel::IsTrackRole).toBool())
        continue;
      QStringList detail;
      for (int role : {BookmarksModel::TypeRole, BookmarksModel::DistanceRole})
      {
        auto const text = m_bookmarks.data(index, role).toString();
        if (!text.isEmpty())
          detail.append(text);
      }
      add(row, m_bookmarks.data(index, BookmarksModel::NameRole).toString(), detail.join(" · "), "place");
    }
  }
  m_cachedRows = rows;
  m_cachedRevision = m_revision;
  return rows;
}

QVariantMap SailplayMapBrowser::Snapshot() const
{
  auto const rows = Rows();
  int const offset = rows.isEmpty() ? 0 : std::min(m_offset, (rows.size() - 1) / kPageSize * kPageSize);
  QVariantList visible;
  for (int row = offset; row < std::min(offset + kPageSize, rows.size()); ++row)
    visible.append(rows[row]);
  auto const * routing = m_map.routing();
  return {{"page", m_page},
          {"query", Text(m_search.query())},
          {"title", Text(m_bookmarks.name())},
          {"searching", m_page == "search" && (m_search.searching() || m_search.resultsQuery() != m_search.query())},
          {"rows", visible},
          {"previous", offset > 0},
          {"next", offset + kPageSize < rows.size()},
          {"routerType", routing->routerType()},
          {"voiceEnabled", routing->voiceEnabled()},
          {"voiceAvailable", routing->voiceAvailable()}};
}

void SailplayMapBrowser::Select(QString const & id)
{
  auto const rows = Rows();
  auto const found = std::find_if(rows.begin(), rows.end(),
                                  [&](QVariant const & value) { return value.toMap().value("id").toString() == id; });
  if (found == rows.end())
    throw std::invalid_argument("The destination list has changed. Select its current item");
  auto const row = found->toMap();
  int const index = row.value("index").toInt();
  if (m_page == "history" || m_page == "categories")
  {
    if (m_page == "categories")
      m_search.searchCategory(m_search.categories().value(index).toMap().value("name").toString());
    else
      m_search.setQuery(m_search.history().value(index));
    SetPage("search");
  }
  else if (m_page == "bookmark-groups")
  {
    m_bookmarks.setListId(row.value("value").toULongLong());
    SetPage("bookmarks");
  }
  else if (m_page == "bookmarks")
  {
    m_bookmarks.showOnMap(index);
    SetPage({});
  }
  else if (m_search.activate(index))
    SetPage({});
}

void SailplayMapBrowser::Invoke(QVariantMap const & parameters)
{
  auto const command = parameters.value("command").toString();
  if (command == "search")
  {
    Fields(parameters, {"command", "query"});
    m_search.setQuery(parameters.value("query").toString());
    SetPage("search");
  }
  else if (command == "select")
  {
    Fields(parameters, {"command", "id"});
    Select(parameters.value("id").toString());
  }
  else
  {
    Fields(parameters, {"command"});
    if (QStringList{"history", "categories", "bookmark-groups", "settings"}.contains(command))
      SetPage(command);
    else if (command == "close")
      SetPage({});
    else if (command == "next" || command == "previous")
    {
      auto const state = Snapshot();
      if (!state.value(command).toBool())
        throw std::invalid_argument("Destination page is unavailable");
      int const count = Rows().size();
      m_offset = std::min(m_offset, std::max(0, (count - 1) / kPageSize * kPageSize));
      m_offset += command == "next" ? kPageSize : -kPageSize;
    }
    else if (command == "mute")
      m_map.routing()->setVoiceEnabled(!m_map.routing()->voiceEnabled());
    else if (command == "clear-place")
      m_map.placePage()->close();
    else if (command == "toggle-bookmark")
    {
      if (!m_map.placePage()->open())
        throw std::invalid_argument("Select a place before saving a bookmark");
      m_map.placePage()->toggleBookmark();
    }
    else if (QStringList{"vehicle", "walking", "cycling"}.contains(command))
      m_map.routing()->setRouterType(QStringList{"vehicle", "walking", "cycling"}.indexOf(command));
    else
      throw std::invalid_argument("Unknown map browsing command");
  }
}
}  // namespace sailfish
