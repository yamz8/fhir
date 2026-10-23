import QtQuick
import QtQuick.Controls

// Pretty-printed JSON with one element highlighted, scrolled into view.
Rectangle {
    id: pane
    property string text
    property int start: 0
    property int end: 0
    readonly property bool highlighted: end > start && end <= body.length

    color: "#111114"
    clip: true

    function reveal() {
        if (!highlighted || start > body.length) {
            flick.contentY = 0
            return
        }
        const top = body.positionToRectangle(start).y
        const target = Math.max(0, Math.min(top - flick.height / 3, flick.contentHeight - flick.height))
        flick.contentY = target
    }
    onTextChanged: Qt.callLater(reveal)
    onStartChanged: Qt.callLater(reveal)

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.margins: 1
        contentWidth: Math.max(width, body.contentWidth + 32)
        contentHeight: body.contentHeight + 24
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        ScrollBar.horizontal: ScrollBar {}

        // Behind the text: the lines the highlighted element spans.
        Rectangle {
            visible: pane.highlighted
            readonly property rect first: pane.highlighted ? body.positionToRectangle(pane.start) : Qt.rect(0, 0, 0, 0)
            readonly property rect last: pane.highlighted ? body.positionToRectangle(Math.max(pane.start, pane.end - 1)) : Qt.rect(0, 0, 0, 0)
            x: 0
            y: body.y + first.y - 1
            width: flick.contentWidth
            height: last.y + last.height - first.y + 2
            color: Qt.alpha(theme.accent, 0.16)
            Rectangle { width: 3; height: parent.height; color: theme.accent }
        }

        TextEdit {
            id: body
            x: 16
            y: 12
            text: pane.text
            readOnly: true
            selectByMouse: true
            persistentSelection: true
            font.family: win.mono
            font.pixelSize: 12
            color: "#d6d6da"
            selectionColor: Qt.alpha(theme.accent, 0.45)
            selectedTextColor: "#ffffff"
            textFormat: TextEdit.PlainText
            wrapMode: TextEdit.NoWrap
        }
    }
}
