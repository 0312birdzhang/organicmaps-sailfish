#include "sailfish/live_sources.hpp"

#include "sailfish/framework_access.hpp"
#include "sailfish/helpers.hpp"
#include "sailfish/transit_providers.hpp"

#include "map/framework.hpp"

#include "storage/country_info_getter.hpp"

#include "platform/settings.hpp"

#include "base/assert.hpp"

#include "geometry/mercator.hpp"

#include <QStringList>

#include <algorithm>
#include <string>
#include <vector>

namespace sailfish
{
namespace
{
// Ids of the sources whose missing key isn't hinted at, comma separated.
std::string_view constexpr kHiddenKeyHintsSetting = "SailfishHiddenKeyHints";

LiveSource::Key constexpr kTrafiklabKeys[] = {
    {kTrafiklabKeySetting, "pref_departures_key", "pref_departures_summary"},
    {kTrafiklabVehiclesKeySetting, "pref_departures_vehicles_key", "pref_departures_vehicles_summary"},
};

// Keyless ones come with Transitous, which matches its trips to their vehicles.
LiveSource constexpr kSources[] = {
    {"hsl", "HSL", "Finland_Southern Finland_Helsinki", "live_source_hsl_summary", {}, {}},
    {"entur", "Entur", "Norway", "live_source_entur_summary", {}, {}},
    {"trafiklab", "Trafiklab", "Sweden", {}, "https://www.trafiklab.se", kTrafiklabKeys},
};

bool HasKeys(LiveSource const & source)
{
  return std::ranges::any_of(source.m_keys,
                             [](auto const & key) { return !LoadSetting(key.m_setting, std::string()).empty(); });
}

QStringList HiddenKeyHints()
{
  return ToQString(LoadSetting(kHiddenKeyHintsSetting, std::string())).split(',', QString::SkipEmptyParts);
}

QVariantMap ToVariant(LiveSource const & source)
{
  QVariantList keys;
  for (auto const & key : source.m_keys)
  {
    keys.append(QVariantMap{{"setting", ToQString(key.m_setting)},
                            {"label", ToQString(key.m_label)},
                            {"summary", ToQString(key.m_summary)},
                            {"value", QString::fromStdString(LoadSetting(key.m_setting, std::string()))}});
  }
  return {{"id", ToQString(source.m_id)},
          {"name", ToQString(source.m_name)},
          {"summary", ToQString(source.m_summary)},
          {"signUpUrl", ToQString(source.m_signUpUrl)},
          {"keys", keys},
          {"hasKeys", HasKeys(source)}};
}

std::string MapAt(ms::LatLon const & latLon)
{
  return GetFramework().GetCountryInfoGetter().GetRegionCountryId(mercator::FromLatLon(latLon));
}
}  // namespace

QVariantList RelevantLiveSources()
{
  auto & framework = GetFramework();
  std::vector<std::string> maps;
  std::vector<storage::LocalFilePtr> localMaps;
  framework.GetStorage().GetLocalMaps(localMaps);
  for (auto const & map : localMaps)
    maps.push_back(map->GetCountryName());
  if (auto const position = framework.GetCurrentPosition())
    maps.push_back(framework.GetCountryInfoGetter().GetRegionCountryId(*position));

  QVariantList result;
  for (auto const & source : kSources)
    if (std::ranges::any_of(maps, [&](std::string const & map) { return map.starts_with(source.m_maps); }))
      result.append(ToVariant(source));
  return result;
}

QVariantMap MissingKeySource(ms::LatLon const & latLon)
{
  auto const map = MapAt(latLon);
  auto const hidden = HiddenKeyHints();
  for (auto const & source : kSources)
  {
    if (!source.m_keys.empty() && map.starts_with(source.m_maps) && !HasKeys(source) &&
        !hidden.contains(ToQString(source.m_id)))
    {
      return ToVariant(source);
    }
  }
  return {};
}

bool HasLiveSourceKeys()
{
  return std::ranges::any_of(kSources, HasKeys);
}

void SetLiveSourceKey(QString const & setting, QString const & key)
{
  for (auto const & source : kSources)
  {
    for (auto const & k : source.m_keys)
    {
      if (setting == ToQString(k.m_setting))
      {
        settings::Set(k.m_setting, key.trimmed().toStdString());
        return;
      }
    }
  }
  ASSERT(false, (setting.toStdString()));
}

void HideMissingKeyHint(QString const & id)
{
  auto hidden = HiddenKeyHints();
  if (!hidden.contains(id))
    hidden.append(id);
  settings::Set(kHiddenKeyHintsSetting, hidden.join(',').toStdString());
}
}  // namespace sailfish
