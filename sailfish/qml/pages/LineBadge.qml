import QtQuick 2.6
import Sailfish.Silica 1.0

// A transit line's mode and number on its color, as on the map.
Rectangle {
    id: badge

    // In sailfish/icons/routing, or none.
    property string icon
    property string number
    // "#rrggbb", or empty for a line without one.
    property string lineColor
    // Like qGray(), readable on light line colors too.
    readonly property color textColor: lineColor === "" ? Theme.primaryColor
                                       : (color.r * 11 + color.g * 16 + color.b * 5) / 32 > 150 / 255 ? "black"
                                       : "white"

    width: content.width + 2 * Theme.paddingSmall
    height: Theme.iconSizeSmall + Theme.paddingSmall
    radius: Theme.paddingSmall
    color: lineColor !== "" ? lineColor : Theme.rgba(Theme.primaryColor, 0.15)

    Row {
        id: content
        anchors.centerIn: parent
        spacing: Theme.paddingSmall

        Icon {
            visible: badge.icon !== ""
            source: visible ? "../../icons/routing/" + badge.icon : ""
            sourceSize: Qt.size(Theme.iconSizeExtraSmall, Theme.iconSizeExtraSmall)
            color: badge.textColor
            anchors.verticalCenter: parent.verticalCenter
        }
        Label {
            visible: badge.number !== ""
            text: badge.number
            color: badge.textColor
            font.pixelSize: Theme.fontSizeExtraSmall
            font.bold: true
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
