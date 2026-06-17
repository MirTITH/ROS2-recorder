import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQml.Models 2.15

Panel {
    id: root

    property var model
    property int visibleCameraCount: 0

    title: "相机预览"

    visible: visibleCameraCount > 0
    SplitView.preferredHeight: visible ? 260 : 0
    SplitView.minimumHeight: visible ? 140 : 0
    SplitView.maximumHeight: visible ? 520 : 0

    DelegateModel {
        id: visualModel

        model: root.model
        filterOnGroup: "visibleCamera"
        groups: [
            DelegateModelGroup {
                name: "visibleCamera"
                includeByDefault: false
            }
        ]

        delegate: Item {
            id: cameraCell

            width: grid.cellWidth
            height: grid.cellHeight
            visible: model.isCamera && model.isVisible

            DelegateModel.inVisibleCamera: model.isCamera && model.isVisible
            Drag.active: dragHandler.active
            Drag.source: cameraCell
            Drag.hotSpot.x: width / 2
            Drag.hotSpot.y: height / 2

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
                    visualModel.items.move(
                        drag.source.DelegateModel.itemsIndex,
                        cameraCell.DelegateModel.itemsIndex,
                        1)
                }
            }

            DragHandler {
                id: dragHandler

                target: null
            }
        }
    }

    GridView {
        id: grid

        anchors.fill: parent
        anchors.margins: 4
        clip: true
        interactive: false
        model: visualModel

        readonly property int columns: Math.max(1, Math.ceil(Math.sqrt(Math.max(1, root.visibleCameraCount) * width / Math.max(1, height))))
        readonly property int rows: Math.max(1, Math.ceil(Math.max(1, root.visibleCameraCount) / columns))

        cellWidth: width / columns
        cellHeight: height / rows
    }
}
