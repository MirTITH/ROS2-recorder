import QtQuick 2.15

// Drag state machine for the camera preview panel. Tracks an in-progress
// reorder gesture by the dragged camera's VISIBLE INDEX (stable for the duration
// of one drag, since the model is only mutated on drop). It computes the live
// drop-insert position via the injected layout helper and, on release, persists
// the new order through the C++ model's moveCamera. The push-aside reflow itself
// is rendered by CameraGridLayout from this controller's dragActive /
// dragSourceIndex / dropInsertIndex; those are read continuously during the drag
// while the model stays untouched until commit.
QtObject {
    id: dragController

    // The C++ CameraGridModel (for count and moveCamera).
    property var model
    // The CameraGridLayout helper, used for hit-testing the drop position.
    property var layout

    property string pendingDragSourceKey: ""
    property int pendingDragSourceIndex: -1
    property real dragStartX: 0
    property real dragStartY: 0
    property string dragSourceKey: ""
    property int dragSourceIndex: -1
    property int dropInsertIndex: -1
    property real dragX: 0
    property real dragY: 0
    // Display data of the dragged camera, captured at press time so the floating
    // preview can render it without reading model rows imperatively.
    property string dragTopicName: ""
    property string dragResolutionText: ""
    property color dragSeriesColor: "#2563eb"
    readonly property bool dragActive: dragSourceKey.length > 0
    readonly property real dragStartThreshold: 6

    function beginDragPress(sourceKey, sourceIndex, topicName, resolutionText, seriesColor, xPosition, yPosition) {
        pendingDragSourceKey = sourceKey
        pendingDragSourceIndex = sourceIndex
        dragTopicName = topicName
        dragResolutionText = resolutionText
        dragSeriesColor = seriesColor
        dragStartX = xPosition
        dragStartY = yPosition
        dragX = xPosition
        dragY = yPosition
    }

    function activatePendingDrag(xPosition, yPosition) {
        if (pendingDragSourceKey.length <= 0) {
            return
        }
        dragSourceKey = pendingDragSourceKey
        dragSourceIndex = pendingDragSourceIndex
        updateDropInsertIndex(xPosition, yPosition)
    }

    function updateDragPosition(xPosition, yPosition) {
        dragX = xPosition
        dragY = yPosition
        if (!dragController.dragActive) {
            var deltaX = xPosition - dragStartX
            var deltaY = yPosition - dragStartY
            if (deltaX * deltaX + deltaY * deltaY < dragStartThreshold * dragStartThreshold) {
                return
            }
            activatePendingDrag(xPosition, yPosition)
            return
        }
        updateDropInsertIndex(xPosition, yPosition)
    }

    function updateDropInsertIndex(xPosition, yPosition) {
        dragX = xPosition
        dragY = yPosition
        dropInsertIndex = dragController.layout
            ? dragController.layout.insertIndexAtPoint(xPosition, yPosition)
            : -1
    }

    // On drop, persist the new order via the C++ model. dropInsertIndex is an
    // insert position in [0, count]; moveCamera expects a destination VISIBLE
    // index in [0, count-1]. After removing the dragged item, an insert position
    // beyond the source shifts down by one. Dragging item 0 to the far-right slot
    // yields insert == count -> to == count-1 -> moveCamera(0, count-1) (lands last).
    function commitDropInsertIndex() {
        if (!dragController.dragActive || dragSourceIndex < 0 || dropInsertIndex < 0 ||
            !dragController.model) {
            finishDrag()
            return
        }
        var from = dragSourceIndex
        var to = dropInsertIndex > from ? dropInsertIndex - 1 : dropInsertIndex
        to = Math.max(0, Math.min(dragController.model.count - 1, to))
        if (from >= 0 && to >= 0 && from !== to) {
            dragController.model.moveCamera(from, to)
        }
        finishDrag()
    }

    function finishDrag() {
        pendingDragSourceKey = ""
        pendingDragSourceIndex = -1
        dragSourceKey = ""
        dragSourceIndex = -1
        dropInsertIndex = -1
    }
}
