import QtQuick 2.6
import Sailfish.Silica 1.0

// A source of live data, with the fields of its keys if it needs any. The keys apply when leaving the page.
Page {
    id: page

    // See AppSettings::liveSources.
    property var source

    allowedOrientations: Orientation.All
    onStatusChanged: {
        if (status !== PageStatus.Deactivating)
            return
        for (var i = 0; i < keys.count; ++i) {
            var field = keys.itemAt(i).field
            if (field.text.trim() !== source.keys[i].value)
                appSettings.setLiveSourceKey(source.keys[i].setting, field.text)
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            visible: page.source.signUpUrl !== ""

            MenuItem {
                text: appInfo.localized("pref_live_source_get_key")
                onClicked: Qt.openUrlExternally(page.source.signUpUrl)
            }
        }

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: page.source.name
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: page.source.summary !== ""
                text: visible ? appInfo.localized(page.source.summary) : ""
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
            }
            Repeater {
                id: keys
                model: page.source.keys

                Column {
                    property alias field: keyField

                    width: column.width

                    Label {
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * x
                        bottomPadding: Theme.paddingMedium
                        text: appInfo.localized(modelData.summary)
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.highlightColor
                    }
                    TextField {
                        id: keyField
                        width: parent.width
                        label: appInfo.localized(modelData.label)
                        placeholderText: label
                        text: modelData.value
                        inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhSensitiveData
                        EnterKey.iconSource: index + 1 < keys.count ? "image://theme/icon-m-enter-next"
                                                                    : "image://theme/icon-m-enter-close"
                        EnterKey.onClicked: {
                            if (index + 1 < keys.count)
                                keys.itemAt(index + 1).field.focus = true
                            else
                                focus = false
                        }
                    }
                }
            }
        }
    }
}
