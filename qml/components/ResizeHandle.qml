import QtQuick 2.15

Item {
    id: root

    property int lineOrientation: Qt.Vertical
    property int lineGravity: Qt.AlignCenter
    readonly property bool isVerticalLine: lineOrientation === Qt.Vertical
    readonly property bool hovered: hoverHandler.hovered

    implicitWidth: isVerticalLine ? 8 : 1
    implicitHeight: isVerticalLine ? 1 : 8

    function lineOffset(extent, lineExtent, startFlag, endFlag) {
        if (lineGravity & startFlag) {
            return 0
        }
        if (lineGravity & endFlag) {
            return Math.max(0, extent - lineExtent)
        }
        return Math.max(0, (extent - lineExtent) / 2)
    }

    Rectangle {
        width: root.isVerticalLine ? (root.hovered ? 3 : 1) : parent.width
        height: root.isVerticalLine ? parent.height : (root.hovered ? 3 : 1)
        x: root.isVerticalLine ? root.lineOffset(parent.width, width, Qt.AlignLeft, Qt.AlignRight) : 0
        y: root.isVerticalLine ? 0 : root.lineOffset(parent.height, height, Qt.AlignTop, Qt.AlignBottom)
        color: root.hovered ? "#2563eb" : "#cbd5e1"
    }

    HoverHandler {
        id: hoverHandler
        cursorShape: root.isVerticalLine ? Qt.SizeHorCursor : Qt.SizeVerCursor
    }
}
