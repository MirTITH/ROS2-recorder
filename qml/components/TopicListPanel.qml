import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Panel {
    id: root

    property var model

    title: "话题列表"

    ListView {
        id: topicList

        anchors.fill: parent
        anchors.margins: 10
        clip: true
        spacing: 8
        model: root.model

        delegate: Rectangle {
            width: ListView.view.width
            height: 64
            radius: 6
            color: model.isVisible ? "#ffffff" : "#f1f5f9"
            border.color: model.isVisible ? "#dbe3ef" : "#cbd5e1"
            border.width: 1
            opacity: model.isVisible ? 1.0 : 0.72

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 10

                Rectangle {
                    Layout.preferredWidth: 12
                    Layout.preferredHeight: 42
                    radius: 3
                    color: model.seriesColor
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Label {
                        Layout.fillWidth: true
                        text: model.topicName
                        color: "#162033"
                        font.pixelSize: 12
                        font.bold: true
                        elide: Text.ElideMiddle
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            text: model.backendName
                            color: "#475569"
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }

                        Label {
                            Layout.fillWidth: true
                            text: model.frequencyText
                            color: "#64748b"
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }
                }

                Button {
                    Layout.preferredWidth: 54
                    Layout.preferredHeight: 30
                    text: model.isVisible ? "显示" : "隐藏"
                    font.pixelSize: 11

                    onClicked: {
                        if (root.model && root.model.toggleVisible) {
                            root.model.toggleVisible(index)
                        }
                    }
                }
            }
        }

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }
    }
}
