#include "testing/testing.hpp"

#include "live_transit/gtfs_realtime.hpp"

#include <bit>
#include <cstdint>
#include <string>

namespace gtfs_realtime_tests
{
using namespace live_transit;

// Protobuf encoding, to write the feeds of the tests.
std::string Varint(uint64_t value)
{
  std::string s;
  for (; value >= 0x80; value >>= 7)
    s += static_cast<char>((value & 0x7F) | 0x80);
  s += static_cast<char>(value);
  return s;
}

std::string Key(uint32_t field, uint8_t type)
{
  return Varint((field << 3) | type);
}

std::string Bytes(uint32_t field, std::string const & bytes)
{
  return Key(field, 2) + Varint(bytes.size()) + bytes;
}

std::string Float(uint32_t field, float value)
{
  auto const bits = std::bit_cast<uint32_t>(value);
  std::string s = Key(field, 5);
  for (int i = 0; i < 4; ++i)
    s += static_cast<char>((bits >> (8 * i)) & 0xFF);
  return s;
}

std::string VarintField(uint32_t field, uint64_t value)
{
  return Key(field, 0) + Varint(value);
}

std::string Entity(std::string const & vehicle)
{
  return Bytes(2, Bytes(1, "entity id") + VarintField(2, 0) + Bytes(4, vehicle));
}

UNIT_TEST(GtfsRealtime_VehiclePositions)
{
  std::string const header = Bytes(1, Bytes(1, "2.0") + VarintField(3, 1700000000));
  std::string const trip = Bytes(1, Bytes(1, "14010000731153414") + Bytes(3, "20261010") + VarintField(6, 1));
  std::string const feed =
      header +
      // Full, with fields that aren't read: a descriptor, the stop status and a double odometer.
      Entity(trip +
             Bytes(2, Float(1, 59.3428f) + Float(2, 18.0486f) + Float(3, 90.5f) + Key(4, 1) + std::string(8, 0) +
                          Float(5, 8.5f)) +
             VarintField(4, 2) + VarintField(5, 1700000005) + Bytes(8, Bytes(1, "9031008000500546"))) +
      // Without a bearing and a timestamp.
      Entity(Bytes(1, Bytes(1, "2")) + Bytes(2, Float(2, 17.5f) + Float(1, 60.0f))) +
      // Without a position or a trip: both are skipped.
      Entity(trip + VarintField(5, 1)) + Entity(Bytes(2, Float(1, 1.0f) + Float(2, 2.0f))) +
      // A trip update entity.
      Bytes(2, Bytes(1, "3") + Bytes(3, trip));

  std::vector<Vehicle> vehicles;
  TEST(gtfs_realtime::ParseVehiclePositions(feed, vehicles), ());
  TEST_EQUAL(vehicles.size(), 2, ());

  TEST_EQUAL(vehicles[0].m_tripId, "14010000731153414", ());
  TEST_ALMOST_EQUAL_ABS(vehicles[0].m_latLon.m_lat, 59.3428, 1e-5, ());
  TEST_ALMOST_EQUAL_ABS(vehicles[0].m_latLon.m_lon, 18.0486, 1e-5, ());
  TEST(vehicles[0].m_bearing, ());
  TEST_ALMOST_EQUAL_ABS(*vehicles[0].m_bearing, 90.5, 1e-5, ());
  TEST(vehicles[0].m_speed, ());
  TEST_ALMOST_EQUAL_ABS(*vehicles[0].m_speed, 8.5, 1e-5, ());
  TEST_EQUAL(vehicles[0].m_timestamp, 1700000005, ());

  TEST_EQUAL(vehicles[1].m_tripId, "2", ());
  TEST_ALMOST_EQUAL_ABS(vehicles[1].m_latLon.m_lat, 60.0, 1e-5, ());
  TEST_ALMOST_EQUAL_ABS(vehicles[1].m_latLon.m_lon, 17.5, 1e-5, ());
  TEST(!vehicles[1].m_bearing, ());
  TEST(!vehicles[1].m_speed, ());
  TEST_EQUAL(vehicles[1].m_timestamp, 0, ());
}

UNIT_TEST(GtfsRealtime_Malformed)
{
  std::vector<Vehicle> vehicles;
  TEST(gtfs_realtime::ParseVehiclePositions("", vehicles), ("An empty feed"));
  TEST(vehicles.empty(), ());

  std::string const entity = Entity(Bytes(1, Bytes(1, "1")) + Bytes(2, Float(1, 1.0f) + Float(2, 2.0f)));
  TEST(gtfs_realtime::ParseVehiclePositions(entity, vehicles), ());
  TEST_EQUAL(vehicles.size(), 1, ());
  // Truncated.
  TEST(!gtfs_realtime::ParseVehiclePositions(entity.substr(0, entity.size() - 1), vehicles), ());
  // An unterminated varint.
  TEST(!gtfs_realtime::ParseVehiclePositions(std::string(1, '\x80'), vehicles), ());
  // A length past the end.
  TEST(!gtfs_realtime::ParseVehiclePositions(Key(2, 2) + Varint(100), vehicles), ());
  // A deprecated group.
  TEST(!gtfs_realtime::ParseVehiclePositions(Key(2, 3), vehicles), ());
  // An HTML error page instead of protobuf.
  TEST(!gtfs_realtime::ParseVehiclePositions("<html>Forbidden</html>", vehicles), ());
}
}  // namespace gtfs_realtime_tests
