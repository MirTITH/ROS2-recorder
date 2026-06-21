import QtQuick 2.15

MouseArea {
    id: dragArea

    // The EventTrackRow root, which owns the drag state machine and menus that
    // this area calls back into.
    property var row

    // Drag mode for this hit area: "point" | "range" | "left" | "right".
    property string dragMode: "point"

    // Identifier and seconds passed through to row.startDrag for this mode.
    property int instanceId: -1
    property real startSeconds: 0
    property real endSeconds: 0

    acceptedButtons: Qt.LeftButton | Qt.RightButton

    onWheel: function(wheel) {
        row.zoomAtLocalX(row.localXFromMouse(dragArea, wheel), wheel)
    }

    onPressed: function(mouse) {
        if (mouse.button === Qt.RightButton) {
            row.requestDelete(
                dragArea.instanceId,
                row.localXFromMouse(dragArea, mouse),
                dragArea.mapToItem(row, mouse.x, mouse.y).y)
            return
        }
        row.startDrag(
            dragArea.dragMode,
            dragArea.instanceId,
            dragArea.startSeconds,
            dragArea.endSeconds,
            row.localXFromMouse(dragArea, mouse))
    }

    onPositionChanged: function(mouse) {
        if (pressedButtons & Qt.LeftButton) {
            row.updateDrag(row.localXFromMouse(dragArea, mouse))
        }
    }

    onReleased: row.finishDrag()
    onCanceled: row.finishDrag()
}
