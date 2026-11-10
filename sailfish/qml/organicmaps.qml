import QtQuick 2.6
import Sailfish.Silica 1.0
import Sailfish.Share 1.0
import app.organicmaps 1.0
import "cover"
import "pages"
import "pages/notices.js" as Toast

ApplicationWindow {
    id: appWindow

    property Item mapPage

    initialPage: Component {
        MapPage {
            id: mapPageItem
            Component.onCompleted: appWindow.mapPage = mapPageItem
        }
    }
    cover: Component {
        CoverPage {
            mapPage: appWindow.mapPage
            onActivateRequested: appWindow.activate()
        }
    }
    allowedOrientations: defaultAllowedOrientations
    // The virtual car output has no phone camera cutout.
    defaultPageCutoutMode: sailplayProjected ? 0 : CutoutMode.AvoidLandscapeCutout
    Component.onCompleted: {
        // Phone soft keys reserve a strip even in landscape. The car has its own
        // navigation and touch controls; change only this projected window.
        if (sailplayProjected && appWindow["_softKeyBar"]) {
            appWindow["_softKeyBar"].visible = false
            appWindow["_softKeyBar"].height = 0
        }
        if (sailplayProjected)
            console.log("SAILPLAY_SOFTKEY_LAYOUT", !!appWindow["_softKeyBar"])
        if (mapsStorage.moveFailed)
            Toast.showLong(appInfo.localized("move_maps_error"))
    }

    ShareAction {
        id: fileShare
    }
    Connections {
        target: bookmarksIO
        onExportReady: {
            fileShare.mimeType = mimeType
            fileShare.resources = [fileUrl]
            fileShare.trigger()
        }
        onExportFailed: Toast.show(message)
        onImportFinished: Toast.show(message)
        onBackupFinished: if (!success) Toast.show(appInfo.localized("backup_failed"))
    }

    Connections {
        target: urlHandler
        onActivated: {
            // ApplicationWindow.activate() also restores the physical full-screen
            // geometry. The car compositor already owns this window's viewport.
            if (sailplayProjected)
                appWindow.__quickWindow.requestActivate()
            else
                activate()
        }
    }

    // Offered once, when the user is on the map: e.g. a start by a link opens other pages first.
    property bool updateOfferPending: false
    function offerUpdate() {
        if (!updateOfferPending || pageStack.depth !== 1 || pageStack.busy)
            return
        updateOfferPending = false
        if (!downloads.shouldOfferUpdate() || appSettings.downloadPermission() === AppSettings.DownloadDenied)
            return
        // Accepting also confirms a download over mobile data, which the dialog explains then.
        downloads.setUpdateOffered()
        var mobile = appSettings.downloadPermission() === AppSettings.DownloadAsk
        pageStack.push(Qt.resolvedUrl("pages/MessageDialog.qml"), {
            title: appInfo.localized("whats_new_auto_update_title"),
            message: appInfo.localized("whats_new_auto_update_message")
                     + (mobile ? "\n\n" + appInfo.localized("download_over_mobile_header") + " "
                                 + appInfo.localized("download_over_mobile_message") : ""),
            acceptText: appInfo.localized("whats_new_auto_update_button_size", [downloads.updateSize]),
            cancelText: appInfo.localized("later"),
            acceptAction: function() {
                appSettings.confirmDownload()
                downloads.updateAll()
            }
        })
    }
    // Not the moment the map appears.
    Timer {
        running: true
        interval: 3000
        onTriggered: {
            updateOfferPending = true
            offerUpdate()
        }
    }
    Connections {
        target: pageStack
        onDepthChanged: offerUpdate()
        onBusyChanged: offerUpdate()
    }

}
