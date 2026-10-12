#pragma once

#include "live_transit/departures.hpp"

#include "geometry/latlon.hpp"
#include "geometry/point2d.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace live_transit
{
// Where a vehicle likely is between its reports: carried on at its speed, along its route when it is on it or else
// straight on, braking to a halt at the next stop, and eased onto each new report rather than jumping to it, or
// waited for when just behind.
class VehicleMotion
{
public:
  // A route as polylines in mercator, possibly in pieces and running either way.
  using Paths = std::vector<std::vector<m2::PointD>>;

  struct Route
  {
    Paths m_paths;
    // Where the vehicles stop, in mercator.
    std::vector<m2::PointD> m_stops;
    // Whether |m_stops| come in the order they are served, which tells the way the vehicles go.
    bool m_ordered = false;
  };

  struct State
  {
    ms::LatLon m_latLon;
    // Degrees clockwise from north.
    std::optional<double> m_bearing;
  };

  // |stop| is where the riders wait: a vehicle on its way there isn't carried past it.
  VehicleMotion(Route const & route, ms::LatLon const & stop);

  // |now| in Unix seconds, as the vehicle's timestamp.
  void Report(Vehicle const & vehicle, double now);
  // For a route known only later, eased onto as a report.
  void SetRoute(Route const & route, double now);
  // Only after a report.
  State Get(double now) const;
  // Whether the last report has it on the route before the stop, going the way of the route's stops; empty when
  // that isn't known.
  std::optional<bool> IsApproaching() const;

private:
  struct Path
  {
    std::vector<m2::PointD> m_points;
    // Meters from the start to each point.
    std::vector<double> m_distances;
    // 1 when the route's stops come in the order of the points, -1 when in reverse, 0 when unknown.
    int m_order = 0;
  };

  struct Location
  {
    size_t m_path = 0;
    // Meters from the path's start.
    double m_distance = 0.0;
  };

  void BuildPaths(Route const & route);
  std::optional<Location> Project(m2::PointD const & point) const;
  // Meters from the start of |path| and from it, of the nearest point on it.
  std::pair<double, double> ProjectOn(Path const & path, m2::PointD const & point) const;
  // 1 when |bearing| runs along the path at |location|, -1 against it.
  int DirectionOf(Location const & location, double bearing) const;
  // Eases from where the vehicle was shown before a change.
  void EaseFrom(std::optional<m2::PointD> const & shown, double now);
  m2::PointD PointAt(Location const & location) const;
  // Degrees clockwise from north, of the way the path runs at |location|.
  double DirectionAt(Location const & location) const;
  // From the last report, without the easing.
  m2::PointD Predict(double now, std::optional<double> & bearing) const;
  // Meters along the path from |m_location| to the next stop the way it goes, if any.
  std::optional<double> ToNextStop() const;

  std::vector<Path> m_paths;
  m2::PointD m_stopPoint;
  std::optional<Location> m_stop;
  // The route's stops on it, |m_stop| among them.
  std::vector<Location> m_stopLocations;

  bool m_reported = false;
  // Of the last report: where and when the vehicle was.
  m2::PointD m_point;
  double m_time = 0.0;
  std::optional<double> m_speed;
  std::optional<double> m_bearing;
  std::optional<Location> m_location;
  // 1 along the path, -1 against it, 0 when unknown.
  int m_direction = 0;
  // How far off the prediction the vehicle was shown at |m_easeStart|, fading away.
  m2::PointD m_offset = m2::PointD::Zero();
  double m_easeStart = 0.0;
  // The time constant of the fading.
  double m_easeS = 1.0;
};
}  // namespace live_transit
