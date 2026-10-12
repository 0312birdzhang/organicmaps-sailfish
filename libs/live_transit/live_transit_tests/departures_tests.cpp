#include "testing/testing.hpp"

#include "live_transit/departures.hpp"

namespace departures_tests
{
using namespace live_transit;
using namespace std::chrono;

UNIT_TEST(ParseLocalTime_Formats)
{
  auto const expected = local_days{2025y / April / 1} + 16h + 5min;
  TEST(ParseLocalTime("2025-04-01T16:05") == expected, ());
  TEST(ParseLocalTime("2025-04-01T16:05:00") == expected, ());
  TEST(ParseLocalTime("2025-04-01 16:05:30") == expected + 30s, ());
  // A fraction and a time zone are ignored: departures are compared with the provider's own clock.
  TEST(ParseLocalTime("2025-04-01T16:05:30.123+02:00") == expected + 30s, ());
  TEST(ParseLocalTime("2025-04-01T16:05Z") == expected, ());
}

UNIT_TEST(ParseLocalTime_Invalid)
{
  TEST(!ParseLocalTime(""), ());
  TEST(!ParseLocalTime("2025-04-01"), ());
  TEST(!ParseLocalTime("2025-04-01T16"), ());
  TEST(!ParseLocalTime("2025-13-01T16:05"), ());
  TEST(!ParseLocalTime("2025-02-30T16:05"), ());
  TEST(!ParseLocalTime("2025-04-01T24:00"), ());
  TEST(!ParseLocalTime("2025/04/01T16:05"), ());
  TEST(!ParseLocalTime("2025-04-01T16:05:x0"), ());
}

UNIT_TEST(KeepNearestPlatform)
{
  // A platform on each side of the street, 30 m apart, and one around the corner.
  ms::LatLon const north(59.34830, 18.0270);
  ms::LatLon const south(59.34803, 18.0270);
  ms::LatLon const corner(59.34800, 18.0280);
  auto const departures = [&](std::vector<std::optional<ms::LatLon>> const & platforms)
  {
    Departures result;
    for (auto const & platform : platforms)
      result.m_departures.push_back({.m_line = "1", .m_platformLatLon = platform});
    return result;
  };
  auto const count = [](Departures const & d) { return d.m_departures.size(); };

  auto both = departures({north, south, corner, north});
  KeepPlatform(both, {{59.34828, 18.02702}, {}, {}, {}});
  TEST_EQUAL(count(both), 2, ());
  TEST(both.m_departures[1].m_platformLatLon == north, ());

  // Far from all.
  auto far = departures({north, south});
  KeepPlatform(far, {{59.3500, 18.0270}, {}, {}, {}});
  TEST_EQUAL(count(far), 2, ());

  // A platform unknown.
  auto unknown = departures({north, std::nullopt});
  KeepPlatform(unknown, {north, {}, {}, {}});
  TEST_EQUAL(count(unknown), 2, ());
}

UNIT_TEST(KeepPlatform_RefAndLines)
{
  // All at one place, as providers often have them.
  ms::LatLon const stop(59.348, 18.027);
  auto const departures = [&]
  {
    Departures result;
    for (auto const & [line, platform] :
         std::vector<std::pair<std::string, std::string>>{{"1", "A"}, {"4", "B"}, {"1", "B"}, {"72", "C"}})
    {
      result.m_departures.push_back({.m_line = line, .m_platform = platform, .m_platformLatLon = stop});
    }
    return result;
  };
  auto const lines = [](Departures const & d)
  {
    std::vector<std::string> result;
    for (auto const & departure : d.m_departures)
      result.push_back(departure.m_line);
    return result;
  };

  auto byRef = departures();
  KeepPlatform(byRef, {stop, "b", {"1"}, {}});
  TEST_EQUAL(lines(byRef), std::vector<std::string>({"4", "1"}), ());

  // The ref is unknown to the provider: by the lines.
  auto byLines = departures();
  KeepPlatform(byLines, {stop, "13", {"1", "72"}, {}});
  TEST_EQUAL(lines(byLines), std::vector<std::string>({"1", "1", "72"}), ());

  // None of the lines calls: all stay.
  auto none = departures();
  KeepPlatform(none, {stop, {}, {"99"}, {}});
  TEST_EQUAL(lines(none).size(), 4, ());
}

UNIT_TEST(KeepPlatform_Direction)
{
  ms::LatLon const stop(59.348, 18.027);
  auto const departures = [&]
  {
    Departures result;
    for (auto const & [line, destination] : std::vector<std::pair<std::string, std::string>>{
             {"3", "Karolinska sjukhuset norra"}, {"3", "Södersjukhuset"}, {"3", "Fridhemsplan"}, {"4", "Gullmarsplan"}})
    {
      result.m_departures.push_back({.m_line = line, .m_destination = destination, .m_platformLatLon = stop});
    }
    return result;
  };
  auto const destinations = [](Departures const & d)
  {
    std::vector<std::string> result;
    for (auto const & departure : d.m_departures)
      result.push_back(departure.m_destination);
    return result;
  };

  // Towards where line 3 through the platform comes from; a short trip the same way stays, as do other lines.
  auto southbound = departures();
  KeepPlatform(southbound, {stop, {}, {}, {{"3", "Karolinska sjukhuset", "Södersjukhuset"}}});
  TEST_EQUAL(destinations(southbound), std::vector<std::string>({"Södersjukhuset", "Fridhemsplan", "Gullmarsplan"}),
             ());

  // A ring: both ends are one place.
  auto ring = departures();
  KeepPlatform(ring, {stop, {}, {}, {{"3", "Karolinska sjukhuset", "KAROLINSKA SJUKHUSET"}}});
  TEST_EQUAL(destinations(ring).size(), 4, ());

  // Without the ends' names.
  auto unnamed = departures();
  KeepPlatform(unnamed, {stop, {}, {}, {{"3", {}, {}}}});
  TEST_EQUAL(destinations(unnamed).size(), 4, ());
}

UNIT_TEST(IsSamePlace)
{
  TEST(IsSamePlace("Karolinska sjukhuset norra", "Karolinska sjukhuset"), ());
  TEST(IsSamePlace("radiohuset", "Radiohuset (Oxenstiernsgatan)"), ());
  TEST(IsSamePlace("SÖDERSJUKHUSET", "Södersjukhuset"), ());
  TEST(!IsSamePlace("Ropsten", "Liljeholmen"), ());
  TEST(!IsSamePlace("", "Ropsten"), ());
}

UNIT_TEST(IsApproaching)
{
  ms::LatLon const stop(59.3480, 18.0270);
  auto const vehicle = [](double lat, double lon, std::optional<double> bearing)
  { return Vehicle{.m_tripId = "1", .m_latLon = {lat, lon}, .m_bearing = bearing}; };
  // 1 km south, heading north, north-east and west.
  TEST(IsApproaching(vehicle(59.3390, 18.0270, 0.0), stop), ());
  TEST(IsApproaching(vehicle(59.3390, 18.0270, 45.0), stop), ());
  TEST(!IsApproaching(vehicle(59.3390, 18.0270, 270.0 - 1.0), stop), ());
  // Heading south, on the trip before.
  TEST(!IsApproaching(vehicle(59.3390, 18.0270, 180.0), stop), ());
  // East of the stop, heading west and east.
  TEST(IsApproaching(vehicle(59.3480, 18.0450, 270.0), stop), ());
  TEST(!IsApproaching(vehicle(59.3480, 18.0450, 90.0), stop), ());
  // Near, or without a heading.
  TEST(IsApproaching(vehicle(59.3470, 18.0270, 180.0), stop), ());
  TEST(IsApproaching(vehicle(59.3390, 18.0270, {}), stop), ());
}
UNIT_TEST(IsApproaching_NextStop)
{
  // The trip goes on east, 500 m to its next stop.
  ms::LatLon const stop(59.3480, 18.0270);
  ms::LatLon const next(59.3480, 18.0358);
  auto const vehicle = [](double lat, double lon, std::optional<double> bearing)
  { return Vehicle{.m_tripId = "1", .m_latLon = {lat, lon}, .m_bearing = bearing}; };
  // West of the stop heading east, and near it, at it.
  TEST(IsApproaching(vehicle(59.3480, 18.0180, 90.0), stop, next), ());
  TEST(IsApproaching(vehicle(59.3481, 18.0265, 90.0), stop, next), ());
  TEST(IsApproaching(vehicle(59.3480, 18.0273, 90.0), stop, next), ());
  // On the trip before: from the east heading west, near too, or west of it heading away.
  TEST(!IsApproaching(vehicle(59.3480, 18.0330, 270.0), stop, next), ());
  TEST(!IsApproaching(vehicle(59.3480, 18.0280, 270.0), stop, next), ());
  TEST(!IsApproaching(vehicle(59.3480, 18.0180, 270.0), stop, next), ());
  // Without a heading, by the side only.
  TEST(IsApproaching(vehicle(59.3480, 18.0180, {}), stop, next), ());
  TEST(!IsApproaching(vehicle(59.3480, 18.0330, {}), stop, next), ());
  // A next stop too near tells nothing: as without it.
  TEST(IsApproaching(vehicle(59.3480, 18.0330, 270.0), stop, {59.3480, 18.0271}), ());
}
}  // namespace departures_tests
