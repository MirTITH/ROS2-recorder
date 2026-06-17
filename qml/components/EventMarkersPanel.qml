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

                    Layout.preferredWidth: Math.max(104, markerText.implicitWidth + 24)
                    Layout.preferredHeight: Math.max(34, Math.min(48, markerRow.height))
                    font.pixelSize: 12
                    font.bold: model.isSelected

                    onClicked: {
                        if (root.model && root.model.select) {
                            root.model.select(index)
                        }
                    }

                    contentItem: ColumnLayout {
                        spacing: 2

                        Label {
                            id: markerText

                            Layout.fillWidth: true
                            text: model.shortcut + "  " + model.name
                            color: model.isSelected ? "#ffffff" : "#162033"
                            font.pixelSize: 12
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                        }

                        Label {
                            Layout.fillWidth: true
                            text: model.kind
                            color: model.isSelected ? "#e2e8f0" : "#64748b"
                            font.pixelSize: 10
                            horizontalAlignment: Text.AlignHCenter
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
