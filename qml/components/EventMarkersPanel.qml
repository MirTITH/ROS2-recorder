import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Panel {
    id: root

    property var model

    title: "事件标记"

    Flickable {
        anchors.fill: parent
        anchors.margins: 6
        clip: true
        contentWidth: markerRow.implicitWidth
        contentHeight: height

        RowLayout {
            id: markerRow

            height: parent.height
            spacing: 8

            Repeater {
                model: root.model

                delegate: Button {
                    id: markerButton

                    objectName: "eventMarkerButton_" + model.shortcut
                    Layout.preferredWidth: Math.max(112, markerText.implicitWidth + (model.kind === "range" ? 52 : 38))
                    Layout.preferredHeight: Math.max(34, Math.min(48, markerRow.height))
                    font.pixelSize: 12
                    font.bold: model.isSelected

                    onClicked: root.model.select(index)

                    contentItem: RowLayout {
                        spacing: 6

                        Rectangle {
                            Layout.preferredWidth: model.kind === "range" ? 22 : 8
                            Layout.preferredHeight: model.kind === "range" ? 6 : 8
                            radius: model.kind === "range" ? 3 : 4
                            color: model.color
                        }

                        Label {
                            id: markerText
                            Layout.fillWidth: true
                            text: model.shortcut + "  " + model.name
                            color: model.isSelected ? "#ffffff" : "#162033"
                            font.pixelSize: 12
                            font.bold: true
                            elide: Text.ElideRight
                        }
                    }

                    background: Rectangle {
                        radius: 6
                        color: model.isSelected ? model.color : "#ffffff"
                        border.color: model.color
                        border.width: 1
                    }
                }
            }
        }
    }
}
