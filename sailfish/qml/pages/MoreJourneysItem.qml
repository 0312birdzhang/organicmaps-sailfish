import QtQuick 2.6
import Sailfish.Silica 1.0

// Loads the transit journeys before or after the listed ones.
BackgroundItem {
    property alias text: label.text
    property bool busy

    width: parent.width
    height: Theme.itemSizeExtraSmall
    enabled: !busy

    Label {
        id: label
        anchors.centerIn: parent
        font.pixelSize: Theme.fontSizeSmall
        color: highlighted ? Theme.highlightColor : Theme.primaryColor
        opacity: busy ? Theme.opacityLow : 1.0
    }
    BusyIndicator {
        anchors.centerIn: parent
        size: BusyIndicatorSize.Small
        running: busy
    }
}
