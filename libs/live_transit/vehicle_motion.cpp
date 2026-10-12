#include "live_transit/vehicle_motion.hpp"

#include "geometry/mercator.hpp"
#include "geometry/parametrized_segment.hpp"

#include "base/assert.hpp"
#include "base/math.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace live_transit
{
namespace
{
// GPS puts a vehicle beside its road, and a road beside the mapped route.
double constexpr kOnPathM = 50.0;
// Not carried on further without news: it may have stopped.
double constexpr kMaxPredictionS = 30.0;
// The time constant of easing onto a report: mostly there after a second, all but after three.
double constexpr kEaseS = 1.0;
// Moved less, it tells little of the way it goes.
double constexpr kMinMoveM = 5.0;
// Shown ahead of a report, a moving vehicle slows for it to catch up rather than going back, if it takes no longer.
double constexpr kMaxWaitS = 10.0;
double constexpr kMinMovingSpeedMps = 1.0;
// Of the way ahead: 45 degrees off it at most.
double constexpr kMinAheadCos = 0.7;
// Reported a little past the stop, it may still be at it.
double constexpr kAtStopM = 30.0;
// As a bus brakes, gently.
double constexpr kBrakeMps2 = 1.0;
// Nearer a stop it is leaving it.
double constexpr kLeavingStopM = 5.0;

double Normalize(double degrees)
{
  double const result = std::fmod(degrees, 360.0);
  return result < 0 ? result + 360.0 : result;
}

// Meters gone in |seconds| at |speed| m/s, braking to a halt at |toStopM| meters on, harder when nearer than gently.
double Travel(double speed, double seconds, std::optional<double> toStopM)
{
  if (!toStopM || speed <= 0)
    return speed * seconds;
  double const brakingM = speed * speed / (2 * kBrakeMps2);
  double const cruiseS = std::max(0.0, *toStopM - brakingM) / speed;
  if (seconds <= cruiseS)
    return speed * seconds;
  double const brake = *toStopM >= brakingM ? kBrakeMps2 : speed * speed / (2 * *toStopM);
  double const brakeS = std::min(seconds - cruiseS, speed / brake);
  return std::min(*toStopM, speed * cruiseS + speed * brakeS - brake * brakeS * brakeS / 2);
}
}  // namespace

VehicleMotion::VehicleMotion(Route const & route, ms::LatLon const & stop) : m_stopPoint(mercator::FromLatLon(stop))
{
  BuildPaths(route);
}

void VehicleMotion::BuildPaths(Route const & route)
{
  m_paths.clear();
  for (auto const & points : route.m_paths)
  {
    if (points.size() < 2)
      continue;
    Path & path = m_paths.emplace_back();
    path.m_points = points;
    path.m_distances.push_back(0.0);
    for (size_t i = 1; i < points.size(); ++i)
      path.m_distances.push_back(path.m_distances.back() + mercator::DistanceOnEarth(points[i - 1], points[i]));

    if (!route.m_ordered)
      continue;
    // The stops on it say which way it runs.
    int order = 0;
    std::optional<double> last;
    for (auto const & stop : route.m_stops)
    {
      auto const [distance, offM] = ProjectOn(path, stop);
      if (offM > kOnPathM)
        continue;
      if (last && distance != *last)
        order += distance > *last ? 1 : -1;
      last = distance;
    }
    path.m_order = order > 0 ? 1 : (order < 0 ? -1 : 0);
  }
  m_stop = Project(m_stopPoint);
  m_stopLocations.clear();
  if (m_stop)
    m_stopLocations.push_back(*m_stop);
  for (auto const & stop : route.m_stops)
    if (auto const location = Project(stop))
      m_stopLocations.push_back(*location);
}

void VehicleMotion::SetRoute(Route const & route, double now)
{
  std::optional<m2::PointD> shown;
  if (m_reported)
    shown = mercator::FromLatLon(Get(now).m_latLon);
  BuildPaths(route);
  if (!m_reported)
    return;
  m_location = Project(m_point);
  m_direction = m_location && m_bearing ? DirectionOf(*m_location, *m_bearing) : 0;
  EaseFrom(shown, now);
}

void VehicleMotion::EaseFrom(std::optional<m2::PointD> const & shown, double now)
{
  std::optional<double> bearing;
  m2::PointD const predicted = Predict(now, bearing);
  m_offset = shown ? *shown - predicted : m2::PointD::Zero();
  m_easeStart = now;
  m_easeS = kEaseS;

  double const speed = m_speed.value_or(0.0);
  double const length = m_offset.Length();
  if (!bearing || speed < kMinMovingSpeedMps || length == 0)
    return;
  double const radians = math::DegToRad(*bearing);
  if (m2::DotProduct(m_offset, m2::PointD(std::sin(radians), std::cos(radians))) < kMinAheadCos * length)
    return;
  // Easing as slow as gap / speed never goes back: the prediction gains on the offset at least as fast as it fades.
  double const waitS = mercator::DistanceOnEarth(predicted, *shown) / speed;
  if (waitS <= kMaxWaitS)
    m_easeS = std::max(kEaseS, waitS);
}

int VehicleMotion::DirectionOf(Location const & location, double bearing) const
{
  return std::abs(std::remainder(bearing - DirectionAt(location), 360.0)) <= 90.0 ? 1 : -1;
}

void VehicleMotion::Report(Vehicle const & vehicle, double now)
{
  m2::PointD const point = mercator::FromLatLon(vehicle.m_latLon);
  // A clock ahead of the feed's isn't believed.
  double const time = vehicle.m_timestamp > 0 ? std::min(static_cast<double>(vehicle.m_timestamp), now) : now;
  auto const location = Project(point);
  bool const samePath = location && m_location && location->m_path == m_location->m_path;

  std::optional<double> speed = vehicle.m_speed;
  double moved = 0.0;
  if (m_reported && time > m_time)
  {
    moved = samePath ? location->m_distance - m_location->m_distance : mercator::DistanceOnEarth(m_point, point);
    if (!speed)
      speed = std::abs(moved) / (time - m_time);
  }
  else if (m_reported && !speed)
  {
    // The same report again.
    speed = m_speed;
  }

  int direction = 0;
  if (location && vehicle.m_bearing)
    direction = DirectionOf(*location, *vehicle.m_bearing);
  else if (samePath && std::abs(moved) >= kMinMoveM)
    direction = moved > 0 ? 1 : -1;
  else if (samePath)
    direction = m_direction;

  std::optional<m2::PointD> shown;
  if (m_reported)
  {
    auto const state = Get(now);
    shown = mercator::FromLatLon(state.m_latLon);
  }

  m_reported = true;
  m_point = point;
  m_time = time;
  m_speed = speed;
  m_bearing = vehicle.m_bearing;
  m_location = location;
  m_direction = direction;
  EaseFrom(shown, now);
}

VehicleMotion::State VehicleMotion::Get(double now) const
{
  ASSERT(m_reported, ());
  State state;
  m2::PointD const predicted = Predict(now, state.m_bearing);
  double const fade = std::exp(-std::max(0.0, now - m_easeStart) / m_easeS);
  state.m_latLon = mercator::ToLatLon(predicted + m_offset * fade);
  return state;
}

std::optional<bool> VehicleMotion::IsApproaching() const
{
  if (!m_location || !m_stop || m_location->m_path != m_stop->m_path || m_direction == 0)
    return {};
  int const order = m_paths[m_location->m_path].m_order;
  if (order == 0)
    return {};
  return m_direction == order && (m_stop->m_distance - m_location->m_distance) * order >= -kAtStopM;
}

m2::PointD VehicleMotion::Predict(double now, std::optional<double> & bearing) const
{
  double const elapsed = std::clamp(now - m_time, 0.0, kMaxPredictionS);
  double const distance = m_speed.value_or(0.0) * elapsed;

  if (m_location && m_direction != 0)
  {
    auto const & path = m_paths[m_location->m_path];
    double const travel = Travel(m_speed.value_or(0.0), elapsed, ToNextStop());
    Location const at{m_location->m_path,
                      std::clamp(m_location->m_distance + m_direction * travel, 0.0, path.m_distances.back())};
    bearing = Normalize(DirectionAt(at) + (m_direction < 0 ? 180.0 : 0.0));
    return PointAt(at);
  }

  bearing = m_bearing;
  if (!m_bearing || distance <= 0)
    return m_point;
  // Straight on: mercator keeps the angles, and its scale is the same both ways at a point.
  double const radians = math::DegToRad(*m_bearing);
  double const length = mercator::MetersToMercator(distance) / std::cos(math::DegToRad(mercator::YToLat(m_point.y)));
  return m_point + m2::PointD(std::sin(radians), std::cos(radians)) * length;
}

std::optional<double> VehicleMotion::ToNextStop() const
{
  std::optional<double> nearest;
  for (auto const & stop : m_stopLocations)
  {
    if (stop.m_path != m_location->m_path)
      continue;
    double const ahead = (stop.m_distance - m_location->m_distance) * m_direction;
    if (ahead > kLeavingStopM && (!nearest || ahead < *nearest))
      nearest = ahead;
  }
  return nearest;
}

std::optional<VehicleMotion::Location> VehicleMotion::Project(m2::PointD const & point) const
{
  std::optional<Location> nearest;
  double nearestM = kOnPathM;
  for (size_t p = 0; p < m_paths.size(); ++p)
  {
    auto const [distance, offM] = ProjectOn(m_paths[p], point);
    if (offM <= nearestM)
    {
      nearestM = offM;
      nearest = Location{p, distance};
    }
  }
  return nearest;
}

std::pair<double, double> VehicleMotion::ProjectOn(Path const & path, m2::PointD const & point) const
{
  std::pair<double, double> nearest{0.0, std::numeric_limits<double>::max()};
  for (size_t i = 1; i < path.m_points.size(); ++i)
  {
    m2::ParametrizedSegment<m2::PointD> const segment(path.m_points[i - 1], path.m_points[i]);
    m2::PointD const closest = segment.ClosestPointTo(point);
    double const offM = mercator::DistanceOnEarth(point, closest);
    if (offM < nearest.second)
      nearest = {path.m_distances[i - 1] + mercator::DistanceOnEarth(path.m_points[i - 1], closest), offM};
  }
  return nearest;
}

m2::PointD VehicleMotion::PointAt(Location const & location) const
{
  auto const & path = m_paths[location.m_path];
  auto const & distances = path.m_distances;
  size_t const i =
      std::clamp<size_t>(std::upper_bound(distances.begin(), distances.end(), location.m_distance) - distances.begin(),
                         1, distances.size() - 1);
  double const length = distances[i] - distances[i - 1];
  double const t = length > 0 ? std::clamp((location.m_distance - distances[i - 1]) / length, 0.0, 1.0) : 0.0;
  return path.m_points[i - 1] + (path.m_points[i] - path.m_points[i - 1]) * t;
}

double VehicleMotion::DirectionAt(Location const & location) const
{
  auto const & path = m_paths[location.m_path];
  auto const & distances = path.m_distances;
  size_t const i =
      std::clamp<size_t>(std::upper_bound(distances.begin(), distances.end(), location.m_distance) - distances.begin(),
                         1, distances.size() - 1);
  m2::PointD const delta = path.m_points[i] - path.m_points[i - 1];
  return Normalize(math::RadToDeg(std::atan2(delta.x, delta.y)));
}
}  // namespace live_transit
