#pragma once

#include "geometry/latlon.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Live public transport data from online providers.
namespace live_transit
{
// Wall-clock time at the stop: providers report local times, which are compared with the provider's own clock.
using LocalTime = std::chrono::local_seconds;

enum class TransportMode : uint8_t
{
  Bus,
  Tram,
  Metro,
  Train,
  Ferry,
  Other,
};

struct Departure
{
  LocalTime GetTime() const { return m_realtime.value_or(m_scheduled); }

  std::string m_line;
  std::string m_destination;
  TransportMode m_mode = TransportMode::Other;
  LocalTime m_scheduled;
  // Without a prediction only the timetable is known.
  std::optional<LocalTime> m_realtime;
  std::string m_platform;
  // Of the platform, when the provider tells.
  std::optional<ms::LatLon> m_platformLatLon;
  bool m_canceled = false;
  // Identify the vehicle in the provider's realtime feeds, if it has any.
  std::string m_tripId;
  std::string m_agencyId;
};

enum class Status : uint8_t
{
  Ok,
  StopNotFound,
  NetworkError,
  Unauthorized,
  ServerError,
  // E.g. no journey between the places.
  NoResult,
};

struct Departures
{
  Status m_status = Status::Ok;
  // The provider's clock when it answered.
  LocalTime m_now;
  std::vector<Departure> m_departures;
};

struct Stop
{
  ms::LatLon m_latLon;
  std::string m_name;
};

// Where a departure's trip calls after the stop it leaves from.
struct TripStop
{
  LocalTime GetTime() const { return m_realtime.value_or(m_scheduled); }

  std::string m_name;
  ms::LatLon m_latLon;
  // Arrivals, but the departure where there is none.
  LocalTime m_scheduled;
  std::optional<LocalTime> m_realtime;
};

struct TripStops
{
  Status m_status = Status::Ok;
  std::vector<TripStop> m_stops;
};

struct Vehicle
{
  std::string m_tripId;
  ms::LatLon m_latLon;
  // Degrees clockwise from north.
  std::optional<double> m_bearing;
  // Meters per second.
  std::optional<double> m_speed;
  // Unix time of the position, 0 when unknown.
  uint64_t m_timestamp = 0;
};

// A line's route through a platform, as the map has it.
struct PlatformRoute
{
  std::string m_line;
  // Its ends' names, empty when unknown.
  std::string m_from;
  std::string m_to;
};

// A platform of a stop, as the map has it.
struct StopPlatform
{
  ms::LatLon m_latLon;
  // Its letter or number, e.g. "A" or "13", empty when unknown.
  std::string m_ref;
  // The lines calling at it, empty when unknown.
  std::vector<std::string> m_lines;
  std::vector<PlatformRoute> m_routes;
};

// Keeps the departures from |platform|: providers also list the platforms of the other direction and of nearby
// streets under one stop. Those near it are kept, then those at its ref, or else those of its lines; then those not
// heading for where a route through it comes from. When none would be left, all are.
void KeepPlatform(Departures & departures, StopPlatform const & platform);

// Whether a provider's and the map's names likely mean one place, as "Karolinska sjukhuset norra" and "Karolinska
// sjukhuset".
bool IsSamePlace(std::string_view a, std::string_view b);

// Heading for |stop|, or near enough that its heading tells little. Feeds give a vehicle its next trip while it still
// drives its previous one, away from the stop.
bool IsApproaching(Vehicle const & vehicle, ms::LatLon const & stop);
// Before |stop| on a trip calling next at |next|, and going that way, as told by the line through them: a vehicle on
// the trip before comes from the side of |next|, or goes away.
bool IsApproaching(Vehicle const & vehicle, ms::LatLon const & stop, ms::LatLon const & next);

// "YYYY-MM-DDTHH:MM[:SS]", ignoring any fraction or time zone after it.
std::optional<LocalTime> ParseLocalTime(std::string_view s);

std::string DebugPrint(TransportMode mode);
std::string DebugPrint(Status status);
}  // namespace live_transit
