#include "testing/testing.hpp"

#include "live_transit/trafiklab.hpp"

namespace trafiklab_tests
{
using namespace live_transit;
using namespace std::chrono;

// Two groups named like the searched stop, one far away.
std::string_view constexpr kStops = R"({
  "timestamp": "2025-04-01T16:00:00",
  "query": {"queryTime": "2025-04-01T16:00:00", "query": "Odenplan"},
  "stop_groups": [
    {"id": "740000001", "name": "Odenplan", "area_type": "META_STOP", "average_daily_stop_times": 10.5,
     "transport_modes": ["BUS"],
     "stops": [{"id": "1", "name": "Odenplan", "lat": 59.0, "lon": 18.0}]},
    {"id": "740021705", "name": "Odenplan", "area_type": "RIKSHALLPLATS", "average_daily_stop_times": 1234.0,
     "transport_modes": ["BUS", "METRO", "TRAIN"],
     "stops": [{"id": "2", "name": "Odenplan", "lat": 59.3429, "lon": 18.0491},
               {"id": "3", "name": "Odenplan", "lat": 59.3431, "lon": 18.0457}]}
  ]
})";

UNIT_TEST(Trafiklab_NearestStopGroup)
{
  TEST_EQUAL(trafiklab::ParseNearestStopGroup(kStops, {59.3430, 18.0480}), "740021705", ());
  TEST_EQUAL(trafiklab::ParseNearestStopGroup(kStops, {59.0001, 18.0}), "740000001", ());
  // Too far from both.
  TEST(!trafiklab::ParseNearestStopGroup(kStops, {59.1700, 18.0}), ());
  TEST(!trafiklab::ParseNearestStopGroup(R"({"stop_groups": []})", {59.0, 18.0}), ());
  TEST(!trafiklab::ParseNearestStopGroup("not json", {59.0, 18.0}), ());
}

std::string_view constexpr kDepartures = R"({
  "timestamp": "2025-04-01T16:00:12",
  "query": {"queryTime": "2025-04-01T16:00:00", "query": "740021705"},
  "stops": [{"id": "740021705", "name": "Odenplan", "lat": 59.343, "lon": 18.048, "transport_modes": [], "alerts": []}],
  "departures": [
    {"scheduled": "2025-04-01T16:10:00", "realtime": "2025-04-01T16:10:00", "delay": 0, "canceled": false,
     "route": {"name": null, "designation": "4", "transport_mode_code": 700, "transport_mode": "BUS",
               "direction": "Radiohuset",
               "origin": {"id": "1", "name": "Gullmarsplan"}, "destination": {"id": "2", "name": "Radiohuset"}},
     "trip": {"trip_id": "1", "start_date": "2025-04-01", "technical_number": 4},
     "agency": {"id": "1", "name": "SL", "operator": "Keolis"},
     "stop": {"id": "3", "name": "Odenplan", "lat": 59.3, "lon": 18.0},
     "scheduled_platform": {"id": "3", "designation": "A"}, "realtime_platform": null,
     "alerts": [], "is_realtime": false},
    {"scheduled": "2025-04-01T16:02:00", "realtime": "2025-04-01T16:04:30", "delay": 150, "canceled": false,
     "route": {"name": "Gröna linjen", "designation": "18", "transport_mode_code": 401, "transport_mode": "METRO",
               "direction": "Farsta strand", "destination": {"id": "4", "name": "Farsta strand"}},
     "scheduled_platform": {"id": "5", "designation": "1"}, "realtime_platform": {"id": "6", "designation": "2"},
     "alerts": [], "is_realtime": true},
    {"scheduled": "2025-04-01T16:06:00", "realtime": "2025-04-01T16:06:00", "delay": 0, "canceled": true,
     "route": {"designation": "41", "transport_mode": "TRAIN", "direction": "Märsta"},
     "alerts": [], "is_realtime": true},
    {"scheduled": "garbage", "route": {"designation": "1", "transport_mode": "BUS", "direction": "X"}}
  ]
})";

UNIT_TEST(Trafiklab_Departures)
{
  Departures result;
  TEST(trafiklab::ParseDepartures(kDepartures, result), ());
  auto const at = [](auto time) { return local_days{2025y / April / 1} + 16h + time; };
  TEST(result.m_now == at(12s), ());
  // Sorted by the expected time; one without a valid time is dropped.
  TEST_EQUAL(result.m_departures.size(), 3, ());

  auto const & metro = result.m_departures[0];
  TEST_EQUAL(metro.m_line, "18", ());
  TEST_EQUAL(metro.m_destination, "Farsta strand", ());
  TEST_EQUAL(metro.m_mode, TransportMode::Metro, ());
  TEST(metro.m_scheduled == at(2min), ());
  TEST(metro.m_realtime == at(4min + 30s), ());
  TEST_EQUAL(metro.m_platform, "2", ());
  TEST(!metro.m_canceled, ());
  TEST(metro.m_tripId.empty(), ());

  auto const & train = result.m_departures[1];
  TEST_EQUAL(train.m_line, "41", ());
  // Without a destination.
  TEST_EQUAL(train.m_destination, "Märsta", ());
  TEST_EQUAL(train.m_mode, TransportMode::Train, ());
  TEST(train.m_canceled, ());
  TEST(train.m_platform.empty(), ());

  auto const & bus = result.m_departures[2];
  TEST_EQUAL(bus.m_line, "4", ());
  TEST_EQUAL(bus.m_destination, "Radiohuset", ());
  TEST_EQUAL(bus.m_mode, TransportMode::Bus, ());
  // A realtime field without a prediction is the timetable.
  TEST(!bus.m_realtime, ());
  TEST(bus.GetTime() == at(10min), ());
  TEST_EQUAL(bus.m_platform, "A", ());
  TEST_EQUAL(bus.m_tripId, "1", ());
  TEST_EQUAL(bus.m_agencyId, "1", ());
  TEST(bus.m_platformLatLon == ms::LatLon(59.3, 18.0), ());
  TEST(!metro.m_platformLatLon, ());
}

UNIT_TEST(Trafiklab_DeparturesInvalid)
{
  Departures result;
  TEST(!trafiklab::ParseDepartures("{}", result), ());
  TEST(!trafiklab::ParseDepartures("[]", result), ());
  TEST(!trafiklab::ParseDepartures(R"({"timestamp": "now", "departures": []})", result), ());
}

UNIT_TEST(Trafiklab_CanTrack)
{
  Departure departure;
  departure.m_tripId = "14010000731153414";
  departure.m_agencyId = "505000000000000001";
  TEST(TrafiklabProvider({.m_vehicles = "key"}).CanTrack(departure), ());
  TEST(!TrafiklabProvider({.m_realtime = "key"}).CanTrack(departure), ("Without a vehicles key"));
  departure.m_canceled = true;
  TEST(!TrafiklabProvider({.m_vehicles = "key"}).CanTrack(departure), ());
  departure.m_canceled = false;
  departure.m_agencyId = "1";
  TEST(!TrafiklabProvider({.m_vehicles = "key"}).CanTrack(departure), ("Without a feed"));
}

UNIT_TEST(Trafiklab_StopQueries)
{
  using Queries = std::vector<std::string>;
  TEST_EQUAL(trafiklab::GetStopQueries("Odenplan"), Queries{"Odenplan"}, ());
  TEST_EQUAL(trafiklab::GetStopQueries("Skövde resecentrum (13)"), (Queries{"Skövde resecentrum", "Skövde"}), ());
  TEST_EQUAL(trafiklab::GetStopQueries("Karolinska institutet Biomedicum"),
             (Queries{"Karolinska institutet Biomedicum", "Karolinska institutet", "Karolinska"}), ());
  TEST_EQUAL(trafiklab::GetStopQueries("Sergels torg (på Sveavägen)"), (Queries{"Sergels torg", "Sergels"}), ());
  // At most three.
  TEST_EQUAL(trafiklab::GetStopQueries("A B C D").size(), 3, ());
  TEST(trafiklab::GetStopQueries("").empty(), ());
}

UNIT_TEST(Trafiklab_Operator)
{
  TEST_EQUAL(trafiklab::GetOperator("505000000000000001"), "sl", ());
  TEST_EQUAL(trafiklab::GetOperator("505000000000000012"), "skane", ());
  TEST_EQUAL(trafiklab::GetOperator("505000000000000022"), "dintur", ());
  // Västtrafik, without a feed; SJ, a national operator.
  TEST(trafiklab::GetOperator("505000000000000014").empty(), ());
  TEST(trafiklab::GetOperator("505000000000000100").empty(), ());
  TEST(trafiklab::GetOperator("500000000000000001").empty(), ());
  TEST(trafiklab::GetOperator("1").empty(), ());
  TEST(trafiklab::GetOperator("").empty(), ());
}

// Two trips: a walk, a train and a bus; and a single bus leg, written as objects instead of lists.
std::string_view constexpr kTrips = R"({
  "Trip": [
    {"idx": 1, "tripId": "C-1", "duration": "PT20M", "LegList": {"Leg": {
      "idx": 0, "name": "Länstrafik - Buss 4", "type": "JNY", "direction": "Radiohuset",
      "Product": {"name": "Länstrafik - Buss 4", "num": "4", "displayNumber": "4", "catCode": 7, "cls": 128},
      "Origin": {"name": "Odenplan", "lat": 59.3429, "lon": 18.0491, "date": "2026-10-12", "time": "08:30:00"},
      "Destination": {"name": "Radiohuset", "lat": 59.3330, "lon": 18.0233, "date": "2026-10-12", "time": "08:50:00"}
    }}},
    {"idx": 0, "tripId": "C-0", "duration": "PT47M", "LegList": {"Leg": [
      {"idx": 0, "name": "Promenad", "type": "WALK", "dist": 412, "duration": "PT6M",
       "Origin": {"name": "59.33, 18.06", "type": "ADR", "lat": 59.33, "lon": 18.06,
                  "date": "2026-10-12", "time": "08:01:00"},
       "Destination": {"name": "Stockholm Centralstation", "type": "ST", "extId": "740000001",
                       "lat": 59.330136, "lon": 18.058151, "date": "2026-10-12", "time": "08:07:00"}},
      {"idx": 1, "name": "Regional Tåg 231", "type": "JNY", "direction": "Uppsala Centralstation",
       "reachable": true, "duration": "PT38M", "JourneyStatus": "P",
       "Product": [{"name": "Regional Tåg 231", "num": "231", "displayNumber": "231", "line": "40",
                    "catCode": "2", "cls": "4", "catOutL": "Regional Tåg", "operator": "SJ"}],
       "Origin": {"name": "Stockholm Centralstation", "lat": 59.330136, "lon": 18.058151,
                  "date": "2026-10-12", "time": "08:10:00"},
       "Destination": {"name": "Uppsala Centralstation", "lat": 59.858, "lon": 17.645,
                       "date": "2026-10-12", "time": "08:48:00", "rtDate": "2026-10-12", "rtTime": "08:51:00"},
       "Stops": {"Stop": [
         {"name": "Stockholm Centralstation", "routeIdx": 0, "lat": 59.330136, "lon": 18.058151,
          "depTime": "08:10:00", "depDate": "2026-10-12"},
         {"name": "Knivsta station", "extId": "740000559", "routeIdx": 1, "lat": 59.72, "lon": 17.79,
          "arrTime": "08:35:00", "arrDate": "2026-10-12", "depTime": "08:36:00", "depDate": "2026-10-12"},
         {"name": "Uppsala Centralstation", "routeIdx": 2, "lat": 59.858, "lon": 17.645,
          "arrTime": "08:48:00", "arrDate": "2026-10-12", "rtArrTime": "08:51:00", "rtArrDate": "2026-10-12"}]},
       "JourneyDetailRef": {"ref": "1|65488|20|80|12102026"}}
    ]}}
  ],
  "scrB": "1|OB|MT",
  "scrF": "1|OF|MT"
})";

UNIT_TEST(Trafiklab_Journeys)
{
  auto const result = trafiklab::ParseJourneys(kTrips);
  TEST_EQUAL(result.m_status, Status::Ok, ());
  TEST_EQUAL(result.m_journeys.size(), 2, ());
  auto const at = [](auto time) { return local_days{2026y / October / 12} + 8h + time; };

  // Sorted by departure.
  auto const & train = result.m_journeys[0];
  TEST(train.GetDeparture() == at(1min), ());
  // The realtime arrival.
  TEST(train.GetArrival() == at(51min), ());
  TEST_EQUAL(train.m_legs.size(), 2, ());

  auto const & walk = train.m_legs[0];
  TEST(walk.m_walk, ());
  TEST_ALMOST_EQUAL_ABS(walk.m_distanceM, 412.0, 1e-9, ());
  TEST_EQUAL(walk.m_to.m_name, "Stockholm Centralstation", ());
  TEST_EQUAL(walk.m_path.size(), 2, ());
  TEST_EQUAL(walk.m_stops.size(), 2, ());

  auto const & ride = train.m_legs[1];
  TEST(!ride.m_walk, ());
  TEST_EQUAL(ride.m_mode, TransportMode::Train, ());
  // The line, rather than the train number.
  TEST_EQUAL(ride.m_line, "40", ());
  TEST_EQUAL(ride.m_direction, "Uppsala Centralstation", ());
  TEST(ride.m_from.m_time == at(10min), ());
  // Through the stop in between.
  TEST_EQUAL(ride.m_path.size(), 3, ());
  TEST_ALMOST_EQUAL_ABS(ride.m_path[1].m_lat, 59.72, 1e-9, ());
  // At the departure from the first stop, the arrivals at the others, realtime when known.
  TEST_EQUAL(ride.m_stops.size(), 3, ());
  TEST_EQUAL(ride.m_stops[1].m_name, "Knivsta station", ());
  TEST(ride.m_stops[0].m_time == at(10min), ());
  TEST(ride.m_stops[1].m_time == at(35min), ());
  TEST(ride.m_stops[2].m_time == at(51min), ());

  auto const & bus = result.m_journeys[1];
  TEST_EQUAL(bus.m_legs.size(), 1, ());
  TEST_EQUAL(bus.m_legs[0].m_mode, TransportMode::Bus, ());
  TEST_EQUAL(bus.m_legs[0].m_line, "4", ());
  // Without the stops listed.
  TEST_EQUAL(bus.m_legs[0].m_stops.size(), 2, ());

  TEST_EQUAL(result.m_earlier, "1|OB|MT", ());
  TEST_EQUAL(result.m_later, "1|OF|MT", ());
}

UNIT_TEST(Trafiklab_JourneysErrors)
{
  TEST_EQUAL(trafiklab::ParseJourneys(R"({"errorCode": "SVC_NO_RESULT", "errorText": "No trips found"})").m_status,
             Status::NoResult, ());
  TEST_EQUAL(trafiklab::ParseJourneys(R"({"errorCode": "SVC_LOC_ARR", "errorText": "Bad arrival"})").m_status,
             Status::NoResult, ());
  TEST_EQUAL(trafiklab::ParseJourneys(R"({"errorCode": "API_AUTH", "errorText": "Bad key"})").m_status,
             Status::Unauthorized, ());
  TEST_EQUAL(trafiklab::ParseJourneys(R"({"errorCode": "INT_ERR"})").m_status, Status::ServerError, ());
  // An XML error, as sent without a key.
  TEST_EQUAL(trafiklab::ParseJourneys(R"(<Error errorCode="API_PARAM"/>)").m_status, Status::ServerError, ());
  // No trips.
  auto const empty = trafiklab::ParseJourneys("{}");
  TEST_EQUAL(empty.m_status, Status::Ok, ());
  TEST(empty.m_journeys.empty(), ());
  TEST(empty.m_earlier.empty() && empty.m_later.empty(), ());
}

// Two departures of line 5 a minute apart, the other line at the same time as the first.
std::string_view constexpr kBoard = R"({
  "Departure": [
    {"name": "Länstrafik buss 2", "date": "2026-10-11", "time": "16:23:00", "direction": "Skövde Sjukhuset",
     "ProductAtStop": {"num": "2", "displayNumber": "2", "line": "2"},
     "Stops": {"Stop": [{"name": "Skövde Centralstation", "lat": 58.3909, "lon": 13.8532, "depDate": "2026-10-11",
                         "depTime": "16:23:00"},
                        {"name": "Sandtorget", "lat": 58.3915, "lon": 13.8465, "arrDate": "2026-10-11",
                         "arrTime": "16:24:00"}]}},
    {"name": "Länstrafik buss 5", "date": "2026-10-11", "time": "16:23:00", "direction": "Skövde Sjukhuset",
     "ProductAtStop": {"num": "5", "displayNumber": "5", "line": "5"},
     "Stops": {"Stop": [
       {"name": "Skövde Centralstation", "lat": 58.3909, "lon": 13.8532, "depDate": "2026-10-11",
        "depTime": "16:23:00"},
       {"name": "Mariesjö", "lat": 58.3936, "lon": 13.8696, "arrDate": "2026-10-11", "arrTime": "16:25:00",
        "rtArrDate": "2026-10-11", "rtArrTime": "16:27:00", "depDate": "2026-10-11", "depTime": "16:25:00"},
       {"name": "Sjukhuset", "lat": 58.4100, "lon": 13.8700, "arrDate": "2026-10-11", "arrTime": "16:40:00"}]}},
    {"name": "Länstrafik buss 5", "date": "2026-10-11", "time": "16:24:00",
     "ProductAtStop": {"num": "5", "displayNumber": "5"},
     "Stops": {"Stop": {"name": "Skövde Centralstation", "lat": 58.3909, "lon": 13.8532, "depDate": "2026-10-11",
                        "depTime": "16:24:00"}}}
  ]
})";

UNIT_TEST(Trafiklab_TripStops)
{
  Departure departure;
  departure.m_line = "5";
  departure.m_scheduled = *ParseLocalTime("2026-10-11T16:23");
  auto const result = trafiklab::ParseTripStops(kBoard, {departure})[0];
  TEST_EQUAL(result.m_status, Status::Ok, ());
  // After the board's own stop.
  TEST_EQUAL(result.m_stops.size(), 2, ());
  auto const & next = result.m_stops[0];
  TEST_EQUAL(next.m_name, "Mariesjö", ());
  TEST_ALMOST_EQUAL_ABS(next.m_latLon.m_lat, 58.3936, 1e-9, ());
  TEST(next.m_scheduled == *ParseLocalTime("2026-10-11T16:25"), ());
  TEST(next.m_realtime == ParseLocalTime("2026-10-11T16:27"), ());
  TEST(!result.m_stops[1].m_realtime, ());

  // With seconds, which ResRobot drops.
  departure.m_scheduled = *ParseLocalTime("2026-10-11T16:23:48");
  TEST_EQUAL(trafiklab::ParseTripStops(kBoard, {departure})[0].m_stops.size(), 2, ());

  departure.m_scheduled = *ParseLocalTime("2026-10-11T16:30");
  TEST_EQUAL(trafiklab::ParseTripStops(kBoard, {departure})[0].m_status, Status::NoResult, ());
  TEST_EQUAL(trafiklab::ParseTripStops(R"({"errorCode": "API_AUTH"})", {departure})[0].m_status, Status::Unauthorized,
             ());
  TEST_EQUAL(trafiklab::ParseTripStops("<Error/>", {departure})[0].m_status, Status::ServerError, ());

  // Several in one board, in their order.
  Departure two;
  two.m_line = "2";
  two.m_scheduled = *ParseLocalTime("2026-10-11T16:23");
  departure.m_scheduled = *ParseLocalTime("2026-10-11T16:23");
  auto const both = trafiklab::ParseTripStops(kBoard, {two, departure});
  TEST_EQUAL(both.size(), 2, ());
  TEST_EQUAL(both[0].m_stops.size(), 1, ());
  TEST_EQUAL(both[0].m_stops[0].m_name, "Sandtorget", ());
  TEST_EQUAL(both[1].m_stops.size(), 2, ());
}

UNIT_TEST(Trafiklab_Covers)
{
  TrafiklabProvider const provider({});
  TEST(provider.Covers({59.33, 18.06}), ("Stockholm"));
  TEST(provider.Covers({67.86, 20.22}), ("Kiruna"));
  TEST(!provider.Covers({52.52, 13.40}), ("Berlin"));
}
}  // namespace trafiklab_tests
