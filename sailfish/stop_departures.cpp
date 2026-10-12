#include "sailfish/stop_departures.hpp"

#include "sailfish/live_sources.hpp"
#include "sailfish/transit_providers.hpp"

#include "live_transit/provider.hpp"

#include "platform/platform.hpp"

#include <QDateTime>
#include <QGuiApplication>
#include <QPointer>
#include <QVariantMap>

#include <algorithm>
#include <chrono>
#include <limits>

namespace sailfish
{
namespace
{
// Shown at the most, which also covers the next hour at busy stops.
size_t constexpr kMaxDepartures = 12;
// The feeds update every few seconds; slower keeps within the free request quota.
int constexpr kVehicleIntervalMs = 15000;
// The markers glide over each step.
int constexpr kMotionIntervalMs = 250;

double NowS()
{
  return QDateTime::currentMSecsSinceEpoch() / 1000.0;
}

QString ModeIcon(live_transit::TransportMode mode)
{
  using live_transit::TransportMode;
  switch (mode)
  {
  case TransportMode::Bus: return QStringLiteral("ic_20px_route_planning_bus.svg");
  case TransportMode::Tram: return QStringLiteral("ic_20px_route_planning_tram.svg");
  case TransportMode::Metro: return QStringLiteral("ic_20px_route_planning_metro.webp");
  case TransportMode::Train: return QStringLiteral("ic_20px_route_planning_train.webp");
  case TransportMode::Ferry: return QStringLiteral("ic_20px_route_planning_ferry.svg");
  case TransportMode::Other: return {};
  }
  return {};
}

QString FormatTime(live_transit::LocalTime t)
{
  using namespace std::chrono;
  hh_mm_ss const clock{t - floor<days>(t)};
  return QStringLiteral("%1:%2")
      .arg(clock.hours().count(), 2, 10, QLatin1Char('0'))
      .arg(clock.minutes().count(), 2, 10, QLatin1Char('0'));
}

QString KeyOf(live_transit::Departure const & d)
{
  if (!d.m_tripId.empty())
    return QString::fromStdString(d.m_tripId);
  return QString::fromStdString(d.m_line + '|' + d.m_destination + '|') + FormatTime(d.m_scheduled);
}

QVariantList ToVariant(live_transit::Departures const & result, live_transit::Provider const & provider,
                       std::vector<live_transit::Departure> & shown, std::vector<live_transit::Departure> & live)
{
  shown.clear();
  live.clear();
  using namespace std::chrono;
  QVariantList list;
  for (auto const & d : result.m_departures)
  {
    if (static_cast<size_t>(list.size()) == kMaxDepartures)
      break;
    auto const time = d.GetTime();
    // Gone already, by the provider's clock.
    if (time + 1min < result.m_now)
      continue;
    shown.push_back(d);
    list.append(QVariantMap{
        {"key", KeyOf(d)},
        {"line", QString::fromStdString(d.m_line)},
        {"destination", QString::fromStdString(d.m_destination)},
        {"icon", ModeIcon(d.m_mode)},
        {"minutes", static_cast<int>(std::max(floor<minutes>(time - result.m_now).count(), minutes::rep{0}))},
        {"time", FormatTime(time)},
        {"scheduled", FormatTime(d.m_scheduled)},
        {"delay", static_cast<int>(round<minutes>(time - d.m_scheduled).count())},
        {"realtime", d.m_realtime.has_value()},
        {"platform", QString::fromStdString(d.m_platform)},
        {"train", d.m_mode == live_transit::TransportMode::Train},
        {"canceled", d.m_canceled},
        {"tripId", QString::fromStdString(d.m_tripId)},
        {"agencyId", QString::fromStdString(d.m_agencyId)},
        {"live", provider.CanTrack(d)},
    });
    if (provider.CanTrack(d))
      live.push_back(d);
  }
  return list;
}
}  // namespace

StopDepartures::StopDepartures(QObject * parent) : QObject(parent)
{
  m_vehicleTimer.setInterval(kVehicleIntervalMs);
  connect(&m_vehicleTimer, &QTimer::timeout, this, &StopDepartures::RequestVehicles);
  m_motionTimer.setInterval(kMotionIntervalMs);
  connect(&m_motionTimer, &QTimer::timeout, this, [this]
  {
    // Not on the cover.
    if (QGuiApplication::applicationState() == Qt::ApplicationActive)
      UpdateVehicles();
  });
}

StopDepartures::~StopDepartures() = default;

bool StopDepartures::canExpand() const
{
  return m_active && m_provider->HasTripStops();
}

void StopDepartures::SetStop(double lat, double lon, std::string name,
                             std::optional<live_transit::StopPlatform> platform)
{
  m_provider = GetDeparturesProvider({lat, lon});
  if (!m_provider || !m_provider->HasDepartures())
  {
    Clear();
    return;
  }
  // The place page also updates for the same place, e.g. when it is bookmarked.
  if (m_active && lat == m_lat && lon == m_lon && name == m_name)
    return;
  StopVehicles();
  CollapseTripStops();
  m_active = true;
  m_lat = lat;
  m_lon = lon;
  m_name = std::move(name);
  m_platform = std::move(platform);
  m_departures.clear();
  m_selected.clear();
  m_status = Ok;
  auto const attribution = m_provider->GetAttribution();
  m_attribution = QString::fromUtf8(attribution.data(), static_cast<int>(attribution.size()));
  refresh();
}

void StopDepartures::Clear()
{
  StopVehicles();
  CollapseTripStops();
  ++m_requestId;
  if (!m_active)
    return;
  m_active = false;
  m_loading = false;
  m_departures.clear();
  m_selected.clear();
  emit changed();
}

void StopDepartures::refresh()
{
  if (!m_active)
    return;
  // A key may have been entered meanwhile.
  m_missingKey = MissingKeySource({m_lat, m_lon});
  m_loading = true;
  emit changed();

  QPointer<StopDepartures> self(this);
  auto const id = ++m_requestId;
  GetPlatform().RunTask(
      Platform::Thread::Network,
      [self, id, provider = m_provider, stop = live_transit::Stop{{m_lat, m_lon}, m_name}, platform = m_platform]
  {
    auto result = provider->RequestDepartures(stop);
    if (platform)
      live_transit::KeepPlatform(result, *platform);
    GetPlatform().RunTask(Platform::Thread::Gui, [self, id, provider, result]
    {
      if (!self || id != self->m_requestId)
        return;
      self->m_loading = false;
      self->m_status = static_cast<int>(result.m_status);
      // Earlier departures stay shown when a refresh fails.
      if (result.m_status == live_transit::Status::Ok)
      {
        self->m_departures = ToVariant(result, *provider, self->m_shown, self->m_live);
        self->m_attribution = QString::fromStdString(provider->GetDeparturesAttribution(self->m_shown));
        self->UpdateSelected();
        // Of a departure still shown, with the latest times.
        if (!self->m_expandedKey.isEmpty())
        {
          if (std::ranges::any_of(self->m_shown, [&](auto const & d) { return KeyOf(d) == self->m_expandedKey; }))
            self->RequestTripStops();
          else
            self->expand(self->m_expandedKey);
        }
        self->RequestNextStops();
        if (!self->m_live.empty() && !self->m_vehicleTimer.isActive())
        {
          self->RequestVehicles();
          self->m_vehicleTimer.start();
        }
      }
      emit self->changed();
    });
  });
}

void StopDepartures::hideMissingKey()
{
  HideMissingKeyHint(m_missingKey.value("id").toString());
  m_missingKey.clear();
  emit changed();
}

void StopDepartures::expand(QString const & key)
{
  ++m_tripStopsRequestId;
  m_tripStops.clear();
  m_tripStopsLoading = false;
  m_tripStopsStatus = Ok;
  m_expandedKey = key == m_expandedKey ? QString() : key;
  if (!m_expandedKey.isEmpty())
    RequestTripStops();
  emit tripStopsChanged();
}

void StopDepartures::RequestTripStops()
{
  auto const it = std::ranges::find_if(m_shown, [&](auto const & d) { return KeyOf(d) == m_expandedKey; });
  if (it == m_shown.end() || !m_provider->HasTripStops())
    return;
  m_tripStopsLoading = true;
  emit tripStopsChanged();

  QPointer<StopDepartures> self(this);
  auto const id = ++m_tripStopsRequestId;
  GetPlatform().RunTask(Platform::Thread::Network, [self, id, provider = m_provider, departure = *it,
                                                    stop = live_transit::Stop{{m_lat, m_lon}, m_name}]
  {
    auto const result = provider->RequestTripStops(stop, {departure}).front();
    GetPlatform().RunTask(Platform::Thread::Gui, [self, id, result, departure]
    {
      if (!self || id != self->m_tripStopsRequestId)
        return;
      self->m_tripStopsLoading = false;
      self->m_tripStopsStatus = static_cast<int>(result.m_status);
      // Earlier stops stay shown when a refresh fails.
      if (result.m_status == live_transit::Status::Ok)
      {
        using namespace std::chrono;
        self->m_tripStops.clear();
        for (auto const & stop : result.m_stops)
        {
          self->m_tripStops.append(QVariantMap{
              {"name", QString::fromStdString(stop.m_name)},
              {"lat", stop.m_latLon.m_lat},
              {"lon", stop.m_latLon.m_lon},
              {"time", FormatTime(stop.GetTime())},
              {"delay", static_cast<int>(round<minutes>(stop.GetTime() - stop.m_scheduled).count())},
              {"realtime", stop.m_realtime.has_value()},
          });
        }
      }
      emit self->tripStopsChanged();
    });
  });
}

void StopDepartures::RequestNextStops()
{
  if (!m_provider->HasTripStops())
    return;
  // One of each line and destination, whose direction stays.
  std::vector<live_transit::Departure> departures;
  for (auto const & departure : m_live)
  {
    std::pair<std::string, std::string> route{departure.m_line, departure.m_destination};
    if (m_nextStops.contains(route) || m_nextStopsRequested.contains(route))
      continue;
    m_nextStopsRequested.insert(std::move(route));
    departures.push_back(departure);
  }
  if (departures.empty())
    return;

  QPointer<StopDepartures> self(this);
  GetPlatform().RunTask(Platform::Thread::Network, [self, id = m_pathsRequestId, provider = m_provider, departures,
                                                    stop = live_transit::Stop{{m_lat, m_lon}, m_name}]
  {
    auto const result = provider->RequestTripStops(stop, departures);
    GetPlatform().RunTask(Platform::Thread::Gui, [self, id, departures, result]
    {
      // For an earlier stop.
      if (!self || id != self->m_pathsRequestId)
        return;
      for (size_t i = 0; i < departures.size(); ++i)
      {
        auto & next = self->m_nextStops[{departures[i].m_line, departures[i].m_destination}];
        if (!result[i].m_stops.empty())
          next = result[i].m_stops.front().m_latLon;
      }
    });
  });
}

void StopDepartures::track(QString const & tripId)
{
  auto const it = std::ranges::find(m_live, tripId.toStdString(), &live_transit::Departure::m_tripId);
  if (it == m_live.end())
    return;
  m_tracked = *it;
  for (auto const & item : m_departures)
    if (item.toMap()["tripId"] == tripId)
      m_trackedItem = item.toMap();
  emit vehicleChanged();
  RequestVehicles();
  m_vehicleTimer.start();
}

void StopDepartures::select(QString const & line, QString const & destination, QString const & icon,
                            QString const & color)
{
  m_selected = QVariantMap{{"line", line}, {"destination", destination}, {"icon", icon}, {"color", color}};
  UpdateSelected();
  emit changed();
}

void StopDepartures::UpdateSelected()
{
  // Shown at the most.
  int constexpr kNext = 3;
  if (m_selected.isEmpty())
    return;
  QVariantList next;
  for (auto const & item : m_departures)
  {
    auto const departure = item.toMap();
    if (departure["line"] != m_selected["line"] || departure["destination"] != m_selected["destination"])
      continue;
    next.append(QVariantMap{
        {"minutes", departure["minutes"]}, {"time", departure["time"]}, {"canceled", departure["canceled"]}});
    if (next.size() == kNext)
      break;
  }
  m_selected["next"] = next;
}

void StopDepartures::CollapseTripStops()
{
  if (!m_expandedKey.isEmpty())
    expand(m_expandedKey);
}

void StopDepartures::StopVehicles()
{
  ++m_vehicleRequestId;
  m_vehicleTimer.stop();
  m_live.clear();
  m_tracked.reset();
  m_onTrip.clear();
  m_nextStops.clear();
  m_nextStopsRequested.clear();
  m_motionTimer.stop();
  m_paths.clear();
  ++m_pathsRequestId;
  m_vehicleItems.clear();
  m_motions.clear();
  if (m_vehicles.isEmpty())
    return;
  m_vehicles.clear();
  emit vehicleChanged();
}

void StopDepartures::RequestPaths(std::pair<std::string, std::string> const & route)
{
  QPointer<StopDepartures> self(this);
  m_routePaths(route.first, route.second,
               [self, route, id = m_pathsRequestId](live_transit::VehicleMotion::Route && paths)
  {
    if (!self || id != self->m_pathsRequestId)
      return;
    auto const & routePaths = *(self->m_paths[route] = std::move(paths));
    double const now = NowS();
    for (auto & [tripId, motion] : self->m_motions)
    {
      auto const item = self->m_vehicleItems.find(tripId);
      if (item != self->m_vehicleItems.end() && item->second["line"].toString().toStdString() == route.first &&
          item->second["destination"].toString().toStdString() == route.second)
      {
        motion.SetRoute(routePaths, now);
      }
    }
  });
}

void StopDepartures::UpdateVehicles()
{
  double const now = NowS();
  m_vehicles.clear();
  for (auto const & [tripId, item] : m_vehicleItems)
  {
    auto const state = m_motions.at(tripId).Get(now);
    QVariantMap vehicle = item;
    vehicle["lat"] = state.m_latLon.m_lat;
    vehicle["lon"] = state.m_latLon.m_lon;
    vehicle["bearing"] = state.m_bearing.value_or(std::numeric_limits<double>::quiet_NaN());
    m_vehicles.append(vehicle);
  }
  emit vehicleChanged();
}

void StopDepartures::RequestVehicles()
{
  auto departures = m_live;
  if (m_tracked && std::ranges::find(m_live, m_tracked->m_tripId, &live_transit::Departure::m_tripId) == m_live.end())
    departures.push_back(*m_tracked);

  QPointer<StopDepartures> self(this);
  auto const id = ++m_vehicleRequestId;
  GetPlatform().RunTask(Platform::Thread::Network, [self, id, provider = m_provider, departures = std::move(departures)]
  {
    auto status = live_transit::Status::Ok;
    auto const vehicles = provider->RequestVehicles(departures, status);
    GetPlatform().RunTask(Platform::Thread::Gui, [self, id, status, vehicles]
    {
      // The last positions stay shown when a request fails.
      if (!self || id != self->m_vehicleRequestId || status != live_transit::Status::Ok)
        return;
      double const now = NowS();
      std::map<std::string, QVariantMap> items;
      std::set<std::string> reported;
      for (auto const & vehicle : vehicles)
      {
        reported.insert(vehicle.m_tripId);
        QString const tripId = QString::fromStdString(vehicle.m_tripId);
        auto const shown = std::ranges::find_if(
            self->m_departures, [&](QVariant const & departure) { return departure.toMap()["tripId"] == tripId; });
        bool const gone = shown == self->m_departures.end();
        if (gone && tripId != self->trackedTrip())
          continue;
        QVariantMap const item = gone ? self->m_trackedItem : shown->toMap();
        std::pair<std::string, std::string> const route{item["line"].toString().toStdString(),
                                                        item["destination"].toString().toStdString()};
        auto const [pathsIt, newRoute] = self->m_paths.try_emplace(route);
        if (newRoute && self->m_routePaths)
          self->RequestPaths(route);
        else if (newRoute)
          pathsIt->second.emplace();
        // Which way it goes is told by the route.
        if (!pathsIt->second)
          continue;
        auto motion = self->m_motions.try_emplace(vehicle.m_tripId, *pathsIt->second, ms::LatLon(self->m_lat, self->m_lon))
                          .first;
        motion->second.Report(vehicle, now);

        // The feed has a vehicle on its next trip while it is still on the one before, maybe the other way. Once on its
        // way to the stop, it stays shown past it.
        if (!self->m_onTrip.contains(vehicle.m_tripId))
        {
          ms::LatLon const stop(self->m_lat, self->m_lon);
          auto const next = self->m_nextStops.find(route);
          bool approaching = false;
          if (pathsIt->second->m_ordered)
            approaching = motion->second.IsApproaching().value_or(false);
          else if (next == self->m_nextStops.end() && self->m_provider->HasTripStops())
            continue;  // Which way the trip goes is on its way.
          else if (next != self->m_nextStops.end() && next->second)
            approaching = live_transit::IsApproaching(vehicle, stop, *next->second);
          else
            approaching = live_transit::IsApproaching(vehicle, stop);
          if (!approaching)
            continue;
          self->m_onTrip.insert(vehicle.m_tripId);
        }
        items[vehicle.m_tripId] = QVariantMap{
            {"tripId", tripId},
            {"line", item["line"]},
            {"destination", item["destination"]},
            {"icon", item["icon"]},
            // -1 once it has left.
            {"minutes", gone ? QVariant(-1) : item["minutes"]},
            {"time", item["time"]},
            {"scheduled", item["scheduled"]},
            {"delay", item["delay"]},
            {"realtime", item["realtime"]},
            // Unix time, 0 when unknown.
            {"timestamp", static_cast<double>(vehicle.m_timestamp)},
        };
      }
      // Those not on their way yet are followed to tell which way they go.
      std::erase_if(self->m_motions, [&](auto const & motion) { return !reported.contains(motion.first); });
      self->m_vehicleItems = std::move(items);
      self->UpdateVehicles();
      if (self->m_vehicleItems.empty())
        self->m_motionTimer.stop();
      else if (!self->m_motionTimer.isActive())
        self->m_motionTimer.start();
    });
  });
}
}  // namespace sailfish
