import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "."

Rectangle {
    id: root

    property string topicName: ""
    property string frequencyText: ""
    property string backendName: ""
    property bool isVisible: true
    property bool isCamera: false
    property bool isExpanded: false
    property bool isPlottable: false
    property var seriesList: []
    readonly property int collapsedHeight: 32
    readonly property int expandedHeight: 120
    signal toggleVisibleRequested()
    signal toggleExpandRequested()
    signal seriesVisibilityRequested(string seriesKey, bool visible)

    height: root.isExpanded ? root.expandedHeight : root.collapsedHeight
    clip: true
    color: Theme.surfaceAlt
    border.color: Theme.border
    border.width: 0

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 6
        anchors.topMargin: 2
        anchors.bottomMargin: 2
        spacing: 1

        Label {
            Layout.fillWidth: true
            Layout.preferredHeight: 12
            text: root.topicName
            color: Theme.textPrimary
            font.pixelSize: 10
            font.bold: true
            elide: Text.ElideMiddle
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 14
            spacing: 6

            Button {
                id: expandToggleButton

                objectName: "expandToggleButton_" + root.topicName
                enabled: root.isPlottable
                Layout.preferredWidth: 16
                Layout.preferredHeight: 14
                padding: 0
                Accessible.name: root.isExpanded ? "折叠曲线" : "展开曲线"
                ToolTip.visible: hovered && enabled
                ToolTip.text: root.isExpanded ? "折叠曲线" : "展开曲线"
                onClicked: root.toggleExpandRequested()

                background: Rectangle {
                    color: expandToggleButton.hovered ? Theme.gridLine : "transparent"
                    border.width: 0
                }

                contentItem: Image {
                    anchors.centerIn: parent
                    width: 12
                    height: 12
                    source: root.isExpanded ?
                        "../assets/icons/chevron-down.svg" :
                        "../assets/icons/chevron-right.svg"
                    opacity: root.isPlottable ? 1.0 : 0.4
                    fillMode: Image.PreserveAspectFit
                }
            }

            Label {
                Layout.fillWidth: true
                text: root.frequencyText + " · " + root.backendName
                color: Theme.textMuted
                font.pixelSize: 10
                elide: Text.ElideRight
            }

            Button {
                id: cameraVisibilityButton

                objectName: "cameraVisibilityButton_" + root.topicName
                visible: root.isCamera
                Layout.preferredWidth: 24
                Layout.preferredHeight: 20
                padding: 0
                Accessible.name: root.isVisible ? "隐藏相机预览" : "显示相机预览"
                ToolTip.visible: hovered
                ToolTip.text: root.isVisible ? "隐藏相机预览" : "显示相机预览"
                onClicked: root.toggleVisibleRequested()

                background: Rectangle {
                    color: cameraVisibilityButton.hovered ? Theme.gridLine : "transparent"
                    border.width: 0
                }

                contentItem: Image {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    source: root.isVisible ? "../assets/icons/eye.svg" : "../assets/icons/eye-off.svg"
                    fillMode: Image.PreserveAspectFit
                }
            }
        }

        Item {
            id: legendViewport

            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.isExpanded
            clip: true

            Flickable {
                id: legendFlickable

                anchors.fill: parent
                clip: true
                interactive: false
                boundsBehavior: Flickable.StopAtBounds
                contentWidth: Math.max(width, legendFlow.childrenRect.x + legendFlow.childrenRect.width)
                contentHeight: legendFlow.implicitHeight

                Flow {
                    id: legendFlow

                    width: legendFlickable.width
                    spacing: 6

                    Repeater {
                        model: root.isExpanded ? (root.seriesList || []) : []

                        delegate: Rectangle {
                            required property var modelData

                            objectName: "seriesChip_" + root.topicName + "_" + (modelData.key || "")
                            property string seriesKey: modelData.key || ""
                            property bool seriesVisible: modelData.visible !== false
                            height: 16
                            radius: 8
                            width: chipRow.implicitWidth + 12
                            color: Theme.surface
                            border.width: 1
                            border.color: Theme.gridLine
                            opacity: (modelData.visible === false) ? 0.45 : 1.0

                            Row {
                                id: chipRow
                                anchors.centerIn: parent
                                spacing: 4

                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 8
                                    height: 8
                                    radius: 4
                                    color: modelData.color || "#2563eb"
                                }

                                Label {
                                    text: modelData.label || modelData.key || ""
                                    color: Theme.textPrimary
                                    font.pixelSize: 9
                                }
                            }

                        }
                    }
                }

                ScrollBar.vertical: ScrollBar {
                    id: legendVerticalScrollBar
                    policy: ScrollBar.AsNeeded
                }

                ScrollBar.horizontal: ScrollBar {
                    id: legendHorizontalScrollBar
                    policy: ScrollBar.AsNeeded
                }
            }

            MouseArea {
                id: legendHitArea

                anchors.fill: parent
                anchors.rightMargin: legendVerticalScrollBar.visible ? legendVerticalScrollBar.width : 0
                acceptedButtons: Qt.LeftButton
                cursorShape: Qt.PointingHandCursor
                hoverEnabled: true
                preventStealing: true

                function boundedScroll(value, maximum) {
                    return Math.max(0, Math.min(Math.max(0, maximum), value))
                }

                function chipAt(mouse) {
                    for (var index = 0; index < legendFlow.children.length; ++index) {
                        var chip = legendFlow.children[index]
                        if (!chip || chip.seriesKey === undefined) {
                            continue
                        }
                        var point = legendHitArea.mapToItem(chip, mouse.x, mouse.y)
                        if (point.x >= 0 && point.x <= chip.width &&
                            point.y >= 0 && point.y <= chip.height) {
                            return chip
                        }
                    }
                    return null
                }

                onClicked: function(mouse) {
                    var chip = chipAt(mouse)
                    if (chip && chip.seriesKey !== "") {
                        root.seriesVisibilityRequested(chip.seriesKey, !chip.seriesVisible)
                    }
                }

                onWheel: function(wheel) {
                    var delta = -wheel.angleDelta.y / 2
                    if (wheel.modifiers & Qt.ShiftModifier) {
                        legendFlickable.contentX = boundedScroll(
                            legendFlickable.contentX + delta,
                            legendFlickable.contentWidth - legendFlickable.width)
                    } else {
                        legendFlickable.contentY = boundedScroll(
                            legendFlickable.contentY + delta,
                            legendFlickable.contentHeight - legendFlickable.height)
                    }
                    wheel.accepted = true
                }
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.gridLine
    }
}
