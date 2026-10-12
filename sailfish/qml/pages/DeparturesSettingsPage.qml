import QtQuick 2.6
import Sailfish.Silica 1.0

// Without Transitous or a key no stop is sent anywhere.
Page {
    id: page

    allowedOrientations: Orientation.All

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: appInfo.localized("pref_departures_title")
            }
            TextSwitch {
                text: "Transitous"
                description: appInfo.localized("pref_departures_transitous_summary")
                checked: appSettings.transitous
                automaticCheck: false
                onClicked: appSettings.transitous = !checked
            }
            SectionHeader {
                text: appInfo.localized("pref_departures_vehicles_title")
            }
            // Those of the downloaded maps.
            Repeater {
                model: appSettings.liveSources

                ValueButton {
                    label: modelData.name
                    value: appInfo.localized(modelData.keys.length === 0 ? "pref_live_source_no_key"
                                             : modelData.hasKeys ? "pref_live_source_key_added"
                                                                 : "pref_live_source_add_key")
                    onClicked: pageStack.push(Qt.resolvedUrl("LiveSourcePage.qml"), { source: modelData })
                }
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: appSettings.liveSources.length === 0
                text: appInfo.localized("pref_live_sources_none")
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
            }
        }
    }
}
