import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root

    property var controller
    readonly property bool isRecording: !!controller && controller.recording
    readonly property string configPath: controller && controller.configPath ? controller.configPath : "未加载配置文件"
    readonly property string statusText: controller && controller.statusText ? controller.statusText : "就绪"

    implicitHeight: 56
    color: "#ffffff"
    border.color: "#d5dce8"
    border.width: 1

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 12

        Label {
            text: "DataRecorder"
            color: "#111827"
            font.pixelSize: 18
            font.bold: true
        }

        Rectangle {
            Layout.preferredWidth: rosBadgeLabel.implicitWidth + 18
            Layout.preferredHeight: 24
            radius: 4
            color: "#e0f2fe"
            border.color: "#7dd3fc"
            border.width: 1

            Label {
                id: rosBadgeLabel
                anchors.centerIn: parent
                text: "ROS 2 HUMBLE"
                color: "#075985"
                font.pixelSize: 11
                font.bold: true
            }
        }

        Label {
            Layout.fillWidth: true
            text: root.configPath
            color: "#475569"
            font.pixelSize: 12
            elide: Text.ElideMiddle
        }

        Label {
            text: root.statusText
            color: root.isRecording ? "#dc2626" : "#166534"
            font.pixelSize: 12
            font.bold: true
            elide: Text.ElideRight
        }

        Button {
            objectName: "recordButton"
            text: root.isRecording ? "停止" : "录制"
            enabled: !!root.controller
            onClicked: root.controller.toggleRecording()
        }
    }
}
