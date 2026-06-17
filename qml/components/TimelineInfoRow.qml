import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root

    property string topicName: ""
    property string frequencyText: ""
    property string backendName: ""
    property string trackKind: "empty"
    property bool isVisible: true
    property bool isCamera: false
    signal toggleVisibleRequested()

    height: 48
    color: "#f8fafc"
    border.color: "#dbe3ef"
    border.width: 0

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 6
        spacing: 6

        Button {
            visible: root.isCamera
            Layout.preferredWidth: 26
            Layout.preferredHeight: 24
            text: root.isVisible ? "◉" : "○"
            font.pixelSize: 13
            Accessible.name: root.isVisible ? "隐藏相机预览" : "显示相机预览"
            ToolTip.visible: hovered
            ToolTip.text: root.isVisible ? "隐藏相机预览" : "显示相机预览"
            onClicked: root.toggleVisibleRequested()
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1

            Label {
                Layout.fillWidth: true
                text: root.topicName
                color: "#111827"
                font.pixelSize: 11
                font.bold: true
                elide: Text.ElideMiddle
            }

            Label {
                Layout.fillWidth: true
                text: root.frequencyText + " · " + root.backendName + " · " + root.trackKind
                color: "#64748b"
                font.pixelSize: 10
                elide: Text.ElideRight
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: "#e2e8f0"
    }
}
