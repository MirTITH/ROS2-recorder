import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Panel {
    id: root

    property var controller
    property var model
    property real durationSeconds: 80
    property bool listsReady: false
    property int rulerLabelTickStride: 10
    readonly property real effectiveDurationSeconds: Math.max(1, Number(durationSeconds) || 1)
    readonly property real playheadSeconds: controller ? Number(controller.playheadSeconds) : 0
    readonly property real visibleStartSeconds: viewport.visibleStartSeconds
    readonly property real visibleDurationSeconds: viewport.boundedVisibleDuration

    title: "时间轴"

    function scrollRows(deltaY) {
        var nextY = Math.max(0, Math.min(infoList.contentHeight - infoList.height, infoList.contentY - deltaY))
        infoList.contentY = nextY
        curveList.contentY = nextY
    }

    function syncCurveToInfo() {
        if (!listsReady) {
            return
        }
        if (Math.abs(curveList.contentY - infoList.contentY) > 0.5) {
            curveList.contentY = infoList.contentY
        }
    }

    function syncInfoToCurve() {
        if (!listsReady) {
            return
        }
        if (Math.abs(infoList.contentY - curveList.contentY) > 0.5) {
            infoList.contentY = curveList.contentY
        }
    }

    function seekFromCurveX(xPosition) {
        if (controller && controller.setPlayheadSeconds) {
            controller.setPlayheadSeconds(viewport.timeAtX(xPosition, curveViewport.width))
        }
    }

    function formatTickLabel(seconds) {
        var totalMs = Math.max(0, Math.round(Number(seconds || 0) * 1000))
        var ms = totalMs % 1000
        var totalSeconds = Math.floor(totalMs / 1000)
        var s = totalSeconds % 60
        var totalMinutes = Math.floor(totalSeconds / 60)
        return totalMinutes + ":" +
            s.toString().padStart(2, "0") + "." +
            ms.toString().padStart(3, "0")
    }

    function timeString(seconds) {
        var totalMs = Math.max(0, Math.round(seconds * 1000))
        var ms = totalMs % 1000
        var totalSeconds = Math.floor(totalMs / 1000)
        var s = totalSeconds % 60
        var m = Math.floor(totalSeconds / 60) % 60
        var h = Math.floor(totalSeconds / 3600)
        return h.toString().padStart(2, "0") + ":" +
            m.toString().padStart(2, "0") + ":" +
            s.toString().padStart(2, "0") + "." +
            ms.toString().padStart(3, "0")
    }

    TimelineViewport {
        id: viewport
        totalDurationSeconds: root.effectiveDurationSeconds
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal
        handle: ResizeHandle { lineOrientation: Qt.Vertical }

        ColumnLayout {
            SplitView.preferredWidth: 300
            SplitView.minimumWidth: 50
            SplitView.maximumWidth: 520
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                color: "#eef2f7"
                border.color: "#dbe3ef"
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 6

                    Label {
                        Layout.fillWidth: true
                        text: root.timeString(root.playheadSeconds)
                        color: "#111827"
                        font.pixelSize: 12
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    Button {
                        Layout.preferredWidth: 72
                        Layout.preferredHeight: 24
                        visible: root.controller && root.controller.recording && !root.controller.followingLiveEdge
                        text: "回到实时"
                        font.pixelSize: 10
                        onClicked: root.controller.returnToLiveEdge()
                    }
                }
            }

            ListView {
                id: infoList

                Layout.fillWidth: true
                Layout.fillHeight: true
                model: root.model
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                onContentYChanged: root.syncCurveToInfo()

                delegate: TimelineInfoRow {
                    width: ListView.view.width
                    topicName: model.topicName
                    frequencyText: model.frequencyText
                    backendName: model.backendName
                    isVisible: model.isVisible
                    isCamera: model.isCamera
                    onToggleVisibleRequested: {
                        if (root.controller && root.controller.toggleTopicVisible) {
                            root.controller.toggleTopicVisible(index)
                        }
                    }
                }

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }
            }
        }

        ColumnLayout {
            SplitView.fillWidth: true
            spacing: 0

            Rectangle {
                id: ruler

                Layout.fillWidth: true
                Layout.preferredHeight: 30
                clip: true
                color: "#ffffff"
                border.color: "#dbe3ef"
                border.width: 1

                readonly property var rulerTickTimes: viewport.tickTimes(width, viewport.denseTickInterval(width))

                Repeater {
                    model: ruler.rulerTickTimes

                    delegate: Item {
                        required property int index

                        readonly property real tickTime: ruler.rulerTickTimes[index]
                        readonly property bool labeledTick: index % root.rulerLabelTickStride === 0

                        x: viewport.xAtTime(tickTime, ruler.width)
                        width: 1
                        height: ruler.height

                        Rectangle {
                            width: 1
                            height: labeledTick ? 11 : 6
                            color: labeledTick ? "#94a3b8" : "#cbd5e1"
                        }

                        Label {
                            anchors.top: parent.top
                            anchors.topMargin: 12
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: labeledTick
                            text: root.formatTickLabel(tickTime)
                            color: "#64748b"
                            font.pixelSize: 10
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onPressed: root.seekFromCurveX(mouse.x)
                    onPositionChanged: {
                        if (pressed) {
                            root.seekFromCurveX(mouse.x)
                        }
                    }
                }

                Rectangle {
                    objectName: "timelineRulerPlayhead"
                    width: 2
                    height: parent.height
                    visible: viewport.isTimeVisible(root.playheadSeconds)
                    x: viewport.xAtTime(root.playheadSeconds, parent.width) - width / 2
                    color: "#dc2626"
                    z: 5
                }
            }

            Item {
                id: curveViewport

                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ListView {
                    id: curveList

                    anchors.fill: parent
                    model: root.model
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: false
                    onContentYChanged: root.syncInfoToCurve()

                    delegate: TimelineCurveRow {
                        width: ListView.view.width
                        trackKind: model.trackKind
                        seriesList: model.seriesList
                        xMax: root.effectiveDurationSeconds
                        visibleStartSeconds: viewport.visibleStartSeconds
                        visibleDurationSeconds: viewport.boundedVisibleDuration
                    }
                }

                MouseArea {
                    objectName: "timelineCurveMouseArea"
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    onPressed: root.seekFromCurveX(mouse.x)
                    onPositionChanged: {
                        if (pressed) {
                            root.seekFromCurveX(mouse.x)
                        }
                    }
                    onWheel: function(wheel) {
                        if (wheel.modifiers & Qt.ShiftModifier) {
                            viewport.panByWheel(wheel.angleDelta.y)
                        } else {
                            viewport.zoomAt(wheel.x, curveViewport.width, wheel.angleDelta.y)
                        }
                        wheel.accepted = true
                    }
                }

                Rectangle {
                    objectName: "timelineCurvePlayhead"
                    width: 2
                    height: parent.height
                    visible: viewport.isTimeVisible(root.playheadSeconds)
                    x: viewport.xAtTime(root.playheadSeconds, parent.width) - width / 2
                    color: "#dc2626"
                    z: 5
                }
            }

            TimelineRangeBar {
                Layout.fillWidth: true
                totalDurationSeconds: root.effectiveDurationSeconds
                visibleStartSeconds: viewport.visibleStartSeconds
                visibleDurationSeconds: viewport.boundedVisibleDuration
                onWindowRequested: function(startSeconds, durationSeconds) {
                    viewport.setWindow(startSeconds, durationSeconds)
                }
            }
        }
    }

    Component.onCompleted: {
        viewport.setWindow(0, root.effectiveDurationSeconds)
        listsReady = true
    }
}
