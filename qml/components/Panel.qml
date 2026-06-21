import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "."

Rectangle {
    id: root

    property alias title: titleLabel.text
    default property alias contentData: body.data
    property bool active: false

    color: Theme.surfaceAlt
    border.color: active ? Theme.accent : Theme.border
    border.width: 1
    radius: 0

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 20
            color: Theme.panelHeader

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8

                Label {
                    id: titleLabel
                    Layout.fillWidth: true
                    color: Theme.titleText
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
