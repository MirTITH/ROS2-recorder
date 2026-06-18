import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: root

    property int rowIndex: -1
    property string kind: "point"
    property string markerColor: "#2563eb"
    property var instances: []
    property bool hasPendingRangeStart: false
    property real pendingStartSeconds: 0
    property real playheadSeconds: 0
    property var viewport
    property var markerModel

    property int contextInstanceId: -1

    height: 32
    color: "#fbfdff"

    function xAtTime(seconds) {
        if (root.viewport && root.viewport.xAtTime) {
            return root.viewport.xAtTime(seconds, root.width)
        }
        return 0
    }

    function timeAtX(xPosition) {
        if (root.viewport && root.viewport.timeAtX) {
            return root.viewport.timeAtX(xPosition, root.width)
        }
        return Math.max(0, Number(xPosition || 0))
    }

    function localXFromMouse(item, mouse) {
        return item.mapToItem(root, mouse.x, mouse.y).x
    }

    function zoomAtLocalX(localX, wheel) {
        if (!root.viewport) {
            return
        }
        if ((wheel.modifiers & Qt.ShiftModifier) && root.viewport.panByWheel) {
            root.viewport.panByWheel(wheel.angleDelta.y)
        } else if (root.viewport.zoomAt) {
            root.viewport.zoomAt(localX, root.width, wheel.angleDelta.y)
        } else {
            return
        }
        wheel.accepted = true
    }

    function normalizedStart(instance) {
        return Math.min(Number(instance.startSeconds || 0), Number(instance.endSeconds || 0))
    }

    function normalizedEnd(instance) {
        return Math.max(Number(instance.startSeconds || 0), Number(instance.endSeconds || 0))
    }

    function requestDelete(instanceId) {
        root.contextInstanceId = instanceId
        deleteMenu.popup()
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: "#e2e8f0"
    }

    Rectangle {
        id: pendingRangePreview

        visible: root.kind === "range" && root.hasPendingRangeStart
        x: Math.min(root.xAtTime(root.pendingStartSeconds), root.xAtTime(root.playheadSeconds))
        y: 11
        width: Math.max(
            3,
            Math.abs(root.xAtTime(root.playheadSeconds) - root.xAtTime(root.pendingStartSeconds)))
        height: 10
        color: root.markerColor
        opacity: 0.32
        border.color: root.markerColor
        border.width: 1
    }

    Repeater {
        model: root.instances

        delegate: Item {
            id: instanceDelegate

            required property var modelData

            readonly property int instanceId: Number(modelData.id)
            readonly property string instanceKind: String(modelData.kind || root.kind)
            readonly property real instanceStart: Number(modelData.startSeconds || 0)
            readonly property real instanceEnd: Number(modelData.endSeconds || instanceStart)
            readonly property string instanceColor: String(modelData.color || root.markerColor)

            x: instanceKind === "point" ?
                root.xAtTime(instanceStart) - width / 2 :
                root.xAtTime(root.normalizedStart(modelData))
            y: 0
            width: instanceKind === "point" ?
                16 :
                Math.max(14, root.xAtTime(root.normalizedEnd(modelData)) - root.xAtTime(root.normalizedStart(modelData)))
            height: root.height
            z: 5

            Rectangle {
                visible: instanceDelegate.instanceKind === "point"
                anchors.centerIn: parent
                width: 8
                height: 8
                rotation: 45
                color: instanceDelegate.instanceColor
                border.color: "#ffffff"
                border.width: 1
            }

            MouseArea {
                id: pointMouseArea

                visible: instanceDelegate.instanceKind === "point"
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onWheel: function(wheel) {
                    root.zoomAtLocalX(root.localXFromMouse(pointMouseArea, wheel), wheel)
                }

                onPressed: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                        root.requestDelete(instanceDelegate.instanceId)
                    }
                }

                onPositionChanged: function(mouse) {
                    if ((pressedButtons & Qt.LeftButton) && root.markerModel && root.markerModel.movePoint) {
                        root.markerModel.movePoint(
                            root.rowIndex,
                            instanceDelegate.instanceId,
                            root.timeAtX(root.localXFromMouse(pointMouseArea, mouse)))
                    }
                }
            }

            Rectangle {
                id: rangeBody

                visible: instanceDelegate.instanceKind === "range"
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                height: 12
                color: instanceDelegate.instanceColor
                opacity: 0.78
                border.color: instanceDelegate.instanceColor
                border.width: 1
            }

            MouseArea {
                id: rangeBodyMouseArea

                property real pressTrackX: 0
                property real pressStartSeconds: 0
                property real pressEndSeconds: 0

                visible: instanceDelegate.instanceKind === "range"
                anchors.fill: rangeBody
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                z: 10
                onWheel: function(wheel) {
                    root.zoomAtLocalX(root.localXFromMouse(rangeBodyMouseArea, wheel), wheel)
                }

                onPressed: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                        root.requestDelete(instanceDelegate.instanceId)
                        return
                    }
                    pressTrackX = root.localXFromMouse(rangeBodyMouseArea, mouse)
                    pressStartSeconds = root.normalizedStart(instanceDelegate.modelData)
                    pressEndSeconds = root.normalizedEnd(instanceDelegate.modelData)
                }

                onPositionChanged: function(mouse) {
                    if ((pressedButtons & Qt.LeftButton) && root.markerModel && root.markerModel.moveRange) {
                        var deltaSeconds = root.timeAtX(root.localXFromMouse(rangeBodyMouseArea, mouse)) - root.timeAtX(pressTrackX)
                        root.markerModel.moveRange(
                            root.rowIndex,
                            instanceDelegate.instanceId,
                            pressStartSeconds + deltaSeconds,
                            pressEndSeconds + deltaSeconds)
                    }
                }
            }

            MouseArea {
                id: leftResizeHandle

                visible: instanceDelegate.instanceKind === "range"
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 10
                height: 18
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: Qt.SizeHorCursor
                z: 20
                onWheel: function(wheel) {
                    root.zoomAtLocalX(root.localXFromMouse(leftResizeHandle, wheel), wheel)
                }

                onPressed: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                        root.requestDelete(instanceDelegate.instanceId)
                    }
                }

                onPositionChanged: function(mouse) {
                    if ((pressedButtons & Qt.LeftButton) && root.markerModel && root.markerModel.moveRange) {
                        root.markerModel.moveRange(
                            root.rowIndex,
                            instanceDelegate.instanceId,
                            root.timeAtX(root.localXFromMouse(leftResizeHandle, mouse)),
                            root.normalizedEnd(instanceDelegate.modelData))
                    }
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: 3
                    height: 14
                    color: "#ffffff"
                    opacity: 0.9
                }
            }

            MouseArea {
                id: rightResizeHandle

                visible: instanceDelegate.instanceKind === "range"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 10
                height: 18
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: Qt.SizeHorCursor
                z: 20
                onWheel: function(wheel) {
                    root.zoomAtLocalX(root.localXFromMouse(rightResizeHandle, wheel), wheel)
                }

                onPressed: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                        root.requestDelete(instanceDelegate.instanceId)
                    }
                }

                onPositionChanged: function(mouse) {
                    if ((pressedButtons & Qt.LeftButton) && root.markerModel && root.markerModel.moveRange) {
                        root.markerModel.moveRange(
                            root.rowIndex,
                            instanceDelegate.instanceId,
                            root.normalizedStart(instanceDelegate.modelData),
                            root.timeAtX(root.localXFromMouse(rightResizeHandle, mouse)))
                    }
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: 3
                    height: 14
                    color: "#ffffff"
                    opacity: 0.9
                }
            }
        }
    }

    Menu {
        id: deleteMenu

        MenuItem {
            text: "删除"
            onTriggered: {
                if (root.markerModel && root.markerModel.deleteInstance) {
                    root.markerModel.deleteInstance(root.rowIndex, root.contextInstanceId)
                }
            }
        }
    }
}
