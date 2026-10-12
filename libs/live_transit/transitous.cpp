#include "live_transit/transitous.hpp"

#include "platform/http_client.hpp"

#include "coding/url.hpp"

#include "base/logging.hpp"
#include "base/string_utils.hpp"
#include "base/timegm.hpp"

#include "std/target_os.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <optional>

#include <glaze/json.hpp>

namespace live_transit
{
namespace transitous_json
{
struct Place
{
  std::string name;
  double lat = 0.0;
  double lon = 0.0;
  std::optional<std::string> track;
  // The first stop of a trip has no arrival and the last no departure.
  std::optional<std::string> arrival;
  std::optional<std::string> departure;
  std::optional<std::string> scheduledArrival;
  std::optional<std::string> scheduledDeparture;
  bool cancelled = false;
  // NORMAL for the coordinates asked for, named START and END.
  std::string vertexType;
};

struct StopTime
{
  Place place;
  std::string mode;
  bool realTime = false;
  std::string headsign;
  std::optional<Place> tripTo;
  std::string agencyId;
  std::string tripId;
  std::string routeShortName;
  std::string displayName;
  bool cancelled = false;
  bool tripCancelled = false;
  std::optional<std::vector<Place>> nextStops;
};

struct StopTimesResponse
{
  std::vector<StopTime> stopTimes;
};

struct EncodedPolyline
{
  std::string points;
  int precision = 6;
};

struct Leg
{
  std::string mode;
  bool realTime = false;
  Place from;
  Place to;
  std::string headsign;
  std::string routeShortName;
  std::string displayName;
  std::string tripId;
  std::optional<double> distance;
  std::optional<std::vector<Place>> intermediateStops;
  std::optional<EncodedPolyline> legGeometry;
};

struct Itinerary
{
  std::vector<Leg> legs;
};

struct PlanResponse
{
  std::vector<Itinerary> itineraries;
  std::optional<std::string> previousPageCursor;
  std::optional<std::string> nextPageCursor;
};
}  // namespace transitous_json

namespace transitous
{
namespace
{
std::string_view constexpr kBaseUrl = "https://api.transitous.org/api/v6/";
// Its usage policy asks to be told who calls.
std::string_view constexpr kUserAgent = "OrganicMaps-Sailfish (+https://organicmaps.app)";
double constexpr kTimeoutSec = 10.0;
// Around the stop on the map, which may lie apart from the timetable's.
int constexpr kStopRadiusM = 100;
int constexpr kDeparturesCount = 30;

glz::opts constexpr kJsonOpts{.error_on_unknown_keys = false};

TransportMode ToMode(std::string_view mode)
{
  if (mode == "BUS" || mode == "COACH")
    return TransportMode::Bus;
  if (mode == "TRAM" || mode == "CABLE_CAR")
    return TransportMode::Tram;
  if (mode == "SUBWAY" || mode == "METRO")
    return TransportMode::Metro;
  if (mode == "FERRY")
    return TransportMode::Ferry;
  if (mode.ends_with("RAIL") || mode == "LONG_DISTANCE" || mode == "SUBURBAN")
    return TransportMode::Train;
  return TransportMode::Other;
}

std::string LineOf(std::string const & shortName, std::string const & displayName)
{
  return shortName.empty() ? displayName : shortName;
}

// On the device's time zone, which is the stop's but when looking at another one's.
LocalTime ToLocal(std::time_t t)
{
  std::tm local;
#ifdef OMIM_OS_WINDOWS
  localtime_s(&local, &t);
#else
  localtime_r(&t, &local);
#endif
  return LocalTime(std::chrono::seconds(base::TimeGM(local)));
}

std::optional<LocalTime> ParseTime(std::optional<std::string> const & s)
{
  return s ? transitous::ParseUtcTime(*s) : std::nullopt;
}

// Arrivals, but the departure where there is none.
std::optional<TripStop> ToTripStop(transitous_json::Place const & place)
{
  bool const arrives = place.scheduledArrival.has_value();
  auto const scheduled = ParseTime(arrives ? place.scheduledArrival : place.scheduledDeparture);
  if (!scheduled)
    return {};
  auto realtime = ParseTime(arrives ? place.arrival : place.departure);
  if (realtime == scheduled)
    realtime.reset();
  return TripStop{place.name, {place.lat, place.lon}, *scheduled, realtime};
}

Status ToStatus(int httpCode)
{
  if (httpCode == 404)
    return Status::StopNotFound;
  // Codes below 100 are transport errors.
  return httpCode < 100 ? Status::NetworkError : Status::ServerError;
}

std::optional<std::string> Get(std::string const & url, Status & status)
{
  platform::HttpClient request(url);
  request.SetTimeout(kTimeoutSec);
  request.SetRawHeader("User-Agent", std::string(kUserAgent));
  if (!request.RunHttpRequest() || request.ErrorCode() != 200)
  {
    status = ToStatus(request.ErrorCode());
    LOG(LWARNING, ("Transitous request failed:", request.ErrorCode()));
    return {};
  }
  return request.ServerResponse();
}

std::string ToCoord(ms::LatLon const & latLon)
{
  return strings::to_string_dac(latLon.m_lat, 6) + ',' + strings::to_string_dac(latLon.m_lon, 6);
}
}  // namespace
}  // namespace transitous

Departures TransitousProvider::RequestDepartures(Stop const & stop)
{
  Departures result;
  // The stops after each departure come along, for the trip stops.
  auto const json = transitous::Get(
      std::string(transitous::kBaseUrl) + "stoptimes?fetchStops=true&center=" + transitous::ToCoord(stop.m_latLon) +
          "&radius=" + std::to_string(transitous::kStopRadiusM) + "&n=" + std::to_string(transitous::kDeparturesCount),
      result.m_status);
  if (!json)
    return result;
  std::unordered_map<std::string, TripStops> tripStops;
  if (!transitous::ParseDepartures(*json, result, tripStops))
  {
    result.m_status = Status::ServerError;
    return result;
  }
  // Not in its data.
  if (result.m_departures.empty())
    result.m_status = Status::StopNotFound;
  std::lock_guard lock(m_mutex);
  m_tripStops = std::move(tripStops);
  return result;
}

std::vector<TripStops> TransitousProvider::RequestTripStops(Stop const &, std::vector<Departure> const & departures)
{
  std::vector<TripStops> result(departures.size());
  std::lock_guard lock(m_mutex);
  for (size_t i = 0; i < departures.size(); ++i)
    if (auto const it = m_tripStops.find(departures[i].m_tripId); it != m_tripStops.end())
      result[i] = it->second;
    else
      result[i].m_status = Status::NoResult;
  return result;
}

Journeys TransitousProvider::RequestJourneys(JourneyQuery const & query)
{
  Journeys result;
  auto const json =
      transitous::Get(std::string(transitous::kBaseUrl) + "plan?detailedTransfers=false&fromPlace=" +
                          transitous::ToCoord(query.m_from) + "&toPlace=" + transitous::ToCoord(query.m_to) +
                          (query.m_context.empty() ? "" : "&pageCursor=" + url::UrlEncode(query.m_context)),
                      result.m_status);
  if (json)
    result = transitous::ParseJourneys(*json);
  return result;
}

namespace transitous
{
bool ParseDepartures(std::string_view json, Departures & departures,
                     std::unordered_map<std::string, TripStops> & tripStops)
{
  transitous_json::StopTimesResponse response;
  if (auto const error = glz::read<kJsonOpts>(response, json); error)
  {
    LOG(LWARNING, ("Can't parse Transitous departures:", glz::format_error(error, json)));
    return false;
  }

  departures.m_now = ToLocal(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
  departures.m_departures.clear();
  for (auto const & st : response.stopTimes)
  {
    auto const scheduled = ParseTime(st.place.scheduledDeparture);
    if (!scheduled)
      continue;
    Departure & departure = departures.m_departures.emplace_back();
    departure.m_line = LineOf(st.routeShortName, st.displayName);
    departure.m_destination = !st.headsign.empty() ? st.headsign : st.tripTo ? st.tripTo->name : std::string();
    departure.m_mode = ToMode(st.mode);
    departure.m_scheduled = *scheduled;
    if (st.realTime)
      departure.m_realtime = ParseTime(st.place.departure);
    departure.m_platform = st.place.track.value_or("");
    departure.m_platformLatLon = ms::LatLon(st.place.lat, st.place.lon);
    departure.m_canceled = st.cancelled || st.tripCancelled || st.place.cancelled;
    departure.m_tripId = st.tripId;
    departure.m_agencyId = st.agencyId;

    if (!st.nextStops || st.tripId.empty())
      continue;
    TripStops & stops = tripStops[st.tripId];
    for (auto const & place : *st.nextStops)
      if (auto stop = ToTripStop(place))
        stops.m_stops.push_back(std::move(*stop));
  }
  std::ranges::stable_sort(departures.m_departures, {}, &Departure::GetTime);
  return true;
}

Journeys ParseJourneys(std::string_view json)
{
  Journeys result;
  transitous_json::PlanResponse response;
  if (auto const error = glz::read<kJsonOpts>(response, json); error)
  {
    LOG(LWARNING, ("Can't parse Transitous journeys:", glz::format_error(error, json)));
    result.m_status = Status::ServerError;
    return result;
  }

  auto const toStop = [](transitous_json::Place const & place, bool departs) -> std::optional<JourneyStop>
  {
    auto const time = ParseTime(departs ? place.departure : place.arrival);
    if (!time)
      return {};
    return JourneyStop{place.vertexType == "NORMAL" ? std::string() : place.name, {place.lat, place.lon}, *time};
  };

  for (auto const & itinerary : response.itineraries)
  {
    Journey journey;
    bool valid = !itinerary.legs.empty();
    for (auto const & l : itinerary.legs)
    {
      auto from = toStop(l.from, true);
      auto to = toStop(l.to, false);
      if (!from || !to)
      {
        valid = false;
        break;
      }
      Leg & leg = journey.m_legs.emplace_back();
      leg.m_walk = l.tripId.empty();
      leg.m_mode = ToMode(l.mode);
      if (!leg.m_walk)
      {
        leg.m_line = LineOf(l.routeShortName, l.displayName);
        leg.m_direction = l.headsign;
      }
      leg.m_distanceM = l.distance.value_or(0.0);
      leg.m_stops.push_back(*from);
      if (l.intermediateStops)
      {
        for (auto const & place : *l.intermediateStops)
        {
          auto const time = ParseTime(place.arrival ? place.arrival : place.departure);
          if (time)
            leg.m_stops.push_back({place.name, {place.lat, place.lon}, *time});
        }
      }
      leg.m_stops.push_back(*to);
      if (l.legGeometry)
        leg.m_path = DecodePolyline(l.legGeometry->points, l.legGeometry->precision);
      if (leg.m_path.size() < 2)
      {
        leg.m_path.clear();
        for (auto const & stop : leg.m_stops)
          leg.m_path.push_back(stop.m_latLon);
      }
      leg.m_from = std::move(*from);
      leg.m_to = std::move(*to);
    }
    if (valid)
      result.m_journeys.push_back(std::move(journey));
  }
  if (result.m_journeys.empty())
    result.m_status = Status::NoResult;
  std::ranges::stable_sort(result.m_journeys, {}, &Journey::GetDeparture);
  result.m_earlier = response.previousPageCursor.value_or("");
  result.m_later = response.nextPageCursor.value_or("");
  return result;
}

std::vector<ms::LatLon> DecodePolyline(std::string_view points, int precision)
{
  double const scale = std::pow(10.0, precision);
  std::vector<ms::LatLon> result;
  int64_t lat = 0;
  int64_t lon = 0;
  size_t i = 0;
  // Each value is a zigzag-encoded delta in 5-bit chunks, the lowest first, offset by 63.
  auto const next = [&](int64_t & value)
  {
    uint64_t chunks = 0;
    int shift = 0;
    while (i < points.size())
    {
      int const c = points[i++] - 63;
      chunks |= static_cast<uint64_t>(c & 0x1f) << shift;
      shift += 5;
      if (c < 0x20)
      {
        value += (chunks & 1) ? ~static_cast<int64_t>(chunks >> 1) : static_cast<int64_t>(chunks >> 1);
        return true;
      }
    }
    return false;
  };
  while (i < points.size())
  {
    if (!next(lat) || !next(lon))
      break;
    result.emplace_back(lat / scale, lon / scale);
  }
  return result;
}

std::optional<LocalTime> ParseUtcTime(std::string_view s)
{
  auto const utc = ParseLocalTime(s);
  if (!utc)
    return {};
  return ToLocal(utc->time_since_epoch().count());
}
}  // namespace transitous
}  // namespace live_transit
