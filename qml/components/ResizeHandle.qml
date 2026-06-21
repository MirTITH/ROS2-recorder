import QtQuick 2.15
import "."

Item {
    id: root

    property int lineOrientation: Qt.Vertical
    readonly property bool isVerticalLine: lineOrientation === Qt.Vertical
    readonly property bool hovered: hoverHandler.hovered

    implicitWidth: 6
    implicitHeight: 6

    Rectangle {
        anchors.centerIn: parent
        width: root.isVerticalLine ? (root.hovered ? 3 : 1) : parent.width
        height: root.isVerticalLine ? parent.height : (root.hovered ? 3 : 1)
        color: root.hovered ? Theme.accent : Theme.resizeHandle
    }

    HoverHandler {
        id: hoverHandler
    }
}
