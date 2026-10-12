#pragma once

#include "live_transit/departures.hpp"

#include <string>
#include <vector>

namespace live_transit
{
struct JourneyQuery
{
  // Leaving now.
  ms::LatLon m_from;
  ms::LatLon m_to;
  // Journeys::m_earlier or m_later of an earlier query from |m_from| to |m_to|, for the journeys before or after.
  std::string m_context;
};

struct JourneyStop
{
  std::string m_name;
  ms::LatLon m_latLon;
  LocalTime m_time;
};

struct Leg
{
  // Walks have no line.
  bool m_walk = false;
  TransportMode m_mode = TransportMode::Other;
  std::string m_line;
  std::string m_direction;
  JourneyStop m_from;
  JourneyStop m_to;
  // Where it stops, |m_from| and |m_to| included, when the provider lists them.
  std::vector<JourneyStop> m_stops;
  // From |m_from| to |m_to|: the road or track when the provider has it, otherwise through the stops.
  std::vector<ms::LatLon> m_path;
  // Of walks, 0 when unknown.
  double m_distanceM = 0.0;
};

struct Journey
{
  LocalTime GetDeparture() const { return m_legs.front().m_from.m_time; }
  LocalTime GetArrival() const { return m_legs.back().m_to.m_time; }

  // Never empty.
  std::vector<Leg> m_legs;
};

struct Journeys
{
  Status m_status = Status::Ok;
  // By departure.
  std::vector<Journey> m_journeys;
  // For JourneyQuery::m_context, empty when there are no more.
  std::string m_earlier;
  std::string m_later;
};
}  // namespace live_transit
