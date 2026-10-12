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
// The Trafiklab keys, one per API; without any no place is sent anywhere.
std::string_view constexpr kTrafiklabKeySetting = "SailfishTrafiklabKey";
std::string_view constexpr kTrafiklabVehiclesKeySetting = "SailfishTrafiklabVehiclesKey";
std::string_view constexpr kTrafiklabResRobotKeySetting = "SailfishTrafiklabResRobotKey";

// The online public transport provider covering the position, with the keys from the settings, or null. Keys entered
// in the settings apply to the next call. Called on the GUI thread.
std::shared_ptr<live_transit::Provider> GetTransitProvider(ms::LatLon const & latLon);
}  // namespace sailfish
