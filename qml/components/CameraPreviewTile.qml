import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root

    property string topicName: ""
    property string resolutionText: "1280x720"
    property color seriesColor: "#2563eb"
    property bool dragActive: false

    color: dragActive ? "#172554" : "#0f172a"
    radius: 3
    clip: true
    border.color: dragActive ? seriesColor : "#334155"
    border.width: 1
    scale: dragActive ? 0.98 : 1.0

    Behavior on scale {
        NumberAnimation { duration: 80 }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 20
            color: "#111827"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                spacing: 6

                Label {
                    Layout.fillWidth: true
                    text: root.topicName
                    color: "#e5e7eb"
                    font.pixelSize: 10
                    elide: Text.ElideMiddle
                }

                Label {
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: Math.min(70, implicitWidth)
                    Layout.maximumWidth: Math.min(70, Math.max(0, root.width * 0.32))
                    text: root.resolutionText
                    color: "#cbd5e1"
                    font.pixelSize: 10
                    elide: Text.ElideRight
                    visible: root.width >= 150 && root.resolutionText.length > 0
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Canvas {
                id: previewCanvas

                anchors.fill: parent
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    ctx.fillStyle = "#111827"
                    ctx.fillRect(0, 0, width, height)
                    ctx.strokeStyle = "#475569"
                    ctx.lineWidth = 1
                    for (var x = 0; x < width; x += Math.max(24, width / 8)) {
                        ctx.beginPath()
                        ctx.moveTo(x, 0)
                        ctx.lineTo(x, height)
                        ctx.stroke()
                    }
                    for (var y = 0; y < height; y += Math.max(18, height / 6)) {
                        ctx.beginPath()
                        ctx.moveTo(0, y)
                        ctx.lineTo(width, y)
                        ctx.stroke()
                    }
                    ctx.strokeStyle = root.seriesColor
                    ctx.lineWidth = 3
                    ctx.strokeRect(width * 0.18, height * 0.18, width * 0.64, height * 0.64)
                }

                Connections {
                    target: root

                    function onSeriesColorChanged() {
                        previewCanvas.requestPaint()
                    }
                }
            }
        }
    }
}
