import QtQuick 2.6
import Sailfish.Silica 1.0

// The keys apply when leaving the page; without one no stop is sent anywhere.
Page {
    id: page

    allowedOrientations: Orientation.All
    onStatusChanged: {
        if (status !== PageStatus.Deactivating)
            return
        if (keyField.text.trim() !== appSettings.departuresKey)
            appSettings.departuresKey = keyField.text
        if (vehiclesKeyField.text.trim() !== appSettings.vehiclesKey)
            appSettings.vehiclesKey = vehiclesKeyField.text
        if (journeysKeyField.text.trim() !== appSettings.journeysKey)
            appSettings.journeysKey = journeysKeyField.text
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: appInfo.localized("pref_departures_title")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                text: appInfo.localized("pref_departures_summary")
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
            }
            TextField {
                id: keyField
                width: parent.width
                label: appInfo.localized("pref_departures_key")
                placeholderText: appInfo.localized("pref_departures_key")
                text: appSettings.departuresKey
                inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhSensitiveData
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: vehiclesKeyField.focus = true
            }
            SectionHeader {
                text: appInfo.localized("pref_departures_vehicles_title")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                text: appInfo.localized("pref_departures_vehicles_summary")
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
            }
            TextField {
                id: vehiclesKeyField
                width: parent.width
                label: appInfo.localized("pref_departures_vehicles_key")
                placeholderText: appInfo.localized("pref_departures_vehicles_key")
                text: appSettings.vehiclesKey
                inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhSensitiveData
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: journeysKeyField.focus = true
            }
            SectionHeader {
                text: appInfo.localized("pref_departures_journeys_title")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                text: appInfo.localized("pref_departures_journeys_summary")
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
            }
            TextField {
                id: journeysKeyField
                width: parent.width
                label: appInfo.localized("pref_departures_journeys_key")
                placeholderText: appInfo.localized("pref_departures_journeys_key")
                text: appSettings.journeysKey
                inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhSensitiveData
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false
            }
        }
    }
}
