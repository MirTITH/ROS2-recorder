import QtQuick 2.15
import QtQuick.Controls 2.15
import "."

Panel {
    id: root

    // Bound to the C++ CameraGridModel: rows are the visible cameras in order
    // (0..count-1), exposing topicName/backendName/resolutionText/seriesColor and
    // Q_INVOKABLE moveCamera(from, to). The model owns visibility filtering and
    // reorder; this panel is the view that renders the grid and wires two pure-view
    // helpers: CameraGridLayout (geometry / push-aside reflow) and
    // CameraDragController (drag state machine + drop-commit via moveCamera).
    property var model
    property int visibleCameraCount: 0

    // Per-visible-index resolution snapshot, populated by the cell delegates and
    // consumed by gridLayout for aspect-aware column/row scoring. The Repeater
    // instantiates every delegate eagerly and recreates them on any model reset
    // (including moveCamera), so this map mirrors the visible rows.
    property var resolutionByIndex: ({})

    title: "相机预览"

    visible: visibleCameraCount > 0
    SplitView.preferredHeight: visible ? 260 : 0
    SplitView.minimumHeight: visible ? 140 : 0
    SplitView.maximumHeight: visible ? 520 : 0

    CameraGridLayout {
        id: gridLayout

        model: root.model
        previewWidth: previewArea.width
        previewHeight: previewArea.height
        resolutionByIndex: root.resolutionByIndex
        dragActive: dragController.dragActive
        dragSourceIndex: dragController.dragSourceIndex
        dropInsertIndex: dragController.dropInsertIndex
    }

    CameraDragController {
        id: dragController

        model: root.model
        layout: gridLayout
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
                required property string topicKey
                required property int frameSeq

                // Per-cell key (matches the C++ model's internal "topic|backend"
                // key). Kept as a stable per-drag identity and for placeholder
                // bookkeeping; layout/drag math uses the visible index.
                readonly property string sourceKey: topicName + "|" + backendName

                readonly property var cellLayout: gridLayout.previewLayoutForKey(cameraCell.sourceKey, cameraCell.index)

                x: cellLayout.x
                y: cellLayout.y
                width: cellLayout.width
                height: cellLayout.height

                Component.onCompleted: root.resolutionByIndex[index] = resolutionText
                onResolutionTextChanged: root.resolutionByIndex[index] = resolutionText

                CameraPreviewTile {
                    anchors.fill: parent
                    visible: !(dragController.dragActive && dragController.dragSourceIndex === cameraCell.index)
                    topicName: cameraCell.topicName
                    resolutionText: cameraCell.resolutionText
                    seriesColor: cameraCell.seriesColor
                    topicKey: cameraCell.topicKey
                    frameSeq: cameraCell.frameSeq
                }

                MouseArea {
                    anchors.fill: parent
                    preventStealing: true

                    onPressed: function(mouse) {
                        var point = cameraCell.mapToItem(previewArea, mouse.x, mouse.y)
                        dragController.beginDragPress(
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
                            dragController.updateDragPosition(point.x, point.y)
                        }
                    }

                    onReleased: dragController.commitDropInsertIndex()
                    onCanceled: dragController.finishDrag()
                }
            }
        }

        Rectangle {
            id: dropPlaceholder

            readonly property var targetLayout: gridLayout.placeholderLayout()

            visible: dragController.dragActive
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

            readonly property var previewLayout: gridLayout.floatingPreviewLayout()

            visible: dragController.dragActive && dragController.dragSourceIndex >= 0
            width: previewLayout.width
            height: previewLayout.height
            x: Math.max(0, Math.min(previewArea.width - width, dragController.dragX - width / 2))
            y: Math.max(0, Math.min(previewArea.height - height, dragController.dragY - height / 2))
            z: 10
            opacity: 0.92
            topicName: dragController.dragTopicName
            resolutionText: dragController.dragResolutionText
            seriesColor: dragController.dragSeriesColor
            dragActive: true
        }
    }
}
