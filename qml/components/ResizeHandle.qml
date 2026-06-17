import QtQuick 2.15

Item {
    id: root

    property int lineOrientation: Qt.Vertical
    readonly property bool isVerticalLine: lineOrientation === Qt.Vertical
    readonly property bool hovered: hoverHandler.hovered

    implicitWidth: isVerticalLine ? 8 : 1
    implicitHeight: isVerticalLine ? 1 : 8

    Rectangle {
        anchors.centerIn: parent
        width: root.isVerticalLine ? (root.hovered ? 3 : 1) : parent.width
        height: root.isVerticalLine ? parent.height : (root.hovered ? 3 : 1)
        color: root.hovered ? "#2563eb" : "#cbd5e1"
    }

    HoverHandler {
        id: hoverHandler
        cursorShape: root.isVerticalLine ? Qt.SizeHorCursor : Qt.SizeVerCursor
    }
}
