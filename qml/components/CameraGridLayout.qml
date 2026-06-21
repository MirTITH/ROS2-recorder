import QtQuick 2.15

// Pure-view grid geometry for the camera preview panel. Computes the adaptive
// row/column layout, the per-cell rectangle, and the drag "push-aside" reflow
// (placeholder insertion + dragged-cell hiding). Holds no model state of its own:
// the model (for count), the preview area dimensions, the per-index resolution
// snapshot, and the live drag state are all injected as properties. The model is
// never mutated here; reorder is committed by CameraDragController on drop.
QtObject {
    id: gridLayout

    // The C++ CameraGridModel (visible cameras, 0..count-1). Only count is read.
    property var model

    // Dimensions of the host previewArea Item.
    property real previewWidth: 1
    property real previewHeight: 1

    // Per-visible-index resolution snapshot, populated by the panel's cell delegates.
    property var resolutionByIndex: ({})

    // Live drag state, bound from CameraDragController.
    property bool dragActive: false
    property int dragSourceIndex: -1
    property int dropInsertIndex: -1

    readonly property real tileGap: 4
    readonly property string placeholderSourceKey: "__drop_placeholder__"

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
        var count = gridLayout.model ? gridLayout.model.count : 0
        if (count <= 0) {
            return 16 / 9
        }
        var aspectSum = 0
        for (var index = 0; index < count; ++index) {
            aspectSum += cameraAspectRatio(gridLayout.resolutionByIndex[index])
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
        var modelCount = gridLayout.model ? gridLayout.model.count : 0
        var count = Math.max(1, itemCount === undefined ? modelCount : itemCount)
        var availableWidth = Math.max(1, gridLayout.previewWidth)
        var availableHeight = Math.max(1, gridLayout.previewHeight)
        var layout = chooseLayout(availableWidth, availableHeight, count)
        var row = Math.floor(itemIndex / layout.columns)
        var column = itemIndex % layout.columns
        var itemsInRow = Math.min(layout.columns, count - row * layout.columns)
        var rowWidth = itemsInRow * layout.cellWidth
        var rowOffsetX = Math.max(0, (availableWidth - rowWidth) / 2)
        return {
            x: rowOffsetX + column * layout.cellWidth + gridLayout.tileGap / 2,
            y: row * layout.cellHeight + gridLayout.tileGap / 2,
            width: Math.max(1, layout.cellWidth - gridLayout.tileGap),
            height: Math.max(1, layout.cellHeight - gridLayout.tileGap)
        }
    }

    // Build the reflowed cell order during a drag. Entries are index-based for
    // real cameras (identity == the cell's stable visible index) and the dragged
    // camera is omitted; a placeholder entry (carrying placeholderSourceKey) is
    // spliced in at the adjusted drop position. The model is never mutated here.
    function previewSequence() {
        var sequence = []
        var draggedIndex = gridLayout.dragActive ? gridLayout.dragSourceIndex : -1
        var count = gridLayout.model ? gridLayout.model.count : 0
        for (var index = 0; index < count; ++index) {
            if (index !== draggedIndex) {
                sequence.push({
                    sourceKey: "",
                    cellIndex: index
                })
            }
        }

        if (gridLayout.dragActive && gridLayout.dropInsertIndex >= 0) {
            var insertIndex = gridLayout.dropInsertIndex
            if (draggedIndex >= 0 && insertIndex > draggedIndex) {
                insertIndex -= 1
            }
            insertIndex = Math.max(0, Math.min(sequence.length, insertIndex))
            sequence.splice(insertIndex, 0, {
                sourceKey: gridLayout.placeholderSourceKey,
                cellIndex: -1
            })
        }

        return sequence
    }

    // Position of a cell within the reflowed sequence. The placeholder is matched
    // by its sourceKey; real cameras are matched by their stable visible index
    // (passed as cellIndex / the cell's fallback index).
    function previewIndexForKey(sourceKey, cellIndex) {
        if (!gridLayout.dragActive) {
            return cellIndex
        }
        var isPlaceholder = sourceKey === gridLayout.placeholderSourceKey
        var sequence = gridLayout.previewSequence()
        for (var index = 0; index < sequence.length; ++index) {
            if (isPlaceholder) {
                if (sequence[index].sourceKey === gridLayout.placeholderSourceKey) {
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
        if (gridLayout.dragActive && sourceKey !== gridLayout.placeholderSourceKey &&
            cellIndex === gridLayout.dragSourceIndex) {
            return gridLayout.layoutForIndex(cellIndex, gridLayout.model ? gridLayout.model.count : 1)
        }
        var sequence = gridLayout.previewSequence()
        var index = gridLayout.dragActive ? previewIndexForKey(sourceKey, cellIndex) : cellIndex
        var count = gridLayout.dragActive
            ? Math.max(1, sequence.length)
            : (gridLayout.model ? gridLayout.model.count : 1)
        return gridLayout.layoutForIndex(index, count)
    }

    function placeholderLayout() {
        if (!gridLayout.dragActive || gridLayout.dropInsertIndex < 0) {
            return {
                x: 0,
                y: 0,
                width: 0,
                height: 0
            }
        }
        return gridLayout.previewLayoutForKey(gridLayout.placeholderSourceKey, 0)
    }

    function floatingPreviewLayout() {
        if (gridLayout.dragSourceIndex < 0) {
            return {
                x: 0,
                y: 0,
                width: 1,
                height: 1
            }
        }
        return gridLayout.layoutForIndex(gridLayout.dragSourceIndex, gridLayout.model ? gridLayout.model.count : 1)
    }

    function insertIndexAtPoint(xPosition, yPosition) {
        var count = gridLayout.model ? gridLayout.model.count : 0
        if (count <= 0) {
            return 0
        }
        var bestIndex = count
        var bestDistance = Number.MAX_VALUE
        for (var index = 0; index < count; ++index) {
            var itemLayout = gridLayout.layoutForIndex(index, count)
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
}
