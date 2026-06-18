import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root

    property var controller
    readonly property bool isRecording: !!controller && controller.recording
    readonly property string statusText: controller && controller.statusText ? controller.statusText : "就绪"

    implicitHeight: 32
    color: "#ffffff"
    border.color: "#d5dce8"
    border.width: 1

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 12
        spacing: 10

        Button {
            objectName: "recordButton"
            Layout.preferredWidth: 64
            Layout.preferredHeight: 24
            text: root.isRecording ? "停止" : "录制"
            enabled: !!root.controller
            onClicked: root.controller.toggleRecording()
        }

        Label {
            text: root.statusText
            color: root.isRecording ? "#dc2626" : "#166534"
            font.pixelSize: 11
            font.bold: true
            elide: Text.ElideRight
        }

        Item {
            Layout.fillWidth: true
        }

        Label {
            text: "磁盘 --"
            color: "#64748b"
            font.pixelSize: 11
        }
    }
}
