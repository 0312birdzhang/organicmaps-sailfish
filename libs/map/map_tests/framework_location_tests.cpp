#include "testing/testing.hpp"

#include "map/framework.hpp"

#include "geometry/mercator.hpp"

#include "platform/location.hpp"

namespace framework_location_tests
{
UNIT_TEST(Framework_HeadlessRouteFollowNeedsNoRenderer)
{
  Framework framework({}, false /* loadMaps */);
  auto & delegate = static_cast<RoutingManager::Delegate &>(framework);
  for (auto type : {routing::RouterType::Vehicle, routing::RouterType::Pedestrian, routing::RouterType::Bicycle})
    delegate.OnRouteFollow(type);
  TEST(!framework.IsDrapeEngineCreated(), ());
}

UNIT_TEST(Framework_HeadlessLocationOwnsPosition)
{
  Framework framework({}, false /* loadMaps */);
  TEST(!framework.IsDrapeEngineCreated(), ());
  TEST(!framework.GetCurrentPosition(), ());
  int updates = 0;
  framework.SetUserPositionListener([&updates](bool hasPosition)
  {
    TEST(hasPosition, ());
    ++updates;
  });

  location::GpsInfo info;
  info.m_source = location::EUser;
  info.m_horizontalAccuracy = 5;
  info.m_timestamp = 1700000000;
  for (double latitude : {22.5, 22.6, 22.7})
  {
    info.m_latitude = latitude;
    info.m_longitude = 113.5;
    info.m_timestamp += 1;
    framework.OnLocationUpdate(info);
    auto const position = framework.GetCurrentPosition();
    TEST(position, ());
    auto const expected = mercator::FromLatLon(info.m_latitude, info.m_longitude);
    TEST_ALMOST_EQUAL_ABS(position->x, expected.x, 1e-8, ());
    TEST_ALMOST_EQUAL_ABS(position->y, expected.y, 1e-8, ());
  }
  TEST_EQUAL(updates, 3, ());
  TEST(!framework.IsDrapeEngineCreated(), ());
}
}  // namespace framework_location_tests
