import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root

    property string topicName: ""
    property string frequencyText: ""
    property string backendName: ""
    property bool isVisible: true
    property bool isCamera: false
    signal toggleVisibleRequested()

    height: 48
    color: "#f8fafc"
    border.color: "#dbe3ef"
    border.width: 0

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 6
        anchors.topMargin: 5
        anchors.bottomMargin: 5
        spacing: 2

        Label {
            Layout.fillWidth: true
            text: root.topicName
            color: "#111827"
            font.pixelSize: 11
            font.bold: true
            elide: Text.ElideMiddle
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Label {
                Layout.fillWidth: true
                text: root.frequencyText + " · " + root.backendName
                color: "#64748b"
                font.pixelSize: 10
                elide: Text.ElideRight
            }

            Button {
                id: cameraVisibilityButton

                visible: root.isCamera
                Layout.preferredWidth: 24
                Layout.preferredHeight: 20
                padding: 0
                Accessible.name: root.isVisible ? "隐藏相机预览" : "显示相机预览"
                ToolTip.visible: hovered
                ToolTip.text: root.isVisible ? "隐藏相机预览" : "显示相机预览"
                onClicked: root.toggleVisibleRequested()

                background: Rectangle {
                    color: cameraVisibilityButton.hovered ? "#e2e8f0" : "transparent"
                    border.width: 0
                }

                contentItem: Image {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    source: root.isVisible ? "../assets/icons/eye.svg" : "../assets/icons/eye-off.svg"
                    fillMode: Image.PreserveAspectFit
                }
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
