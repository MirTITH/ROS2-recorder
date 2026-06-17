import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Panel {
    id: root

    property var model

    title: "采集记录"

    ListView {
        id: sessionList

        anchors.fill: parent
        anchors.margins: 10
        clip: true
        spacing: 0
        model: root.model

        delegate: Rectangle {
            width: ListView.view.width
            height: 46
            color: "#ffffff"

            ToolTip.visible: hoverHandler.hovered
            ToolTip.text: model.folderName + "\n时长: " + model.fullDuration + "\n磁盘: " + model.sizeText

            HoverHandler {
                id: hoverHandler
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
                    font.bold: true
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
