#pragma once

#include "live_transit/gtfs_realtime.hpp"
#include "live_transit/provider.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace live_transit
{
// Transitous, a community-run MOTIS instance with the open timetables of many countries, and their realtime data where
// published; it needs no key. See transitous.org for its coverage and usage policy.
class TransitousProvider : public Provider
{
public:
  // Its data has no bounds, an uncovered stop just has no departures.
  bool Covers(ms::LatLon const &) const override { return true; }
  std::string_view GetAttribution() const override { return "Transitous"; }
  // With the operators whose vehicles can be followed.
  std::string GetDeparturesAttribution(std::vector<Departure> const & departures) const override;

  bool HasDepartures() const override { return true; }
  Departures RequestDepartures(Stop const & stop) override;

  // Its own vehicles are interpolated from the timetable, so those of some feeds come from their operators.
  bool CanTrack(Departure const & departure) const override;
  std::vector<Vehicle> RequestVehicles(std::vector<Departure> const & departures, Status & status) override;

  // From the departures' last request, which lists them.
  bool HasTripStops() const override { return true; }
  std::vector<TripStops> RequestTripStops(Stop const & stop, std::vector<Departure> const & departures) override;

  bool HasJourneys() const override { return true; }
  Journeys RequestJourneys(JourneyQuery const & query) override;

private:
  std::mutex m_mutex;
  // By Departure::m_tripId, of the last departures requested.
  std::unordered_map<std::string, TripStops> m_tripStops;
};

namespace transitous
{
// A response to /api/v6/stoptimes with the stops after each departure, by Departure::m_tripId.
bool ParseDepartures(std::string_view json, Departures & departures,
                     std::unordered_map<std::string, TripStops> & tripStops);
// A response to /api/v6/plan.
Journeys ParseJourneys(std::string_view json);
// Google's encoded polyline, with |precision| decimal digits.
std::vector<ms::LatLon> DecodePolyline(std::string_view points, int precision);
// An ISO 8601 UTC time, e.g. "2026-10-12T06:04:00Z", on the device's clock.
std::optional<LocalTime> ParseUtcTime(std::string_view s);

// The parts of a trip id, "<YYYYMMDD>_<HH:MM>_<feed>_<GTFS trip id>".
struct TripId
{
  std::string_view m_feed;
  std::string_view m_gtfsTripId;
};
std::optional<TripId> ParseTripId(std::string_view tripId);

enum class VehicleApi : uint8_t
{
  // All the vehicles at once.
  GtfsRealtime,
  // Entur's GraphQL API, asked for the trips' vehicles.
  Entur,
};

// The vehicle positions of an operator, or of a country's operators, for the trips of a Transitous feed.
struct VehicleFeed
{
  std::string_view m_feed;
  VehicleApi m_api;
  std::string_view m_url;
  // Its license asks for it.
  std::string_view m_attribution;
  // Its vehicles name no trip, only the route, direction and start that its trip ids
  // "<route>_<date>_<day>_<direction 1|2>_<HHMM>" tell.
  bool m_tripsByStart = false;
};
VehicleFeed const * FindVehicleFeed(std::string_view feed);
// Equal for a GTFS trip id of the feed and the trip of a vehicle on it; empty when unknown.
std::string GetTripKey(VehicleFeed const & feed, std::string_view gtfsTripId);
std::string GetTripKey(VehicleFeed const & feed, gtfs_realtime::VehicleTrip const & trip);

// The query for the vehicles on the ServiceJourneys with |ids|.
std::string MakeEnturVehiclesQuery(std::vector<std::string_view> const & ids);
// A response to it, with the vehicles' ServiceJourney ids as their trip ids. False for an error response.
bool ParseEnturVehicles(std::string_view json, std::vector<std::pair<gtfs_realtime::VehicleTrip, Vehicle>> & vehicles);
}  // namespace transitous
}  // namespace live_transit
