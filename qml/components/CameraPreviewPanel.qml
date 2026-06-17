import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Panel {
    id: root

    property string topicName: "相机预览"
    property string backendName: ""
    property string frequencyText: ""
    property color seriesColor: "#2563eb"
    property bool visibleState: true

    title: topicName
    active: visibleState

    Rectangle {
        id: previewFrame

        anchors.fill: parent
        anchors.margins: 10
        radius: 6
        color: "#111827"
        border.color: root.visibleState ? root.seriesColor : "#475569"
        border.width: 1
        clip: true
        opacity: root.visibleState ? 1.0 : 0.55

        Canvas {
            id: testPattern

            anchors.fill: parent

            onPaint: {
                var ctx = getContext("2d")
                ctx.resetTransform()
                ctx.clearRect(0, 0, width, height)

                var w = width
                var h = height
                var bars = ["#1f2937", "#2563eb", "#16a34a", "#facc15", "#dc2626", "#9333ea"]
                var barWidth = Math.max(1, w / bars.length)
                for (var i = 0; i < bars.length; ++i) {
                    ctx.fillStyle = bars[i]
                    ctx.fillRect(i * barWidth, 0, barWidth + 1, h)
                }

                ctx.fillStyle = "rgba(15, 23, 42, 0.72)"
                ctx.fillRect(0, h * 0.58, w, h * 0.42)

                ctx.strokeStyle = "rgba(255, 255, 255, 0.36)"
                ctx.lineWidth = 1
                for (var gx = 0; gx <= w; gx += Math.max(24, w / 8)) {
                    ctx.beginPath()
                    ctx.moveTo(gx, 0)
                    ctx.lineTo(gx, h)
                    ctx.stroke()
                }
                for (var gy = 0; gy <= h; gy += Math.max(18, h / 6)) {
                    ctx.beginPath()
                    ctx.moveTo(0, gy)
                    ctx.lineTo(w, gy)
                    ctx.stroke()
                }

                ctx.strokeStyle = root.seriesColor
                ctx.lineWidth = 3
                ctx.beginPath()
                ctx.arc(w * 0.5, h * 0.45, Math.min(w, h) * 0.24, 0, Math.PI * 2)
                ctx.stroke()

                ctx.strokeStyle = "rgba(255, 255, 255, 0.75)"
                ctx.lineWidth = 2
                ctx.beginPath()
                ctx.moveTo(w * 0.5, 0)
                ctx.lineTo(w * 0.5, h)
                ctx.moveTo(0, h * 0.45)
                ctx.lineTo(w, h * 0.45)
                ctx.stroke()
            }

            Connections {
                target: root
                function onSeriesColorChanged() {
                    testPattern.requestPaint()
                }
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 34
            color: "#0f172acc"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8

                Label {
                    Layout.fillWidth: true
                    text: root.backendName
                    color: "#e2e8f0"
                    font.pixelSize: 11
                    font.bold: true
                    elide: Text.ElideRight
                }

                Label {
                    text: root.frequencyText
                    color: "#bfdbfe"
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }
        }
    }
}
