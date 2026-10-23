import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Fhir

// One row per dated event, grouped by patient, with the selected event's
// details and trend beside it.
Item {
    id: pane
    property int currentRow: -1
    property int selectedEvent: -1
    readonly property var rows: backend.timelineModel
    readonly property var current: (rows.revision, currentRow >= 0 && currentRow < rows.count ? rows.get(currentRow) : null)
    readonly property var detail: current && !current.header
                                  ? (backend.redact, translator.language, backend.eventDetail(current.event)) : null

    function selectable(from, step) {
        return rows.find("header", false, from, step)
    }
    function move(delta) {
        const step = delta > 0 ? 1 : -1
        let start = currentRow < 0 ? (delta > 0 ? 0 : rows.count - 1) : currentRow + delta
        start = Math.max(0, Math.min(rows.count - 1, start))
        let found = selectable(start, step)
        if (found < 0)
            found = selectable(start, -step)
        if (found >= 0)
            select(found)
    }
    function select(row) {
        if (row < 0 || row >= rows.count)
            return
        currentRow = row
        selectedEvent = rows.value(row, "event")
        Qt.callLater(() => { if (list.height > 0) { list.forceLayout(); list.positionViewAtIndex(pane.currentRow, ListView.Contain) } })
    }
    function activate() {
        if (current)
            win.openResource(current.resource, "")
    }
    // A new filter keeps the same event selected when it's still shown.
    function restoreSelection() {
        const kept = selectedEvent >= 0 ? rows.find("event", selectedEvent) : -1
        currentRow = kept >= 0 ? kept : selectable(0, 1)
        selectedEvent = rows.value(currentRow, "event") ?? -1
    }
    Connections {
        target: pane.rows
        function onReset() { pane.restoreSelection() }
    }
    Component.onCompleted: restoreSelection()

    RowLayout {
        anchors.fill: parent
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
                height: model.header ? 44 : 34

                // Patient heading.
                RowLayout {
                    visible: row.model.header
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    anchors.topMargin: 10
                    spacing: 10
                    TypeIcon { type: "Patient"; Layout.alignment: Qt.AlignVCenter }
                    Label {
                        text: row.model.label || ""
                        font.pixelSize: 14
                        font.bold: true
                        color: win.textColor
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        text: row.model.header ? qsTr("%n events", "", row.model.count) : ""
                        color: win.muted
                        font.pixelSize: 11
                    }
                }

                Rectangle {
                    visible: !row.model.header
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    radius: Math.min(4, theme.radius)
                    color: row.selected ? Qt.rgba(1, 1, 1, 0.07) : hover.hovered ? Qt.rgba(1, 1, 1, 0.03) : "transparent"
                    Rectangle {
                        visible: row.selected
                        width: 3
                        height: parent.height - 10
                        anchors.verticalCenter: parent.verticalCenter
                        color: theme.accent
                        radius: Math.min(2, theme.radius)
                    }
                    HoverHandler { id: hover }
                }

                RowLayout {
                    visible: !row.model.header
                    anchors.fill: parent
                    anchors.leftMargin: 26
                    anchors.rightMargin: 20
                    spacing: 12
                    TypeIcon { type: row.model.type || "" }
                    Label {
                        text: row.model.date || ""
                        color: win.muted
                        font.pixelSize: 12
                        font.features: { "tnum": 1 }
                        Layout.preferredWidth: 140
                        elide: Text.ElideRight
                    }
                    Label {
                        text: row.model.summary || ""
                        color: win.textColor
                        font.pixelSize: 13
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Image {
                        visible: row.model.hasTrend === true
                        source: "qrc:/icons/trend.svg"
                        sourceSize: Qt.size(14, 14)
                        opacity: 0.7
                    }
                    Label {
                        text: row.model.type || ""
                        color: win.muted
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignRight
                        Layout.preferredWidth: 120
                        elide: Text.ElideRight
                    }
                }

                TapHandler {
                    enabled: !row.model.header
                    onTapped: pane.select(row.index)
                    onDoubleTapped: { pane.select(row.index); pane.activate() }
                }
            }
        }

        // Filtered down to nothing.
        Item {
            visible: pane.rows.count === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 10
                Label {
                    text: backend.filtering ? qsTr("No events match the filter or search")
                                            : qsTr("This file has no dated events")
                    color: win.muted
                    Layout.alignment: Qt.AlignHCenter
                }
                Button {
                    focusPolicy: Qt.NoFocus
                    visible: backend.filtering
                    text: qsTr("Clear filter")
                    flat: true
                    Layout.alignment: Qt.AlignHCenter
                    onClicked: backend.clearFilters()
                }
            }
        }

        Rectangle {
            visible: win.detailVisible && pane.rows.count > 0
            Layout.preferredWidth: Math.min(400, pane.width * 0.4)
            Layout.fillHeight: true
            color: win.panel
            Rectangle { width: 1; height: parent.height; color: win.line }

            ColumnLayout {
                visible: pane.detail !== null
                anchors.fill: parent
                anchors.margins: 20
                spacing: 10

                RowLayout {
                    spacing: 8
                    TypeIcon { type: pane.detail ? pane.detail.type : "" }
                    Label {
                        text: pane.detail ? pane.detail.type : ""
                        color: win.muted
                        font.pixelSize: 12
                    }
                }
                Label {
                    text: pane.detail ? pane.detail.summary : ""
                    color: win.textColor
                    font.pixelSize: 18
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                GridLayout {
                    columns: 2
                    columnSpacing: 14
                    rowSpacing: 6
                    Layout.fillWidth: true
                    Label { text: qsTr("Date"); color: win.muted; font.pixelSize: 12 }
                    Label { text: pane.detail ? pane.detail.date : ""; color: win.textColor; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Label { text: qsTr("Status"); color: win.muted; font.pixelSize: 12; visible: statusValue.text.length > 0 }
                    Label { id: statusValue; text: pane.detail ? pane.detail.status : ""; color: win.textColor; font.pixelSize: 12; visible: text.length > 0 }
                    Label { text: qsTr("Patient"); color: win.muted; font.pixelSize: 12 }
                    Label { text: pane.detail ? pane.detail.patient : ""; color: win.textColor; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }
                    Label { text: qsTr("Resource"); color: win.muted; font.pixelSize: 12 }
                    Label { text: pane.detail ? pane.detail.label : ""; color: win.textColor; font.pixelSize: 12; font.family: win.mono; Layout.fillWidth: true; elide: Text.ElideMiddle }
                    Label { text: qsTr("Codes"); color: win.muted; font.pixelSize: 12; visible: codesValue.text.length > 0; Layout.alignment: Qt.AlignTop }
                    Label {
                        id: codesValue
                        text: pane.detail ? pane.detail.codes.join("\n") : ""
                        visible: text.length > 0
                        color: win.textColor
                        font.pixelSize: 11
                        font.family: win.mono
                        wrapMode: Text.WrapAnywhere
                        Layout.fillWidth: true
                    }
                }

                Rectangle {
                    visible: pane.detail !== null && pane.detail.hasTrend
                    Layout.fillWidth: true
                    Layout.preferredHeight: 200
                    Layout.topMargin: 8
                    radius: theme.radius
                    color: "#111114"
                    TrendChart {
                        anchors.fill: parent
                        anchors.margins: 12
                        title: pane.detail && pane.detail.hasTrend ? pane.detail.trendTitle : ""
                        unit: pane.detail && pane.detail.hasTrend ? pane.detail.trendUnit : ""
                        points: pane.detail && pane.detail.hasTrend ? pane.detail.trendPoints : []
                        lineColor: theme.accent
                    }
                }
                Label {
                    visible: pane.detail !== null && pane.detail.hasTrend
                    text: pane.detail && pane.detail.hasTrend ? qsTr("%n readings with this code", "", pane.detail.trendCount) : ""
                    color: win.muted
                    font.pixelSize: 11
                }

                Item { Layout.fillHeight: true }

                Button {

                    focusPolicy: Qt.NoFocus
                    text: qsTr("Show resource  ↵")
                    flat: true
                    Layout.alignment: Qt.AlignLeft
                    onClicked: pane.activate()
                }
            }
        }
    }
}
