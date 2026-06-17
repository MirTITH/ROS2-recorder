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
        spacing: 8
        model: root.model

        delegate: Rectangle {
            width: ListView.view.width
            height: 58
            radius: 6
            color: "#ffffff"
            border.color: "#dbe3ef"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 10

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Label {
                        Layout.fillWidth: true
                        text: model.name
                        color: "#162033"
                        font.pixelSize: 13
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "时长 " + model.duration
                        color: "#64748b"
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }

                Label {
                    text: model.size
                    color: "#334155"
                    font.pixelSize: 12
                    font.bold: true
                    elide: Text.ElideRight
                }
            }
        }

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }
    }
}
