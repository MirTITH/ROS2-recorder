import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Panel {
    id: root

    property var controller
    property var model
    property real durationSeconds: 60
    property real timeScale: 1.0
    property bool listsReady: false
    readonly property real effectiveDurationSeconds: Math.max(1, Number(durationSeconds) || 1)
    readonly property real playheadSeconds: controller ? Number(controller.playheadSeconds) : 0

    title: "时间轴"

    function boundedTimeScale() {
        return Math.max(0.2, Number(timeScale) || 1)
    }

    function curveDurationSeconds() {
        return effectiveDurationSeconds / boundedTimeScale()
    }

    function playheadX(widthValue) {
        return Math.max(0, Math.min(widthValue, (playheadSeconds / curveDurationSeconds()) * widthValue))
    }

    function seekFromCurveX(xPosition) {
        var seconds = Math.max(0, Math.min(effectiveDurationSeconds, (xPosition / Math.max(1, curveViewport.width)) * curveDurationSeconds()))
        if (controller && controller.setPlayheadSeconds) {
            controller.setPlayheadSeconds(seconds)
        }
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
        handle: SplitHandle { vertical: true }

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
                    trackKind: model.trackKind
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
                    model: Math.floor(root.effectiveDurationSeconds / 5) + 1

                    delegate: Item {
                        required property int index

                        x: ((index * 5) / root.curveDurationSeconds()) * ruler.width
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
                            text: (index * 5) + "s"
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
                        timeScale: root.timeScale
                        xMax: root.effectiveDurationSeconds
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
                        root.timeScale = Math.max(0.25, Math.min(6.0, root.timeScale + (wheel.angleDelta.y > 0 ? 0.15 : -0.15)))
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
        }
    }

    Component.onCompleted: listsReady = true
}
