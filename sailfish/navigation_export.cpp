#include "sailfish/navigation_export.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

#include <ctime>
#include <unistd.h>

namespace sailfish
{
namespace
{
qint64 BootMilliseconds()
{
  timespec value{};
  if (clock_gettime(CLOCK_BOOTTIME, &value) != 0)
    return -1;
  return value.tv_sec * 1000LL + value.tv_nsec / 1000000;
}

QByteArray ReadFile(QString const & path)
{
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll().trimmed() : QByteArray();
}
}  // namespace

NavigationExport::NavigationExport()
{
  if (qgetenv("SAILPLAY_NAVIGATION_EXPORT") != "1")
    return;
  auto const runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
  if (runtime.isEmpty())
    return;
  auto const directory = runtime + QStringLiteral("/sailplay-navigation");
  if (!QDir().mkpath(directory))
    return;
  QFileInfo const info(directory);
  if (info.isSymLink() || info.ownerId() != geteuid() ||
      !QFile::setPermissions(directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner))
    return;
  auto const stat = ReadFile(QStringLiteral("/proc/self/stat"));
  auto const fields = stat.mid(stat.lastIndexOf(')') + 2).simplified().split(' ');
  auto const boot = ReadFile(QStringLiteral("/proc/sys/kernel/random/boot_id"));
  if (fields.size() <= 19 || boot.isEmpty() || BootMilliseconds() < 0)
    return;
  m_path = directory + QStringLiteral("/organicmaps-%1.json").arg(QCoreApplication::applicationPid());
  m_snapshot = {{"schema", 1}, {"source", "organicmaps"},
                {"pid", static_cast<double>(QCoreApplication::applicationPid())},
                {"startTicks", QString::fromLatin1(fields[19])}, {"bootId", QString::fromLatin1(boot)},
                {"appKey", QString::fromLocal8Bit(qgetenv("CARLIFE_APP_KEY"))}};
  Publish({{"state", "idle"}});
  m_heartbeat.setInterval(1000);
  QObject::connect(&m_heartbeat, &QTimer::timeout, &m_heartbeat, [this] { Write(); });
  m_heartbeat.start();
}

NavigationExport::~NavigationExport()
{
  m_heartbeat.stop();
  if (!m_path.isEmpty())
    QFile::remove(m_path);
}

void NavigationExport::Publish(QJsonObject const & navigation)
{
  if (m_path.isEmpty())
    return;
  m_snapshot["navigation"] = navigation;
  m_snapshot["updatedMs"] = static_cast<double>(BootMilliseconds());
  m_snapshot["sequence"] = static_cast<double>(++m_sequence);
  Write();
}

void NavigationExport::Write()
{
  if (m_path.isEmpty())
    return;
  m_snapshot["heartbeatMs"] = static_cast<double>(BootMilliseconds());
  QSaveFile file(m_path);
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly))
    return;
  if (!file.setPermissions(QFile::ReadOwner | QFile::WriteOwner))
    return;
  auto const data = QJsonDocument(m_snapshot).toJson(QJsonDocument::Compact);
  if (file.write(data) == data.size())
    file.commit();
}
}  // namespace sailfish
