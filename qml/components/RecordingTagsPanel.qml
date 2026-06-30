import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "."

Panel {
    id: root

    property var model
    property var controller

    title: "记录标签"

    Flickable {
        anchors.fill: parent
        anchors.margins: 8
        clip: true
        contentWidth: width
        contentHeight: tagFlow.implicitHeight
        // 未录制且非历史时整组置灰不可编辑（标签跟随选中的数据源）。
        enabled: !!root.controller && (root.controller.recording || root.controller.historyMode)
        opacity: enabled ? 1.0 : 0.45

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
                        color: "transparent"
                    }

                    contentItem: TagChip {
                        label: model.name
                        chipColor: model.color
                        maxTextWidth: 72
                    }

                    onClicked: {
                        if (root.controller && root.controller.toggleTag) {
                            root.controller.toggleTag(index)
                        }
                    }
                }
            }
        }
    }
}
