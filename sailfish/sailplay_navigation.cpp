#include "sailfish/sailplay_navigation.hpp"

#include "sailplayprovider.h"

#include <QStringList>
#include <QVector>

#include <algorithm>

namespace sailfish
{
namespace
{
QString BoundedText(QString const & text)
{
  auto points = text.toUcs4();
  points.erase(std::remove(points.begin(), points.end(), 0), points.end());
  return QString::fromUcs4(points.constData(), std::min(1024, points.size()));
}

double DistanceMeters(QJsonObject const & navigation, QString const & value, QString const & unit)
{
  // platform::Distance::Units. Keep the SDK distance in meters independently of phone formatting.
  double constexpr factors[] = {1, 1000, 0.3048, 1609.344};
  int const index = navigation.value(unit).toInt(-1);
  return index >= 0 && index < 4 ? navigation.value(value).toDouble() * factors[index] : 0;
}
}  // namespace

QVariantMap SailplayGuidance(QJsonObject const & navigation)
{
  auto guidance = SailplayProvider::navigationGuidance(navigation.value("state").toString("idle"));
  if (guidance.value("state") != "active")
    return guidance;

  // routing::turns::CarDirection keeps these numeric values stable across platforms.
  QStringList const maneuvers = {
      "unknown",          "continue",        "turn-right",          "sharp-right", "slight-right",
      "turn-left",        "sharp-left",      "slight-left",         "uturn",       "uturn",
      "roundabout-enter", "roundabout-exit", "roundabout-continue", "unknown",     "arrive",
      "exit-left",        "exit-right"};
  QStringList const pedestrianManeuvers = {"unknown", "continue", "turn-right", "turn-left", "arrive"};
  QStringList const modes = {"driving", "walking", "cycling", "transit"};
  int const type = navigation.value("routerType").toInt();
  int const turn = navigation.value("turn").toInt();
  guidance["mode"] = modes.value(type, "driving");
  guidance["maneuver"] = type == 1 ? pedestrianManeuvers.value(navigation.value("pedestrianTurn").toInt(), "unknown")
                                   : maneuvers.value(turn, "unknown");
  guidance["drivingSide"] = turn == 9 ? "left" : "right";
  guidance["exitNumber"] = static_cast<uint>(navigation.value("exitNumber").toDouble());
  guidance["remainingSeconds"] = static_cast<qulonglong>(navigation.value("remainingSeconds").toDouble());
  guidance["nextDistanceMeters"] = DistanceMeters(navigation, "nextDistance", "nextDistanceUnit");
  guidance["remainingDistanceMeters"] = DistanceMeters(navigation, "remainingDistance", "remainingDistanceUnit");
  for (auto const & field : {"currentRoad", "nextRoad", "destination"})
    guidance[field] = BoundedText(navigation.value(field).toString());
  return guidance;
}
}  // namespace sailfish
