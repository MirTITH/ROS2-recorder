import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: root

    property string label: ""
    property color chipColor: "#94a3b8"
    property int maxTextWidth: 72
    readonly property real luminance: 0.2126 * chipColor.r + 0.7152 * chipColor.g + 0.0722 * chipColor.b
    readonly property bool dotOnly: labelText.implicitWidth > maxTextWidth
    readonly property bool textOverflows: labelText.truncated

    implicitWidth: dotOnly ? 10 : Math.min(maxTextWidth, labelText.implicitWidth) + 12
    implicitHeight: 18
    radius: dotOnly ? width / 2 : 9
    color: chipColor

    Label {
        id: labelText
        anchors.centerIn: parent
        visible: !root.dotOnly
        text: root.label
        color: root.luminance > 0.56 ? "#111827" : "#ffffff"
        font.pixelSize: 10
        elide: Text.ElideRight
        width: Math.min(root.maxTextWidth, implicitWidth)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    ToolTip.delay: 350
    ToolTip.visible: hoverHandler.hovered && root.label.length > 0 && (root.dotOnly || root.textOverflows)
    ToolTip.text: root.label

    HoverHandler {
        id: hoverHandler
    }
}
