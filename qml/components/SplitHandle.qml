import QtQuick 2.15

Item {
    id: root

    property bool vertical: true
    readonly property bool hovered: hoverHandler.hovered

    implicitWidth: vertical ? 5 : 1
    implicitHeight: vertical ? 1 : 5

    Rectangle {
        anchors.centerIn: parent
        width: root.vertical ? (root.hovered ? 3 : 1) : parent.width
        height: root.vertical ? parent.height : (root.hovered ? 3 : 1)
        color: root.hovered ? "#2563eb" : "#cbd5e1"
    }

    HoverHandler {
        id: hoverHandler
        cursorShape: root.vertical ? Qt.SizeHorCursor : Qt.SizeVerCursor
    }
}
