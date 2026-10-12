import QtQuick 2.6
import Sailfish.Silica 1.0
import "../pages"

CoverBackground {
    id: cover

    property Item mapPage
    readonly property QtObject map: mapPage ? mapPage.mapItem : null
    readonly property QtObject routing: map ? map.routing : null
    readonly property var navigation: routing ? routing.navigation : ({})
    readonly property var position: map ? map.positionInfo : ({})
    readonly property QtObject departures: map ? map.placePage.departures : null
    // The line picked at the open stop.
    readonly property var line: departures && map.placePage.open ? departures.selected : ({})
    readonly property string mode: !map ? "idle"
        : routing.navigating ? "navigation"
        : map.trackRecording ? "recording"
        : downloads.inProgress && downloads.downloadingName !== "" ? "download"
        : line.line !== undefined ? "departures"
        : routing.built ? "route"
        : "idle"

    function departureText(departure) {
        return departure.canceled ? appInfo.localized("departures_canceled")
            : departure.minutes === 0 ? appInfo.localized("departures_now")
            : departure.minutes >= 60 ? departure.time
            : appInfo.localized("departures_minutes", [String(departure.minutes)])
    }

    signal activateRequested()

    Row {
        id: header
        x: Theme.paddingLarge
        y: Theme.paddingMedium
        spacing: Theme.paddingSmall

        Image {
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.iconSizeSmall
            height: width
            sourceSize: Qt.size(width, height)
            source: "../../icons/help/logo.svg"
        }
        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: "Organic Maps"
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryColor
        }
    }

    Column {
        anchors {
            top: header.bottom
            topMargin: Theme.paddingMedium
            left: parent.left
            leftMargin: Theme.paddingLarge
            right: parent.right
            rightMargin: Theme.paddingLarge
        }
        spacing: Theme.paddingSmall

        LineBadge {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: cover.mode === "departures"
            icon: cover.line.icon || ""
            number: cover.line.line || ""
            lineColor: cover.line.color || ""
        }
        Icon {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: cover.mode === "navigation"
            width: Theme.iconSizeLarge
            height: width
            sourceSize: Qt.size(width, height)
            source: navigation.turnIcon ? "../../icons/navigation/" + navigation.turnIcon : ""
            color: Theme.primaryColor
        }
        Icon {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: cover.mode === "recording" || cover.mode === "download"
            width: Theme.iconSizeLarge
            height: width
            sourceSize: Qt.size(width, height)
            source: cover.mode === "recording" ? "../../icons/menu/ic_track_recording_status.svg"
                                               : "image://theme/icon-m-cloud-download"
            color: cover.mode === "recording" ? Theme.errorColor : Theme.primaryColor
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: text !== ""
            text: {
                switch (cover.mode) {
                case "navigation": return navigation.distanceToTurn || ""
                case "recording": return appInfo.localized("track_recording_title")
                case "download": return Math.round(downloads.downloadingProgress * 100) + "%"
                case "route": return routing.summary
                case "departures":
                    var next = cover.line.next || []
                    return next.length > 0 ? cover.departureText(next[0]) : appInfo.localized("departures_none")
                default: return position.address || ""
                }
            }
            font.pixelSize: cover.mode === "idle" || cover.mode === "route" ? Theme.fontSizeMedium : Theme.fontSizeLarge
            font.bold: cover.mode === "navigation" || cover.mode === "download" || cover.mode === "departures"
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: text !== ""
            text: {
                switch (cover.mode) {
                case "navigation": return navigation.street || ""
                case "recording": return map.recordingSummary
                case "download": return downloads.downloadingName
                case "route":
                    var points = routing.points
                    return points.length > 0 ? "→ " + points[points.length - 1].title : ""
                case "departures": return "→ " + cover.line.destination
                default: return position.coordinates || ""
                }
            }
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.secondaryColor
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: text !== ""
            text: {
                switch (cover.mode) {
                case "navigation": return navigation.arrival ? "⚑ " + navigation.arrival : ""
                // The ones after, and where.
                case "departures":
                    var later = (cover.line.next || []).slice(1).map(cover.departureText).join("   ")
                    return later !== "" ? later + "\n" + cover.departures.stopName : cover.departures.stopName
                case "idle":
                    return [position.altitude, position.speed].filter(function(part) { return !!part }).join("   ")
                default: return ""
                }
            }
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryHighlightColor
        }
        Rectangle {
            visible: cover.mode === "download"
            width: parent.width
            height: Theme.paddingSmall / 2
            color: Theme.rgba(Theme.primaryColor, 0.2)

            Rectangle {
                width: parent.width * downloads.downloadingProgress
                height: parent.height
                color: Theme.highlightColor
            }
        }
    }

    // Mute only with a voice to mute.
    CoverActionList {
        enabled: cover.mode === "navigation" && routing.voiceAvailable
        CoverAction {
            iconSource: routing && routing.voiceEnabled ? "image://theme/icon-cover-mute"
                                                        : "image://theme/icon-cover-unmute"
            onTriggered: routing.voiceEnabled = !routing.voiceEnabled
        }
        CoverAction {
            iconSource: "image://theme/icon-cover-cancel"
            onTriggered: routing.stopNavigation()
        }
    }
    CoverActionList {
        enabled: cover.mode === "navigation" && !routing.voiceAvailable
        CoverAction {
            iconSource: "image://theme/icon-cover-cancel"
            onTriggered: routing.stopNavigation()
        }
    }
    CoverActionList {
        enabled: cover.mode === "recording"
        CoverAction {
            iconSource: "image://theme/icon-cover-favorite"
            onTriggered: map.saveAndStopTrackRecording()
        }
    }
    CoverActionList {
        enabled: cover.mode === "download"
        CoverAction {
            iconSource: "image://theme/icon-cover-cancel"
            onTriggered: downloads.cancelAll()
        }
    }
    // Opens the app when the route needs confirming first.
    CoverActionList {
        enabled: cover.mode === "route"
        CoverAction {
            iconSource: "image://theme/icon-cover-play"
            onTriggered: {
                if (routing.canStart && routing.startIsMyPosition && routing.disclaimerAccepted)
                    routing.start()
                else
                    cover.activateRequested()
            }
        }
    }
    CoverActionList {
        enabled: cover.mode === "idle"
        CoverAction {
            iconSource: "image://theme/icon-cover-search"
            onTriggered: {
                cover.activateRequested()
                mapPage.openSearchFromCover()
            }
        }
    }
}
