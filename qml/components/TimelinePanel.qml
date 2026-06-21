import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "."

Panel {
    id: root

    property var controller
    property var model
    property var eventMarkerModel
    property real durationSeconds: 80
    property bool listsReady: false
    property int rulerLabelTickStride: 10
    readonly property real effectiveDurationSeconds: Math.max(1, Number(durationSeconds) || 1)
    readonly property real playheadSeconds: controller ? Number(controller.playheadSeconds) : 0
    readonly property var timelineViewport: viewport
    readonly property real visibleStartSeconds: viewport.visibleStartSeconds
    readonly property real visibleDurationSeconds: viewport.boundedVisibleDuration

    title: "时间轴"

    function scrollRows(deltaY) {
        var nextY = Math.max(0, Math.min(infoColumnPane.contentHeight - infoColumnPane.viewportHeight, infoColumnPane.contentY - deltaY))
        infoColumnPane.contentY = nextY
        laneColumnPane.contentY = nextY
    }

    function syncLaneToInfo() {
        if (!listsReady) {
            return
        }
        if (Math.abs(laneColumnPane.contentY - infoColumnPane.contentY) > 0.5) {
            laneColumnPane.contentY = infoColumnPane.contentY
        }
    }

    function syncInfoToLane() {
        if (!listsReady) {
            return
        }
        if (Math.abs(infoColumnPane.contentY - laneColumnPane.contentY) > 0.5) {
            infoColumnPane.contentY = laneColumnPane.contentY
        }
    }

    TimelineViewport {
        id: viewport
        totalDurationSeconds: root.effectiveDurationSeconds
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal
        handle: ResizeHandle { lineOrientation: Qt.Vertical }

        TrackInfoColumn {
            id: infoColumnPane
            SplitView.preferredWidth: 300
            SplitView.minimumWidth: 50
            SplitView.maximumWidth: 520
            controller: root.controller
            model: root.model
            eventMarkerModel: root.eventMarkerModel
            playheadSeconds: root.playheadSeconds
            onContentScrolled: root.syncLaneToInfo()
        }

        TrackLaneColumn {
            id: laneColumnPane
            SplitView.fillWidth: true
            controller: root.controller
            model: root.model
            eventMarkerModel: root.eventMarkerModel
            viewport: viewport
            playheadSeconds: root.playheadSeconds
            effectiveDurationSeconds: root.effectiveDurationSeconds
            rulerLabelTickStride: root.rulerLabelTickStride
            onContentScrolled: root.syncInfoToLane()
        }
    }

    Component.onCompleted: {
        viewport.setWindow(0, root.effectiveDurationSeconds)
        listsReady = true
    }
}
