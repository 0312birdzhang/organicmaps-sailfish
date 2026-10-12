#include "live_transit/trafiklab.hpp"

#include "platform/http_client.hpp"

#include "coding/url.hpp"

#include "geometry/distance_on_sphere.hpp"

#include "base/assert.hpp"
#include "base/logging.hpp"

#include <algorithm>
#include <map>
#include <ranges>
#include <set>
#include <utility>
#include <vector>

#include <glaze/json.hpp>

namespace live_transit
{
namespace trafiklab_json
{
struct ChildStop
{
  double lat = 0.0;
  double lon = 0.0;
};

struct StopGroup
{
  std::string id;
  std::vector<ChildStop> stops;
};

struct StopsResponse
{
  std::vector<StopGroup> stop_groups;
};

struct Place
{
  std::string name;
};

struct Route
{
  std::string designation;
  std::string transport_mode;
  std::string direction;
  std::optional<Place> destination;
};

struct Trip
{
  std::string trip_id;
};

struct Agency
{
  std::string id;
};

struct Platform
{
  std::string designation;
};

struct Departure
{
  std::string scheduled;
  std::optional<std::string> realtime;
  bool canceled = false;
  bool is_realtime = false;
  Route route;
  std::optional<Trip> trip;
  std::optional<Agency> agency;
  std::optional<ChildStop> stop;
  std::optional<Platform> scheduled_platform;
  std::optional<Platform> realtime_platform;
};

struct DeparturesResponse
{
  std::string timestamp;
  std::vector<Departure> departures;
};
}  // namespace trafiklab_json

namespace
{
std::string_view constexpr kBaseUrl = "https://realtime-api.trafiklab.se/v1/";
std::string_view constexpr kVehiclesUrl = "https://opendata.samtrafiken.se/gtfs-rt/";
double constexpr kTimeoutSec = 10.0;
// Between the stop on the map and the nearest stop of a group with its name.
double constexpr kMaxStopDistanceM = 500.0;

glz::opts constexpr kJsonOpts{.error_on_unknown_keys = false};

TransportMode ToMode(std::string_view mode)
{
  if (mode == "BUS")
    return TransportMode::Bus;
  if (mode == "TRAM")
    return TransportMode::Tram;
  if (mode == "METRO")
    return TransportMode::Metro;
  if (mode == "TRAIN")
    return TransportMode::Train;
  if (mode == "FERRY" || mode == "BOAT")
    return TransportMode::Ferry;
  return TransportMode::Other;
}

Status ToStatus(int httpCode)
{
  if (httpCode == 401 || httpCode == 403)
    return Status::Unauthorized;
  if (httpCode == 404)
    return Status::StopNotFound;
  // Codes below 100 are transport errors.
  return httpCode < 100 ? Status::NetworkError : Status::ServerError;
}

std::optional<std::string> Get(std::string const & url, Status & status)
{
  platform::HttpClient request(url);
  request.SetTimeout(kTimeoutSec);
  if (!request.RunHttpRequest() || request.ErrorCode() != 200)
  {
    status = ToStatus(request.ErrorCode());
    LOG(LWARNING, ("Trafiklab request failed:", request.ErrorCode()));
    return {};
  }
  return request.ServerResponse();
}
}  // namespace

bool TrafiklabProvider::Covers(ms::LatLon const & latLon) const
{
  // Sweden's bounding box.
  return latLon.m_lat >= 55.0 && latLon.m_lat <= 69.1 && latLon.m_lon >= 10.9 && latLon.m_lon <= 24.2;
}

std::optional<std::string> TrafiklabProvider::FindStopGroup(Stop const & stop, Status & status)
{
  auto const cacheKey = stop.m_name + '|' + DebugPrint(stop.m_latLon);
  {
    std::lock_guard lock(m_mutex);
    if (auto const it = m_stopGroups.find(cacheKey); it != m_stopGroups.end())
      return it->second;
  }

  std::optional<std::string> id;
  for (auto const & query : trafiklab::GetStopQueries(stop.m_name))
  {
    auto const json =
        Get(std::string(kBaseUrl) + "stops/name/" + url::UrlEncode(query) + "?key=" + m_keys.m_realtime, status);
    if (!json)
      return {};
    id = trafiklab::ParseNearestStopGroup(*json, stop.m_latLon);
    if (id)
      break;
  }
  if (!id)
  {
    status = Status::StopNotFound;
    return {};
  }

  std::lock_guard lock(m_mutex);
  m_stopGroups.emplace(cacheKey, *id);
  return id;
}

Departures TrafiklabProvider::RequestDepartures(Stop const & stop)
{
  Departures result;
  if (stop.m_name.empty())
  {
    result.m_status = Status::StopNotFound;
    return result;
  }

  auto const id = FindStopGroup(stop, result.m_status);
  if (!id)
    return result;
  auto const json =
      Get(std::string(kBaseUrl) + "departures/" + url::UrlEncode(*id) + "?key=" + m_keys.m_realtime, result.m_status);
  if (json && !trafiklab::ParseDepartures(*json, result))
    result.m_status = Status::ServerError;
  return result;
}

bool TrafiklabProvider::CanTrack(Departure const & departure) const
{
  return !m_keys.m_vehicles.empty() && !departure.m_canceled && !departure.m_tripId.empty() &&
         !trafiklab::GetOperator(departure.m_agencyId).empty();
}

std::vector<Vehicle> TrafiklabProvider::RequestVehicles(std::vector<Departure> const & departures, Status & status)
{
  // One feed per operator.
  std::map<std::string_view, std::set<std::string_view>> tripsByOperator;
  for (auto const & departure : departures)
    if (CanTrack(departure))
      tripsByOperator[trafiklab::GetOperator(departure.m_agencyId)].insert(departure.m_tripId);

  std::vector<Vehicle> result;
  for (auto const & [op, trips] : tripsByOperator)
  {
    auto const feed =
        Get(std::string(kVehiclesUrl) + std::string(op) + "/VehiclePositions.pb?key=" + m_keys.m_vehicles, status);
    if (!feed)
      return {};
    std::vector<Vehicle> vehicles;
    if (!gtfs_realtime::ParseVehiclePositions(*feed, vehicles))
    {
      LOG(LWARNING, ("Can't parse the vehicle positions of", op));
      status = Status::ServerError;
      return {};
    }
    // The others aren't on their way yet, or have no position.
    for (auto & vehicle : vehicles)
      if (trips.contains(vehicle.m_tripId))
        result.push_back(std::move(vehicle));
  }
  return result;
}

namespace trafiklab
{
std::vector<std::string> GetStopQueries(std::string_view name)
{
  // The platform, e.g. "Skövde resecentrum (13)".
  if (name.ends_with(')'))
    if (auto const open = name.rfind(" ("); open != std::string_view::npos)
      name = name.substr(0, open);

  std::vector<std::string> queries;
  size_t constexpr kMaxQueries = 3;
  while (!name.empty() && queries.size() < kMaxQueries)
  {
    queries.emplace_back(name);
    auto const space = name.rfind(' ');
    if (space == std::string_view::npos)
      break;
    name = name.substr(0, space);
  }
  return queries;
}

std::string_view GetOperator(std::string_view agencyId)
{
  // Regional operators have the agency id 5050000000000000NN, with NN the county code. Västtrafik (14), Sörmland (04)
  // and Norrbotten (25) have no VehiclePositions feed.
  static std::pair<std::string_view, std::string_view> constexpr kOperators[] = {
      {"01", "sl"},          {"03", "ul"},       {"05", "otraf"}, {"06", "jlt"},     {"07", "krono"}, {"08", "klt"},
      {"09", "gotland"},     {"10", "blekinge"}, {"12", "skane"}, {"13", "halland"}, {"17", "varm"},  {"18", "orebro"},
      {"19", "vastmanland"}, {"20", "dt"},       {"21", "xt"},    {"22", "dintur"}};
  std::string_view constexpr kPrefix = "5050000000000000";
  if (agencyId.size() != kPrefix.size() + 2 || !agencyId.starts_with(kPrefix))
    return {};
  auto const county = agencyId.substr(kPrefix.size());
  for (auto const & [code, op] : kOperators)
    if (code == county)
      return op;
  return {};
}

std::optional<std::string> ParseNearestStopGroup(std::string_view json, ms::LatLon const & latLon)
{
  trafiklab_json::StopsResponse response;
  if (auto const error = glz::read<kJsonOpts>(response, json); error)
  {
    LOG(LWARNING, ("Can't parse Trafiklab stops:", glz::format_error(error, json)));
    return {};
  }

  std::optional<std::string> nearest;
  double nearestDistance = kMaxStopDistanceM;
  for (auto const & group : response.stop_groups)
  {
    for (auto const & stop : group.stops)
    {
      double const distance = ms::DistanceOnEarth(latLon, {stop.lat, stop.lon});
      if (distance <= nearestDistance)
      {
        nearestDistance = distance;
        nearest = group.id;
      }
    }
  }
  return nearest;
}

bool ParseDepartures(std::string_view json, Departures & departures)
{
  trafiklab_json::DeparturesResponse response;
  if (auto const error = glz::read<kJsonOpts>(response, json); error)
  {
    LOG(LWARNING, ("Can't parse Trafiklab departures:", glz::format_error(error, json)));
    return false;
  }
  auto const now = ParseLocalTime(response.timestamp);
  if (!now)
    return false;

  departures.m_now = *now;
  departures.m_departures.clear();
  for (auto const & d : response.departures)
  {
    auto const scheduled = ParseLocalTime(d.scheduled);
    if (!scheduled)
      continue;
    Departure & departure = departures.m_departures.emplace_back();
    departure.m_line = d.route.designation;
    departure.m_destination = d.route.destination ? d.route.destination->name : d.route.direction;
    departure.m_mode = ToMode(d.route.transport_mode);
    departure.m_scheduled = *scheduled;
    if (d.is_realtime && d.realtime)
      departure.m_realtime = ParseLocalTime(*d.realtime);
    auto const & track = d.realtime_platform ? d.realtime_platform : d.scheduled_platform;
    if (track)
      departure.m_platform = track->designation;
    if (d.stop)
      departure.m_platformLatLon = ms::LatLon(d.stop->lat, d.stop->lon);
    departure.m_canceled = d.canceled;
    if (d.trip)
      departure.m_tripId = d.trip->trip_id;
    if (d.agency)
      departure.m_agencyId = d.agency->id;
  }
  std::ranges::stable_sort(departures.m_departures, {}, &Departure::GetTime);
  return true;
}
}  // namespace trafiklab
}  // namespace live_transit
