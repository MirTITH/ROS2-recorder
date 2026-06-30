import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "."

Rectangle {
    id: root

    property var controller
    readonly property bool isRecording: !!controller && controller.recording
    readonly property string statusText: controller && controller.statusText ? controller.statusText : "就绪"

    implicitHeight: 32
    color: Theme.surface
    border.color: Theme.border
    border.width: 1

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 12
        spacing: 10

        Label {
            text: root.statusText
            color: root.isRecording ? Theme.danger : Theme.success
            font.pixelSize: 11
            font.bold: true
            elide: Text.ElideRight
        }

        Item {
            Layout.fillWidth: true
        }

        Label {
            text: controller ? controller.diskSpaceText : "磁盘 -- / --"
            color: (controller && controller.diskSpaceLow) ? Theme.danger : Theme.textMuted
            font.pixelSize: 11
        }
    }
}
