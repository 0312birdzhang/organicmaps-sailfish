#pragma once

#include "live_transit/provider.hpp"

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace live_transit
{
// Trafiklab keys each API separately, all from one project at trafiklab.se. An empty key turns its data off.
struct TrafiklabKeys
{
  // Trafiklab Realtime APIs, for the departures.
  std::string m_realtime;
  // GTFS Regional Realtime, for the vehicles.
  std::string m_vehicles;
  // ResRobot v2.1, for the journeys.
  std::string m_resrobot;

  friend bool operator==(TrafiklabKeys const &, TrafiklabKeys const &) = default;
};

// All public transport in Sweden.
class TrafiklabProvider : public Provider
{
public:
  explicit TrafiklabProvider(TrafiklabKeys keys) : m_keys(std::move(keys)) {}

  bool Covers(ms::LatLon const & latLon) const override;
  std::string_view GetAttribution() const override { return "Trafiklab.se"; }

  bool HasDepartures() const override { return !m_keys.m_realtime.empty(); }
  Departures RequestDepartures(Stop const & stop) override;

  bool CanTrack(Departure const & departure) const override;
  std::vector<Vehicle> RequestVehicles(std::vector<Departure> const & departures, Status & status) override;

  // ResRobot takes the stop groups' ids.
  bool HasTripStops() const override { return !m_keys.m_realtime.empty() && !m_keys.m_resrobot.empty(); }
  std::vector<TripStops> RequestTripStops(Stop const & stop, std::vector<Departure> const & departures) override;

  bool HasJourneys() const override { return !m_keys.m_resrobot.empty(); }
  Journeys RequestJourneys(JourneyQuery const & query) override;

private:
  // The stop group id, found by the stop's name, since the API has no lookup by position.
  std::optional<std::string> FindStopGroup(Stop const & stop, Status & status);

  TrafiklabKeys const m_keys;
  std::mutex m_mutex;
  // By the stop's name and position, so that refreshes look it up once.
  std::unordered_map<std::string, std::string> m_stopGroups;
};

namespace trafiklab
{
// The names to search a stop by, from the most specific: the search only matches stop groups, which are often named
// more broadly than the stops on the map, e.g. "Skövde Centralstation" for "Skövde resecentrum (13)".
std::vector<std::string> GetStopQueries(std::string_view name);
// The GTFS Regional operator of a Trafiklab Realtime agency, empty without a known feed.
std::string_view GetOperator(std::string_view agencyId);

// Of the groups matching a name, the one with a stop nearest to |latLon|, if near enough.
std::optional<std::string> ParseNearestStopGroup(std::string_view json, ms::LatLon const & latLon);
bool ParseDepartures(std::string_view json, Departures & departures);
// A ResRobot trip search response, also an error response.
Journeys ParseJourneys(std::string_view json);
// Of |departures|, in their order, from a ResRobot departure board with the stops passed.
std::vector<TripStops> ParseTripStops(std::string_view json, std::vector<Departure> const & departures);
}  // namespace trafiklab
}  // namespace live_transit
