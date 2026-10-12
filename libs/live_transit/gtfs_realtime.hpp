#pragma once

#include "live_transit/departures.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace live_transit
{
namespace gtfs_realtime
{
// The trip a vehicle is on: some feeds name no trip but its route, direction and start.
struct VehicleTrip
{
  std::string m_tripId;
  std::string m_routeId;
  std::optional<uint32_t> m_directionId;
  // "HH:MM:SS", past "24:00:00" after midnight.
  std::string m_startTime;
};

// The vehicles with a position and a trip id or route in a GTFS Realtime VehiclePositions feed (protobuf), with their
// trips. False for a malformed feed.
bool ParseVehiclePositions(std::string_view feed, std::vector<std::pair<VehicleTrip, Vehicle>> & vehicles);
// The vehicles with both a trip id, as Vehicle::m_tripId, and a position.
bool ParseVehiclePositions(std::string_view feed, std::vector<Vehicle> & vehicles);
}  // namespace gtfs_realtime
}  // namespace live_transit
