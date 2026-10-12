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
}  // namespace transitous_tests
