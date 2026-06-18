import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Panel {
    id: root

    property var model

    title: "记录标签"

    Flickable {
        anchors.fill: parent
        anchors.margins: 8
        clip: true
        contentWidth: width
        contentHeight: tagFlow.implicitHeight

        Flow {
            id: tagFlow

            width: parent.width
            spacing: 4

            Repeater {
                model: root.model

                delegate: Button {
                    id: tagButton

                    text: model.name
                    implicitHeight: 18
                    padding: 0
                    leftPadding: 0
                    rightPadding: 0
                    checkable: true
                    checked: model.isSelected
                    Accessible.name: model.name

                    background: Rectangle {
                        color: model.isSelected ? "#dbeafe" : "transparent"
                        radius: 4
                    }

                    contentItem: TagChip {
                        label: model.name
                        chipColor: model.color
                        maxTextWidth: 72
                    }

                    onClicked: {
                        if (root.model && root.model.select) {
                            root.model.select(index)
                        }
                    }
                }
            }
        }
    }
}
