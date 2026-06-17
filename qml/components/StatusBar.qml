import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root

    property var controller
    readonly property string outputDirectory: controller && controller.outputDirectory ? controller.outputDirectory : "未设置"
    readonly property string displayTime: controller ? Number(controller.playheadSeconds).toFixed(1) + "s" : "0.0s"

    implicitHeight: 28
    color: "#ffffff"
    border.color: "#d5dce8"
    border.width: 1

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 18

        Label {
            Layout.fillWidth: true
            text: "保存目录: " + root.outputDirectory
            color: "#475569"
            font.pixelSize: 11
            elide: Text.ElideMiddle
        }

        Label {
            text: "时间: " + root.displayTime
            color: "#334155"
            font.pixelSize: 11
        }

        Label {
            text: "缩放 1.0x"
            color: "#334155"
            font.pixelSize: 11
        }

        Label {
            text: "磁盘 --"
            color: "#64748b"
            font.pixelSize: 11
        }
    }
}
