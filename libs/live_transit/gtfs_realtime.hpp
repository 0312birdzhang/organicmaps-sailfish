#pragma once

#include "live_transit/departures.hpp"

#include <string_view>
#include <vector>

namespace live_transit
{
namespace gtfs_realtime
{
// The vehicles with both a trip and a position in a GTFS Realtime VehiclePositions feed (protobuf). False for a
// malformed feed.
bool ParseVehiclePositions(std::string_view feed, std::vector<Vehicle> & vehicles);
}  // namespace gtfs_realtime
}  // namespace live_transit
