#include "live_transit/gtfs_realtime.hpp"

#include <bit>
#include <cstring>

namespace live_transit
{
namespace gtfs_realtime
{
namespace
{
// Just enough protobuf wire format for the fields read here, see
// https://gtfs.org/documentation/realtime/proto/ for the field numbers.
class Reader
{
public:
  enum WireType : uint8_t
  {
    Varint = 0,
    Fixed64 = 1,
    Bytes = 2,
    Fixed32 = 5,
  };

  explicit Reader(std::string_view data) : m_data(data) {}

  bool AtEnd() const { return m_pos == m_data.size(); }

  bool ReadKey(uint32_t & field, WireType & type)
  {
    uint64_t key;
    if (!ReadVarint(key))
      return false;
    field = static_cast<uint32_t>(key >> 3);
    type = static_cast<WireType>(key & 7);
    return true;
  }

  bool ReadVarint(uint64_t & value)
  {
    value = 0;
    for (int shift = 0; shift < 64 && m_pos < m_data.size(); shift += 7)
    {
      auto const byte = static_cast<uint8_t>(m_data[m_pos++]);
      value |= static_cast<uint64_t>(byte & 0x7F) << shift;
      if ((byte & 0x80) == 0)
        return true;
    }
    return false;
  }

  bool ReadBytes(std::string_view & bytes)
  {
    uint64_t size;
    if (!ReadVarint(size) || size > m_data.size() - m_pos)
      return false;
    bytes = m_data.substr(m_pos, size);
    m_pos += size;
    return true;
  }

  bool ReadFloat(float & value)
  {
    if (m_data.size() - m_pos < sizeof(uint32_t))
      return false;
    uint32_t bits;
    std::memcpy(&bits, m_data.data() + m_pos, sizeof(bits));
    // Little-endian on the wire, as on all supported platforms.
    value = std::bit_cast<float>(bits);
    m_pos += sizeof(bits);
    return true;
  }

  bool Skip(WireType type)
  {
    uint64_t varint;
    std::string_view bytes;
    switch (type)
    {
    case Varint: return ReadVarint(varint);
    case Bytes: return ReadBytes(bytes);
    case Fixed64: return Advance(8);
    case Fixed32: return Advance(4);
    }
    // Deprecated groups aren't used by GTFS Realtime.
    return false;
  }

private:
  bool Advance(size_t size)
  {
    if (m_data.size() - m_pos < size)
      return false;
    m_pos += size;
    return true;
  }

  std::string_view m_data;
  size_t m_pos = 0;
};

// Reads the message's fields with |onField|, which returns false for a malformed field, or skips them when it
// doesn't handle the field (returns std::nullopt).
template <typename OnField>
bool ReadMessage(std::string_view data, OnField && onField)
{
  Reader reader(data);
  while (!reader.AtEnd())
  {
    uint32_t field;
    Reader::WireType type;
    if (!reader.ReadKey(field, type))
      return false;
    std::optional<bool> const handled = onField(reader, field, type);
    if (handled ? !*handled : !reader.Skip(type))
      return false;
  }
  return true;
}

bool ParseTrip(std::string_view data, std::string & tripId)
{
  return ReadMessage(data, [&](Reader & reader, uint32_t field, Reader::WireType type) -> std::optional<bool>
  {
    if (field != 1 || type != Reader::Bytes)
      return {};
    std::string_view id;
    if (!reader.ReadBytes(id))
      return false;
    tripId = id;
    return true;
  });
}

bool ParsePosition(std::string_view data, Vehicle & vehicle, bool & hasPosition)
{
  float lat = 0, lon = 0;
  bool hasLat = false, hasLon = false;
  bool const ok = ReadMessage(data, [&](Reader & reader, uint32_t field, Reader::WireType type) -> std::optional<bool>
  {
    if (type != Reader::Fixed32)
      return {};
    float value;
    switch (field)
    {
    case 1: hasLat = true; return reader.ReadFloat(lat);
    case 2: hasLon = true; return reader.ReadFloat(lon);
    case 3:
      if (!reader.ReadFloat(value))
        return false;
      vehicle.m_bearing = value;
      return true;
    case 5:
      if (!reader.ReadFloat(value))
        return false;
      vehicle.m_speed = value;
      return true;
    }
    return {};
  });
  hasPosition = hasLat && hasLon;
  vehicle.m_latLon = {lat, lon};
  return ok;
}

bool ParseVehicle(std::string_view data, Vehicle & vehicle, bool & complete)
{
  bool hasPosition = false;
  bool const ok = ReadMessage(data, [&](Reader & reader, uint32_t field, Reader::WireType type) -> std::optional<bool>
  {
    std::string_view bytes;
    switch (field)
    {
    case 1:
      if (type != Reader::Bytes)
        return {};
      return reader.ReadBytes(bytes) && ParseTrip(bytes, vehicle.m_tripId);
    case 2:
      if (type != Reader::Bytes)
        return {};
      return reader.ReadBytes(bytes) && ParsePosition(bytes, vehicle, hasPosition);
    case 5:
      if (type != Reader::Varint)
        return {};
      return reader.ReadVarint(vehicle.m_timestamp);
    }
    return {};
  });
  complete = hasPosition && !vehicle.m_tripId.empty();
  return ok;
}
}  // namespace

bool ParseVehiclePositions(std::string_view feed, std::vector<Vehicle> & vehicles)
{
  vehicles.clear();
  // FeedMessage.entity, then FeedEntity.vehicle.
  return ReadMessage(feed, [&](Reader & reader, uint32_t field, Reader::WireType type) -> std::optional<bool>
  {
    if (field != 2 || type != Reader::Bytes)
      return {};
    std::string_view entity;
    if (!reader.ReadBytes(entity))
      return false;
    return ReadMessage(entity, [&](Reader & reader, uint32_t field, Reader::WireType type) -> std::optional<bool>
    {
      if (field != 4 || type != Reader::Bytes)
        return {};
      std::string_view data;
      Vehicle vehicle;
      bool complete;
      if (!reader.ReadBytes(data) || !ParseVehicle(data, vehicle, complete))
        return false;
      if (complete)
        vehicles.push_back(std::move(vehicle));
      return true;
    });
  });
}
}  // namespace gtfs_realtime
}  // namespace live_transit
