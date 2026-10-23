import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1180
    height: 760
    minimumWidth: 760
    minimumHeight: 480
    visible: true
    title: backend.loaded ? qsTr("%1 — FHIR").arg(backend.fileName) : "FHIR"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius
    Material.background: "#0e0e10"

    readonly property color panel: "#151518"
    readonly property color raised: "#1c1c21"
    readonly property color line: "#26262c"
    readonly property color textColor: "#e8e8ea"
    readonly property color muted: "#8b8b93"
    readonly property color errorColor: "#ff6b6b"
    readonly property color warningColor: "#f2b14c"
    readonly property color infoColor: "#6aa9ff"
    readonly property string mono: "monospace"

    // 0 timeline, 1 resources, 2 issues.
    property int view: 0
    property bool detailVisible: true
    property var history: []

    // Single-letter shortcuts stay out of the way while a field takes text.
    readonly property bool typing: activeFocusItem !== null && activeFocusItem.hasOwnProperty("cursorPosition")
                                   && !activeFocusItem.readOnly
    // Visible rather than opened, which turns true only once the opening animation ends.
    readonly property bool overlayOpen: shortcutsOverlay.visible || filterPopup.visible || exportMenu.visible
    readonly property bool keysEnabled: backend.loaded && !backend.loading && !typing && !overlayOpen

    function severityColor(severity) {
        return severity === "error" ? errorColor : severity === "warning" ? warningColor : infoColor
    }
    function severityLabel(severity) {
        return severity === "error" ? qsTr("Error") : severity === "warning" ? qsTr("Warning") : qsTr("Info")
    }
    function pane() { return [timelinePane, resourcesPane, issuesPane][view] }
    function showView(name) {
        view = name === "resources" ? 1 : name === "issues" ? 2 : 0
    }
    function selectRow(row) { pane().select(row) }
    function switchView(step) {
        view = (view + step + 3) % 3
    }
    // Following a reference or an issue remembers where it came from, for Backspace.
    function openResource(resource, path) {
        if (resource < 0)
            return
        history.push({ view: view, row: pane().currentRow, path: resourcesPane.highlightPath })
        view = 1
        resourcesPane.show(resource, path)
    }
    function back() {
        if (history.length === 0)
            return
        const previous = history.pop()
        view = previous.view
        if (view === 1)
            resourcesPane.select(previous.row, previous.path)
        else
            pane().select(previous.row)
    }
    function exportAtPointer() {
        if (pointer.hovered)
            exportMenu.popup()
        else
            exportMenu.popup(content, (content.width - exportMenu.width) / 2, (content.height - exportMenu.height) / 3)
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.typing && !win.overlayOpen; onActivated: shortcutsOverlay.open() }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !win.typing && !win.overlayOpen; onActivated: Qt.quit() }
    Shortcut { sequences: [StandardKey.Open]; context: Qt.ApplicationShortcut; onActivated: backend.requestOpen() }
    Shortcut { sequence: "Tab"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: win.switchView(1) }
    Shortcut { sequence: "Shift+Tab"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: win.switchView(-1) }
    Shortcut { sequence: "Backtab"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: win.switchView(-1) }
    Shortcut { sequence: "1"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: win.view = 0 }
    Shortcut { sequence: "2"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: win.view = 1 }
    Shortcut { sequence: "3"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: win.view = 2 }
    Shortcut { sequence: "/"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: { search.forceActiveFocus(); search.selectAll() } }
    Shortcut { sequence: "F"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: filterPopup.open() }
    Shortcut { sequences: ["J", "Down"]; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: win.pane().move(1) }
    Shortcut { sequences: ["K", "Up"]; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: win.pane().move(-1) }
    Shortcut { sequences: ["PgDown"]; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: win.pane().move(10) }
    Shortcut { sequences: ["PgUp"]; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: win.pane().move(-10) }
    Shortcut { sequences: ["Home"]; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: win.pane().move(-100000) }
    Shortcut { sequences: ["End"]; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: win.pane().move(100000) }
    Shortcut { sequences: ["Return", "Enter"]; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: win.pane().activate() }
    Shortcut { sequence: "Backspace"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: win.back() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: win.detailVisible = !win.detailVisible }
    Shortcut { sequence: "P"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled; onActivated: backend.redact = !backend.redact }
    Shortcut { sequence: "N"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled && win.view === 1; onActivated: resourcesPane.nextReference(1) }
    Shortcut { sequence: "Shift+N"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled && win.view === 1; onActivated: resourcesPane.nextReference(-1) }
    Shortcut { sequence: "S"; context: Qt.ApplicationShortcut; enabled: win.keysEnabled && win.view === 2; onActivated: backend.sortBySeverity = !backend.sortBySeverity }
    Shortcut { sequence: "Ctrl+E"; context: Qt.ApplicationShortcut; enabled: backend.loaded && !win.overlayOpen; onActivated: win.exportAtPointer() }
    Shortcut {
        sequence: "Esc"
        context: Qt.ApplicationShortcut
        enabled: !win.overlayOpen
        onActivated: {
            if (search.activeFocus || search.text.length > 0) {
                search.text = ""
                searchPause.stop()
                backend.searchText = ""
                content.forceActiveFocus()
            } else if (backend.filtering) {
                backend.clearFilters()
            }
        }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        id: content
        anchors.fill: parent
        focus: true
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        HoverHandler { id: pointer }

        DropArea {
            anchors.fill: parent
            onDropped: drop => {
                if (drop.hasUrls)
                    backend.openUrl(drop.urls[0])
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            visible: backend.loaded

            // Header: the file, the views, search, and what's in effect.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                color: win.panel

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 12
                    spacing: 14

                    ColumnLayout {
                        spacing: 0
                        Layout.maximumWidth: 280
                        Label {
                            text: backend.fileName
                            font.pixelSize: 15
                            font.bold: true
                            color: win.textColor
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                        Label {
                            text: qsTr("%1 · %n resources", "", backend.resourceCount).arg(backend.kind)
                            font.pixelSize: 11
                            color: win.muted
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }

                    TabBar {
                        id: tabs
                        currentIndex: win.view
                        onCurrentIndexChanged: win.view = currentIndex
                        Material.background: "transparent"
                        Layout.alignment: Qt.AlignBottom
                        focusPolicy: Qt.NoFocus
                        TabButton { text: qsTr("Timeline"); width: implicitWidth; focusPolicy: Qt.NoFocus }
                        TabButton { text: qsTr("Resources"); width: implicitWidth; focusPolicy: Qt.NoFocus }
                        TabButton {
                            text: backend.errorCount > 0 ? qsTr("Issues (%1)").arg(backend.errorCount + backend.warningCount)
                                                         : backend.warningCount > 0 ? qsTr("Issues (%1)").arg(backend.warningCount)
                                                                                    : qsTr("Issues")
                            width: implicitWidth
                            focusPolicy: Qt.NoFocus
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Chip {
                        visible: backend.redact
                        text: qsTr("Redacted")
                        fill: theme.accent
                        ink: theme.accentForeground
                    }
                    Chip {
                        visible: backend.filtering && win.view === 0
                        text: qsTr("Filtered")
                        fill: win.raised
                        ink: win.textColor
                        onClicked: filterPopup.open()
                    }

                    Rectangle {
                        Layout.preferredWidth: 240
                        Layout.preferredHeight: 32
                        radius: Math.min(4, theme.radius)
                        color: win.raised
                        border.color: search.activeFocus ? theme.accent : win.line

                        TextInput {
                            id: search
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            verticalAlignment: TextInput.AlignVCenter
                            color: win.textColor
                            selectionColor: Qt.alpha(theme.accent, 0.45)
                            font.pixelSize: 13
                            clip: true
                            selectByMouse: true
                            // Search once typing pauses, not on every key: a large
                            // file rebuilds thousands of rows per search.
                            onTextChanged: searchPause.restart()
                            Keys.onReturnPressed: content.forceActiveFocus()
                            Keys.onEnterPressed: content.forceActiveFocus()
                            Keys.onDownPressed: content.forceActiveFocus()
                        }
                        Label {
                            visible: search.text.length === 0
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            text: search.activeFocus ? qsTr("Search text and codes") : qsTr("Search  /")
                            color: win.muted
                            font.pixelSize: 13
                        }
                    }
                }

                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: win.line }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: win.view

                TimelinePane { id: timelinePane }
                ResourcesPane { id: resourcesPane }
                IssuesPane { id: issuesPane }
            }

            // Footer: what just happened, and what this app is not.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                color: win.panel
                Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: win.line }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    spacing: 12
                    Label {
                        text: backend.message.length > 0 ? backend.message
                              : qsTr("%n patients", "", backend.patientCount)
                        color: backend.message.length > 0 ? win.textColor : win.muted
                        font.pixelSize: 11
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        text: qsTr("Structural checks only · Not for clinical decision-making · ? for shortcuts")
                        color: win.muted
                        font.pixelSize: 11
                        elide: Text.ElideLeft
                        Layout.maximumWidth: win.width * 0.6
                    }
                }
            }
        }

        // Nothing open yet.
        ColumnLayout {
            anchors.centerIn: parent
            visible: !backend.loaded
            spacing: 14
            width: Math.min(parent.width - 48, 460)

            Image {
                source: "qrc:/icons/report.svg"
                sourceSize: Qt.size(56, 56)
                opacity: 0.6
                Layout.alignment: Qt.AlignHCenter
            }
            Label {
                text: qsTr("Open a FHIR R4 file")
                font.pixelSize: 22
                color: win.textColor
                Layout.alignment: Qt.AlignHCenter
            }
            Label {
                text: qsTr("A resource, a Bundle, or an NDJSON bulk export. The file is only read, never changed.")
                color: win.muted
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            }
            Button {
                focusPolicy: Qt.NoFocus
                text: qsTr("Open…")
                highlighted: true
                Layout.alignment: Qt.AlignHCenter
                onClicked: backend.requestOpen()
            }
            Label {
                text: qsTr("Ctrl+O, or drop a .json or .ndjson file here")
                color: win.muted
                font.pixelSize: 12
                Layout.alignment: Qt.AlignHCenter
            }
            Label {
                visible: backend.loadError.length > 0 || backend.message.length > 0
                text: backend.loadError.length > 0 ? backend.loadError : backend.message
                color: win.errorColor
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            }
        }

        // A large file takes a few seconds to check; say so instead of looking frozen.
        Rectangle {
            visible: backend.loading
            anchors.fill: parent
            color: Qt.rgba(0.05, 0.05, 0.06, 0.82)
            z: 10
            TapHandler {}
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 14
                BusyIndicator {
                    running: backend.loading
                    Layout.alignment: Qt.AlignHCenter
                }
                Label {
                    text: qsTr("Opening %1…").arg(backend.loadingName)
                    color: win.textColor
                    font.pixelSize: 15
                    Layout.alignment: Qt.AlignHCenter
                }
            }
        }

        // A failed open over an open file is said in the window, not dropped.
        Rectangle {
            visible: backend.loaded && backend.loadError.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 40
            width: Math.min(parent.width - 48, loadErrorLabel.implicitWidth + 32)
            height: loadErrorLabel.implicitHeight + 16
            radius: Math.min(4, theme.radius)
            color: "#3a1d1f"
            Label {
                id: loadErrorLabel
                anchors.centerIn: parent
                width: parent.width - 32
                text: backend.loadError
                color: win.textColor
                wrapMode: Text.WordWrap
            }
        }
    }

    Timer {
        id: searchPause
        interval: 150
        onTriggered: backend.searchText = search.text
    }

    Menu {
        id: exportMenu
        onClosed: content.forceActiveFocus()
        Material.roundedScale: theme.radius
        MenuItem { text: qsTr("Validation report as JSON"); onTriggered: backend.requestExport("issues-json") }
        MenuItem { text: qsTr("Validation report as CSV"); onTriggered: backend.requestExport("issues-csv") }
        MenuSeparator {}
        MenuItem { text: qsTr("Timeline as CSV"); onTriggered: backend.requestExport("timeline-csv") }
        MenuItem { text: qsTr("Timeline as PDF"); onTriggered: backend.requestExport("timeline-pdf") }
        MenuItem { text: qsTr("Timeline as SVG"); onTriggered: backend.requestExport("timeline-svg") }
    }

    FilterPopup { id: filterPopup; onClosed: content.forceActiveFocus() }
    ShortcutsOverlay { id: shortcutsOverlay; onClosed: content.forceActiveFocus() }

    component Chip: Rectangle {
        id: chip
        property alias text: chipLabel.text
        property color fill
        property color ink
        signal clicked()
        implicitWidth: chipLabel.implicitWidth + 18
        implicitHeight: 24
        radius: Math.min(4, theme.radius)
        color: fill
        Label {
            id: chipLabel
            anchors.centerIn: parent
            color: chip.ink
            font.pixelSize: 11
            font.bold: true
        }
        TapHandler { onTapped: chip.clicked() }
    }
}
