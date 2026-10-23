import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Resource types and their counts, the selected resource as JSON, and the
// references in and out of it.
Item {
    id: pane
    property int currentRow: -1
    property string highlightPath: ""
    property int refIndex: 0
    readonly property var rows: backend.resourceModel
    readonly property var current: (rows.revision, currentRow >= 0 && currentRow < rows.count ? rows.get(currentRow) : null)
    readonly property int resource: current && !current.header ? current.resource : -1
    readonly property var shown: (backend.redact, resource >= 0 ? backend.resourceView(resource, highlightPath) : null)
    readonly property var refs: resource >= 0 ? backend.references(resource) : []

    function move(delta) {
        if (rows.count === 0)
            return
        const start = currentRow < 0 ? 0 : currentRow
        select(Math.max(0, Math.min(rows.count - 1, start + delta)))
    }
    function select(row, path) {
        if (row < 0 || row >= rows.count)
            return
        highlightPath = path || ""
        currentRow = row
        refIndex = 0
        Qt.callLater(() => { if (tree.height > 0) { tree.forceLayout(); tree.positionViewAtIndex(pane.currentRow, ListView.Contain) } })
    }
    function show(index, path) {
        select(backend.revealResource(index), path)
    }
    function nextReference(step) {
        if (refs.length > 0)
            refIndex = (refIndex + step + refs.length) % refs.length
    }
    function follow(i) {
        if (i >= 0 && i < refs.length && refs[i].target >= 0)
            win.openResource(refs[i].target, "")
    }
    function activate() {
        if (current && current.header)
            backend.toggleType(current.type)
        else
            follow(refIndex)
    }
    // Keep the same row selected, header or resource, when the tree changes under it.
    property string keep: ""
    onCurrentChanged: if (current) keep = current.key
    function restoreSelection() {
        const kept = rows.find("key", keep)
        if (kept >= 0)
            currentRow = kept
        else if (currentRow >= rows.count || currentRow < 0)
            currentRow = rows.count > 1 ? 1 : rows.count - 1
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
            id: tree
            Layout.preferredWidth: win.detailVisible ? Math.min(340, pane.width * 0.32) : pane.width
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
                height: model.header ? 36 : 40

                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
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
                    visible: row.model.header
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    spacing: 8
                    Label {
                        text: row.model.expanded ? "▾" : "▸"
                        color: win.muted
                        font.pixelSize: 12
                    }
                    TypeIcon { type: row.model.type || "" }
                    Label {
                        text: row.model.type || ""
                        color: win.textColor
                        font.pixelSize: 13
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        text: row.model.header ? Number(row.model.count).toLocaleString(Qt.locale(), 'f', 0) : ""
                        color: win.muted
                        font.pixelSize: 12
                    }
                }

                RowLayout {
                    visible: !row.model.header
                    anchors.fill: parent
                    anchors.leftMargin: 42
                    anchors.rightMargin: 16
                    spacing: 8
                    ColumnLayout {
                        spacing: 1
                        Layout.fillWidth: true
                        Label {
                            text: row.model.label || ""
                            color: win.textColor
                            font.pixelSize: 12
                            font.family: win.mono
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                        Label {
                            text: row.model.summary || ""
                            visible: text.length > 0
                            color: win.muted
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    Rectangle {
                        visible: (row.model.issues || 0) > 0
                        implicitWidth: issueCount.implicitWidth + 12
                        implicitHeight: 18
                        radius: 9
                        color: Qt.alpha(win.severityColor(["error", "warning", "info"][row.model.worst] || "info"), 0.22)
                        Label {
                            id: issueCount
                            anchors.centerIn: parent
                            text: row.model.issues || ""
                            font.pixelSize: 10
                            font.bold: true
                            color: win.severityColor(["error", "warning", "info"][row.model.worst] || "info")
                        }
                    }
                }

                TapHandler {
                    onTapped: {
                        pane.select(row.index)
                        if (row.model.header)
                            backend.toggleType(row.model.type)
                    }
                }
            }
        }

        Rectangle {
            visible: win.detailVisible
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: win.panel
            Rectangle { width: 1; height: parent.height; color: win.line }

            Label {
                visible: pane.shown === null
                anchors.centerIn: parent
                text: qsTr("Select a resource")
                color: win.muted
            }

            ColumnLayout {
                visible: pane.shown !== null
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10

                RowLayout {
                    spacing: 10
                    Layout.fillWidth: true
                    TypeIcon { type: pane.shown ? pane.shown.type : "" }
                    Label {
                        text: pane.shown ? pane.shown.label : ""
                        color: win.textColor
                        font.pixelSize: 15
                        font.bold: true
                        font.family: win.mono
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    Label {
                        text: pane.shown ? pane.shown.location : ""
                        color: win.muted
                        font.pixelSize: 11
                        font.family: win.mono
                    }
                }
                Label {
                    visible: text.length > 0
                    text: pane.shown && pane.shown.fullUrl ? pane.shown.fullUrl : ""
                    color: win.muted
                    font.pixelSize: 11
                    font.family: win.mono
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }

                // The issues behind the highlight, so a jump from the report shows why.
                Repeater {
                    model: pane.highlightPath.length > 0 && pane.shown ? pane.shown.issues : []
                    delegate: Label {
                        required property string modelData
                        text: modelData
                        color: win.severityColor(modelData.split(":")[0])
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                JsonPane {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: theme.radius
                    text: pane.shown ? pane.shown.text : ""
                    start: pane.shown ? pane.shown.start : 0
                    end: pane.shown ? pane.shown.end : 0
                }

                Label {
                    visible: pane.refs.length > 0
                    text: qsTr("References · N for the next, Enter to follow")
                    color: win.muted
                    font.pixelSize: 11
                }
                ListView {
                    id: references
                    visible: pane.refs.length > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(contentHeight, 150)
                    clip: true
                    model: pane.refs
                    currentIndex: pane.refIndex
                    onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        id: refRow
                        required property var modelData
                        required property int index
                        width: ListView.view.width
                        height: 26
                        radius: Math.min(4, theme.radius)
                        color: index === pane.refIndex ? Qt.alpha(theme.accent, 0.16) : "transparent"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 10
                            Label {
                                text: refRow.modelData.incoming ? "←" : "→"
                                color: refRow.modelData.target >= 0 ? theme.accent : win.muted
                            }
                            Label {
                                text: refRow.modelData.incoming ? qsTr("%1 refers here").arg(refRow.modelData.targetLabel)
                                                                : refRow.modelData.path
                                color: win.muted
                                font.pixelSize: 11
                                font.family: win.mono
                                elide: Text.ElideMiddle
                                Layout.preferredWidth: parent.width * 0.45
                            }
                            Label {
                                text: refRow.modelData.incoming ? refRow.modelData.path
                                      : refRow.modelData.target >= 0 ? refRow.modelData.targetLabel
                                                                     : qsTr("%1 (not in this file)").arg(refRow.modelData.reference)
                                color: refRow.modelData.target >= 0 ? win.textColor : win.muted
                                font.pixelSize: 11
                                font.family: win.mono
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                        }
                        TapHandler {
                            onTapped: {
                                pane.refIndex = refRow.index
                                pane.follow(refRow.index)
                            }
                        }
                    }
                }
            }
        }
    }
}
