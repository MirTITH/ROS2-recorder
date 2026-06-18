import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root

    property string eventName: ""
    property string shortcut: ""
    property string color: "#2563eb"
    property int count: 0
    property string actionText: ""

    signal actionRequested()

    height: 32
    color: "#f6f8fb"

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 6
        spacing: 6

        Rectangle {
            Layout.preferredWidth: 8
            Layout.preferredHeight: 16
            color: root.color
        }

        Label {
            Layout.fillWidth: true
            text: root.eventName + "（共 " + root.count + " 个）"
            color: "#111827"
            font.pixelSize: 11
            font.bold: true
            elide: Text.ElideRight
        }

        Button {
            objectName: "eventMarkerActionButton_" + root.shortcut
            Layout.preferredWidth: Math.max(78, actionLabel.implicitWidth + 18)
            Layout.preferredHeight: 22
            padding: 0
            onClicked: root.actionRequested()

            contentItem: Label {
                id: actionLabel
                text: root.actionText
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: "#111827"
                font.pixelSize: 10
                font.bold: true
                elide: Text.ElideRight
            }

            background: Rectangle {
                color: parent.hovered ? "#e2e8f0" : "#ffffff"
                border.color: "#cbd5e1"
                border.width: 1
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
