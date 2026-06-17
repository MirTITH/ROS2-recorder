import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root

    property alias title: titleLabel.text
    default property alias contentData: body.data
    property bool active: false

    color: active ? "#eef5ff" : "#f8fafc"
    border.color: active ? "#2563eb" : "#d5dce8"
    border.width: 1
    radius: 0

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 20
            color: root.active ? "#dbeafe" : "#eef2f7"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8

                Label {
                    id: titleLabel
                    Layout.fillWidth: true
                    color: "#162033"
                    font.pixelSize: 11
                    font.bold: true
                    elide: Text.ElideRight
                }
            }
        }

        Item {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
        }
    }
}
