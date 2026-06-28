import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "."

Rectangle {
    id: root

    property string topicName: ""
    property string frequencyText: ""
    property string backendName: ""
    property bool isVisible: true
    property bool isCamera: false
    property bool isExpanded: false
    property bool isPlottable: false
    property var seriesList: []
    signal toggleVisibleRequested()
    signal toggleExpandRequested()
    signal seriesVisibilityRequested(string seriesKey, bool visible)

    height: isExpanded ? 120 : 32
    color: Theme.surfaceAlt
    border.color: Theme.border
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
            color: Theme.textPrimary
            font.pixelSize: 11
            font.bold: true
            elide: Text.ElideMiddle
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Button {
                id: expandToggleButton

                objectName: "expandToggleButton_" + root.topicName
                enabled: root.isPlottable
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
                padding: 0
                Accessible.name: root.isExpanded ? "折叠曲线" : "展开曲线"
                onClicked: root.toggleExpandRequested()

                background: Rectangle {
                    color: expandToggleButton.hovered ? Theme.gridLine : "transparent"
                    border.width: 0
                }

                contentItem: Label {
                    text: root.isExpanded ? "∨" : ">"
                    color: root.isPlottable ? Theme.textPrimary : Theme.textMuted
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Label {
                Layout.fillWidth: true
                text: root.frequencyText + " · " + root.backendName
                color: Theme.textMuted
                font.pixelSize: 10
                elide: Text.ElideRight
            }

            Button {
                id: cameraVisibilityButton

                objectName: "cameraVisibilityButton_" + root.topicName
                visible: root.isCamera
                Layout.preferredWidth: 24
                Layout.preferredHeight: 20
                padding: 0
                Accessible.name: root.isVisible ? "隐藏相机预览" : "显示相机预览"
                ToolTip.visible: hovered
                ToolTip.text: root.isVisible ? "隐藏相机预览" : "显示相机预览"
                onClicked: root.toggleVisibleRequested()

                background: Rectangle {
                    color: cameraVisibilityButton.hovered ? Theme.gridLine : "transparent"
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

        Flow {
            Layout.fillWidth: true
            visible: root.isExpanded
            spacing: 6

            Repeater {
                model: root.isExpanded ? (root.seriesList || []) : []

                delegate: Rectangle {
                    required property var modelData

                    objectName: "seriesChip_" + root.topicName + "_" + (modelData.key || "")
                    height: 16
                    radius: 8
                    width: chipRow.implicitWidth + 12
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.gridLine
                    opacity: (modelData.visible === false) ? 0.45 : 1.0

                    Row {
                        id: chipRow
                        anchors.centerIn: parent
                        spacing: 4

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 8
                            height: 8
                            radius: 4
                            color: modelData.color || "#2563eb"
                        }

                        Label {
                            text: modelData.label || modelData.key || ""
                            color: Theme.textPrimary
                            font.pixelSize: 9
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.seriesVisibilityRequested(
                            modelData.key || "", !(modelData.visible !== false))
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.gridLine
    }
}
