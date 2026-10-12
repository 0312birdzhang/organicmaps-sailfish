#pragma once

#include "geometry/latlon.hpp"

#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <span>
#include <string_view>

namespace sailfish
{
// A source of live departures or vehicles besides Transitous, as the settings list it. No keys ship with the app: users
// get their own from the source.
struct LiveSource
{
  struct Key
  {
    std::string_view m_setting;
    // String ids of the key's field and of what it gives.
    std::string_view m_label;
    std::string_view m_summary;
  };

  std::string_view m_id;
  std::string_view m_name;
  // The maps it covers have ids starting so, e.g. "Sweden".
  std::string_view m_maps;
  // String id of what it gives and is sent, empty when its keys tell.
  std::string_view m_summary;
  // Where users get its keys.
  std::string_view m_signUpUrl;
  std::span<Key const> m_keys;
};

// Those of the downloaded maps and of the map at the position, as {id, name, summary, signUpUrl, keys, hasKeys}, keys
// as {setting, label, summary, value}.
QVariantList RelevantLiveSources();
// A source with keys that covers the place but has none entered, unless its hint was hidden; empty otherwise.
QVariantMap MissingKeySource(ms::LatLon const & latLon);
bool HasLiveSourceKeys();
// Only the settings of the sources' keys are written.
void SetLiveSourceKey(QString const & setting, QString const & key);
void HideMissingKeyHint(QString const & id);
}  // namespace sailfish
