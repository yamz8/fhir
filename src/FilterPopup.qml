import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Which resource types the timeline shows, and between which dates.
Popup {
    id: popup
    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(460, win.width - 48)
    padding: 20
    Material.roundedScale: theme.radius

    background: Rectangle {
        color: win.raised
        radius: theme.radius
        border.color: win.line
    }

    onOpened: {
        from.text = backend.dateFrom
        to.text = backend.dateTo
        dateError.visible = false
        types.forceActiveFocus()
    }

    function apply() {
        if (backend.setDateRange(from.text, to.text))
            close()
        else
            dateError.visible = true
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Label {
            text: qsTr("Filter the timeline")
            font.pixelSize: 16
            font.bold: true
            color: win.textColor
        }
        Label {
            text: qsTr("Resource types")
            color: win.muted
            font.pixelSize: 12
        }
        Flow {
            id: types
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: backend.timelineTypes
                delegate: CheckBox {
                    required property var modelData
                    text: qsTr("%1 (%2)").arg(modelData.type).arg(modelData.count)
                    checked: modelData.enabled
                    onToggled: backend.setTypeFilter(modelData.type, checked)
                }
            }
        }
        RowLayout {
            spacing: 12
            Layout.fillWidth: true
            TextField {
                id: from
                placeholderText: qsTr("From (YYYY-MM-DD)")
                Layout.fillWidth: true
                selectByMouse: true
                onAccepted: popup.apply()
            }
            TextField {
                id: to
                placeholderText: qsTr("To (YYYY-MM-DD)")
                Layout.fillWidth: true
                selectByMouse: true
                onAccepted: popup.apply()
            }
        }
        Label {
            id: dateError
            visible: false
            text: qsTr("Dates are written like 2024-01-31.")
            color: win.errorColor
            font.pixelSize: 12
        }
        RowLayout {
            Layout.fillWidth: true
            Button {
                focusPolicy: Qt.NoFocus
                text: qsTr("Clear")
                flat: true
                onClicked: {
                    backend.clearFilters()
                    popup.close()
                }
            }
            Item { Layout.fillWidth: true }
            Button {
                focusPolicy: Qt.NoFocus
                text: qsTr("Done")
                highlighted: true
                onClicked: popup.apply()
            }
        }
    }
}
