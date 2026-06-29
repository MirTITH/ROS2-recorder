import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "."

ColumnLayout {
    id: root

    property var controller
    property var model
    property var eventMarkerModel
    property var viewport
    property real playheadSeconds: 0
    property real effectiveDurationSeconds: 1
    property int rulerLabelTickStride: 10
    property alias contentY: trackLaneList.contentY
    readonly property bool showTimelineData: !!controller && (controller.recording || controller.historyMode)

    signal contentScrolled()

    spacing: 0

    function seekFromLaneX(xPosition) {
        if (controller && controller.setPlayheadSeconds) {
            controller.setPlayheadSeconds(viewport.timeAtX(xPosition, trackLaneViewport.width))
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

    Rectangle {
        id: ruler

        Layout.fillWidth: true
        Layout.preferredHeight: 30
        clip: true
        color: Theme.surface
        border.color: Theme.border
        border.width: 1

        readonly property var rulerTickTimes: viewport.tickTimes(width, viewport.denseTickInterval(width))

        Repeater {
            model: ruler.rulerTickTimes

            delegate: Item {
                required property int index

                readonly property real tickTime: ruler.rulerTickTimes[index]
                readonly property bool labeledTick: index % rulerLabelTickStride === 0

                x: viewport.xAtTime(tickTime, ruler.width)
                width: 1
                height: ruler.height

                Rectangle {
                    width: 1
                    height: labeledTick ? 11 : 6
                    color: labeledTick ? Theme.tickStrong : Theme.border
                }

                Label {
                    anchors.top: parent.top
                    anchors.topMargin: 12
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: labeledTick
                    text: formatTickLabel(tickTime)
                    color: Theme.textMuted
                    font.pixelSize: 10
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            onPressed: seekFromLaneX(mouse.x)
            onPositionChanged: {
                if (pressed) {
                    seekFromLaneX(mouse.x)
                }
            }
        }

        Rectangle {
            objectName: "timelineRulerPlayhead"
            width: 2
            height: parent.height
            visible: viewport.isTimeVisible(playheadSeconds)
            x: viewport.xAtTime(playheadSeconds, parent.width) - width / 2
            color: Theme.danger
            z: 5
        }
    }

    Item {
        id: trackLaneViewport

        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true

        MouseArea {
            objectName: "timelineLaneMouseArea"
            anchors.fill: parent
            z: 0
            acceptedButtons: Qt.LeftButton
            onPressed: seekFromLaneX(mouse.x)
            onPositionChanged: {
                if (pressed) {
                    seekFromLaneX(mouse.x)
                }
            }
            onWheel: function(wheel) {
                if (wheel.modifiers & Qt.ShiftModifier) {
                    viewport.panByWheel(wheel.angleDelta.y)
                } else {
                    viewport.zoomAt(wheel.x, trackLaneViewport.width, wheel.angleDelta.y)
                }
                wheel.accepted = true
            }
        }

        Flickable {
            id: trackLaneList

            anchors.fill: parent
            z: 1
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: width
            contentHeight: trackLaneColumn.implicitHeight
            interactive: false
            onContentYChanged: root.contentScrolled()

            Column {
                id: trackLaneColumn

                width: trackLaneList.width

                Repeater {
                    id: eventLaneRepeater
                    model: eventMarkerModel

                    EventTrackRow {
                        width: trackLaneColumn.width
                        rowIndex: index
                        eventName: model.name
                        kind: model.kind
                        markerColor: model.color
                        instances: model.instances
                        hasPendingRangeStart: model.hasPendingRangeStart
                        pendingStartSeconds: model.pendingStartSeconds
                        playheadSeconds: root.playheadSeconds
                        viewport: root.viewport
                        markerModel: eventMarkerModel
                        controller: root.controller
                    }
                }

                Rectangle {
                    width: trackLaneColumn.width
                    height: eventLaneRepeater.count > 0 ? 1 : 0
                    color: Theme.border
                }

                Repeater {
                    model: root.model

                    TimelineTrackRow {
                        width: trackLaneColumn.width
                        trackKind: model.trackKind
                        seriesList: model.seriesList
                        isExpanded: model.isExpanded
                        messageDots: model.messageDots
                        showData: root.showTimelineData
                        xMax: root.effectiveDurationSeconds
                        visibleStartSeconds: viewport.visibleStartSeconds
                        visibleDurationSeconds: viewport.boundedVisibleDuration
                        profiler: root.controller
                        profilerTag: model.topicName !== undefined ? model.topicName : ""
                    }
                }
            }
        }

        Rectangle {
            objectName: "timelineLanePlayhead"
            width: 2
            height: parent.height
            visible: viewport.isTimeVisible(playheadSeconds)
            x: viewport.xAtTime(playheadSeconds, parent.width) - width / 2
            color: Theme.danger
            z: 5
        }
    }

    TimelineRangeBar {
        Layout.fillWidth: true
        totalDurationSeconds: root.effectiveDurationSeconds
        visibleStartSeconds: viewport.visibleStartSeconds
        visibleDurationSeconds: viewport.boundedVisibleDuration
        onWindowRequested: function(startSeconds, durationSeconds) {
            viewport.manualInteraction()
            viewport.setWindow(startSeconds, durationSeconds)
        }
    }
}
