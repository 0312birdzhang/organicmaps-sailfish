#pragma once

#include "live_transit/provider.hpp"

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

  bool HasDepartures() const override { return true; }
  Departures RequestDepartures(Stop const & stop) override;

  // Its vehicles are interpolated from the timetable, not reported.
  bool CanTrack(Departure const &) const override { return false; }
  std::vector<Vehicle> RequestVehicles(std::vector<Departure> const &, Status &) override { return {}; }

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
}  // namespace transitous
}  // namespace live_transit
