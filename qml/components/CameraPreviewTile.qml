import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "."

Rectangle {
    id: root

    property string topicName: ""
    property string resolutionText: "1280x720"
    property color seriesColor: "#2563eb"
    property bool dragActive: false

    function videoRect(widthValue, heightValue) {
        var match = /^([0-9]+)x([0-9]+)$/.exec(String(resolutionText || ""))
        var aspect = 16 / 9
        if (match) {
            var parsedWidth = Number(match[1])
            var parsedHeight = Number(match[2])
            if (isFinite(parsedWidth) && isFinite(parsedHeight) && parsedHeight > 0) {
                aspect = parsedWidth / parsedHeight
            }
        }
        var targetWidth = widthValue
        var targetHeight = targetWidth / aspect
        if (targetHeight > heightValue) {
            targetHeight = heightValue
            targetWidth = targetHeight * aspect
        }
        return {
            x: (widthValue - targetWidth) / 2,
            y: (heightValue - targetHeight) / 2,
            width: targetWidth,
            height: targetHeight
        }
    }

    color: Theme.cameraTileBg
    clip: true
    border.color: dragActive ? seriesColor : Theme.textSecondary
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
            color: Theme.textPrimary

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                spacing: 6

                Label {
                    Layout.fillWidth: true
                    text: root.topicName
                    color: Theme.gridLine
                    font.pixelSize: 10
                    elide: Text.ElideMiddle
                }

                Label {
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: Math.min(70, implicitWidth)
                    Layout.maximumWidth: Math.min(70, Math.max(0, root.width * 0.32))
                    text: root.resolutionText
                    color: Theme.border
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
                    ctx.fillStyle = Theme.cameraTileBg
                    ctx.fillRect(0, 0, width, height)

                    var rect = root.videoRect(width, height)
                    ctx.save()
                    ctx.beginPath()
                    ctx.rect(rect.x, rect.y, rect.width, rect.height)
                    ctx.clip()

                    ctx.fillStyle = Theme.textPrimary
                    ctx.fillRect(rect.x, rect.y, rect.width, rect.height)
                    ctx.strokeStyle = Theme.textSecondary
                    ctx.lineWidth = 1
                    for (var x = rect.x; x < rect.x + rect.width; x += Math.max(24, rect.width / 8)) {
                        ctx.beginPath()
                        ctx.moveTo(x, rect.y)
                        ctx.lineTo(x, rect.y + rect.height)
                        ctx.stroke()
                    }
                    for (var y = rect.y; y < rect.y + rect.height; y += Math.max(18, rect.height / 6)) {
                        ctx.beginPath()
                        ctx.moveTo(rect.x, y)
                        ctx.lineTo(rect.x + rect.width, y)
                        ctx.stroke()
                    }
                    ctx.strokeStyle = root.seriesColor
                    ctx.lineWidth = 3
                    ctx.strokeRect(
                        rect.x + rect.width * 0.18,
                        rect.y + rect.height * 0.18,
                        rect.width * 0.64,
                        rect.height * 0.64)
                    ctx.restore()
                }

                Connections {
                    target: root

                    function onSeriesColorChanged() {
                        previewCanvas.requestPaint()
                    }

                    function onResolutionTextChanged() {
                        previewCanvas.requestPaint()
                    }
                }
            }
        }
    }
}
