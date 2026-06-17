import QtQuick 2.15
import QtQuick.Controls 2.15

Panel {
    id: root

    property var model
    property int visibleCameraCount: 0
    property var visualOrder: []
    property string dragTopicName: ""
    property bool sourceRefreshActive: false

    title: "相机预览"

    visible: visibleCameraCount > 0
    SplitView.preferredHeight: visible ? 260 : 0
    SplitView.minimumHeight: visible ? 140 : 0
    SplitView.maximumHeight: visible ? 520 : 0

    function cameraObject(topicName, resolutionText, seriesColor, isVisible) {
        return {
            topicName: topicName,
            resolutionText: resolutionText,
            seriesColor: seriesColor,
            isVisible: isVisible
        }
    }

    function findSourceIndex(topicName) {
        for (var index = 0; index < sourceCameras.count; ++index) {
            if (sourceCameras.get(index).topicName === topicName) {
                return index
            }
        }
        return -1
    }

    function findProxyIndex(topicName) {
        for (var index = 0; index < cameraProxy.count; ++index) {
            if (cameraProxy.get(index).topicName === topicName) {
                return index
            }
        }
        return -1
    }

    function visibleTopicOrder() {
        var order = []
        for (var index = 0; index < cameraProxy.count; ++index) {
            order.push(cameraProxy.get(index).topicName)
        }
        return order
    }

    function rebuildVisibleCameras() {
        var nextOrder = []
        var seen = ({})

        for (var orderedIndex = 0; orderedIndex < visualOrder.length; ++orderedIndex) {
            var orderedTopic = visualOrder[orderedIndex]
            var sourceIndex = findSourceIndex(orderedTopic)
            if (sourceIndex >= 0 && !seen[orderedTopic]) {
                nextOrder.push(orderedTopic)
                seen[orderedTopic] = true
            }
        }

        for (var sourceCameraIndex = 0; sourceCameraIndex < sourceCameras.count; ++sourceCameraIndex) {
            var sourceCamera = sourceCameras.get(sourceCameraIndex)
            if (!seen[sourceCamera.topicName]) {
                nextOrder.push(sourceCamera.topicName)
                seen[sourceCamera.topicName] = true
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
                        proxyCamera.topicName,
                        proxyCamera.resolutionText,
                        proxyCamera.seriesColor,
                        proxyCamera.isVisible))
                }
            }
        }
    }

    function moveCamera(fromIndex, toIndex) {
        if (fromIndex < 0 || toIndex < 0 || fromIndex === toIndex ||
            fromIndex >= cameraProxy.count || toIndex >= cameraProxy.count) {
            return
        }

        var movedTopic = cameraProxy.get(fromIndex).topicName
        var targetTopic = cameraProxy.get(toIndex).topicName
        var nextOrder = visualOrder.slice()
        var movedOrderIndex = nextOrder.indexOf(movedTopic)
        if (movedOrderIndex < 0) {
            return
        }
        nextOrder.splice(movedOrderIndex, 1)

        var targetOrderIndex = nextOrder.indexOf(targetTopic)
        if (targetOrderIndex < 0) {
            return
        }
        nextOrder.splice(targetOrderIndex + (toIndex > fromIndex ? 1 : 0), 0, movedTopic)
        visualOrder = nextOrder
        rebuildVisibleCameras()
    }

    function startDragTopic(topicName) {
        dragTopicName = topicName
    }

    function finishDragTopic() {
        dragTopicName = ""
    }

    function moveDragTopicTo(targetIndex) {
        if (dragTopicName.length === 0 || cameraProxy.count === 0) {
            return
        }

        var boundedTarget = Math.max(0, Math.min(targetIndex, cameraProxy.count - 1))
        moveCamera(findProxyIndex(dragTopicName), boundedTarget)
    }

    function dragToPoint(item, itemX, itemY) {
        if (grid.cellWidth <= 0 || grid.cellHeight <= 0 || grid.columns <= 0) {
            return
        }

        var gridPoint = item.mapToItem(grid, itemX, itemY)
        var column = Math.max(0, Math.min(grid.columns - 1, Math.floor(gridPoint.x / grid.cellWidth)))
        var row = Math.max(0, Math.floor(gridPoint.y / grid.cellHeight))
        moveDragTopicTo((row * grid.columns) + column)
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
            property string topicName: model.topicName || ""
            property string resolutionText: model.resolutionText || ""
            property string seriesColor: model.seriesColor || "#2563eb"
            property bool isCamera: Boolean(model.isCamera)
            property bool isVisible: Boolean(model.isVisible)

            function syncToSourceList() {
                var sourceIndex = root.findSourceIndex(topicName)
                if (isCamera) {
                    var camera = root.cameraObject(topicName, resolutionText, seriesColor, isVisible)
                    if (sourceIndex >= 0) {
                        sourceCameras.set(sourceIndex, camera)
                    } else {
                        sourceCameras.append(camera)
                    }
                } else if (sourceIndex >= 0) {
                    sourceCameras.remove(sourceIndex)
                }
                if (!root.sourceRefreshActive) {
                    root.rebuildVisibleCameras()
                }
            }

            Component.onCompleted: syncToSourceList()
            Component.onDestruction: {
                var sourceIndex = root.findSourceIndex(topicName)
                if (sourceIndex >= 0) {
                    sourceCameras.remove(sourceIndex)
                    root.rebuildVisibleCameras()
                }
            }
            onTopicNameChanged: syncToSourceList()
            onResolutionTextChanged: syncToSourceList()
            onSeriesColorChanged: syncToSourceList()
            onIsCameraChanged: syncToSourceList()
            onIsVisibleChanged: syncToSourceList()
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

    GridView {
        id: grid

        anchors.fill: parent
        anchors.margins: 4
        clip: true
        interactive: false
        model: cameraProxy

        readonly property int columns: Math.max(1, Math.ceil(Math.sqrt(Math.max(1, root.visibleCameraCount) * width / Math.max(1, height))))
        readonly property int rows: Math.max(1, Math.ceil(Math.max(1, root.visibleCameraCount) / columns))

        cellWidth: width / columns
        cellHeight: height / rows

        delegate: Item {
            id: cameraCell

            width: grid.cellWidth
            height: grid.cellHeight

            CameraPreviewTile {
                anchors.fill: parent
                anchors.margins: 2
                topicName: model.topicName
                resolutionText: model.resolutionText
                seriesColor: model.seriesColor
                dragActive: root.dragTopicName === model.topicName
            }

            MouseArea {
                anchors.fill: parent
                preventStealing: true

                onPressed: function(mouse) {
                    root.startDragTopic(model.topicName)
                    root.dragToPoint(cameraCell, mouse.x, mouse.y)
                }

                onPositionChanged: function(mouse) {
                    if (pressed) {
                        root.dragToPoint(cameraCell, mouse.x, mouse.y)
                    }
                }

                onReleased: root.finishDragTopic()
                onCanceled: root.finishDragTopic()
            }
        }
    }
}
