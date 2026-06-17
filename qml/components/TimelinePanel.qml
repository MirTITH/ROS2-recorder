import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Panel {
    id: root

    property var controller
    property var model
    property real durationSeconds: 80
    property real visibleStartSeconds: 0
    property real visibleDurationSeconds: 80
    property bool listsReady: false
    readonly property real effectiveDurationSeconds: Math.max(1, Number(durationSeconds) || 1)
    readonly property real playheadSeconds: controller ? Number(controller.playheadSeconds) : 0

    title: "时间轴"

    function clamp(value, low, high) {
        return Math.max(low, Math.min(high, value))
    }

    function boundedVisibleDuration() {
        return Math.max(0.001, Math.min(effectiveDurationSeconds, Number(visibleDurationSeconds) || effectiveDurationSeconds))
    }

    function visibleEndSeconds() {
        return Math.min(effectiveDurationSeconds, visibleStartSeconds + boundedVisibleDuration())
    }

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

    function setVisibleWindow(startSeconds, durationSeconds) {
        var duration = Math.max(0.001, Math.min(effectiveDurationSeconds, Number(durationSeconds) || effectiveDurationSeconds))
        visibleDurationSeconds = duration
        visibleStartSeconds = clamp(Number(startSeconds) || 0, 0, Math.max(0, effectiveDurationSeconds - duration))
    }

    function playheadX(widthValue) {
        return clamp(((playheadSeconds - visibleStartSeconds) / boundedVisibleDuration()) * widthValue, 0, widthValue)
    }

    function seekFromCurveX(xPosition) {
        var seconds = visibleStartSeconds + (xPosition / Math.max(1, curveViewport.width)) * boundedVisibleDuration()
        if (controller && controller.setPlayheadSeconds) {
            controller.setPlayheadSeconds(clamp(seconds, 0, effectiveDurationSeconds))
        }
    }

    function zoomVisibleWindow(deltaY, anchorX, widthValue) {
        var oldDuration = boundedVisibleDuration()
        var factor = deltaY > 0 ? 0.86 : 1.16
        var newDuration = clamp(oldDuration * factor, 0.05, effectiveDurationSeconds)
        var anchorRatio = clamp(anchorX / Math.max(1, widthValue), 0, 1)
        var anchorTime = visibleStartSeconds + oldDuration * anchorRatio
        setVisibleWindow(anchorTime - newDuration * anchorRatio, newDuration)
    }

    function nudgePlayhead(deltaY) {
        var step = boundedVisibleDuration() / 40
        var direction = deltaY > 0 ? 1 : -1
        if (controller && controller.setPlayheadSeconds) {
            controller.setPlayheadSeconds(clamp(playheadSeconds + direction * step, 0, effectiveDurationSeconds))
        }
    }

    function niceTickInterval(widthValue) {
        var intervals = [0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 30, 60, 120, 300]
        var targetPixels = 110
        var rawInterval = boundedVisibleDuration() / Math.max(1, widthValue / targetPixels)
        for (var index = 0; index < intervals.length; ++index) {
            if (intervals[index] >= rawInterval) {
                return intervals[index]
            }
        }
        return intervals[intervals.length - 1]
    }

    function firstTick(interval) {
        return Math.ceil(visibleStartSeconds / interval) * interval
    }

    function tickCount(widthValue) {
        var interval = niceTickInterval(widthValue)
        return Math.max(1, Math.floor((visibleEndSeconds() - firstTick(interval)) / interval) + 1)
    }

    function tickTimeAt(index, widthValue) {
        var interval = niceTickInterval(widthValue)
        return firstTick(interval) + index * interval
    }

    function formatTickLabel(seconds) {
        if (boundedVisibleDuration() < 2) {
            return Math.round(seconds * 1000) + "ms"
        }
        if (boundedVisibleDuration() < 90) {
            return seconds.toFixed(seconds < 10 ? 1 : 0) + "s"
        }
        var totalSeconds = Math.floor(seconds)
        var minutes = Math.floor(totalSeconds / 60)
        var remainder = totalSeconds % 60
        return minutes + ":" + remainder.toString().padStart(2, "0")
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

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal
        handle: ResizeHandle { lineOrientation: Qt.Vertical }

        ColumnLayout {
            SplitView.preferredWidth: 300
            SplitView.minimumWidth: 190
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

                Repeater {
                    model: root.tickCount(ruler.width)

                    delegate: Item {
                        required property int index

                        readonly property real tickTime: root.tickTimeAt(index, ruler.width)

                        x: ((tickTime - root.visibleStartSeconds) / root.boundedVisibleDuration()) * ruler.width
                        width: 1
                        height: ruler.height

                        Rectangle {
                            width: 1
                            height: 9
                            color: "#94a3b8"
                        }

                        Label {
                            anchors.top: parent.top
                            anchors.topMargin: 11
                            anchors.horizontalCenter: parent.horizontalCenter
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
                    width: 2
                    height: parent.height
                    x: Math.max(0, Math.min(parent.width - width, root.playheadX(parent.width) - width / 2))
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
                        visibleStartSeconds: root.visibleStartSeconds
                        visibleDurationSeconds: root.boundedVisibleDuration()
                    }
                }

                MouseArea {
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
                            root.nudgePlayhead(wheel.angleDelta.y)
                        } else {
                            root.zoomVisibleWindow(wheel.angleDelta.y, wheel.x, curveViewport.width)
                        }
                        wheel.accepted = true
                    }
                }

                Rectangle {
                    width: 2
                    height: parent.height
                    x: Math.max(0, Math.min(parent.width - width, root.playheadX(parent.width) - width / 2))
                    color: "#dc2626"
                    z: 5
                }
            }

            TimelineRangeBar {
                Layout.fillWidth: true
                totalDurationSeconds: root.effectiveDurationSeconds
                visibleStartSeconds: root.visibleStartSeconds
                visibleDurationSeconds: root.boundedVisibleDuration()
                onWindowRequested: function(startSeconds, durationSeconds) {
                    root.setVisibleWindow(startSeconds, durationSeconds)
                }
            }
        }
    }

    Component.onCompleted: {
        root.setVisibleWindow(0, root.effectiveDurationSeconds)
        listsReady = true
    }
}
