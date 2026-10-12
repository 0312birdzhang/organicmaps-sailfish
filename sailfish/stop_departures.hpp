#pragma once

#include "live_transit/departures.hpp"
#include "live_transit/vehicle_motion.hpp"

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace live_transit
{
class Provider;
}  // namespace live_transit

namespace sailfish
{
// Live departures at the place page's stop, from an online provider.
class StopDepartures : public QObject
{
  Q_OBJECT
  // A provider covers the stop.
  Q_PROPERTY(bool available READ available NOTIFY changed)
  Q_PROPERTY(bool loading READ loading NOTIFY changed)
  Q_PROPERTY(int status READ status NOTIFY changed)
  // {key, line, destination, icon, minutes, time, scheduled, delay, realtime, platform, train, canceled, tripId, agencyId,
  // live}; minutes until it leaves, as the provider's clock tells; realtime when the time is predicted; live when its
  // vehicle can be tracked.
  Q_PROPERTY(QVariantList departures READ departures NOTIFY changed)
  Q_PROPERTY(QString attribution READ attribution NOTIFY changed)
  // A source with a key that would give more here, see MissingKeySource().
  Q_PROPERTY(QVariantMap missingKey READ missingKey NOTIFY changed)
  Q_PROPERTY(QString stopName READ stopName NOTIFY changed)
  // The line picked from the departures as {line, destination, icon, color, next}, next as the first departures
  // of the line in the same direction, each {minutes, time, canceled}; empty when none is picked.
  Q_PROPERTY(QVariantMap selected READ selected NOTIFY changed)
  // The vehicles of the live departures on their way to the stop, at their last known positions, as {tripId, lat,
  // lon, bearing, timestamp} and the departure's line, destination, icon, minutes, time, scheduled, delay and realtime;
  // bearing in degrees clockwise from north, NaN when unknown; timestamp of the position in Unix time, 0 when unknown.
  // Carried on between the reports, along their routes when known.
  Q_PROPERTY(QVariantList vehicles READ vehicles NOTIFY vehicleChanged)
  // The departure followed from the place page, whose vehicle stands out.
  Q_PROPERTY(QString trackedTrip READ trackedTrip NOTIFY vehicleChanged)
  // Whether the provider tells where a departure's trip calls next.
  Q_PROPERTY(bool canExpand READ canExpand NOTIFY changed)
  // The key of the departure whose trip's stops are shown, empty when none.
  Q_PROPERTY(QString expandedKey READ expandedKey NOTIFY tripStopsChanged)
  // Where its trip calls after the stop, as {name, lat, lon, time, delay, realtime}.
  Q_PROPERTY(QVariantList tripStops READ tripStops NOTIFY tripStopsChanged)
  Q_PROPERTY(bool tripStopsLoading READ tripStopsLoading NOTIFY tripStopsChanged)
  Q_PROPERTY(int tripStopsStatus READ tripStopsStatus NOTIFY tripStopsChanged)

public:
  // Same as live_transit::Status.
  enum Status
  {
    Ok,
    StopNotFound,
    NetworkError,
    Unauthorized,
    ServerError,
    NoResult,
  };
  Q_ENUM(Status)

  explicit StopDepartures(QObject * parent = nullptr);
  ~StopDepartures() override;

  bool available() const { return m_active; }
  bool loading() const { return m_loading; }
  int status() const { return m_status; }
  QVariantList departures() const { return m_departures; }
  QString attribution() const { return m_attribution; }
  QVariantMap missingKey() const { return m_missingKey; }
  QString stopName() const { return QString::fromStdString(m_name); }
  QVariantMap selected() const { return m_selected; }
  QVariantList vehicles() const { return m_vehicles; }
  QString trackedTrip() const { return QString::fromStdString(m_tracked ? m_tracked->m_tripId : std::string()); }
  bool canExpand() const;
  QString expandedKey() const { return m_expandedKey; }
  QVariantList tripStops() const { return m_tripStops; }
  bool tripStopsLoading() const { return m_tripStopsLoading; }
  int tripStopsStatus() const { return m_tripStopsStatus; }

  Q_INVOKABLE void refresh();
  Q_INVOKABLE void select(QString const & line, QString const & destination, QString const & icon,
                          QString const & color);
  // Follows a live departure's vehicle, also after it leaves, until another stop or none is shown.
  Q_INVOKABLE void track(QString const & tripId);
  // Shows where a departure's trip calls next, refreshed with the departures, or hides it when shown.
  Q_INVOKABLE void expand(QString const & key);
  // Stops hinting at the missing key, also at other stops.
  Q_INVOKABLE void hideMissingKey();

  // Clears without a provider for the position. Of a platform only its departures are shown, rather than those of
  // the whole stop.
  void SetStop(double lat, double lon, std::string name, std::optional<live_transit::StopPlatform> platform);
  // Builds the route at the stop of the line to a destination off the GUI thread, then passes it on in the GUI
  // thread, empty when the route is unknown.
  using OnPaths = std::function<void(live_transit::VehicleMotion::Route && route)>;
  using RoutePaths = std::function<void(std::string const & line, std::string const & destination, OnPaths onPaths)>;
  void SetRoutePaths(RoutePaths routePaths) { m_routePaths = std::move(routePaths); }
  void Clear();

signals:
  void changed();
  void vehicleChanged();
  void tripStopsChanged();

private:
  void RequestVehicles();
  void RequestTripStops();
  // Where the live departures' trips call next, telling which way they go.
  void RequestNextStops();
  void CollapseTripStops();
  void UpdateSelected();
  void StopVehicles();
  void RequestPaths(std::pair<std::string, std::string> const & route);
  // Moves the vehicles on to where they likely are now.
  void UpdateVehicles();

  std::shared_ptr<live_transit::Provider> m_provider;
  bool m_active = false;
  double m_lat = 0.0;
  double m_lon = 0.0;
  std::string m_name;
  std::optional<live_transit::StopPlatform> m_platform;
  // Results of earlier requests are dropped.
  quint64 m_requestId = 0;
  bool m_loading = false;
  int m_status = Ok;
  QVariantList m_departures;
  QString m_attribution;
  QVariantMap m_missingKey;
  QVariantMap m_selected;

  std::vector<live_transit::Departure> m_shown;
  QString m_expandedKey;
  QVariantList m_tripStops;
  bool m_tripStopsLoading = false;
  int m_tripStopsStatus = Ok;
  quint64 m_tripStopsRequestId = 0;

  // The shown departures whose vehicles can be tracked.
  std::vector<live_transit::Departure> m_live;
  std::optional<live_transit::Departure> m_tracked;
  // As in |m_departures|, kept for the tracked one after it leaves.
  QVariantMap m_trackedItem;
  QTimer m_vehicleTimer;
  quint64 m_vehicleRequestId = 0;
  // Trips whose vehicles were seen heading for the stop, so on the departure's trip.
  std::set<std::string> m_onTrip;
  // By line and destination, none when unknown.
  std::map<std::pair<std::string, std::string>, std::optional<ms::LatLon>> m_nextStops;
  std::set<std::pair<std::string, std::string>> m_nextStopsRequested;
  RoutePaths m_routePaths;
  // By line and destination; none until built.
  std::map<std::pair<std::string, std::string>, std::optional<live_transit::VehicleMotion::Route>> m_paths;
  // Paths built for an earlier stop are dropped.
  quint64 m_pathsRequestId = 0;
  // By trip: the shown vehicles' departures and motions.
  std::map<std::string, QVariantMap> m_vehicleItems;
  std::map<std::string, live_transit::VehicleMotion> m_motions;
  QTimer m_motionTimer;
  QVariantList m_vehicles;
};
}  // namespace sailfish
