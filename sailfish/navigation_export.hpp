#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>

namespace sailfish
{
// Opt-in, process-local snapshots for a projected navigation consumer.
class NavigationExport : public QObject
{
  Q_OBJECT

public:
  NavigationExport();
  ~NavigationExport();
  void Publish(QJsonObject const & navigation);
  QJsonObject Snapshot() const { return m_navigation; }

signals:
  void changed();

private:
  void Write();
  QString m_path;
  QJsonObject m_snapshot;
  QJsonObject m_navigation{{"state", "idle"}};
  QTimer m_heartbeat;
  qint64 m_sequence = 0;
};
}  // namespace sailfish
