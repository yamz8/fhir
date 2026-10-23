import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Every shortcut, on ?.
Popup {
    id: overlay
    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(620, win.width - 48)
    padding: 24
    Material.roundedScale: theme.radius

    background: Rectangle {
        color: win.raised
        radius: theme.radius
        border.color: win.line
    }

    readonly property var keys: [
        ["Tab", qsTr("Next view: Timeline, Resources, Issues")],
        ["1 2 3", qsTr("Go to a view")],
        ["J K  ↑ ↓", qsTr("Move")],
        ["Enter", qsTr("Open the resource, follow a reference, or jump to an issue")],
        ["Backspace", qsTr("Go back")],
        ["N  Shift+N", qsTr("Next or previous reference (Resources)")],
        ["Space", qsTr("Show or hide the detail pane")],
        ["/", qsTr("Search")],
        ["F", qsTr("Filter by type and date")],
        ["Esc", qsTr("Clear the search or the filter")],
        ["S", qsTr("Sort issues by severity or file order")],
        ["P", qsTr("Redact names, identifiers, addresses, telecom, birth dates")],
        ["Ctrl+E", qsTr("Export the report or the timeline")],
        ["Ctrl+O", qsTr("Open a file")],
        ["?", qsTr("Show these shortcuts")],
        ["Q", qsTr("Quit")]
    ]

    // The overlay closes on ? as it opened, and on Esc or Q.
    Shortcut { sequences: ["?", "Esc", "Q"]; context: Qt.ApplicationShortcut; enabled: overlay.visible; onActivated: overlay.close() }

    ColumnLayout {
        anchors.fill: parent
        spacing: 14
        Label {
            text: qsTr("Shortcuts")
            font.pixelSize: 18
            font.bold: true
            color: win.textColor
        }
        GridLayout {
            columns: 2
            columnSpacing: 20
            rowSpacing: 7
            Layout.fillWidth: true
            Repeater {
                model: overlay.keys.length * 2
                delegate: Label {
                    required property int index
                    readonly property var entry: overlay.keys[Math.floor(index / 2)]
                    text: index % 2 === 0 ? entry[0] : entry[1]
                    color: index % 2 === 0 ? theme.accent : win.textColor
                    font.family: index % 2 === 0 ? win.mono : Qt.application.font.family
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: index % 2 === 1
                }
            }
        }
        Label {
            text: qsTr("fhir runs structural checks only. It is not a certified validator and is not for clinical decision-making.")
            color: win.muted
            font.pixelSize: 11
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }
}
