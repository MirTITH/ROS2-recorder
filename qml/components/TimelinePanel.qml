import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "."

Panel {
    id: root

    property var controller
    property var model
    property var eventMarkerModel
    property real durationSeconds: 60
    property bool listsReady: false
    property int rulerLabelTickStride: 10
    readonly property real effectiveDurationSeconds: Math.max(1, Number(durationSeconds) || 1)
    readonly property real playheadSeconds: controller ? Number(controller.playheadSeconds) : 0
    readonly property bool followingLiveEdge: controller ? Boolean(controller.followingLiveEdge) : false
    readonly property var timelineViewport: viewport
    readonly property real visibleStartSeconds: viewport.visibleStartSeconds
    readonly property real visibleDurationSeconds: viewport.boundedVisibleDuration

    title: "时间轴"

    // 跟随实时端：录制且未脱离时，保持当前缩放级别，将可视窗口滚动到让播放头贴住右缘，
    // 使其在录制超过默认标尺长度后仍然始终可见。
    function followLiveEdgeIfNeeded() {
        if (!listsReady || !followingLiveEdge) {
            return
        }
        var windowDuration = viewport.boundedVisibleDuration
        viewport.setWindow(playheadSeconds - windowDuration, windowDuration)
    }

    onPlayheadSecondsChanged: followLiveEdgeIfNeeded()
    onFollowingLiveEdgeChanged: followLiveEdgeIfNeeded()

    Connections {
        target: viewport
        // 总时长随实时端增长后再跟随一次，避免播放头/总长信号到达顺序导致的窗口钳制错误。
        function onBoundedTotalDurationChanged() {
            root.followLiveEdgeIfNeeded()
        }
        // 用户主动平移 / 缩放视口即脱离实时端，进入“录制中回看”，不再被实时端拉回。
        function onManualInteraction() {
            if (root.controller && root.controller.detachFromLiveEdge) {
                root.controller.detachFromLiveEdge()
            }
        }
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
