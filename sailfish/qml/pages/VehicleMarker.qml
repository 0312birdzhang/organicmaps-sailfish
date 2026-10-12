import QtQuick 2.6
import Sailfish.Silica 1.0

// A live vehicle of a departure on the map: its mode (or line) on the line's color, pointing the way it goes, with
// a speech bubble about the departure when the map is zoomed in.
Item {
    id: marker

    // MapItem; the uncreatable C++ type can't be a property type in Qt 5.6.
    property QtObject mapItem
    property real lat
    property real lon
    // Degrees clockwise from north, negative when unknown.
    property real bearing: -1
    property string line
    property string destination
    property string icon
    // "#rrggbb", or empty for a line without one.
    property string lineColor
    // Until it leaves, -1 once it has.
    property int minutes: -1
    property string time
    property string scheduled
    // Minutes, of a predicted time.
    property int delay
    property bool realtime
    // Of the position, in Unix time, 0 when unknown.
    property real timestamp
    // Followed from the place page: larger, and always with its bubble.
    property bool tracked

    // The marker or its bubble was tapped, which the map under them doesn't get.
    signal clicked()
    // Seconds since the position, ticking while the bubble shows.
    property int age: 0

    function updateAge() {
        age = timestamp > 0 ? Math.max(0, Math.round(Date.now() / 1000 - timestamp)) : -1
    }
    onTimestampChanged: updateAge()

    readonly property color fillColor: lineColor !== "" ? lineColor : Theme.highlightBackgroundColor
    // As LineBadge.
    readonly property color textColor: lineColor !== ""
                                       && (fillColor.r * 11 + fillColor.g * 16 + fillColor.b * 5) / 32 > 150 / 255
                                       ? "black" : "white"
    // On screen, in degrees clockwise.
    property real heading: NaN

    function update() {
        var p = mapItem.geoToScreen(lat, lon)
        x = p.x - width / 2
        y = p.y - height / 2
        if (bearing < 0) {
            heading = NaN
            return
        }
        // Projects a step along the bearing, which accounts for the map's rotation and tilt.
        var radians = bearing * Math.PI / 180
        var step = 0.0001
        var q = mapItem.geoToScreen(lat + Math.cos(radians) * step,
                                lon + Math.sin(radians) * step / Math.cos(lat * Math.PI / 180))
        heading = Math.atan2(q.x - p.x, p.y - q.y) * 180 / Math.PI
    }

    // Glides over each step of the predicted motion, which comes every 250 ms.
    Behavior on lat { NumberAnimation { duration: 250 } }
    Behavior on lon { NumberAnimation { duration: 250 } }
    onLatChanged: update()
    onLonChanged: update()
    onBearingChanged: update()
    Component.onCompleted: update()

    z: tracked ? 2 : 1
    width: icon !== "" ? height : Math.max(height, lineLabel.implicitWidth + 2 * Theme.paddingSmall)
    height: tracked ? Theme.iconSizeMedium : Theme.iconSizeSmallPlus

    Connections {
        target: marker.mapItem
        onViewportChanged: marker.update()
    }

    // Points the way it goes.
    Item {
        anchors.centerIn: parent
        width: parent.height
        height: width
        rotation: marker.heading
        visible: !isNaN(marker.heading)

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: -height / 2
            width: parent.width * 0.45
            height: width
            rotation: 45
            color: marker.fillColor
        }
    }
    MouseArea {
        anchors.fill: parent
        onClicked: marker.clicked()
    }
    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: marker.fillColor
        border.color: "white"
        border.width: Math.max(1, Math.round(Theme.paddingSmall / (marker.tracked ? 2 : 3)))

        Icon {
            anchors.centerIn: parent
            visible: marker.icon !== ""
            source: visible ? "../../icons/routing/" + marker.icon : ""
            sourceSize: Qt.size(parent.height * 0.6, parent.height * 0.6)
            color: marker.textColor
        }
        Label {
            id: lineLabel
            anchors.centerIn: parent
            visible: marker.icon === ""
            text: marker.line
            color: marker.textColor
            font.pixelSize: Theme.fontSizeExtraSmall
            font.bold: true
        }
    }

    // The departure, at street level.
    Item {
        anchors {
            bottom: parent.top
            bottomMargin: Theme.paddingSmall
            horizontalCenter: parent.horizontalCenter
        }
        width: bubble.width
        height: bubble.height + pointer.height / 2
        visible: marker.tracked || marker.mapItem.zoom >= 15

        Rectangle {
            id: pointer
            anchors {
                horizontalCenter: parent.horizontalCenter
                verticalCenter: bubble.bottom
            }
            width: Theme.paddingMedium
            height: width
            rotation: 45
            color: bubble.color
        }
        Timer {
            interval: 1000
            repeat: true
            running: parent.visible
            onTriggered: marker.updateAge()
        }
        Rectangle {
            id: bubble
            readonly property real maxContentWidth: Theme.itemSizeHuge * 1.6

            width: content.width + 2 * Theme.paddingSmall
            height: content.height + Theme.paddingSmall
            radius: Theme.paddingSmall
            color: Theme.rgba("black", 0.8)

            MouseArea {
                anchors.fill: parent
                onClicked: marker.clicked()
            }

            Column {
                id: content
                anchors.centerIn: parent
                width: Math.min(Math.max(departureRow.implicitWidth, statusRow.implicitWidth), bubble.maxContentWidth)

                Row {
                    id: departureRow
                    spacing: Theme.paddingSmall

                    LineBadge {
                        id: badge
                        number: marker.line
                        lineColor: marker.lineColor
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Label {
                        width: Math.min(implicitWidth, bubble.maxContentWidth - badge.width - minutesLabel.implicitWidth
                                        - 2 * departureRow.spacing)
                        text: marker.destination
                        color: "white"
                        font.pixelSize: Theme.fontSizeExtraSmall
                        truncationMode: TruncationMode.Fade
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Label {
                        id: minutesLabel
                        visible: text !== ""
                        text: marker.minutes < 0 ? ""
                            : marker.minutes === 0 ? appInfo.localized("departures_now")
                            : marker.minutes >= 60 ? marker.time
                            : appInfo.localized("departures_minutes", [String(marker.minutes)])
                        color: "white"
                        font.pixelSize: Theme.fontSizeExtraSmall
                        font.bold: true
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                // The timetable, how late it is, and how fresh the position.
                Row {
                    id: statusRow
                    spacing: Theme.paddingSmall

                    // Green when on time.
                    Label {
                        visible: marker.scheduled !== ""
                        text: marker.scheduled
                        color: marker.realtime && marker.delay === 0 ? "#7ddc7d" : Theme.rgba("white", 0.7)
                        font.pixelSize: Theme.fontSizeTiny
                    }
                    Label {
                        visible: marker.realtime && marker.delay !== 0
                        text: (marker.delay > 0 ? "+" : "−") + Math.abs(marker.delay)
                        color: marker.delay > 0 ? "#ff6e6e" : "#7ddc7d"
                        font.pixelSize: Theme.fontSizeTiny
                        font.bold: true
                    }
                    // Live, colored by how long ago.
                    Rectangle {
                        width: Theme.paddingSmall
                        height: width
                        radius: width / 2
                        color: marker.age < 0 ? Theme.rgba("white", 0.7)
                             : marker.age > 60 ? "#ff6e6e"
                             : marker.age > 40 ? "#ffd84d"
                             : "#7ddc7d"
                        anchors.verticalCenter: parent.verticalCenter

                        SequentialAnimation on opacity {
                            running: statusRow.visible
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.3; duration: 800 }
                            NumberAnimation { to: 1.0; duration: 800 }
                        }
                    }
                }
            }
        }
    }
}
