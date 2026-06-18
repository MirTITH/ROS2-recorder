import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Panel {
    id: root

    property var model
    property var controller

    title: "数据"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 0

        Rectangle {
            id: onlineDataSourceRow

            objectName: "onlineDataSourceRow"
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            height: 32
            color: selected ? "#e8f1ff" : "#ffffff"

            readonly property bool selected: root.controller && !root.controller.historyMode

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 3
                color: "#2563eb"
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
                    color: "#22c55e"
                }

                Label {
                    Layout.fillWidth: true
                    text: "在线数据"
                    color: "#111827"
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
                color: "#e2e8f0"
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
                width: ListView.view.width
                height: 46
                color: selected ? "#e8f1ff" : "#ffffff"

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
                    color: "#2563eb"
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
                        color: "#111827"
                        font.pixelSize: 12
                        font.bold: selected
                        elide: Text.ElideMiddle
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: model.shortDuration
                            color: "#64748b"
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
                        if (root.controller) {
                            root.controller.selectHistorySession(index)
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

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }
        }
    }
}
