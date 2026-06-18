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
    property string activeDragMode: ""
    property int activeDragInstanceId: -1
    property real activeDragPressX: 0
    property real activeDragCurrentX: 0
    property real activeDragStartSeconds: 0
    property real activeDragEndSeconds: 0
    property real activeDragPreviewStartSeconds: 0
    property real activeDragPreviewEndSeconds: 0

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

    function requestDelete(instanceId, localX, localY) {
        root.contextInstanceId = instanceId
        deleteMenu.popup(root, localX, localY)
    }

    function startDrag(mode, instanceId, startSeconds, endSeconds, rootX) {
        root.activeDragMode = mode
        root.activeDragInstanceId = instanceId
        root.activeDragPressX = Number(rootX || 0)
        root.activeDragCurrentX = root.activeDragPressX
        root.activeDragStartSeconds = Number(startSeconds || 0)
        root.activeDragEndSeconds = Number(endSeconds || root.activeDragStartSeconds)
        root.activeDragPreviewStartSeconds = root.activeDragStartSeconds
        root.activeDragPreviewEndSeconds = root.activeDragEndSeconds
    }

    function updateDrag(rootX) {
        if (root.activeDragMode === "") {
            return
        }

        root.activeDragCurrentX = Number(rootX || 0)
        var currentTime = root.timeAtX(root.activeDragCurrentX)
        var pressTime = root.timeAtX(root.activeDragPressX)
        var deltaSeconds = currentTime - pressTime

        if (root.activeDragMode === "point") {
            var pointSeconds = root.activeDragStartSeconds + deltaSeconds
            root.activeDragPreviewStartSeconds = pointSeconds
            root.activeDragPreviewEndSeconds = pointSeconds
        } else if (root.activeDragMode === "range") {
            root.activeDragPreviewStartSeconds = root.activeDragStartSeconds + deltaSeconds
            root.activeDragPreviewEndSeconds = root.activeDragEndSeconds + deltaSeconds
        } else if (root.activeDragMode === "left") {
            root.activeDragPreviewStartSeconds = currentTime
            root.activeDragPreviewEndSeconds = root.activeDragEndSeconds
        } else if (root.activeDragMode === "right") {
            root.activeDragPreviewStartSeconds = root.activeDragStartSeconds
            root.activeDragPreviewEndSeconds = currentTime
        }
    }

    function finishDrag() {
        if (root.activeDragMode === "" || !root.markerModel) {
            root.activeDragMode = ""
            root.activeDragInstanceId = -1
            return
        }

        var instanceId = root.activeDragInstanceId
        var startSeconds = root.activeDragPreviewStartSeconds
        var endSeconds = root.activeDragPreviewEndSeconds
        var dragMode = root.activeDragMode
        root.activeDragMode = ""
        root.activeDragInstanceId = -1

        if (dragMode === "point" && root.markerModel.movePoint) {
            root.markerModel.movePoint(root.rowIndex, instanceId, startSeconds)
        } else if (root.markerModel.moveRange) {
            root.markerModel.moveRange(root.rowIndex, instanceId, startSeconds, endSeconds)
        }
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
            readonly property bool isActiveDrag: root.activeDragInstanceId === instanceId
            readonly property real displayStartSeconds: isActiveDrag ?
                root.activeDragPreviewStartSeconds :
                root.normalizedStart(modelData)
            readonly property real displayEndSeconds: isActiveDrag ?
                root.activeDragPreviewEndSeconds :
                root.normalizedEnd(modelData)
            readonly property real displayNormalizedStart: Math.min(displayStartSeconds, displayEndSeconds)
            readonly property real displayNormalizedEnd: Math.max(displayStartSeconds, displayEndSeconds)

            x: instanceKind === "point" ?
                root.xAtTime(displayStartSeconds) - width / 2 :
                root.xAtTime(displayNormalizedStart)
            y: 0
            width: instanceKind === "point" ?
                16 :
                Math.max(14, root.xAtTime(displayNormalizedEnd) - root.xAtTime(displayNormalizedStart))
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
                        root.requestDelete(
                            instanceDelegate.instanceId,
                            root.localXFromMouse(pointMouseArea, mouse),
                            pointMouseArea.mapToItem(root, mouse.x, mouse.y).y)
                        return
                    }
                    root.startDrag(
                        "point",
                        instanceDelegate.instanceId,
                        instanceDelegate.instanceStart,
                        instanceDelegate.instanceStart,
                        root.localXFromMouse(pointMouseArea, mouse))
                }

                onPositionChanged: function(mouse) {
                    if (pressedButtons & Qt.LeftButton) {
                        root.updateDrag(root.localXFromMouse(pointMouseArea, mouse))
                    }
                }

                onReleased: root.finishDrag()
                onCanceled: root.finishDrag()
            }

            Rectangle {
                id: rangeBody

                visible: instanceDelegate.instanceKind === "range"
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                height: 12
                color: instanceDelegate.instanceColor

                Rectangle {
                    id: leftRangeBorder

                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 2
                    color: Qt.darker(instanceDelegate.instanceColor, 1.25)
                }

                Rectangle {
                    id: rightRangeBorder

                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 2
                    color: Qt.darker(instanceDelegate.instanceColor, 1.25)
                }
            }

            MouseArea {
                id: rangeBodyMouseArea

                visible: instanceDelegate.instanceKind === "range"
                anchors.fill: rangeBody
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                z: 10
                onWheel: function(wheel) {
                    root.zoomAtLocalX(root.localXFromMouse(rangeBodyMouseArea, wheel), wheel)
                }

                onPressed: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                        root.requestDelete(
                            instanceDelegate.instanceId,
                            root.localXFromMouse(rangeBodyMouseArea, mouse),
                            rangeBodyMouseArea.mapToItem(root, mouse.x, mouse.y).y)
                        return
                    }
                    root.startDrag(
                        "range",
                        instanceDelegate.instanceId,
                        root.normalizedStart(instanceDelegate.modelData),
                        root.normalizedEnd(instanceDelegate.modelData),
                        root.localXFromMouse(rangeBodyMouseArea, mouse))
                }

                onPositionChanged: function(mouse) {
                    if (pressedButtons & Qt.LeftButton) {
                        root.updateDrag(root.localXFromMouse(rangeBodyMouseArea, mouse))
                    }
                }

                onReleased: root.finishDrag()
                onCanceled: root.finishDrag()
            }

            MouseArea {
                id: leftResizeHandle

                visible: instanceDelegate.instanceKind === "range"
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 4
                height: 18
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: Qt.SizeHorCursor
                z: 20
                onWheel: function(wheel) {
                    root.zoomAtLocalX(root.localXFromMouse(leftResizeHandle, wheel), wheel)
                }

                onPressed: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                        root.requestDelete(
                            instanceDelegate.instanceId,
                            root.localXFromMouse(leftResizeHandle, mouse),
                            leftResizeHandle.mapToItem(root, mouse.x, mouse.y).y)
                        return
                    }
                    root.startDrag(
                        "left",
                        instanceDelegate.instanceId,
                        root.normalizedStart(instanceDelegate.modelData),
                        root.normalizedEnd(instanceDelegate.modelData),
                        root.localXFromMouse(leftResizeHandle, mouse))
                }

                onPositionChanged: function(mouse) {
                    if (pressedButtons & Qt.LeftButton) {
                        root.updateDrag(root.localXFromMouse(leftResizeHandle, mouse))
                    }
                }

                onReleased: root.finishDrag()
                onCanceled: root.finishDrag()
            }

            MouseArea {
                id: rightResizeHandle

                visible: instanceDelegate.instanceKind === "range"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 4
                height: 18
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: Qt.SizeHorCursor
                z: 20
                onWheel: function(wheel) {
                    root.zoomAtLocalX(root.localXFromMouse(rightResizeHandle, wheel), wheel)
                }

                onPressed: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                        root.requestDelete(
                            instanceDelegate.instanceId,
                            root.localXFromMouse(rightResizeHandle, mouse),
                            rightResizeHandle.mapToItem(root, mouse.x, mouse.y).y)
                        return
                    }
                    root.startDrag(
                        "right",
                        instanceDelegate.instanceId,
                        root.normalizedStart(instanceDelegate.modelData),
                        root.normalizedEnd(instanceDelegate.modelData),
                        root.localXFromMouse(rightResizeHandle, mouse))
                }

                onPositionChanged: function(mouse) {
                    if (pressedButtons & Qt.LeftButton) {
                        root.updateDrag(root.localXFromMouse(rightResizeHandle, mouse))
                    }
                }

                onReleased: root.finishDrag()
                onCanceled: root.finishDrag()
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
