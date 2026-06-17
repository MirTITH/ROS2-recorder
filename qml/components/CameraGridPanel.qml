import QtQuick 2.15
import QtQuick.Controls 2.15

Panel {
    id: root

    property var model
    property int visibleCameraCount: 0
    readonly property int topicNameRole: 257
    readonly property int isVisibleRole: 260
    readonly property int seriesColorRole: 262
    readonly property int isCameraRole: 265
    readonly property int resolutionTextRole: 268

    title: "相机预览"

    visible: visibleCameraCount > 0
    SplitView.preferredHeight: visible ? 260 : 0
    SplitView.minimumHeight: visible ? 140 : 0
    SplitView.maximumHeight: visible ? 520 : 0

    function roleValue(row, role, fallbackValue) {
        if (!root.model || !root.model.index || !root.model.data) {
            return fallbackValue
        }

        var sourceIndex = root.model.index(row, 0)
        var value = root.model.data(sourceIndex, role)
        return value === undefined || value === null ? fallbackValue : value
    }

    function rebuildVisibleCameras() {
        cameraProxy.clear()
        if (!root.model || !root.model.rowCount) {
            return
        }

        var rowCount = root.model.rowCount()
        for (var row = 0; row < rowCount; ++row) {
            if (roleValue(row, root.isCameraRole, false) && roleValue(row, root.isVisibleRole, false)) {
                cameraProxy.append({
                    sourceRow: row,
                    topicName: roleValue(row, root.topicNameRole, ""),
                    resolutionText: roleValue(row, root.resolutionTextRole, ""),
                    seriesColor: roleValue(row, root.seriesColorRole, "#2563eb")
                })
            }
        }
    }

    function moveCamera(fromIndex, toIndex) {
        if (fromIndex < 0 || toIndex < 0 || fromIndex === toIndex ||
            fromIndex >= cameraProxy.count || toIndex >= cameraProxy.count) {
            return
        }
        cameraProxy.move(fromIndex, toIndex, 1)
    }

    onModelChanged: rebuildVisibleCameras()
    Component.onCompleted: rebuildVisibleCameras()

    ListModel {
        id: cameraProxy
    }

    Connections {
        target: root.model
        ignoreUnknownSignals: true

        function onModelReset() {
            root.rebuildVisibleCameras()
        }

        function onRowsInserted(parent, first, last) {
            root.rebuildVisibleCameras()
        }

        function onRowsRemoved(parent, first, last) {
            root.rebuildVisibleCameras()
        }

        function onDataChanged(topLeft, bottomRight, roles) {
            root.rebuildVisibleCameras()
        }
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
            Drag.active: dragHandler.active
            Drag.source: cameraCell
            Drag.hotSpot.x: width / 2
            Drag.hotSpot.y: height / 2

            readonly property int visualIndex: model.index

            CameraPreviewTile {
                anchors.fill: parent
                anchors.margins: 2
                topicName: model.topicName
                resolutionText: model.resolutionText
                seriesColor: model.seriesColor
                dragActive: dragHandler.active
            }

            DropArea {
                anchors.fill: parent

                onEntered: function(drag) {
                    if (!drag.source || drag.source === cameraCell) {
                        return
                    }
                    root.moveCamera(drag.source.visualIndex, cameraCell.visualIndex)
                }
            }

            DragHandler {
                id: dragHandler

                target: null
            }
        }
    }
}
