import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Panel {
    id: root

    property var controller
    property var model
    property real durationSeconds: 60
    readonly property real playheadSeconds: controller ? Number(controller.playheadSeconds) : 0
    readonly property real effectiveDurationSeconds: Math.max(1, Number(durationSeconds) || 1)

    title: "时间轴"

    function seekFromX(xPosition) {
        var seconds = Math.max(0, Math.min(effectiveDurationSeconds, (xPosition / Math.max(1, ruler.width)) * effectiveDurationSeconds))
        if (controller && controller.setPlayheadSeconds) {
            controller.setPlayheadSeconds(seconds)
        } else if (controller) {
            controller.playheadSeconds = seconds
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        Rectangle {
            id: ruler

            Layout.fillWidth: true
            Layout.preferredHeight: 30
            radius: 4
            color: "#ffffff"
            border.color: "#dbe3ef"
            border.width: 1

            MouseArea {
                anchors.fill: parent
                onClicked: root.seekFromX(mouse.x)
            }

            Repeater {
                model: Math.floor(root.effectiveDurationSeconds / 5) + 1

                delegate: Item {
                    required property int index

                    x: ((index * 5) / root.effectiveDurationSeconds) * ruler.width
                    width: 1
                    height: ruler.height

                    Rectangle {
                        anchors.top: parent.top
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 1
                        height: 10
                        color: "#94a3b8"
                    }

                    Label {
                        anchors.top: parent.top
                        anchors.topMargin: 12
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: (index * 5) + "s"
                        color: "#64748b"
                        font.pixelSize: 10
                    }
                }
            }

            Rectangle {
                id: rulerPlayhead

                x: Math.max(0, Math.min(parent.width - width, (root.playheadSeconds / root.effectiveDurationSeconds) * parent.width - width / 2))
                width: 2
                height: parent.height
                color: "#dc2626"
                z: 3
            }
        }

        Item {
            id: trackArea

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ListView {
                id: trackList

                anchors.fill: parent
                clip: true
                spacing: 8
                model: root.model

                delegate: TopicTrack {
                    width: ListView.view.width
                    topicName: model.topicName
                    backendName: model.backendName
                    frequencyText: model.frequencyText
                    seriesColor: model.seriesColor
                    series: model.series
                    visibleState: model.isVisible
                }

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }
            }

            TapHandler {
                acceptedButtons: Qt.LeftButton
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: function(eventPoint) {
                    root.seekFromX(eventPoint.position.x)
                }
            }

            Rectangle {
                id: trackPlayhead

                x: Math.max(0, Math.min(parent.width - width, (root.playheadSeconds / root.effectiveDurationSeconds) * parent.width - width / 2))
                width: 2
                height: parent.height
                color: "#dc2626"
                opacity: 0.82
                z: 4
            }
        }
    }
}
