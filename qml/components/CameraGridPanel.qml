import QtQuick 2.15
import QtQuick.Controls 2.15
import "."

Panel {
    id: root

    // Bound to the C++ CameraGridModel: rows are the visible cameras in order
    // (0..count-1), exposing topicName/backendName/resolutionText/seriesColor and
    // Q_INVOKABLE moveCamera(from, to). The model owns visibility filtering and
    // reorder; the panel is a pure view + drag interaction layer.
    property var model
    property int visibleCameraCount: 0

    // Drag state machine (pure view). Drag identity is the VISIBLE INDEX of the
    // dragged camera; that index is stable for the duration of one drag because
    // the model is only mutated on drop (moveCamera). dragSourceKey carries the
    // dragged row's "topic|backend" key for placeholder bookkeeping/identity.
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
    readonly property real tileGap: 4
    readonly property string placeholderSourceKey: "__drop_placeholder__"

    // Per-visible-index resolution snapshot, populated by the cell delegates.
    // The Repeater instantiates every delegate eagerly and recreates them on any
    // model reset (including moveCamera), so this map mirrors the visible rows.
    property var resolutionByIndex: ({})

    title: "相机预览"

    visible: visibleCameraCount > 0
    SplitView.preferredHeight: visible ? 260 : 0
    SplitView.minimumHeight: visible ? 140 : 0
    SplitView.maximumHeight: visible ? 520 : 0

    function cameraAspectRatio(resolutionText) {
        var match = /^([0-9]+)x([0-9]+)$/.exec(String(resolutionText || ""))
        if (!match) {
            return 16 / 9
        }
        var widthValue = Number(match[1])
        var heightValue = Number(match[2])
        if (!isFinite(widthValue) || !isFinite(heightValue) || heightValue <= 0) {
            return 16 / 9
        }
        return Math.max(0.25, Math.min(4.0, widthValue / heightValue))
    }

    function averageCameraAspectRatio() {
        var count = root.model ? root.model.count : 0
        if (count <= 0) {
            return 16 / 9
        }
        var aspectSum = 0
        for (var index = 0; index < count; ++index) {
            aspectSum += cameraAspectRatio(root.resolutionByIndex[index])
        }
        return aspectSum / count
    }

    function chooseLayout(areaWidth, areaHeight, count) {
        var boundedCount = Math.max(1, count)
        var best = {
            columns: 1,
            rows: boundedCount,
            cellWidth: areaWidth,
            cellHeight: areaHeight / boundedCount
        }
        var bestScore = -1
        var averageAspect = averageCameraAspectRatio()
        for (var columns = 1; columns <= boundedCount; ++columns) {
            var rows = Math.ceil(boundedCount / columns)
            var cellWidth = areaWidth / columns
            var cellHeight = areaHeight / rows
            var previewHeight = Math.max(1, cellHeight - 20)
            var previewAspect = cellWidth / previewHeight
            var aspectPenalty = Math.abs(Math.log(previewAspect / averageAspect))
            var usefulArea = cellWidth * previewHeight * boundedCount
            var emptyCells = rows * columns - boundedCount
            var score = usefulArea - emptyCells * cellWidth * previewHeight * 0.45 - aspectPenalty * 8000
            if (score > bestScore) {
                bestScore = score
                best = {
                    columns: columns,
                    rows: rows,
                    cellWidth: cellWidth,
                    cellHeight: cellHeight
                }
            }
        }
        return best
    }

    function layoutForIndex(itemIndex, itemCount) {
        var modelCount = root.model ? root.model.count : 0
        var count = Math.max(1, itemCount === undefined ? modelCount : itemCount)
        var availableWidth = Math.max(1, previewArea.width)
        var availableHeight = Math.max(1, previewArea.height)
        var layout = chooseLayout(availableWidth, availableHeight, count)
        var row = Math.floor(itemIndex / layout.columns)
        var column = itemIndex % layout.columns
        var itemsInRow = Math.min(layout.columns, count - row * layout.columns)
        var rowWidth = itemsInRow * layout.cellWidth
        var rowOffsetX = Math.max(0, (availableWidth - rowWidth) / 2)
        return {
            x: rowOffsetX + column * layout.cellWidth + root.tileGap / 2,
            y: row * layout.cellHeight + root.tileGap / 2,
            width: Math.max(1, layout.cellWidth - root.tileGap),
            height: Math.max(1, layout.cellHeight - root.tileGap)
        }
    }

    // Build the reflowed cell order during a drag. Entries are index-based for
    // real cameras (identity == the cell's stable visible index) and the dragged
    // camera is omitted; a placeholder entry (carrying placeholderSourceKey) is
    // spliced in at the adjusted drop position. The model is never mutated here.
    function previewSequence() {
        var sequence = []
        var draggedIndex = root.dragActive ? root.dragSourceIndex : -1
        var count = root.model ? root.model.count : 0
        for (var index = 0; index < count; ++index) {
            if (index !== draggedIndex) {
                sequence.push({
                    sourceKey: "",
                    cellIndex: index
                })
            }
        }

        if (root.dragActive && root.dropInsertIndex >= 0) {
            var insertIndex = root.dropInsertIndex
            if (draggedIndex >= 0 && insertIndex > draggedIndex) {
                insertIndex -= 1
            }
            insertIndex = Math.max(0, Math.min(sequence.length, insertIndex))
            sequence.splice(insertIndex, 0, {
                sourceKey: root.placeholderSourceKey,
                cellIndex: -1
            })
        }

        return sequence
    }

    // Position of a cell within the reflowed sequence. The placeholder is matched
    // by its sourceKey; real cameras are matched by their stable visible index
    // (passed as cellIndex / the cell's fallback index).
    function previewIndexForKey(sourceKey, cellIndex) {
        if (!root.dragActive) {
            return cellIndex
        }
        var isPlaceholder = sourceKey === root.placeholderSourceKey
        var sequence = root.previewSequence()
        for (var index = 0; index < sequence.length; ++index) {
            if (isPlaceholder) {
                if (sequence[index].sourceKey === root.placeholderSourceKey) {
                    return index
                }
            } else if (sequence[index].cellIndex === cellIndex) {
                return index
            }
        }
        return cellIndex
    }

    function previewLayoutForKey(sourceKey, cellIndex) {
        // The dragged cell keeps its original-slot layout (it is hidden in place;
        // the floating preview follows the cursor). Guard against the placeholder,
        // whose cellIndex (0) may collide with dragSourceIndex.
        if (root.dragActive && sourceKey !== root.placeholderSourceKey &&
            cellIndex === root.dragSourceIndex) {
            return root.layoutForIndex(cellIndex, root.model ? root.model.count : 1)
        }
        var sequence = root.previewSequence()
        var index = root.dragActive ? previewIndexForKey(sourceKey, cellIndex) : cellIndex
        var count = root.dragActive
            ? Math.max(1, sequence.length)
            : (root.model ? root.model.count : 1)
        return root.layoutForIndex(index, count)
    }

    function placeholderLayout() {
        if (!root.dragActive || root.dropInsertIndex < 0) {
            return {
                x: 0,
                y: 0,
                width: 0,
                height: 0
            }
        }
        return root.previewLayoutForKey(root.placeholderSourceKey, 0)
    }

    function floatingPreviewLayout() {
        if (root.dragSourceIndex < 0) {
            return {
                x: 0,
                y: 0,
                width: 1,
                height: 1
            }
        }
        return root.layoutForIndex(root.dragSourceIndex, root.model ? root.model.count : 1)
    }

    function insertIndexAtPoint(xPosition, yPosition) {
        var count = root.model ? root.model.count : 0
        if (count <= 0) {
            return 0
        }
        var bestIndex = count
        var bestDistance = Number.MAX_VALUE
        for (var index = 0; index < count; ++index) {
            var itemLayout = root.layoutForIndex(index, count)
            var centerX = itemLayout.x + itemLayout.width / 2
            var centerY = itemLayout.y + itemLayout.height / 2
            var distance = Math.pow(centerX - xPosition, 2) + Math.pow(centerY - yPosition, 2)
            if (distance < bestDistance) {
                bestDistance = distance
                bestIndex = xPosition < centerX ? index : index + 1
            }
        }
        return Math.max(0, Math.min(count, bestIndex))
    }

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
        if (!root.dragActive) {
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

    function startDrag(sourceKey, sourceIndex, topicName, resolutionText, seriesColor, xPosition, yPosition) {
        pendingDragSourceKey = sourceKey
        pendingDragSourceIndex = sourceIndex
        dragSourceKey = sourceKey
        dragSourceIndex = sourceIndex
        dragTopicName = topicName
        dragResolutionText = resolutionText
        dragSeriesColor = seriesColor
        dragStartX = xPosition
        dragStartY = yPosition
        dragX = xPosition
        dragY = yPosition
        updateDropInsertIndex(xPosition, yPosition)
    }

    function updateDropInsertIndex(xPosition, yPosition) {
        dragX = xPosition
        dragY = yPosition
        dropInsertIndex = insertIndexAtPoint(xPosition, yPosition)
    }

    // On drop, persist the new order via the C++ model. dropInsertIndex is an
    // insert position in [0, count]; moveCamera expects a destination VISIBLE
    // index in [0, count-1]. After removing the dragged item, an insert position
    // beyond the source shifts down by one. Dragging item 0 to the far-right slot
    // yields insert == count -> to == count-1 -> moveCamera(0, count-1) (lands last).
    function commitDropInsertIndex() {
        if (!root.dragActive || dragSourceIndex < 0 || dropInsertIndex < 0 || !root.model) {
            finishDrag()
            return
        }
        var from = dragSourceIndex
        var to = dropInsertIndex > from ? dropInsertIndex - 1 : dropInsertIndex
        to = Math.max(0, Math.min(root.model.count - 1, to))
        if (from >= 0 && to >= 0 && from !== to) {
            root.model.moveCamera(from, to)
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

    Item {
        id: previewArea

        anchors.fill: parent
        anchors.margins: 4
        clip: true

        Repeater {
            model: root.model

            delegate: Item {
                id: cameraCell

                required property int index
                required property string topicName
                required property string backendName
                required property string resolutionText
                required property color seriesColor

                // Per-cell key (matches the C++ model's internal "topic|backend"
                // key). Kept as a stable per-drag identity and for placeholder
                // bookkeeping; layout/drag math uses the visible index.
                readonly property string sourceKey: topicName + "|" + backendName

                readonly property var cellLayout: root.previewLayoutForKey(cameraCell.sourceKey, cameraCell.index)

                x: cellLayout.x
                y: cellLayout.y
                width: cellLayout.width
                height: cellLayout.height

                Component.onCompleted: root.resolutionByIndex[index] = resolutionText
                onResolutionTextChanged: root.resolutionByIndex[index] = resolutionText

                CameraPreviewTile {
                    anchors.fill: parent
                    visible: !(root.dragActive && root.dragSourceIndex === cameraCell.index)
                    topicName: cameraCell.topicName
                    resolutionText: cameraCell.resolutionText
                    seriesColor: cameraCell.seriesColor
                }

                MouseArea {
                    anchors.fill: parent
                    preventStealing: true

                    onPressed: function(mouse) {
                        var point = cameraCell.mapToItem(previewArea, mouse.x, mouse.y)
                        root.beginDragPress(
                            cameraCell.sourceKey,
                            cameraCell.index,
                            cameraCell.topicName,
                            cameraCell.resolutionText,
                            cameraCell.seriesColor,
                            point.x,
                            point.y)
                    }

                    onPositionChanged: function(mouse) {
                        if (pressed) {
                            var point = cameraCell.mapToItem(previewArea, mouse.x, mouse.y)
                            root.updateDragPosition(point.x, point.y)
                        }
                    }

                    onReleased: root.commitDropInsertIndex()
                    onCanceled: root.finishDrag()
                }
            }
        }

        Rectangle {
            id: dropPlaceholder

            readonly property var targetLayout: root.placeholderLayout()

            visible: root.dragActive
            x: targetLayout.x
            y: targetLayout.y
            width: targetLayout.width
            height: targetLayout.height
            color: "transparent"
            border.color: Theme.accent
            border.width: 2
            opacity: 0.85
        }

        CameraPreviewTile {
            id: floatingPreview

            readonly property var previewLayout: root.floatingPreviewLayout()

            visible: root.dragActive && root.dragSourceIndex >= 0
            width: previewLayout.width
            height: previewLayout.height
            x: Math.max(0, Math.min(previewArea.width - width, root.dragX - width / 2))
            y: Math.max(0, Math.min(previewArea.height - height, root.dragY - height / 2))
            z: 10
            opacity: 0.92
            topicName: root.dragTopicName
            resolutionText: root.dragResolutionText
            seriesColor: root.dragSeriesColor
            dragActive: true
        }
    }
}
