#include "live_transit/departures.hpp"

#include "geometry/distance_on_sphere.hpp"
#include "geometry/mercator.hpp"

#include "base/math.hpp"
#include "base/string_utils.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace live_transit
{
namespace
{
bool ParseNumber(std::string_view s, size_t pos, size_t size, int & value)
{
  if (pos + size > s.size())
    return false;
  auto const * begin = s.data() + pos;
  auto const * end = begin + size;
  auto const [ptr, ec] = std::from_chars(begin, end, value);
  return ec == std::errc() && ptr == end;
}

void KeepNearest(Departures & departures, ms::LatLon const & latLon)
{
  // The map and the provider place a platform this far apart at the most.
  double constexpr kMaxDistanceM = 50.0;
  // Closer together is one platform, e.g. the poles of a long one.
  double constexpr kSamePlatformM = 10.0;
  auto & list = departures.m_departures;
  double nearest = kMaxDistanceM;
  for (auto const & departure : list)
  {
    if (!departure.m_platformLatLon)
      return;
    nearest = std::min(nearest, ms::DistanceOnEarth(*departure.m_platformLatLon, latLon));
  }
  if (nearest == kMaxDistanceM)
    return;
  std::erase_if(list, [&](Departure const & departure)
  { return ms::DistanceOnEarth(*departure.m_platformLatLon, latLon) > nearest + kSamePlatformM; });
}

template <typename Pred>
bool KeepIfAny(std::vector<Departure> & list, Pred && pred)
{
  if (std::ranges::none_of(list, pred))
    return false;
  std::erase_if(list, [&](Departure const & departure) { return !pred(departure); });
  return true;
}
}  // namespace

void KeepPlatform(Departures & departures, StopPlatform const & platform)
{
  KeepNearest(departures, platform.m_latLon);
  auto const atRef = [&](Departure const & departure)
  { return strings::EqualAsciiNoCase(departure.m_platform, platform.m_ref); };
  auto const ofLines = [&](Departure const & departure)
  {
    return std::ranges::any_of(
        platform.m_lines, [&](std::string const & line) { return strings::EqualAsciiNoCase(departure.m_line, line); });
  };
  auto & list = departures.m_departures;
  if (platform.m_ref.empty() || !KeepIfAny(list, atRef))
    if (!platform.m_lines.empty())
      KeepIfAny(list, ofLines);

  auto const thisWay = [&](Departure const & departure)
  {
    bool otherWay = false;
    for (auto const & route : platform.m_routes)
    {
      if (!strings::EqualAsciiNoCase(route.m_line, departure.m_line))
        continue;
      if (IsSamePlace(departure.m_destination, route.m_to))
        return true;
      otherWay = otherWay || IsSamePlace(departure.m_destination, route.m_from);
    }
    return !otherWay;
  };
  KeepIfAny(list, thisWay);
}

bool IsSamePlace(std::string_view a, std::string_view b)
{
  if (a.empty() || b.empty())
    return false;
  std::string lowerA(a);
  std::string lowerB(b);
  strings::MakeLowerCaseInplace(lowerA);
  strings::MakeLowerCaseInplace(lowerB);
  return lowerA.find(lowerB) != std::string::npos || lowerB.find(lowerA) != std::string::npos;
}

bool IsApproaching(Vehicle const & vehicle, ms::LatLon const & stop)
{
  double constexpr kNearM = 300.0;
  if (!vehicle.m_bearing || ms::DistanceOnEarth(vehicle.m_latLon, stop) <= kNearM)
    return true;
  // Flat is fine for the angle at these distances.
  double const east = (stop.m_lon - vehicle.m_latLon.m_lon) * std::cos(math::DegToRad(vehicle.m_latLon.m_lat));
  double const north = stop.m_lat - vehicle.m_latLon.m_lat;
  double const toStop = math::RadToDeg(std::atan2(east, north));
  double const off = std::abs(std::remainder(*vehicle.m_bearing - toStop, 360.0));
  return off <= 90.0;
}

bool IsApproaching(Vehicle const & vehicle, ms::LatLon const & stop, ms::LatLon const & next)
{
  // Reported a little past the stop, it may still be at it.
  double constexpr kAtStopM = 30.0;
  // Nearer, the way between them tells little.
  double constexpr kMinWayM = 20.0;
  double const wayM = ms::DistanceOnEarth(stop, next);
  if (wayM < kMinWayM)
    return IsApproaching(vehicle, stop);
  // Mercator keeps the angles, so the signs, and its scale is the same both ways at a point.
  m2::PointD const origin = mercator::FromLatLon(stop);
  m2::PointD const way = (mercator::FromLatLon(next) - origin).Normalize();
  double const metersPerUnit = wayM / mercator::FromLatLon(next).Length(origin);
  if (m2::DotProduct(mercator::FromLatLon(vehicle.m_latLon) - origin, way) * metersPerUnit > kAtStopM)
    return false;
  if (!vehicle.m_bearing)
    return true;
  double const radians = math::DegToRad(*vehicle.m_bearing);
  return m2::DotProduct(m2::PointD(std::sin(radians), std::cos(radians)), way) > 0;
}

std::optional<LocalTime> ParseLocalTime(std::string_view s)
{
  using namespace std::chrono;

  int y, mo, d, h, mi, sec = 0;
  if (!ParseNumber(s, 0, 4, y) || s.size() < 16 || s[4] != '-' || !ParseNumber(s, 5, 2, mo) || s[7] != '-' ||
      !ParseNumber(s, 8, 2, d) || (s[10] != 'T' && s[10] != ' ') || !ParseNumber(s, 11, 2, h) || s[13] != ':' ||
      !ParseNumber(s, 14, 2, mi))
  {
    return {};
  }
  if (s.size() > 16 && s[16] == ':' && !ParseNumber(s, 17, 2, sec))
    return {};

  year_month_day const date{year{y}, month{static_cast<unsigned>(mo)}, day{static_cast<unsigned>(d)}};
  if (!date.ok() || h > 23 || mi > 59 || sec > 60)
    return {};
  return local_days{date} + hours{h} + minutes{mi} + seconds{sec};
}

std::string DebugPrint(TransportMode mode)
{
  switch (mode)
  {
  case TransportMode::Bus: return "Bus";
  case TransportMode::Tram: return "Tram";
  case TransportMode::Metro: return "Metro";
  case TransportMode::Train: return "Train";
  case TransportMode::Ferry: return "Ferry";
  case TransportMode::Other: return "Other";
  }
  return "Unknown";
}

std::string DebugPrint(Status status)
{
  switch (status)
  {
  case Status::Ok: return "Ok";
  case Status::StopNotFound: return "StopNotFound";
  case Status::NetworkError: return "NetworkError";
  case Status::Unauthorized: return "Unauthorized";
  case Status::ServerError: return "ServerError";
  case Status::NoResult: return "NoResult";
  }
  return "Unknown";
}
}  // namespace live_transit
