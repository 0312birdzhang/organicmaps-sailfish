#include "live_transit/transitous.hpp"

#include "platform/http_client.hpp"

#include "coding/url.hpp"

#include "base/assert.hpp"
#include "base/logging.hpp"
#include "base/string_utils.hpp"
#include "base/timegm.hpp"

#include "std/target_os.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <map>
#include <optional>
#include <ranges>
#include <set>

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

namespace entur_json
{
struct ServiceJourneyId
{
  std::string id;
};

struct Variables
{
  std::vector<ServiceJourneyId> ids;
};

struct Request
{
  std::string query;
  Variables variables;
};

struct Location
{
  double latitude = 0.0;
  double longitude = 0.0;
};

struct VehicleUpdate
{
  std::optional<ServiceJourneyId> serviceJourney;
  std::optional<Location> location;
  std::optional<double> bearing;
  std::optional<double> speed;
  std::optional<double> lastUpdatedEpochSecond;
};

struct Data
{
  std::vector<VehicleUpdate> vehicles;
};

struct Response
{
  std::optional<Data> data;
};
}  // namespace entur_json

namespace transitous
{
namespace
{
std::string_view constexpr kBaseUrl = "https://api.transitous.org/api/v6/";
// Its usage policy asks to be told who calls.
std::string_view constexpr kUserAgent = "OrganicMaps-Sailfish (+https://organicmaps.app)";
// Entur asks clients to name themselves as "<company>-<application>".
std::string_view constexpr kEnturClientName = "organicmaps-sailfish";
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

std::string TransitousProvider::GetDeparturesAttribution(std::vector<Departure> const & departures) const
{
  std::string result(GetAttribution());
  std::set<std::string_view> added;
  for (auto const & departure : departures)
  {
    if (!CanTrack(departure))
      continue;
    auto const * feed = transitous::FindVehicleFeed(transitous::ParseTripId(departure.m_tripId)->m_feed);
    if (added.insert(feed->m_attribution).second)
      result += ", " + std::string(feed->m_attribution);
  }
  return result;
}

bool TransitousProvider::CanTrack(Departure const & departure) const
{
  if (departure.m_canceled)
    return false;
  auto const id = transitous::ParseTripId(departure.m_tripId);
  auto const * feed = id ? transitous::FindVehicleFeed(id->m_feed) : nullptr;
  return feed && !transitous::GetTripKey(*feed, id->m_gtfsTripId).empty();
}

std::vector<Vehicle> TransitousProvider::RequestVehicles(std::vector<Departure> const & departures, Status & status)
{
  // The departures' trip ids by their keys, per feed.
  std::map<transitous::VehicleFeed const *, std::map<std::string, std::string>> tripsByFeed;
  for (auto const & departure : departures)
  {
    if (!CanTrack(departure))
      continue;
    auto const id = transitous::ParseTripId(departure.m_tripId);
    auto const * feed = transitous::FindVehicleFeed(id->m_feed);
    tripsByFeed[feed].emplace(transitous::GetTripKey(*feed, id->m_gtfsTripId), departure.m_tripId);
  }

  std::vector<Vehicle> result;
  for (auto const & [feed, trips] : tripsByFeed)
  {
    platform::HttpClient request{std::string(feed->m_url)};
    request.SetTimeout(transitous::kTimeoutSec);
    request.SetRawHeader("User-Agent", std::string(transitous::kUserAgent));
    if (feed->m_api == transitous::VehicleApi::Entur)
    {
      // The keys are the ServiceJourney ids.
      std::vector<std::string_view> ids;
      for (auto const & key : trips | std::views::keys)
        ids.push_back(key);
      request.SetRawHeader("ET-Client-Name", std::string(transitous::kEnturClientName));
      request.SetBodyData(transitous::MakeEnturVehiclesQuery(ids), "application/json");
    }
    if (!request.RunHttpRequest() || request.ErrorCode() != 200)
    {
      LOG(LWARNING, ("Vehicles request failed:", feed->m_feed, request.ErrorCode()));
      status = transitous::ToStatus(request.ErrorCode());
      return {};
    }
    std::vector<std::pair<gtfs_realtime::VehicleTrip, Vehicle>> vehicles;
    bool const parsed = feed->m_api == transitous::VehicleApi::Entur
                          ? transitous::ParseEnturVehicles(request.ServerResponse(), vehicles)
                          : gtfs_realtime::ParseVehiclePositions(request.ServerResponse(), vehicles);
    if (!parsed)
    {
      LOG(LWARNING, ("Can't parse the vehicle positions of", feed->m_feed));
      status = Status::ServerError;
      return {};
    }
    // The others aren't on their way yet, or have no position.
    std::set<std::string_view> found;
    for (auto & [trip, vehicle] : vehicles)
    {
      auto const it = trips.find(transitous::GetTripKey(*feed, trip));
      if (it == trips.end() || !found.insert(it->second).second)
        continue;
      vehicle.m_tripId = it->second;
      result.push_back(std::move(vehicle));
    }
  }
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

std::optional<TripId> ParseTripId(std::string_view tripId)
{
  // The date and time have no '_', and feed names neither.
  size_t pos = 0;
  size_t ends[3];
  for (auto & end : ends)
  {
    end = tripId.find('_', pos);
    if (end == std::string_view::npos || end == pos)
      return {};
    pos = end + 1;
  }
  if (pos == tripId.size())
    return {};
  return TripId{tripId.substr(ends[1] + 1, ends[2] - ends[1] - 1), tripId.substr(pos)};
}

VehicleFeed const * FindVehicleFeed(std::string_view feed)
{
  static VehicleFeed constexpr kFeeds[] = {
      {"fi-hsl", VehicleApi::GtfsRealtime, "https://realtime.hsl.fi/realtime/vehicle-positions/v2/hsl", "HSL",
       true /* tripsByStart */},
      {"no-Entur", VehicleApi::Entur, "https://api.entur.io/realtime/v2/vehicles/graphql", "Entur"},
  };
  auto const it = std::ranges::find(kFeeds, feed, &VehicleFeed::m_feed);
  return it != std::end(kFeeds) ? it : nullptr;
}

std::string GetTripKey(VehicleFeed const & feed, std::string_view gtfsTripId)
{
  // Ruter, in Oslo, publishes no vehicles to Entur.
  if (feed.m_api == VehicleApi::Entur && gtfsTripId.starts_with("RUT:"))
    return {};
  if (!feed.m_tripsByStart)
    return std::string(gtfsTripId);
  auto const parts = strings::Tokenize(gtfsTripId, "_");
  if (parts.size() != 5 || (parts[3] != "1" && parts[3] != "2") || parts[4].size() != 4)
    return {};
  // Directions 1 and 2 are GTFS's 0 and 1.
  return std::string(parts[0]) + '|' + (parts[3] == "1" ? '0' : '1') + '|' + std::string(parts[4]);
}

std::string GetTripKey(VehicleFeed const & feed, gtfs_realtime::VehicleTrip const & trip)
{
  if (!feed.m_tripsByStart)
    return trip.m_tripId;
  auto const & start = trip.m_startTime;
  if (trip.m_routeId.empty() || !trip.m_directionId || *trip.m_directionId > 1 || start.size() < 5 || start[2] != ':')
    return {};
  return trip.m_routeId + '|' + static_cast<char>('0' + *trip.m_directionId) + '|' + start.substr(0, 2) +
         start.substr(3, 2);
}

std::string MakeEnturVehiclesQuery(std::vector<std::string_view> const & ids)
{
  entur_json::Request request{
      .query =
          "query($ids: [ServiceJourneyIdAndDate]) { vehicles(serviceJourneyIdAndDates: $ids) "
          "{ serviceJourney { id } location { latitude longitude } bearing speed lastUpdatedEpochSecond } }"};
  // Without the dates, which Transitous' trip ids may tell otherwise after midnight.
  for (auto const id : ids)
    request.variables.ids.push_back({std::string(id)});
  std::string json;
  CHECK(!glz::write_json(request, json), ());
  return json;
}

bool ParseEnturVehicles(std::string_view json, std::vector<std::pair<gtfs_realtime::VehicleTrip, Vehicle>> & vehicles)
{
  vehicles.clear();
  entur_json::Response response;
  if (auto const error = glz::read<kJsonOpts>(response, json); error)
  {
    LOG(LWARNING, ("Can't parse Entur vehicles:", glz::format_error(error, json)));
    return false;
  }
  // GraphQL errors come without data.
  if (!response.data)
    return false;
  for (auto const & v : response.data->vehicles)
  {
    if (!v.serviceJourney || !v.location)
      continue;
    Vehicle vehicle;
    vehicle.m_latLon = {v.location->latitude, v.location->longitude};
    vehicle.m_bearing = v.bearing;
    vehicle.m_speed = v.speed;
    vehicle.m_timestamp = static_cast<uint64_t>(v.lastUpdatedEpochSecond.value_or(0));
    vehicles.emplace_back(gtfs_realtime::VehicleTrip{.m_tripId = v.serviceJourney->id}, std::move(vehicle));
  }
  return true;
}
}  // namespace transitous
}  // namespace live_transit
