import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "."

Rectangle {
    id: root

    property string topicName: ""
    property string resolutionText: "1280x720"
    property color seriesColor: "#2563eb"
    property bool dragActive: false
    property string topicKey: ""
    property int frameSeq: 0
    property int liveOverlayFrameSeq: 0
    property real liveOverlayOpacity: 0

    color: Theme.cameraTileBg
    clip: true
    border.color: dragActive ? seriesColor : Theme.textSecondary
    border.width: 1
    scale: dragActive ? 0.98 : 1.0

    Behavior on scale {
        NumberAnimation { duration: 80 }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 20
            color: Theme.textPrimary

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                spacing: 6

                Label {
                    Layout.fillWidth: true
                    text: root.topicName
                    color: Theme.gridLine
                    font.pixelSize: 10
                    elide: Text.ElideMiddle
                }

                Label {
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: Math.min(70, implicitWidth)
                    Layout.maximumWidth: Math.min(70, Math.max(0, root.width * 0.32))
                    text: root.resolutionText
                    color: Theme.border
                    font.pixelSize: 10
                    elide: Text.ElideRight
                    visible: root.width >= 150 && root.resolutionText.length > 0
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Image {
                id: previewImage

                anchors.fill: parent
                fillMode: Image.PreserveAspectFit
                cache: false
                asynchronous: false
                source: root.topicKey.length > 0
                    ? "image://camera/" + root.topicKey + "?seq=" + root.frameSeq
                    : ""
                // seq 变化 → source 变化 → 重新拉帧
            }

            Image {
                anchors.centerIn: previewImage
                width: previewImage.paintedWidth
                height: previewImage.paintedHeight
                fillMode: Image.PreserveAspectFit
                cache: false
                opacity: root.liveOverlayOpacity
                source: opacity > 0 && root.frameSeq > 0 && root.topicKey.length > 0
                    ? "image://camera-live/" + root.topicKey + "?seq=" + root.liveOverlayFrameSeq
                    : ""
            }
        }
    }
}
