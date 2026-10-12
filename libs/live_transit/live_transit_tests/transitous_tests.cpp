#include "testing/testing.hpp"

#include "live_transit/transitous.hpp"

#include <chrono>
#include <string_view>

namespace transitous_tests
{
using namespace live_transit;
using namespace std::chrono_literals;

LocalTime Utc(std::string_view s)
{
  auto const time = transitous::ParseUtcTime(s);
  TEST(time, (s));
  return *time;
}

UNIT_TEST(Transitous_ParseUtcTime)
{
  TEST_EQUAL(Utc("2026-10-12T06:05:00Z") - Utc("2026-10-12T06:04:30Z"), 30s, ());
  TEST(!transitous::ParseUtcTime("12:00"), ());
}

UNIT_TEST(Transitous_Departures)
{
  std::string_view constexpr json = R"({
  "place": {"name": "center", "lat": 59.3428, "lon": 18.0496},
  "stopTimes": [
    {
      "place": {"name": "Odenplan T-bana", "lat": 59.342957, "lon": 18.049704, "track": "2",
                "departure": "2026-10-12T06:07:30Z", "scheduledDeparture": "2026-10-12T06:06:00Z"},
      "mode": "BUS", "realTime": true, "headsign": "Karolinska sjukhuset", "agencyId": "275",
      "tripId": "trip-2", "routeShortName": "3", "displayName": "3",
      "nextStops": [
        {"name": "Vasaparken", "lat": 59.34, "lon": 18.04, "arrival": "2026-10-12T06:09:00Z",
         "scheduledArrival": "2026-10-12T06:08:00Z", "departure": "2026-10-12T06:09:00Z"},
        {"name": "Karolinska sjukhuset", "lat": 59.35, "lon": 18.03, "arrival": "2026-10-12T06:15:00Z",
         "scheduledArrival": "2026-10-12T06:15:00Z"}
      ]
    },
    {
      "place": {"name": "Odenplan T-bana", "lat": 59.342957, "lon": 18.049704,
                "departure": "2026-10-12T06:04:00Z", "scheduledDeparture": "2026-10-12T06:04:00Z"},
      "mode": "SUBWAY", "realTime": false, "headsign": "", "tripTo": {"name": "Hagsätra T-bana", "lat": 59.26, "lon": 18.01},
      "tripId": "trip-1", "routeShortName": "", "displayName": "19", "tripCancelled": true
    }
  ]
})";
  Departures departures;
  std::unordered_map<std::string, TripStops> tripStops;
  TEST(transitous::ParseDepartures(json, departures, tripStops), ());
  TEST_EQUAL(departures.m_departures.size(), 2, ());

  auto const & metro = departures.m_departures[0];
  TEST_EQUAL(metro.m_line, "19", ());
  TEST_EQUAL(metro.m_destination, "Hagsätra T-bana", ());
  TEST_EQUAL(metro.m_mode, TransportMode::Metro, ());
  TEST(!metro.m_realtime, ());
  TEST(metro.m_canceled, ());

  auto const & bus = departures.m_departures[1];
  TEST_EQUAL(bus.m_line, "3", ());
  TEST_EQUAL(bus.m_destination, "Karolinska sjukhuset", ());
  TEST_EQUAL(bus.m_mode, TransportMode::Bus, ());
  TEST_EQUAL(bus.m_platform, "2", ());
  TEST_EQUAL(bus.m_tripId, "trip-2", ());
  TEST_EQUAL(*bus.m_realtime - bus.m_scheduled, 90s, ());
  TEST(bus.m_platformLatLon, ());

  TEST(!tripStops.contains("trip-1"), ());
  auto const & stops = tripStops.at("trip-2").m_stops;
  TEST_EQUAL(stops.size(), 2, ());
  TEST_EQUAL(stops[0].m_name, "Vasaparken", ());
  TEST_EQUAL(stops[0].m_scheduled, Utc("2026-10-12T06:08:00Z"), ());
  TEST_EQUAL(*stops[0].m_realtime - stops[0].m_scheduled, 60s, ());
  // On time.
  TEST(!stops[1].m_realtime, ());
}

UNIT_TEST(Transitous_Journeys)
{
  std::string_view constexpr json = R"({
  "itineraries": [
    {"legs": [
      {"mode": "WALK", "distance": 30.0,
       "from": {"name": "START", "vertexType": "NORMAL", "lat": 59.3428, "lon": 18.0496, "departure": "2026-10-12T06:05:00Z"},
       "to": {"name": "Odenplan T-bana", "lat": 59.342957, "lon": 18.049704, "arrival": "2026-10-12T06:06:00Z"},
       "legGeometry": {"points": "_p~iF~ps|U_ulLnnqC_mqNvxq`@", "precision": 5, "length": 3}},
      {"mode": "SUBWAY", "headsign": "Skarpnäck T-bana", "routeShortName": "17", "tripId": "trip-17",
       "from": {"name": "Odenplan T-bana", "lat": 59.342957, "lon": 18.049704, "departure": "2026-10-12T06:06:00Z"},
       "to": {"name": "Hötorget T-bana", "lat": 59.335533, "lon": 18.063538, "arrival": "2026-10-12T06:09:00Z"},
       "intermediateStops": [{"name": "Rådmansgatan T-bana", "lat": 59.340572, "lon": 18.058775,
                              "arrival": "2026-10-12T06:07:00Z", "departure": "2026-10-12T06:08:00Z"}]}
    ]}
  ],
  "previousPageCursor": "EARLIER|1", "nextPageCursor": "LATER|2"
})";
  auto const journeys = transitous::ParseJourneys(json);
  TEST_EQUAL(journeys.m_status, Status::Ok, ());
  TEST_EQUAL(journeys.m_earlier, "EARLIER|1", ());
  TEST_EQUAL(journeys.m_later, "LATER|2", ());
  TEST_EQUAL(journeys.m_journeys.size(), 1, ());

  auto const & legs = journeys.m_journeys[0].m_legs;
  TEST_EQUAL(legs.size(), 2, ());
  TEST(legs[0].m_walk, ());
  TEST_EQUAL(legs[0].m_from.m_name, "", ());
  TEST_EQUAL(legs[0].m_to.m_name, "Odenplan T-bana", ());
  TEST_EQUAL(legs[0].m_distanceM, 30.0, ());
  TEST_EQUAL(legs[0].m_path.size(), 3, ());

  TEST(!legs[1].m_walk, ());
  TEST_EQUAL(legs[1].m_mode, TransportMode::Metro, ());
  TEST_EQUAL(legs[1].m_line, "17", ());
  TEST_EQUAL(legs[1].m_direction, "Skarpnäck T-bana", ());
  TEST_EQUAL(legs[1].m_stops.size(), 3, ());
  TEST_EQUAL(legs[1].m_stops[1].m_name, "Rådmansgatan T-bana", ());
  // Without geometry, through the stops.
  TEST_EQUAL(legs[1].m_path.size(), 3, ());
  TEST_EQUAL(journeys.m_journeys[0].GetArrival() - journeys.m_journeys[0].GetDeparture(), 4min, ());
}

UNIT_TEST(Transitous_NoJourney)
{
  TEST_EQUAL(transitous::ParseJourneys(R"({"itineraries": []})").m_status, Status::NoResult, ());
  TEST_EQUAL(transitous::ParseJourneys("{").m_status, Status::ServerError, ());
}

UNIT_TEST(Transitous_DecodePolyline)
{
  // Google's example.
  auto const points = transitous::DecodePolyline("_p~iF~ps|U_ulLnnqC_mqNvxq`@", 5);
  TEST_EQUAL(points.size(), 3, ());
  TEST_ALMOST_EQUAL_ABS(points[0].m_lat, 38.5, 1e-9, ());
  TEST_ALMOST_EQUAL_ABS(points[0].m_lon, -120.2, 1e-9, ());
  TEST_ALMOST_EQUAL_ABS(points[1].m_lat, 40.7, 1e-9, ());
  TEST_ALMOST_EQUAL_ABS(points[1].m_lon, -120.95, 1e-9, ());
  TEST_ALMOST_EQUAL_ABS(points[2].m_lat, 43.252, 1e-9, ());
  TEST_ALMOST_EQUAL_ABS(points[2].m_lon, -126.453, 1e-9, ());
}

UNIT_TEST(Transitous_ParseTripId)
{
  auto id = transitous::ParseTripId("20261012_16:15_no-Entur_RUT:ServiceJourney:cd971eb4");
  TEST(id, ());
  TEST_EQUAL(id->m_feed, "no-Entur", ());
  TEST_EQUAL(id->m_gtfsTripId, "RUT:ServiceJourney:cd971eb4", ());
  // GTFS trip ids may have '_' too.
  id = transitous::ParseTripId("20261012_17:22_fi-hsl_1004_20261009_Ma_1_1722");
  TEST(id, ());
  TEST_EQUAL(id->m_feed, "fi-hsl", ());
  TEST_EQUAL(id->m_gtfsTripId, "1004_20261009_Ma_1_1722", ());

  TEST(!transitous::ParseTripId("trip-1"), ());
  TEST(!transitous::ParseTripId("20261012_17:22_fi-hsl_"), ());
  TEST(!transitous::ParseTripId("20261012__fi-hsl_1"), ());
}

UNIT_TEST(Transitous_HslTripKeys)
{
  auto const * hsl = transitous::FindVehicleFeed("fi-hsl");
  TEST(hsl, ());
  TEST(!transitous::FindVehicleFeed("de-DELFI"), ());

  gtfs_realtime::VehicleTrip trip{.m_routeId = "1004", .m_directionId = 0, .m_startTime = "17:22:00"};
  auto const key = transitous::GetTripKey(*hsl, trip);
  TEST(!key.empty(), ());
  // Direction 1 is GTFS's 0.
  TEST_EQUAL(transitous::GetTripKey(*hsl, "1004_20261009_Ma_1_1722"), key, ());
  TEST_NOT_EQUAL(transitous::GetTripKey(*hsl, "1004_20261009_Ma_2_1722"), key, ());
  TEST_NOT_EQUAL(transitous::GetTripKey(*hsl, "1004_20261009_Ma_1_1723"), key, ());
  TEST_NOT_EQUAL(transitous::GetTripKey(*hsl, "1005_20261009_Ma_1_1722"), key, ());
  // After midnight.
  trip.m_startTime = "24:30:00";
  TEST_EQUAL(transitous::GetTripKey(*hsl, trip), transitous::GetTripKey(*hsl, "1004_20261009_Ma_1_2430"), ());

  TEST(transitous::GetTripKey(*hsl, "1004_20261009_Ma_1").empty(), ());
  TEST(transitous::GetTripKey(*hsl, "1004_20261009_Ma_3_1722").empty(), ());
  TEST(transitous::GetTripKey(*hsl, gtfs_realtime::VehicleTrip{.m_routeId = "1004", .m_startTime = "17:22:00"}).empty(),
       ("Without a direction"));
  TEST(transitous::GetTripKey(*hsl, gtfs_realtime::VehicleTrip{.m_tripId = "1004_20261009_Ma_1_1722"}).empty(), ());
}

UNIT_TEST(Transitous_CanTrack)
{
  TransitousProvider const provider;
  Departure departure;
  departure.m_tripId = "20261012_17:22_fi-hsl_1004_20261009_Ma_1_1722";
  TEST(provider.CanTrack(departure), ());
  departure.m_canceled = true;
  TEST(!provider.CanTrack(departure), ());
  departure.m_canceled = false;
  departure.m_tripId = "20261012_16:15_no-Entur_SKY:ServiceJourney:450-206181-21457998";
  TEST(provider.CanTrack(departure), ());
  departure.m_tripId = "20261012_16:15_no-Entur_RUT:ServiceJourney:cd971eb4";
  TEST(!provider.CanTrack(departure), ("Ruter publishes no vehicles"));
  departure.m_tripId = "20261012_16:15_de-DELFI_1";
  TEST(!provider.CanTrack(departure), ("Without a vehicles feed"));
  departure.m_tripId = "20261012_17:22_fi-hsl_unknown";
  TEST(!provider.CanTrack(departure), ("Not an HSL trip id"));
}
UNIT_TEST(Transitous_DeparturesAttribution)
{
  TransitousProvider const provider;
  Departure oslo, helsinki;
  oslo.m_tripId = "20261012_16:15_no-Entur_RUT:ServiceJourney:cd971eb4";
  helsinki.m_tripId = "20261012_17:22_fi-hsl_1004_20261009_Ma_1_1722";
  TEST_EQUAL(provider.GetDeparturesAttribution({}), "Transitous", ());
  TEST_EQUAL(provider.GetDeparturesAttribution({oslo}), "Transitous", ());
  TEST_EQUAL(provider.GetDeparturesAttribution({oslo, helsinki, helsinki}), "Transitous, HSL", ());
  Departure bergen;
  bergen.m_tripId = "20261012_16:15_no-Entur_SKY:ServiceJourney:450-206181-21457998";
  TEST_EQUAL(provider.GetDeparturesAttribution({bergen, helsinki}), "Transitous, Entur, HSL", ());
  helsinki.m_canceled = true;
  TEST_EQUAL(provider.GetDeparturesAttribution({helsinki}), "Transitous", ("Its vehicle isn't followed"));
}
UNIT_TEST(Transitous_EnturVehicles)
{
  auto const query = transitous::MakeEnturVehiclesQuery({"SKY:ServiceJourney:1", "ATB:ServiceJourney:\"2"});
  TEST(query.find(R"("variables":{"ids":[{"id":"SKY:ServiceJourney:1"},{"id":"ATB:ServiceJourney:\"2"}]})") !=
           std::string::npos,
       (query));

  std::string_view constexpr kJson = R"({"data": {"vehicles": [
    {"serviceJourney": {"id": "SKY:ServiceJourney:450-206181-21457998"},
     "location": {"latitude": 60.3862698, "longitude": 5.3201692},
     "bearing": 72.27, "speed": null, "lastUpdatedEpochSecond": 1791816093.0},
    {"serviceJourney": {"id": "SKY:ServiceJourney:4"}, "location": null, "bearing": null},
    {"serviceJourney": null, "location": {"latitude": 60.0, "longitude": 5.0}}
  ]}})";
  std::vector<std::pair<gtfs_realtime::VehicleTrip, Vehicle>> vehicles;
  TEST(transitous::ParseEnturVehicles(kJson, vehicles), ());
  TEST_EQUAL(vehicles.size(), 1, ("Only those with a journey and a location"));
  auto const & [trip, vehicle] = vehicles[0];
  TEST_EQUAL(trip.m_tripId, "SKY:ServiceJourney:450-206181-21457998", ());
  TEST_ALMOST_EQUAL_ABS(vehicle.m_latLon.m_lat, 60.3862698, 1e-7, ());
  TEST_ALMOST_EQUAL_ABS(vehicle.m_latLon.m_lon, 5.3201692, 1e-7, ());
  TEST(vehicle.m_bearing, ());
  TEST_ALMOST_EQUAL_ABS(*vehicle.m_bearing, 72.27, 1e-9, ());
  TEST(!vehicle.m_speed, ());
  TEST_EQUAL(vehicle.m_timestamp, 1791816093, ());

  TEST(transitous::ParseEnturVehicles(R"({"data": {"vehicles": []}})", vehicles), ());
  TEST(vehicles.empty(), ());
  TEST(!transitous::ParseEnturVehicles(R"({"errors": [{"message": "Validation error"}]})", vehicles), ());
  TEST(!transitous::ParseEnturVehicles("<html>", vehicles), ());
}
}  // namespace transitous_tests
