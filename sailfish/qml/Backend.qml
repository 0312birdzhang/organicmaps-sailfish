import QtQuick 2.6
import Nemo.KeepAlive 1.2
import Nemo.Notifications 1.0
import "pages/notifications.js" as Notifications

Item {
    // This tree belongs to the application backend and has no window.
    KeepAlive {
        enabled: mapCore.trackRecording || mapCore.routing.navigating || mapCore.routing.building
                 || downloads.inProgress || bookmarksIO.busy || osmAccount.busy
    }

    Notification {
        id: recordingNotification
        appName: "Organic Maps"
        appIcon: appInfo.name
        summary: mapCore.locationLost ? appInfo.localized("current_location_unknown_error_title")
                                      : appInfo.localized("track_recording")
        body: mapCore.locationLost ? appInfo.localized("dialog_routing_location_turn_wifi") : ""
        remoteActions: [Notifications.openApp(),
                        Notifications.action("stop", appInfo.localized("track_recording_stop_and_save"),
                                             "stopTrackRecording")]
    }
    Connections {
        target: mapCore
        onTrackRecordingChanged: {
            if (mapCore.trackRecording)
                recordingNotification.publish()
            else
                recordingNotification.close()
        }
        onLocationLostChanged: if (mapCore.trackRecording) recordingNotification.publish()
    }

    Notification {
        id: downloadNotification
        appName: "Organic Maps"
        appIcon: appInfo.name
        summary: appInfo.localized("downloader_downloading") + " " + downloads.downloadingName
        progress: downloads.downloadingProgress
        remoteActions: [Notifications.openApp(),
                        Notifications.action("cancel", appInfo.localized("cancel"), "cancelDownloads")]
    }
    Timer {
        running: downloads.inProgress && downloads.downloadingName !== ""
        repeat: true
        triggeredOnStart: true
        interval: 3000
        onTriggered: downloadNotification.publish()
    }
    Connections {
        target: downloads
        onInProgressChanged: if (!downloads.inProgress) downloadNotification.close()
        onDownloadFailed: {
            if (Qt.application.state === Qt.ApplicationActive)
                return
            failedNotification.body = appInfo.localized("download_country_failed", [name])
            failedNotification.previewBody = failedNotification.body
            failedNotification.publish()
        }
    }
    Notification {
        id: failedNotification
        appName: "Organic Maps"
        appIcon: appInfo.name
        summary: "Organic Maps"
        previewSummary: summary
        remoteActions: [Notifications.openApp()]
    }

    Component.onCompleted: if (mapCore.trackRecording) recordingNotification.publish()
    Component.onDestruction: {
        recordingNotification.close()
        downloadNotification.close()
    }
}
