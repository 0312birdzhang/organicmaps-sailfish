#include "sailfish/app_info.hpp"
#include "sailfish/app_settings.hpp"
#include "sailfish/bookmark_editor.hpp"
#include "sailfish/bookmarks_io.hpp"
#include "sailfish/bookmarks_model.hpp"
#include "sailfish/countries_model.hpp"
#include "sailfish/framework_access.hpp"
#include "sailfish/map_item.hpp"
#include "sailfish/map_surface.hpp"
#include "sailfish/maps_storage.hpp"
#include "sailfish/opening_hours_editor.hpp"
#include "sailfish/osm_account.hpp"
#include "sailfish/phone_presentation.hpp"
#include "sailfish/place_editor.hpp"
#include "sailfish/place_page.hpp"
#include "sailfish/routing.hpp"
#include "sailfish/search_model.hpp"
#include "sailfish/url_handler.hpp"

#ifdef OMIM_SAILPLAY
#include "sailfish/sailplay_navigation.hpp"
#include "sailplayprovider.h"
#endif

#include "map/framework.hpp"

#include "platform/platform.hpp"
#include "platform/preferred_languages.hpp"

#include "base/logging.hpp"

#include <qqml.h>
#include <QDir>
#include <QEventLoop>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickView>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QTimer>

#include <atomic>
#include <clocale>
#include <cstdlib>
#include <memory>
#include <thread>

#include <sailfishapp.h>

namespace
{
Framework * g_framework = nullptr;

void SetEnvIfUnset(char const * name, QString const & value)
{
  if (qEnvironmentVariableIsEmpty(name))
    qputenv(name, value.toUtf8());
}

void SetDefaultSurfaceFormat()
{
  // Drape's contexts share resources with the scene graph through the global share context.
  QGuiApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QSurfaceFormat format;
  format.setRenderableType(QSurfaceFormat::OpenGLES);
  format.setVersion(3, 0);
  format.setDepthBufferSize(24);
  format.setStencilBufferSize(8);
  QSurfaceFormat::setDefaultFormat(format);
}

// The copy can take minutes, e.g. to a slow memory card, so its progress is shown meanwhile. Without text: the
// strings come from the platform, which may only start once the maps folder is known.
QString MoveMaps(bool showProgress)
{
  if (!showProgress || !sailfish::MapsStorage::HasPendingMove())
    return sailfish::MapsStorage::ApplyPendingMove([](double) {});

  std::unique_ptr<QQuickView> view(SailfishApp::createView());
  view->setSource(SailfishApp::pathTo(QStringLiteral("qml/MovingMaps.qml")));
  view->show();
  std::atomic<double> progress{0};
  QString dir;
  QEventLoop loop;
  std::thread worker([&]
  {
    dir = sailfish::MapsStorage::ApplyPendingMove([&](double copied) { progress = copied; });
    QMetaObject::invokeMethod(&loop, "quit", Qt::QueuedConnection);
  });
  QTimer timer;
  QObject::connect(&timer, &QTimer::timeout, [&] { view->rootObject()->setProperty("progress", progress.load()); });
  timer.start(500);
  loop.exec();
  worker.join();
  return dir;
}

void SetupPlatform(bool showProgress)
{
  // Platform reads these on first use; the defaults match the installed RPM layout and Sailjail.
  SetEnvIfUnset("MWM_RESOURCES_DIR", SailfishApp::pathTo(QStringLiteral("data")).toLocalFile());
  QString writableDir = MoveMaps(showProgress);
  if (writableDir.isEmpty())
    writableDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(writableDir);
  SetEnvIfUnset("MWM_WRITABLE_DIR", writableDir);

  sailfish::AppSettings::InitLogging();
  LOG(LINFO, ("Resources:", GetPlatform().ResourcesDir(), "Writable:", GetPlatform().WritableDir(),
              "Settings:", GetPlatform().SettingsDir()));
}

void RegisterQmlTypes()
{
  qmlRegisterUncreatableType<sailfish::MapItem>("app.organicmaps", 1, 0, "MapItem", "Use mapCore");
  qmlRegisterType<sailfish::MapSurface>("app.organicmaps", 1, 0, "MapSurface");
  qmlRegisterType<sailfish::CountriesModel>("app.organicmaps", 1, 0, "CountriesModel");
  qmlRegisterType<sailfish::SearchModel>("app.organicmaps", 1, 0, "SearchModel");
  qmlRegisterType<sailfish::BookmarkCategoriesModel>("app.organicmaps", 1, 0, "BookmarkCategoriesModel");
  qmlRegisterType<sailfish::BookmarksModel>("app.organicmaps", 1, 0, "BookmarksModel");
  qmlRegisterType<sailfish::BookmarkEditor>("app.organicmaps", 1, 0, "BookmarkEditor");
  qmlRegisterType<sailfish::PlaceEditor>("app.organicmaps", 1, 0, "PlaceEditor");
  qmlRegisterType<sailfish::PlaceCategories>("app.organicmaps", 1, 0, "PlaceCategories");
  qmlRegisterType<sailfish::OpeningHoursEditor>("app.organicmaps", 1, 0, "OpeningHoursEditor");
  qmlRegisterUncreatableType<sailfish::PlacePage>("app.organicmaps", 1, 0, "PlacePage", "Owned by MapItem");
  qmlRegisterUncreatableType<sailfish::Routing>("app.organicmaps", 1, 0, "Routing", "Owned by MapItem");
  qmlRegisterUncreatableType<sailfish::AppSettings>("app.organicmaps", 1, 0, "AppSettings", "Use appSettings");
  qmlRegisterUncreatableType<sailfish::BookmarksIO>("app.organicmaps", 1, 0, "BookmarksIO", "Use bookmarksIO");
}
}  // namespace

namespace sailfish
{
Framework & GetFramework()
{
  return *g_framework;
}
}  // namespace sailfish

__attribute__((visibility("default"))) int OrganicMapsMain(int argc, char * argv[])
{
  SetDefaultSurfaceFormat();
  SetEnvIfUnset("QT_QPA_PLATFORM", "wayland");
  SetEnvIfUnset("WAYLAND_DISPLAY", "../../display/wayland-0");
  SetEnvIfUnset("QT_IM_MODULE", "Maliit");

  std::unique_ptr<QGuiApplication> app(SailfishApp::application(argc, argv));
  app->setQuitOnLastWindowClosed(false);
  // After the application, which sets the system locale: strtod() in platform/measurement_utils.cpp and
  // search/latlon_match.cpp needs dots as decimal separators.
  std::setlocale(LC_NUMERIC, "C");
  app->setOrganizationName(QStringLiteral("app.organicmaps"));
  app->setApplicationName(QStringLiteral("organicmaps"));

  QStringList urls = app->arguments().mid(1);
  bool const background = urls.removeAll(QStringLiteral("--background")) > 0;
  // Single instance: a second one would write the same settings, bookmarks and edits.
  sailfish::DBusInbox dbusInbox;
  if (!dbusInbox.Claim())
  {
    if (!background || !urls.isEmpty())
      sailfish::DBusInbox::OpenInRunningApp(urls);
    return 0;
  }

  SetupPlatform(!background);

  Framework framework;
  framework.SetupMeasurementSystem();
  framework.GetStorage().SetLocale(languages::GetCurrentTwine());
  g_framework = &framework;
  sailfish::BookmarksNotifier::LoadBookmarks(framework);

  RegisterQmlTypes();

  // Declared before the view, which uses them until it is destroyed.
  sailfish::AppInfo appInfo;
  sailfish::AppSettings appSettings(framework);
  sailfish::OsmAccount osmAccount;
  sailfish::BookmarksIO bookmarksIO(framework);
  sailfish::MapsStorage mapsStorage(framework);
  sailfish::DownloadStatus downloads(framework.GetStorage());
  sailfish::UrlHandler urlHandler(framework, bookmarksIO);
  sailfish::MapItem mapCore;
  QQmlEngine::setObjectOwnership(&mapCore, QQmlEngine::CppOwnership);
  // The OpenStreetMap login in the browser returns by an om:// link.
  QObject::connect(&urlHandler, &sailfish::UrlHandler::oauth2CodeReceived, &osmAccount,
                   &sailfish::OsmAccount::LoginWithCode);
  bool const projected = qgetenv("WAYLAND_DISPLAY").contains("imira-comp") &&
                         qgetenv("SAILPLAY_NAVIGATION_EXPORT") == "1";
  auto const setupContext = [&](QQmlContext * context)
  {
    context->setContextProperty(QStringLiteral("appInfo"), &appInfo);
    context->setContextProperty(QStringLiteral("appSettings"), &appSettings);
    context->setContextProperty(QStringLiteral("osmAccount"), &osmAccount);
    context->setContextProperty(QStringLiteral("bookmarksIO"), &bookmarksIO);
    context->setContextProperty(QStringLiteral("urlHandler"), &urlHandler);
    context->setContextProperty(QStringLiteral("mapsStorage"), &mapsStorage);
    context->setContextProperty(QStringLiteral("downloads"), &downloads);
    context->setContextProperty(QStringLiteral("mapCore"), &mapCore);
    context->setContextProperty(QStringLiteral("sailplayProjected"), projected);
  };
  sailfish::PhonePresentation presentation(SailfishApp::pathToMainQml(), setupContext);
  QObject::connect(&urlHandler, &sailfish::UrlHandler::activated, &presentation, [&] { presentation.Show(); });
  QObject::connect(&urlHandler, &sailfish::UrlHandler::phoneCloseRequested, &presentation,
                   &sailfish::PhonePresentation::Close, Qt::QueuedConnection);
  QObject::connect(&urlHandler, &sailfish::UrlHandler::cancelDownloadsRequested, &downloads,
                   &sailfish::DownloadStatus::cancelAll);
  QObject::connect(&urlHandler, &sailfish::UrlHandler::stopTrackRecordingRequested, &mapCore, [&]
  {
    if (!presentation.isOpen() && mapCore.trackRecording())
      mapCore.saveAndStopTrackRecording();
  });

  // Keep notifications and suspend inhibition with the backend when the phone tree is released.
  QQmlEngine backendEngine;
  setupContext(backendEngine.rootContext());
  QQmlComponent backendComponent(&backendEngine, SailfishApp::pathTo(QStringLiteral("qml/Backend.qml")));
  std::unique_ptr<QObject> backendObject(backendComponent.create());
  if (!backendObject)
  {
    for (auto const & error : backendComponent.errors())
      LOG(LERROR, (error.toString().toStdString()));
    return EXIT_FAILURE;
  }

#ifdef OMIM_SAILPLAY
  QVariantMap guidance = sailfish::SailplayGuidance(mapCore.routing()->NavigationSnapshot());
  auto const state = [&]
  {
    return QVariantMap{{"navigation", guidance},
                       {"phoneOpen", presentation.isOpen()},
                       {"navigating", mapCore.routing()->navigating()},
                       {"building", mapCore.routing()->building()},
                       {"recording", mapCore.trackRecording()},
                       {"downloading", downloads.inProgress()}};
  };
  SailplayProvider provider(QStringLiteral(SAILFISH_APP_NAME), state(), {QStringLiteral("navigation")});
  provider.setActionHandler(QStringLiteral("navigation.stop"),
                            [&](qulonglong invocation, QVariantMap const & parameters)
  {
    if (!parameters.isEmpty())
    {
      provider.fail(invocation, "InvalidArgument", "navigation.stop takes no parameters");
      return;
    }
    mapCore.routing()->close();
    provider.complete(invocation, {{"stopped", true}});
  });
  if (!provider.start())
  {
    LOG(LERROR, ("Can't export the Sailplay navigation backend"));
    return EXIT_FAILURE;
  }
  auto const publish = [&] { provider.publish(state()); };
  QObject::connect(mapCore.routing(), &sailfish::Routing::guidanceChanged, &provider, [&]
  {
    guidance = sailfish::SailplayGuidance(mapCore.routing()->NavigationSnapshot());
    publish();
  });
  QObject::connect(mapCore.routing(), &sailfish::Routing::stateChanged, &provider, publish);
  QObject::connect(mapCore.routing(), &sailfish::Routing::navigatingChanged, &provider, publish);
  QObject::connect(&mapCore, &sailfish::MapItem::trackRecordingChanged, &provider, publish);
  QObject::connect(&downloads, &sailfish::DownloadStatus::inProgressChanged, &provider, publish);
  QObject::connect(&presentation, &sailfish::PhonePresentation::changed, &provider, publish);
#endif

  QTimer idleTimer;
  idleTimer.setSingleShot(true);
  idleTimer.setInterval(10000);
  auto const hasViews = [&]
  {
    bool wanted = presentation.isOpen();
#ifdef OMIM_SAILPLAY
    wanted = wanted || provider.sessionCount() > 0;
#endif
    return wanted;
  };
  auto const updateLifetime = [&]
  {
    if (hasViews())
      idleTimer.stop();
    else if (!idleTimer.isActive())
      idleTimer.start();
  };
  QObject::connect(&presentation, &sailfish::PhonePresentation::changed, &idleTimer, updateLifetime);
#ifdef OMIM_SAILPLAY
  QObject::connect(&provider, &SailplayProvider::sessionsChanged, &idleTimer, updateLifetime);
#endif
  QObject::connect(&idleTimer, &QTimer::timeout, &idleTimer, [&]
  {
    bool working = mapCore.routing()->HasBackgroundWork() || mapCore.trackRecording() || downloads.inProgress() ||
                   bookmarksIO.busy() || osmAccount.busy();
#ifdef OMIM_SAILPLAY
    working = working || provider.pendingCount() > 0;
#endif
    if (!hasViews() && !working)
      app->quit();
    else
      updateLifetime();
  });
  if (!background && !presentation.Show())
    return EXIT_FAILURE;
  if (!urls.isEmpty())
    urlHandler.openUrl(urls);
  dbusInbox.SetHandler(urlHandler);
  updateLifetime();

  return app->exec();
}
