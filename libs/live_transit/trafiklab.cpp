#include "live_transit/trafiklab.hpp"

#include "platform/http_client.hpp"

#include "coding/url.hpp"

#include "geometry/distance_on_sphere.hpp"

#include "base/assert.hpp"
#include "base/logging.hpp"
#include "base/string_utils.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <utility>
#include <variant>
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

namespace resrobot_json
{
// HAFAS writes a list with one element as the element.
template <typename T>
using OneOrMany = std::variant<std::vector<T>, T>;

template <typename T, typename Fn>
void ForEach(OneOrMany<T> const & items, Fn && fn)
{
  if (auto const * many = std::get_if<std::vector<T>>(&items))
    std::for_each(many->begin(), many->end(), fn);
  else
    fn(std::get<T>(items));
}

struct Place
{
  std::string name;
  double lat = 0.0;
  double lon = 0.0;
  std::string date;
  std::string time;
  std::optional<std::string> rtDate;
  std::optional<std::string> rtTime;
};

struct ProductJson
{
  std::string num;
  std::string line;
  std::string displayNumber;
  // The product class bit, documented as a string but also seen as a number.
  std::variant<std::string, double> cls;
};

struct StopPoint
{
  std::string name;
  double lat = 0.0;
  double lon = 0.0;
  // The first stop has no arrival and the last no departure.
  std::optional<std::string> arrDate;
  std::optional<std::string> arrTime;
  std::optional<std::string> rtArrDate;
  std::optional<std::string> rtArrTime;
  std::optional<std::string> depDate;
  std::optional<std::string> depTime;
  std::optional<std::string> rtDepDate;
  std::optional<std::string> rtDepTime;
};

struct StopsJson
{
  OneOrMany<StopPoint> Stop;
};

struct LegJson
{
  std::string type;
  std::string direction;
  std::optional<double> dist;
  std::optional<OneOrMany<ProductJson>> Product;
  Place Origin;
  Place Destination;
  std::optional<StopsJson> Stops;
};

struct LegListJson
{
  OneOrMany<LegJson> Leg;
};

struct TripJson
{
  LegListJson LegList;
};

struct BoardProductJson
{
  std::string num;
  std::string displayNumber;
};

struct BoardDepartureJson
{
  std::string date;
  std::string time;
  std::optional<BoardProductJson> ProductAtStop;
  std::optional<StopsJson> Stops;
};

struct BoardResponse
{
  std::optional<std::vector<BoardDepartureJson>> Departure;
  std::optional<std::string> errorCode;
  std::optional<std::string> errorText;
};

struct Response
{
  std::optional<std::vector<TripJson>> Trip;
  // The contexts to scroll back and forward.
  std::optional<std::string> scrB;
  std::optional<std::string> scrF;
  std::optional<std::string> errorCode;
  std::optional<std::string> errorText;
};
}  // namespace resrobot_json

namespace
{
std::string_view constexpr kBaseUrl = "https://realtime-api.trafiklab.se/v1/";
std::string_view constexpr kVehiclesUrl = "https://opendata.samtrafiken.se/gtfs-rt/";
std::string_view constexpr kResRobotUrl = "https://api.resrobot.se/v2.1/trip";
std::string_view constexpr kResRobotBoardUrl = "https://api.resrobot.se/v2.1/departureBoard";
double constexpr kTimeoutSec = 10.0;
// Between the stop on the map and the nearest stop of a group with its name.
double constexpr kMaxStopDistanceM = 500.0;

glz::opts constexpr kJsonOpts{.error_on_unknown_keys = false};

// ResRobot product classes.
TransportMode ToMode(int productClass)
{
  switch (productClass)
  {
  case 2:
  case 4:
  case 16: return TransportMode::Train;
  case 8:
  case 128: return TransportMode::Bus;
  case 32: return TransportMode::Metro;
  case 64: return TransportMode::Tram;
  case 256: return TransportMode::Ferry;
  }
  return TransportMode::Other;
}

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

Status ToResRobotStatus(std::string_view errorCode)
{
  if (errorCode == "API_AUTH")
    return Status::Unauthorized;
  if (errorCode == "SVC_NO_RESULT" || errorCode.starts_with("SVC_LOC"))
    return Status::NoResult;
  return Status::ServerError;
}

std::optional<LocalTime> ParseDateTime(std::optional<std::string> const & date, std::optional<std::string> const & time)
{
  if (!date || !time)
    return {};
  return ParseLocalTime(*date + 'T' + *time);
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

std::vector<TripStops> TrafiklabProvider::RequestTripStops(Stop const & stop,
                                                          std::vector<Departure> const & departures)
{
  if (departures.empty())
    return {};
  auto const fail = [&](Status status)
  {
    std::vector<TripStops> result(departures.size());
    for (auto & stops : result)
      stops.m_status = status;
    return result;
  };
  Status status = Status::Ok;
  auto const id = FindStopGroup(stop, status);
  if (!id)
    return fail(status);

  using namespace std::chrono;
  auto const [first, last] = std::ranges::minmax(departures | std::views::transform(&Departure::m_scheduled));
  auto const day = floor<days>(first);
  year_month_day const date{day};
  hh_mm_ss const clock{first - day};
  auto const twoDigits = [](auto value) { return (value < 10 ? "0" : "") + std::to_string(value); };
  // The departures from |first| up to |last|, as ResRobot drops the seconds.
  auto const duration = floor<minutes>(last).time_since_epoch() - floor<minutes>(first).time_since_epoch() + 1min;
  std::string const requestUrl = std::string(kResRobotBoardUrl) + "?format=json&passlist=1&accessId=" +
                                 m_keys.m_resrobot + "&id=" + url::UrlEncode(*id) + "&date=" +
                                 std::to_string(static_cast<int>(date.year())) + '-' +
                                 twoDigits(static_cast<unsigned>(date.month())) + '-' +
                                 twoDigits(static_cast<unsigned>(date.day())) + "&time=" +
                                 twoDigits(clock.hours().count()) + ':' + twoDigits(clock.minutes().count()) +
                                 "&duration=" + std::to_string(duration.count());
  platform::HttpClient request(requestUrl);
  request.SetTimeout(kTimeoutSec);
  // Errors come with a JSON body.
  if (!request.RunHttpRequest() || request.ErrorCode() < 100)
  {
    LOG(LWARNING, ("ResRobot request failed:", request.ErrorCode()));
    return fail(Status::NetworkError);
  }
  auto result = trafiklab::ParseTripStops(request.ServerResponse(), departures);
  if (request.ErrorCode() != 200 && std::ranges::all_of(result, [](auto const & s) { return s.m_status == Status::Ok; }))
    return fail(ToStatus(request.ErrorCode()));
  return result;
}

Journeys TrafiklabProvider::RequestJourneys(JourneyQuery const & query)
{
  auto const coord = [](double value) { return strings::to_string_dac(value, 6); };
  std::string const requestUrl =
      std::string(kResRobotUrl) + "?format=json&passlist=1&accessId=" + m_keys.m_resrobot +
      "&originCoordLat=" + coord(query.m_from.m_lat) + "&originCoordLong=" + coord(query.m_from.m_lon) +
      "&destCoordLat=" + coord(query.m_to.m_lat) + "&destCoordLong=" + coord(query.m_to.m_lon) +
      (query.m_context.empty() ? "" : "&context=" + url::UrlEncode(query.m_context));
  platform::HttpClient request(requestUrl);
  request.SetTimeout(kTimeoutSec);
  // Errors such as no journey come with a JSON body.
  if (!request.RunHttpRequest() || request.ErrorCode() < 100)
  {
    LOG(LWARNING, ("ResRobot request failed:", request.ErrorCode()));
    Journeys result;
    result.m_status = Status::NetworkError;
    return result;
  }
  auto result = trafiklab::ParseJourneys(request.ServerResponse());
  if (result.m_status == Status::Ok && request.ErrorCode() != 200)
    result.m_status = ToStatus(request.ErrorCode());
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

Journeys ParseJourneys(std::string_view json)
{
  Journeys result;
  resrobot_json::Response response;
  if (auto const error = glz::read<kJsonOpts>(response, json); error)
  {
    LOG(LWARNING, ("Can't parse ResRobot trips:", glz::format_error(error, json)));
    result.m_status = Status::ServerError;
    return result;
  }
  if (response.errorCode)
  {
    LOG(LINFO, ("ResRobot error:", *response.errorCode, response.errorText.value_or("")));
    result.m_status = ToResRobotStatus(*response.errorCode);
    return result;
  }

  auto const toStop = [](resrobot_json::Place const & place) -> std::optional<JourneyStop>
  {
    auto const & date = place.rtDate ? *place.rtDate : place.date;
    auto const & time = place.rtTime ? *place.rtTime : place.time;
    auto const parsed = ParseLocalTime(date + 'T' + time);
    if (!parsed)
      return {};
    return JourneyStop{place.name, {place.lat, place.lon}, *parsed};
  };
  // At the arrival, but the departure at the first stop.
  auto const toPassedStop = [](resrobot_json::StopPoint const & stop) -> std::optional<JourneyStop>
  {
    auto const & date = stop.rtArrDate ? stop.rtArrDate
                      : stop.arrDate   ? stop.arrDate
                      : stop.rtDepDate ? stop.rtDepDate
                                       : stop.depDate;
    auto const & time = stop.rtArrTime ? stop.rtArrTime
                      : stop.arrTime   ? stop.arrTime
                      : stop.rtDepTime ? stop.rtDepTime
                                       : stop.depTime;
    if (!date || !time)
      return {};
    auto const parsed = ParseLocalTime(*date + 'T' + *time);
    if (!parsed)
      return {};
    return JourneyStop{stop.name, {stop.lat, stop.lon}, *parsed};
  };

  for (auto const & trip : response.Trip.value_or(std::vector<resrobot_json::TripJson>{}))
  {
    Journey journey;
    bool valid = true;
    resrobot_json::ForEach(trip.LegList.Leg, [&](resrobot_json::LegJson const & l)
    {
      auto from = toStop(l.Origin);
      auto to = toStop(l.Destination);
      if (!from || !to)
      {
        valid = false;
        return;
      }
      Leg & leg = journey.m_legs.emplace_back();
      leg.m_walk = l.type != "JNY";
      leg.m_direction = l.direction;
      leg.m_distanceM = l.dist.value_or(0.0);
      if (l.Stops)
      {
        resrobot_json::ForEach(l.Stops->Stop, [&](resrobot_json::StopPoint const & stop)
        {
          if (auto passed = toPassedStop(stop))
            leg.m_stops.push_back(std::move(*passed));
        });
      }
      // The stops include the ends.
      if (leg.m_stops.size() < 2)
        leg.m_stops = {*from, *to};
      for (auto const & stop : leg.m_stops)
        leg.m_path.push_back(stop.m_latLon);
      leg.m_from = std::move(*from);
      leg.m_to = std::move(*to);
      if (!leg.m_walk && l.Product)
      {
        bool first = true;
        resrobot_json::ForEach(*l.Product, [&](resrobot_json::ProductJson const & product)
        {
          if (!std::exchange(first, false))
            return;
          leg.m_line = !product.line.empty()          ? product.line
                     : !product.displayNumber.empty() ? product.displayNumber
                                                      : product.num;
          int productClass = 0;
          if (auto const * number = std::get_if<double>(&product.cls))
            productClass = static_cast<int>(*number);
          else if (!strings::to_int(std::get<std::string>(product.cls), productClass))
            productClass = 0;
          leg.m_mode = ToMode(productClass);
        });
      }
    });
    if (valid && !journey.m_legs.empty())
      result.m_journeys.push_back(std::move(journey));
  }
  std::ranges::stable_sort(result.m_journeys, {}, &Journey::GetDeparture);
  result.m_earlier = response.scrB.value_or("");
  result.m_later = response.scrF.value_or("");
  return result;
}

std::vector<TripStops> ParseTripStops(std::string_view json, std::vector<Departure> const & departures)
{
  std::vector<TripStops> result(departures.size());
  auto const fail = [&](Status status)
  {
    for (auto & stops : result)
      stops.m_status = status;
    return result;
  };
  resrobot_json::BoardResponse response;
  if (auto const error = glz::read<kJsonOpts>(response, json); error)
  {
    LOG(LWARNING, ("Can't parse the ResRobot departures:", glz::format_error(error, json)));
    return fail(Status::ServerError);
  }
  if (response.errorCode)
  {
    LOG(LINFO, ("ResRobot error:", *response.errorCode, response.errorText.value_or("")));
    return fail(ToResRobotStatus(*response.errorCode));
  }

  auto const board = response.Departure.value_or(std::vector<resrobot_json::BoardDepartureJson>{});
  for (size_t i = 0; i < departures.size(); ++i)
  {
    auto const & departure = departures[i];
    // By its line and time, as the realtime and ResRobot trip ids differ. ResRobot drops the seconds, which some
    // operators keep, e.g. SL.
    auto const scheduled = std::chrono::floor<std::chrono::minutes>(departure.m_scheduled);
    auto const it = std::ranges::find_if(board, [&](resrobot_json::BoardDepartureJson const & d)
    {
      auto const time = ParseLocalTime(d.date + 'T' + d.time);
      if (!d.ProductAtStop || !d.Stops || !time || std::chrono::floor<std::chrono::minutes>(*time) != scheduled)
        return false;
      auto const & product = *d.ProductAtStop;
      return strings::EqualAsciiNoCase(product.displayNumber.empty() ? product.num : product.displayNumber,
                                       departure.m_line);
    });
    if (it == board.end())
    {
      result[i].m_status = Status::NoResult;
      continue;
    }
    bool first = true;
    resrobot_json::ForEach(it->Stops->Stop, [&](resrobot_json::StopPoint const & s)
    {
      // The board's own stop.
      if (std::exchange(first, false))
        return;
      bool const arrives = s.arrTime.has_value();
      auto const time = arrives ? ParseDateTime(s.arrDate, s.arrTime) : ParseDateTime(s.depDate, s.depTime);
      if (!time)
        return;
      result[i].m_stops.push_back({.m_name = s.name,
                                   .m_latLon = {s.lat, s.lon},
                                   .m_scheduled = *time,
                                   .m_realtime = arrives ? ParseDateTime(s.rtArrDate, s.rtArrTime)
                                                         : ParseDateTime(s.rtDepDate, s.rtDepTime)});
    });
  }
  return result;
}
}  // namespace trafiklab
}  // namespace live_transit
