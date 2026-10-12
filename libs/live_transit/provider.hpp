#pragma once

#include "live_transit/departures.hpp"
#include "live_transit/journeys.hpp"

#include <string_view>
#include <vector>

namespace live_transit
{
// An online source of departures, vehicle positions and journeys, any of which may need a key of its own. The
// requests block on the network, so they are made off the GUI thread. Thread-safe.
class Provider
{
public:
  virtual ~Provider() = default;

  virtual bool Covers(ms::LatLon const & latLon) const = 0;
  // Shown with the provider's data, as some licenses require.
  virtual std::string_view GetAttribution() const = 0;

  virtual bool HasDepartures() const = 0;
  virtual Departures RequestDepartures(Stop const & stop) = 0;

  // Whether the departure's vehicle can be followed.
  virtual bool CanTrack(Departure const & departure) const = 0;
  // The vehicles of the departures that can be tracked, by Vehicle::m_tripId; none for those not on their way yet.
  // |status| tells about request errors.
  virtual std::vector<Vehicle> RequestVehicles(std::vector<Departure> const & departures, Status & status) = 0;

  virtual bool HasTripStops() const = 0;
  // Of departures from |stop|, in their order, in one request.
  virtual std::vector<TripStops> RequestTripStops(Stop const & stop, std::vector<Departure> const & departures) = 0;

  virtual bool HasJourneys() const = 0;
  virtual Journeys RequestJourneys(JourneyQuery const & query) = 0;
};
}  // namespace live_transit
