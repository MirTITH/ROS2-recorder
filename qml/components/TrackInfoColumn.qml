import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "."

ColumnLayout {
    id: root

    property var controller
    property var model
    property var eventMarkerModel
    property real playheadSeconds: 0
    property alias contentY: infoList.contentY

    signal contentScrolled()

    spacing: 0

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

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 30
        color: Theme.surfaceAlt
        border.color: Theme.border
        border.width: 1

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 6

            Label {
                Layout.fillWidth: true
                text: root.timeString(root.playheadSeconds)
                color: Theme.textPrimary
                font.pixelSize: 12
                font.bold: true
                elide: Text.ElideRight
            }

            Button {
                objectName: "primaryActionButton"
                Layout.preferredWidth: 72
                Layout.preferredHeight: 24
                font.pixelSize: 10
                visible: !!root.controller
                enabled: root.controller
                    ? (root.controller.historyMode ? true : root.controller.canRecord)
                    : false
                text: {
                    if (!root.controller) return ""
                    if (root.controller.historyMode)
                        return root.controller.playing ? "暂停" : "播放"
                    return root.controller.recording ? "停止" : "录制"
                }
                onClicked: {
                    if (!root.controller) return
                    if (root.controller.historyMode)
                        root.controller.togglePlayback()
                    else
                        root.controller.toggleRecording()
                }
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

    Flickable {
        id: infoList

        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: infoColumn.implicitHeight
        onContentYChanged: root.contentScrolled()

        Column {
            id: infoColumn

            width: infoList.width

            Repeater {
                id: eventInfoRepeater
                model: root.eventMarkerModel

                EventTrackInfoRow {
                    width: infoColumn.width
                    eventName: model.name
                    shortcut: model.shortcut
                    kind: model.kind
                    markerColor: model.color
                    count: model.count
                    actionText: model.actionText
                    onActionRequested: {
                        if (!(root.controller && root.controller.historyMode)) {
                            root.eventMarkerModel.triggerRowAction(index, root.playheadSeconds)
                        }
                    }
                }
            }

            Rectangle {
                width: infoColumn.width
                height: eventInfoRepeater.count > 0 ? 1 : 0
                color: Theme.border
            }

            Repeater {
                model: root.model

                TimelineInfoRow {
                    width: infoColumn.width
                    topicName: model.topicName
                    frequencyText: model.frequencyText
                    backendName: model.backendName
                    isVisible: model.isVisible
                    isCamera: model.isCamera
                    isExpanded: model.isExpanded
                    isPlottable: model.isPlottable
                    seriesList: model.seriesList
                    onToggleVisibleRequested: {
                        if (root.controller && root.controller.toggleTopicVisible) {
                            root.controller.toggleTopicVisible(index)
                        }
                    }
                    onToggleExpandRequested: {
                        if (root.controller && root.controller.setTopicExpanded) {
                            root.controller.setTopicExpanded(model.topicName, !model.isExpanded)
                        }
                    }
                    onSeriesVisibilityRequested: function(seriesKey, visible) {
                        if (root.controller && root.controller.setSeriesVisible) {
                            root.controller.setSeriesVisible(model.topicName, seriesKey, visible)
                        }
                    }
                }
            }
        }

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }
    }
}
