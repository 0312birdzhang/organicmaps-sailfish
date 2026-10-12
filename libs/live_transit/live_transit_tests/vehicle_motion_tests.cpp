#include "testing/testing.hpp"

#include "live_transit/vehicle_motion.hpp"

#include "geometry/distance_on_sphere.hpp"
#include "geometry/mercator.hpp"

#include "base/math.hpp"

#include <algorithm>
#include <cmath>

namespace vehicle_motion_tests
{
using namespace live_transit;

double constexpr kT0 = 1700000000.0;

// 1 km east along the 59th parallel, then 1 km north.
ms::LatLon const kStart(59.0, 18.0);
ms::LatLon const kCorner(59.0, 18.0 + 1000.0 / (111320.0 * std::cos(math::DegToRad(59.0))));
ms::LatLon const kEnd(59.0 + 1000.0 / 111320.0, kCorner.m_lon);

VehicleMotion::Route LPath()
{
  return {.m_paths = {{mercator::FromLatLon(kStart), mercator::FromLatLon(kCorner), mercator::FromLatLon(kEnd)}},
          .m_stops = {}};
}

// Far away, not to stop anything.
ms::LatLon const kFarStop(60.0, 20.0);

ms::LatLon East(double meters)
{
  return {kStart.m_lat, kStart.m_lon + meters / (111320.0 * std::cos(math::DegToRad(kStart.m_lat)))};
}

Vehicle Report(ms::LatLon const & latLon, std::optional<double> bearing, std::optional<double> speed, double time)
{
  return Vehicle{.m_tripId = "1",
                 .m_latLon = latLon,
                 .m_bearing = bearing,
                 .m_speed = speed,
                 .m_timestamp = static_cast<uint64_t>(time)};
}

double Meters(ms::LatLon const & a, ms::LatLon const & b)
{
  return ms::DistanceOnEarth(a, b);
}

UNIT_TEST(VehicleMotion_AlongPath)
{
  VehicleMotion motion(LPath(), kFarStop);
  motion.Report(Report(kStart, 90.0, 10.0, kT0), kT0);
  TEST_LESS(Meters(motion.Get(kT0).m_latLon, kStart), 1.0, ());
  // 100 m on after 10 s, heading east.
  auto const state = motion.Get(kT0 + 10);
  TEST_LESS(Meters(state.m_latLon, East(100)), 2.0, ());
  TEST(state.m_bearing, ());
  TEST_ALMOST_EQUAL_ABS(*state.m_bearing, 90.0, 1.0, ());
}

UNIT_TEST(VehicleMotion_RoundTheCorner)
{
  VehicleMotion motion(LPath(), kFarStop);
  // From 800 m along, at 20 m/s for 15 s: 100 m past the corner, heading north.
  motion.Report(Report(East(800), 90.0, 20.0, kT0), kT0);
  auto const state = motion.Get(kT0 + 15);
  ms::LatLon const expected(kCorner.m_lat + 100.0 / 111320.0, kCorner.m_lon);
  TEST_LESS(Meters(state.m_latLon, expected), 3.0, ());
  TEST_ALMOST_EQUAL_ABS(*state.m_bearing, 0.0, 1.0, ());
}

UNIT_TEST(VehicleMotion_AgainstPath)
{
  VehicleMotion motion(LPath(), kFarStop);
  // Heading west on the eastward path, a little beside it.
  ms::LatLon const beside(East(500).m_lat + 20.0 / 111320.0, East(500).m_lon);
  motion.Report(Report(beside, 270.0, 10.0, kT0), kT0);
  auto const state = motion.Get(kT0 + 10 + 10);
  // On the path, 200 m back from the report.
  TEST_LESS(Meters(state.m_latLon, East(300)), 3.0, ());
  TEST_ALMOST_EQUAL_ABS(*state.m_bearing, 270.0, 1.0, ());
}

UNIT_TEST(VehicleMotion_StopsAtTheStop)
{
  VehicleMotion motion(LPath(), East(300));
  motion.Report(Report(kStart, 90.0, 20.0, kT0), kT0);
  TEST_LESS(Meters(motion.Get(kT0 + 25).m_latLon, East(300)), 2.0, ());

  // Once past it, it goes on.
  motion.Report(Report(East(350), 90.0, 20.0, kT0 + 30), kT0 + 30);
  TEST_LESS(Meters(motion.Get(kT0 + 40).m_latLon, East(550)), 2.0, ());
}

UNIT_TEST(VehicleMotion_NotForever)
{
  VehicleMotion motion(LPath(), kFarStop);
  motion.Report(Report(kStart, 90.0, 10.0, kT0), kT0);
  TEST_LESS(Meters(motion.Get(kT0 + 100).m_latLon, East(300)), 2.0, ());
}

UNIT_TEST(VehicleMotion_EasesOntoReports)
{
  VehicleMotion motion(LPath(), kFarStop);
  motion.Report(Report(kStart, 90.0, 10.0, kT0), kT0);
  auto const before = motion.Get(kT0 + 15).m_latLon;
  TEST_LESS(Meters(before, East(150)), 2.0, ());

  // Faster than thought: 250 m on.
  motion.Report(Report(East(250), 90.0, 10.0, kT0 + 15), kT0 + 15);
  // No jump...
  TEST_LESS(Meters(motion.Get(kT0 + 15).m_latLon, before), 1.0, ());
  // ...but soon there.
  TEST_LESS(Meters(motion.Get(kT0 + 20).m_latLon, East(300)), 2.0, ());
}

// Meters east of the start, of a point near the eastward leg.
double Along(ms::LatLon const & latLon)
{
  return (latLon.m_lon - kStart.m_lon) * 111320.0 * std::cos(math::DegToRad(kStart.m_lat));
}

UNIT_TEST(VehicleMotion_WaitsWhenJustBehind)
{
  VehicleMotion motion(LPath(), kFarStop);
  motion.Report(Report(kStart, 90.0, 10.0, kT0), kT0);
  // Shown at 150 m, but it is 30 m behind: it slows down rather than going back...
  motion.Report(Report(East(120), 90.0, 10.0, kT0 + 15), kT0 + 15);
  double last = Along(motion.Get(kT0 + 15).m_latLon);
  TEST_ALMOST_EQUAL_ABS(last, 150.0, 1.0, ());
  for (double t = kT0 + 15.25; t <= kT0 + 30; t += 0.25)
  {
    double const along = Along(motion.Get(t).m_latLon);
    TEST_GREATER_OR_EQUAL(along, last - 1e-6, (t - kT0));
    last = along;
  }
  // ...and is soon where it should be.
  TEST_LESS(Meters(motion.Get(kT0 + 30).m_latLon, East(270)), 2.0, ());
}

UNIT_TEST(VehicleMotion_GoesBackWhenFarBehind)
{
  VehicleMotion motion(LPath(), kFarStop);
  motion.Report(Report(kStart, 90.0, 10.0, kT0), kT0);
  // 150 m behind would take too long to wait for.
  motion.Report(Report(East(0.0), 90.0, 10.0, kT0 + 15), kT0 + 15);
  TEST_LESS(Meters(motion.Get(kT0 + 20).m_latLon, East(50)), 2.0, ());
}

UNIT_TEST(VehicleMotion_GoesBackWhenStopped)
{
  VehicleMotion motion(LPath(), kFarStop);
  motion.Report(Report(kStart, 90.0, 10.0, kT0), kT0);
  // Stopped 20 m behind: nothing to wait for.
  motion.Report(Report(East(130), 90.0, 0.0, kT0 + 15), kT0 + 15);
  TEST_LESS(Meters(motion.Get(kT0 + 20).m_latLon, East(130)), 2.0, ());
}

UNIT_TEST(VehicleMotion_SpeedFromReports)
{
  VehicleMotion motion(LPath(), kFarStop);
  // Without a speed or a bearing it stays...
  motion.Report(Report(kStart, std::nullopt, std::nullopt, kT0), kT0);
  TEST_LESS(Meters(motion.Get(kT0 + 10).m_latLon, kStart), 1.0, ());
  // ...until it moves: 150 m in 15 s.
  motion.Report(Report(East(150), std::nullopt, std::nullopt, kT0 + 15), kT0 + 15);
  TEST_LESS(Meters(motion.Get(kT0 + 25).m_latLon, East(250)), 2.0, ());
}

UNIT_TEST(VehicleMotion_PathLater)
{
  // Straight on north-east, beside the eastward path...
  VehicleMotion motion({}, kFarStop);
  motion.Report(Report(East(100), 45.0, 10.0, kT0), kT0);
  auto const before = motion.Get(kT0 + 5).m_latLon;
  // ...until the route comes: no jump, then along it.
  motion.SetRoute(LPath(), kT0 + 5);
  TEST_LESS(Meters(motion.Get(kT0 + 5).m_latLon, before), 1.0, ());
  TEST_LESS(Meters(motion.Get(kT0 + 15).m_latLon, East(250)), 2.0, ());
}

UNIT_TEST(VehicleMotion_IsApproaching)
{
  // Served from the start to the end, with the stop halfway along the eastward leg.
  auto route = LPath();
  for (auto const & stop : {kStart, East(500), kCorner, kEnd})
    route.m_stops.push_back(mercator::FromLatLon(stop));
  route.m_ordered = true;

  auto const approaching = [&](ms::LatLon const & latLon, double bearing)
  {
    VehicleMotion motion(route, East(500));
    motion.Report(Report(latLon, bearing, 10.0, kT0), kT0);
    return motion.IsApproaching();
  };
  TEST_EQUAL(approaching(East(200), 90.0), true, ());
  // Still at the stop.
  TEST_EQUAL(approaching(East(510), 90.0), true, ());
  // The other way, though towards the stop.
  TEST_EQUAL(approaching(East(800), 270.0), false, ());
  TEST_EQUAL(approaching(East(200), 270.0), false, ());
  // Past it.
  TEST_EQUAL(approaching(East(800), 90.0), false, ());

  // Served the other way.
  std::ranges::reverse(route.m_stops);
  TEST_EQUAL(approaching(East(800), 270.0), true, ());
  TEST_EQUAL(approaching(East(200), 90.0), false, ());

  // Unknown without the order, or off the route.
  route.m_ordered = false;
  TEST_EQUAL(approaching(East(200), 90.0), std::nullopt, ());
  VehicleMotion motion(LPath(), East(500));
  motion.Report(Report(kFarStop, 90.0, 10.0, kT0), kT0);
  TEST_EQUAL(motion.IsApproaching(), std::nullopt, ());
}

UNIT_TEST(VehicleMotion_BrakesForStops)
{
  // A stop on the way, in no known order.
  auto route = LPath();
  route.m_stops.push_back(mercator::FromLatLon(East(500)));

  VehicleMotion motion(route, kFarStop);
  // At 10 m/s it brakes over the last 50 m, for 10 s, so it slows down but goes on.
  motion.Report(Report(East(400), 90.0, 10.0, kT0), kT0);
  TEST_ALMOST_EQUAL_ABS(Along(motion.Get(kT0 + 5).m_latLon), 450.0, 1.0, ());
  TEST_ALMOST_EQUAL_ABS(Along(motion.Get(kT0 + 10).m_latLon), 487.5, 1.0, ());
  double last = 450.0;
  for (double t = kT0 + 5.5; t <= kT0 + 25; t += 0.5)
  {
    double const along = Along(motion.Get(t).m_latLon);
    TEST_GREATER_OR_EQUAL(along, last - 1e-6, (t - kT0));
    TEST_LESS_OR_EQUAL(along, 500.0 + 1.0, (t - kT0));
    last = along;
  }
  // Halted at the stop.
  TEST_ALMOST_EQUAL_ABS(Along(motion.Get(kT0 + 25).m_latLon), 500.0, 1.0, ());

  // Too near to brake gently, it brakes harder.
  motion.Report(Report(East(480), 90.0, 10.0, kT0 + 30), kT0 + 30);
  TEST_ALMOST_EQUAL_ABS(Along(motion.Get(kT0 + 40).m_latLon), 500.0, 1.0, ());

  // At the stop, it is leaving it.
  VehicleMotion leaving(route, kFarStop);
  leaving.Report(Report(East(498), 90.0, 10.0, kT0), kT0);
  TEST_ALMOST_EQUAL_ABS(Along(leaving.Get(kT0 + 5).m_latLon), 548.0, 1.0, ());
}

UNIT_TEST(VehicleMotion_OffPath)
{
  // Straight on without a route.
  VehicleMotion motion({}, kFarStop);
  motion.Report(Report(kStart, 0.0, 10.0, kT0), kT0);
  ms::LatLon const north(kStart.m_lat + 100.0 / 111320.0, kStart.m_lon);
  TEST_LESS(Meters(motion.Get(kT0 + 10).m_latLon, north), 2.0, ());
  TEST_ALMOST_EQUAL_ABS(*motion.Get(kT0 + 10).m_bearing, 0.0, 1e-9, ());

  // Standing still.
  VehicleMotion still({}, kFarStop);
  still.Report(Report(kStart, 0.0, 0.0, kT0), kT0);
  TEST_LESS(Meters(still.Get(kT0 + 10).m_latLon, kStart), 1e-3, ());
}
}  // namespace vehicle_motion_tests
