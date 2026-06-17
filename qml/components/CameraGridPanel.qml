import QtQuick 2.15
import QtQuick.Controls 2.15

Panel {
    id: root

    property var model
    property int visibleCameraCount: 0
    property var visualOrder: []
    property string pendingDragSourceKey: ""
    property int pendingDragSourceIndex: -1
    property real dragStartX: 0
    property real dragStartY: 0
    property string dragSourceKey: ""
    property int dragSourceIndex: -1
    property int dropInsertIndex: -1
    property real dragX: 0
    property real dragY: 0
    property bool sourceRefreshActive: false
    readonly property bool dragActive: dragSourceKey.length > 0
    readonly property real dragStartThreshold: 6
    readonly property real tileGap: 4

    title: "相机预览"

    visible: visibleCameraCount > 0
    SplitView.preferredHeight: visible ? 260 : 0
    SplitView.minimumHeight: visible ? 140 : 0
    SplitView.maximumHeight: visible ? 520 : 0

    function makeSourceKey(sourceRow, topicName, backendName) {
        return sourceRow + "|" + topicName + "|" + backendName
    }

    function cameraObject(sourceKey, sourceRow, topicName, backendName, resolutionText, seriesColor, isVisible) {
        return {
            sourceKey: sourceKey,
            sourceRow: sourceRow,
            topicName: topicName,
            backendName: backendName,
            resolutionText: resolutionText,
            seriesColor: seriesColor,
            isVisible: isVisible
        }
    }

    function findSourceIndex(sourceKey) {
        for (var index = 0; index < sourceCameras.count; ++index) {
            if (sourceCameras.get(index).sourceKey === sourceKey) {
                return index
            }
        }
        return -1
    }

    function findProxyIndex(sourceKey) {
        for (var index = 0; index < cameraProxy.count; ++index) {
            if (cameraProxy.get(index).sourceKey === sourceKey) {
                return index
            }
        }
        return -1
    }

    function findSourceRowIndex(sourceRow) {
        for (var index = 0; index < sourceCameras.count; ++index) {
            if (sourceCameras.get(index).sourceRow === sourceRow) {
                return index
            }
        }
        return -1
    }

    function visibleDebugOrder() {
        var order = []
        for (var index = 0; index < cameraProxy.count; ++index) {
            var camera = cameraProxy.get(index)
            order.push(camera.topicName + "@" + camera.backendName)
        }
        return order
    }

    function rebuildVisibleCameras() {
        var knownOrder = ({})
        for (var orderedIndex = 0; orderedIndex < visualOrder.length; ++orderedIndex) {
            knownOrder[visualOrder[orderedIndex]] = true
        }

        var nextOrder = visualOrder.slice()

        for (var sourceCameraIndex = 0; sourceCameraIndex < sourceCameras.count; ++sourceCameraIndex) {
            var sourceCamera = sourceCameras.get(sourceCameraIndex)
            if (!knownOrder[sourceCamera.sourceKey]) {
                nextOrder.push(sourceCamera.sourceKey)
                knownOrder[sourceCamera.sourceKey] = true
            }
        }

        visualOrder = nextOrder
        cameraProxy.clear()
        for (var proxyIndex = 0; proxyIndex < visualOrder.length; ++proxyIndex) {
            var proxySourceIndex = findSourceIndex(visualOrder[proxyIndex])
            if (proxySourceIndex >= 0) {
                var proxyCamera = sourceCameras.get(proxySourceIndex)
                if (proxyCamera.isVisible) {
                    cameraProxy.append(root.cameraObject(
                        proxyCamera.sourceKey,
                        proxyCamera.sourceRow,
                        proxyCamera.topicName,
                        proxyCamera.backendName,
                        proxyCamera.resolutionText,
                        proxyCamera.seriesColor,
                        proxyCamera.isVisible))
                }
            }
        }
    }

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
        if (cameraProxy.count <= 0) {
            return 16 / 9
        }
        var aspectSum = 0
        for (var index = 0; index < cameraProxy.count; ++index) {
            aspectSum += cameraAspectRatio(cameraProxy.get(index).resolutionText)
        }
        return aspectSum / cameraProxy.count
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

    function layoutForIndex(itemIndex) {
        var count = Math.max(1, cameraProxy.count)
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

    function insertIndexAtPoint(xPosition, yPosition) {
        var count = cameraProxy.count
        if (count <= 0) {
            return 0
        }
        var bestIndex = count
        var bestDistance = Number.MAX_VALUE
        for (var index = 0; index < count; ++index) {
            var itemLayout = layoutForIndex(index)
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

    function targetIndexAfterRemovingSource() {
        if (dropInsertIndex < 0) {
            return -1
        }
        var targetIndex = dropInsertIndex
        if (dragSourceIndex >= 0 && targetIndex > dragSourceIndex) {
            targetIndex -= 1
        }
        return Math.max(0, Math.min(Math.max(0, cameraProxy.count - 1), targetIndex))
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
        return layoutForIndex(targetIndexAfterRemovingSource())
    }

    function beginDragPress(sourceKey, sourceIndex, xPosition, yPosition) {
        pendingDragSourceKey = sourceKey
        pendingDragSourceIndex = sourceIndex
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

    function startDrag(sourceKey, sourceIndex, xPosition, yPosition) {
        pendingDragSourceKey = sourceKey
        pendingDragSourceIndex = sourceIndex
        dragSourceKey = sourceKey
        dragSourceIndex = sourceIndex
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

    function commitDropInsertIndex() {
        if (!root.dragActive || dragSourceIndex < 0 || dropInsertIndex < 0) {
            finishDrag()
            return
        }
        var movedKey = dragSourceKey
        var nextOrder = visualOrder.slice()
        var fromOrderIndex = nextOrder.indexOf(movedKey)
        if (fromOrderIndex >= 0) {
            nextOrder.splice(fromOrderIndex, 1)
            var visibleKeys = []
            for (var proxyIndex = 0; proxyIndex < cameraProxy.count; ++proxyIndex) {
                var proxyKey = cameraProxy.get(proxyIndex).sourceKey
                if (proxyKey !== movedKey) {
                    visibleKeys.push(proxyKey)
                }
            }
            var boundedInsert = Math.max(0, Math.min(
                dropInsertIndex > dragSourceIndex ? dropInsertIndex - 1 : dropInsertIndex,
                visibleKeys.length))
            var anchorKey = boundedInsert < visibleKeys.length ? visibleKeys[boundedInsert] : ""
            var orderInsertIndex = anchorKey.length > 0 ? nextOrder.indexOf(anchorKey) : nextOrder.length
            if (orderInsertIndex < 0) {
                orderInsertIndex = nextOrder.length
            }
            nextOrder.splice(orderInsertIndex, 0, movedKey)
            visualOrder = nextOrder
            rebuildVisibleCameras()
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

    ListModel {
        id: sourceCameras
    }

    ListModel {
        id: cameraProxy
    }

    Instantiator {
        id: sourceInstantiator

        model: root.model

        delegate: QtObject {
            property int sourceRow: model.index
            property string backendName: model.backendName || ""
            property string topicName: model.topicName || ""
            property string resolutionText: model.resolutionText || ""
            property string seriesColor: model.seriesColor || "#2563eb"
            property bool isCamera: Boolean(model.isCamera)
            property bool isVisible: Boolean(model.isVisible)
            property string sourceKey: root.makeSourceKey(sourceRow, topicName, backendName)
            property string syncedSourceKey: ""

            function syncToSourceList() {
                if (syncedSourceKey.length > 0 && syncedSourceKey !== sourceKey) {
                    var previousIndex = root.findSourceIndex(syncedSourceKey)
                    if (previousIndex >= 0) {
                        sourceCameras.remove(previousIndex)
                    }
                    syncedSourceKey = ""
                }

                var sourceIndex = root.findSourceIndex(sourceKey)
                if (isCamera && topicName.length > 0) {
                    var camera = root.cameraObject(
                        sourceKey,
                        sourceRow,
                        topicName,
                        backendName,
                        resolutionText,
                        seriesColor,
                        isVisible)
                    if (sourceIndex >= 0) {
                        sourceCameras.set(sourceIndex, camera)
                    } else {
                        var sourceRowIndex = root.findSourceRowIndex(sourceRow)
                        if (sourceRowIndex >= 0) {
                            sourceCameras.remove(sourceRowIndex)
                        }
                        sourceCameras.append(camera)
                    }
                    syncedSourceKey = sourceKey
                } else if (sourceIndex >= 0) {
                    sourceCameras.remove(sourceIndex)
                    syncedSourceKey = ""
                }
                if (!root.sourceRefreshActive) {
                    root.rebuildVisibleCameras()
                }
            }

            Component.onCompleted: syncToSourceList()
            Component.onDestruction: {
                var sourceIndex = root.findSourceIndex(syncedSourceKey)
                if (sourceIndex >= 0) {
                    sourceCameras.remove(sourceIndex)
                    root.rebuildVisibleCameras()
                }
            }
            onBackendNameChanged: syncToSourceList()
            onTopicNameChanged: syncToSourceList()
            onResolutionTextChanged: syncToSourceList()
            onSeriesColorChanged: syncToSourceList()
            onSourceRowChanged: syncToSourceList()
            onIsCameraChanged: syncToSourceList()
            onIsVisibleChanged: syncToSourceList()
            onSourceKeyChanged: syncToSourceList()
        }
    }

    Connections {
        target: root.model
        ignoreUnknownSignals: true

        function onModelReset() {
            root.refreshSourceList()
        }

        function onRowsInserted(parent, first, last) {
            root.refreshSourceList()
        }

        function onRowsRemoved(parent, first, last) {
            root.refreshSourceList()
        }

        function onDataChanged(topLeft, bottomRight, roles) {
            root.refreshSourceList()
        }
    }

    onModelChanged: {
        sourceCameras.clear()
        cameraProxy.clear()
        visualOrder = []
    }

    function refreshSourceList() {
        sourceCameras.clear()
        root.sourceRefreshActive = true
        for (var index = 0; index < sourceInstantiator.count; ++index) {
            sourceInstantiator.objectAt(index).syncToSourceList()
        }
        root.sourceRefreshActive = false
        rebuildVisibleCameras()
    }

    Item {
        id: previewArea

        anchors.fill: parent
        anchors.margins: 4
        clip: true

        Repeater {
            model: cameraProxy

            delegate: Item {
                id: cameraCell

                required property int index
                required property string sourceKey
                required property string topicName
                required property string resolutionText
                required property color seriesColor

                readonly property var cellLayout: root.layoutForIndex(index)

                x: cellLayout.x
                y: cellLayout.y
                width: cellLayout.width
                height: cellLayout.height

                CameraPreviewTile {
                    anchors.fill: parent
                    visible: !(root.dragActive && root.dragSourceKey === cameraCell.sourceKey)
                    topicName: cameraCell.topicName
                    resolutionText: cameraCell.resolutionText
                    seriesColor: cameraCell.seriesColor
                }

                MouseArea {
                    anchors.fill: parent
                    preventStealing: true

                    onPressed: function(mouse) {
                        var point = cameraCell.mapToItem(previewArea, mouse.x, mouse.y)
                        root.beginDragPress(cameraCell.sourceKey, cameraCell.index, point.x, point.y)
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
            border.color: "#2563eb"
            border.width: 2
            opacity: 0.85
        }

        CameraPreviewTile {
            id: floatingPreview

            visible: root.dragActive && root.dragSourceIndex >= 0
            width: root.dragSourceIndex >= 0 ? root.layoutForIndex(root.dragSourceIndex).width : 1
            height: root.dragSourceIndex >= 0 ? root.layoutForIndex(root.dragSourceIndex).height : 1
            x: Math.max(0, Math.min(previewArea.width - width, root.dragX - width / 2))
            y: Math.max(0, Math.min(previewArea.height - height, root.dragY - height / 2))
            z: 10
            opacity: 0.92
            topicName: root.dragSourceIndex >= 0 ? cameraProxy.get(root.dragSourceIndex).topicName : ""
            resolutionText: root.dragSourceIndex >= 0 ? cameraProxy.get(root.dragSourceIndex).resolutionText : ""
            seriesColor: root.dragSourceIndex >= 0 ? cameraProxy.get(root.dragSourceIndex).seriesColor : "#2563eb"
            dragActive: true
        }
    }
}
