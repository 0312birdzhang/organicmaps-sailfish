#include "sailfish/transit_providers.hpp"

#include "sailfish/helpers.hpp"

#include "live_transit/trafiklab.hpp"
#include "live_transit/transitous.hpp"

#include <string>

namespace sailfish
{
std::shared_ptr<live_transit::Provider> GetTransitProvider(ms::LatLon const & latLon)
{
  static std::shared_ptr<live_transit::TrafiklabProvider> trafiklab;
  static live_transit::TrafiklabKeys trafiklabKeys;

  live_transit::TrafiklabKeys keys{LoadSetting(kTrafiklabKeySetting, std::string()),
                                   LoadSetting(kTrafiklabVehiclesKeySetting, std::string()),
                                   LoadSetting(kTrafiklabResRobotKeySetting, std::string())};
  if (keys != trafiklabKeys || !trafiklab)
  {
    trafiklabKeys = keys;
    trafiklab = std::make_shared<live_transit::TrafiklabProvider>(std::move(keys));
  }
  // Vehicles are only found from departures.
  if (trafiklab->Covers(latLon) && (trafiklab->HasDepartures() || trafiklab->HasJourneys()))
    return trafiklab;
  if (LoadSetting(kTransitousSetting, false))
  {
    static auto const transitous = std::make_shared<live_transit::TransitousProvider>();
    return transitous;
  }
  return nullptr;
}
}  // namespace sailfish
