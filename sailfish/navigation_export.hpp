#pragma once

#include <QJsonObject>
#include <QString>
#include <QTimer>

namespace sailfish
{
// Opt-in, process-local snapshots for a projected navigation consumer.
class NavigationExport
{
public:
  NavigationExport();
  ~NavigationExport();
  void Publish(QJsonObject const & navigation);

private:
  void Write();
  QString m_path;
  QJsonObject m_snapshot;
  QTimer m_heartbeat;
  qint64 m_sequence = 0;
};
}  // namespace sailfish
