import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Panel {
    id: root

    property var model

    title: "记录标签"

    Flickable {
        anchors.fill: parent
        anchors.margins: 10
        clip: true
        contentWidth: width
        contentHeight: tagFlow.implicitHeight

        Flow {
            id: tagFlow

            width: parent.width
            spacing: 8

            Repeater {
                model: root.model

                delegate: Button {
                    id: tagButton

                    text: model.name
                    implicitHeight: 30
                    leftPadding: 12
                    rightPadding: 12
                    font.pixelSize: 12
                    font.bold: model.isSelected

                    onClicked: {
                        if (root.model && root.model.select) {
                            root.model.select(index)
                        }
                    }

                    contentItem: Label {
                        text: tagButton.text
                        color: model.isSelected ? "#ffffff" : model.color
                        font: tagButton.font
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    background: Rectangle {
                        radius: 15
                        color: model.isSelected ? model.color : "#ffffff"
                        border.color: model.color
                        border.width: 1
                    }
                }
            }
        }
    }
}
