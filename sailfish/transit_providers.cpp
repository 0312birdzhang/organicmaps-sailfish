#include "sailfish/transit_providers.hpp"

#include "sailfish/helpers.hpp"

#include "live_transit/trafiklab.hpp"
#include "live_transit/transitous.hpp"

#include <string>

namespace sailfish
{
namespace
{
std::shared_ptr<live_transit::Provider> GetTransitousProvider()
{
  if (!LoadSetting(kTransitousSetting, false))
    return nullptr;
  static auto const transitous = std::make_shared<live_transit::TransitousProvider>();
  return transitous;
}
}  // namespace

std::shared_ptr<live_transit::Provider> GetDeparturesProvider(ms::LatLon const & latLon)
{
  static std::shared_ptr<live_transit::TrafiklabProvider> trafiklab;
  static live_transit::TrafiklabKeys trafiklabKeys;

  live_transit::TrafiklabKeys keys{LoadSetting(kTrafiklabKeySetting, std::string()),
                                   LoadSetting(kTrafiklabVehiclesKeySetting, std::string())};
  if (keys != trafiklabKeys || !trafiklab)
  {
    trafiklabKeys = keys;
    trafiklab = std::make_shared<live_transit::TrafiklabProvider>(std::move(keys));
  }
  // Trafiklab's departures can be followed on the map.
  if (trafiklab->Covers(latLon) && trafiklab->HasDepartures())
    return trafiklab;
  return GetTransitousProvider();
}

std::shared_ptr<live_transit::Provider> GetJourneysProvider()
{
  return GetTransitousProvider();
}
}  // namespace sailfish
