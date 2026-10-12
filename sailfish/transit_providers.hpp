#pragma once

#include "geometry/latlon.hpp"

#include <memory>
#include <string_view>

namespace live_transit
{
class Provider;
}

namespace sailfish
{
// The Trafiklab keys, one per API; without any no place is sent there.
std::string_view constexpr kTrafiklabKeySetting = "SailfishTrafiklabKey";
std::string_view constexpr kTrafiklabVehiclesKeySetting = "SailfishTrafiklabVehiclesKey";
// Transitous needs no key, so it is turned on explicitly.
std::string_view constexpr kTransitousSetting = "SailfishTransitous";

// The online provider of departures at a stop at the position, with the keys from the settings, or null. Keys entered
// in the settings apply to the next call. Called on the GUI thread.
std::shared_ptr<live_transit::Provider> GetDeparturesProvider(ms::LatLon const & latLon);
// The online journey planner, or null.
std::shared_ptr<live_transit::Provider> GetJourneysProvider();
}  // namespace sailfish
