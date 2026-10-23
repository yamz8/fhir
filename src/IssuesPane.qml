import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The validation report, with the selected issue's resource beside it.
Item {
    id: pane
    property int currentRow: rows.count > 0 ? 0 : -1
    readonly property var rows: backend.issueModel
    readonly property var current: (rows.revision, currentRow >= 0 && currentRow < rows.count ? rows.get(currentRow) : null)
    readonly property var preview: current && current.resource >= 0
                                   ? (backend.redact, backend.resourceView(current.resource, current.path)) : null

    function move(delta) {
        if (rows.count > 0)
            select(Math.max(0, Math.min(rows.count - 1, (currentRow < 0 ? 0 : currentRow) + delta)))
    }
    function select(row) {
        if (row < 0 || row >= rows.count)
            return
        currentRow = row
        Qt.callLater(() => { if (list.height > 0) { list.forceLayout(); list.positionViewAtIndex(pane.currentRow, ListView.Contain) } })
    }
    function activate() {
        if (current)
            win.openResource(current.resource, current.path)
    }
    Connections {
        target: pane.rows
        function onReset() {
            if (pane.currentRow >= pane.rows.count)
                pane.currentRow = pane.rows.count - 1
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Counts, the sort, and the limits of what was checked.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            color: "transparent"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 16
                Repeater {
                    model: [
                        { severity: "error", count: backend.errorCount, label: qsTr("%n errors", "", backend.errorCount) },
                        { severity: "warning", count: backend.warningCount, label: qsTr("%n warnings", "", backend.warningCount) },
                        { severity: "info", count: backend.infoCount, label: qsTr("%n info", "", backend.infoCount) }
                    ]
                    delegate: RowLayout {
                        required property var modelData
                        spacing: 6
                        Rectangle { width: 8; height: 8; radius: 4; color: win.severityColor(parent.modelData.severity) }
                        Label {
                            text: parent.modelData.label
                            color: parent.modelData.count > 0 ? win.textColor : win.muted
                            font.pixelSize: 12
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: qsTr("Structural checks only: no profiles, terminology, or invariants")
                    color: win.muted
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    Layout.maximumWidth: pane.width * 0.4
                }
                Button {
                    focusPolicy: Qt.NoFocus
                    flat: true
                    text: backend.sortBySeverity ? qsTr("Sorted by severity (S)") : qsTr("In file order (S)")
                    font.pixelSize: 12
                    onClicked: backend.sortBySeverity = !backend.sortBySeverity
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: win.line }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            ListView {
                id: list
                visible: pane.rows.count > 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: pane.rows
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                delegate: Item {
                    id: row
                    required property var model
                    required property int index
                    readonly property bool selected: index === pane.currentRow
                    width: ListView.view.width
                    height: Math.max(48, text.implicitHeight + 18)

                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        anchors.topMargin: 2
                        radius: Math.min(4, theme.radius)
                        color: row.selected ? Qt.rgba(1, 1, 1, 0.07) : hover.hovered ? Qt.rgba(1, 1, 1, 0.03) : "transparent"
                        Rectangle {
                            visible: row.selected
                            width: 3
                            height: parent.height - 10
                            anchors.verticalCenter: parent.verticalCenter
                            color: theme.accent
                        }
                        HoverHandler { id: hover }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 24
                        anchors.rightMargin: 20
                        spacing: 14
                        Rectangle {
                            Layout.preferredWidth: Math.max(70, severity.implicitWidth + 14)
                            Layout.preferredHeight: 20
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 2
                            radius: Math.min(4, theme.radius)
                            color: Qt.alpha(win.severityColor(row.model.severity), 0.18)
                            Label {
                                id: severity
                                anchors.centerIn: parent
                                text: win.severityLabel(row.model.severity)
                                color: win.severityColor(row.model.severity)
                                font.pixelSize: 11
                                font.bold: true
                            }
                        }
                        ColumnLayout {
                            id: text
                            spacing: 2
                            Layout.fillWidth: true
                            Label {
                                text: row.model.message
                                color: win.textColor
                                font.pixelSize: 13
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                            Label {
                                text: row.model.location
                                color: win.muted
                                font.pixelSize: 11
                                font.family: win.mono
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                        }
                    }

                    TapHandler {
                        onTapped: pane.select(row.index)
                        onDoubleTapped: { pane.select(row.index); pane.activate() }
                    }
                }
            }

            Item {
                visible: pane.rows.count === 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                Label {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 48, 520)
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: backend.searchText.length > 0 ? qsTr("No issues match the search")
                          : qsTr("No issues found by the structural checks. This does not mean the file conforms to any profile.")
                    color: win.muted
                }
            }

            Rectangle {
                visible: win.detailVisible && pane.rows.count > 0
                Layout.preferredWidth: Math.min(520, pane.width * 0.45)
                Layout.fillHeight: true
                color: win.panel
                Rectangle { width: 1; height: parent.height; color: win.line }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10
                    Label {
                        text: pane.preview ? pane.preview.label : qsTr("The file as a whole")
                        color: win.textColor
                        font.pixelSize: 14
                        font.bold: true
                        font.family: win.mono
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    JsonPane {
                        visible: pane.preview !== null
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: theme.radius
                        text: pane.preview ? pane.preview.text : ""
                        start: pane.preview ? pane.preview.start : 0
                        end: pane.preview ? pane.preview.end : 0
                    }
                    Item { visible: pane.preview === null; Layout.fillHeight: true }
                    Label {
                        visible: pane.preview !== null
                        text: qsTr("Enter opens it in Resources")
                        color: win.muted
                        font.pixelSize: 11
                    }
                }
            }
        }
    }
}
