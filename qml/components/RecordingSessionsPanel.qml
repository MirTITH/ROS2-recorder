import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "."

Panel {
    id: root

    property var model
    property var controller

    title: "数据"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            id: onlineDataSourceRow

            objectName: "onlineDataSourceRow"
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            height: 32
            color: selected ? Theme.rowSelected : Theme.surface

            readonly property bool selected: root.controller && !root.controller.historyMode

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 3
                color: Theme.accent
                visible: onlineDataSourceRow.selected
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 7

                Rectangle {
                    Layout.preferredWidth: 7
                    Layout.preferredHeight: 7
                    radius: width / 2
                    color: Theme.online
                }

                Label {
                    Layout.fillWidth: true
                    text: "在线数据"
                    color: Theme.textPrimary
                    font.pixelSize: 12
                    font.bold: onlineDataSourceRow.selected
                    elide: Text.ElideRight
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if (root.controller) {
                        root.controller.selectOnlineData()
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

        ListView {
            id: sessionList

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 0
            model: root.model

            delegate: Rectangle {
                objectName: "historyDataSourceRow_" + index
                width: ListView.view.width
                height: 46
                color: selected ? Theme.rowSelected : Theme.surface
                enabled: !(root.controller && root.controller.recording)
                opacity: enabled ? 1.0 : 0.45

                readonly property bool selected: root.controller && root.controller.historyMode && root.controller.selectedSessionRow === index

                ToolTip.visible: hoverHandler.hovered
                ToolTip.text: model.folderName + "\n时长: " + model.fullDuration + "\n磁盘: " + model.sizeText

                HoverHandler {
                    id: hoverHandler
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 3
                    color: Theme.accent
                    visible: selected
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 2

                    Label {
                        Layout.fillWidth: true
                        text: model.folderName
                        color: Theme.textPrimary
                        font.pixelSize: 12
                        font.bold: selected
                        elide: Text.ElideMiddle
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: model.shortDuration
                            color: Theme.textMuted
                            font.pixelSize: 11
                        }
                        TagChip {
                            label: model.tagName
                            chipColor: model.tagColor
                            maxTextWidth: 54
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (root.controller && !root.controller.recording) {
                            root.controller.selectHistorySession(index)
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

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }
        }
    }
}
