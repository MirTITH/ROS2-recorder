import QtQuick 2.15

Rectangle {
    id: root

    property string trackKind: "empty"
    property var seriesList: []
    property real xMax: 80
    property real visibleStartSeconds: 0
    property real visibleDurationSeconds: 80

    height: 48
    color: trackKind === "empty" ? "#f8fafc" : "#ffffff"

    function boundedDuration() {
        return Math.max(0.001, Number(visibleDurationSeconds) || 1)
    }

    function seriesColor(value) {
        var text = String(value || "")
        return /^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.test(text) ? text : "#2563eb"
    }

    Canvas {
        id: curveCanvas

        anchors.fill: parent
        visible: root.trackKind === "numeric"
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.fillStyle = "#ffffff"
            ctx.fillRect(0, 0, width, height)

            var entries = root.seriesList || []
            var minY = -1
            var maxY = 1
            for (var seriesIndex = 0; seriesIndex < entries.length; ++seriesIndex) {
                var points = (entries[seriesIndex] || {}).points || []
                for (var pointIndex = 0; pointIndex < points.length; ++pointIndex) {
                    var yValue = Number(points[pointIndex].y)
                    if (isFinite(yValue)) {
                        minY = Math.min(minY, yValue)
                        maxY = Math.max(maxY, yValue)
                    }
                }
            }
            if (minY === maxY) {
                minY -= 1
                maxY += 1
            }

            ctx.strokeStyle = "#e2e8f0"
            ctx.lineWidth = 1
            for (var gridY = 0; gridY <= 2; ++gridY) {
                var yLine = (gridY / 2) * height
                ctx.beginPath()
                ctx.moveTo(0, yLine)
                ctx.lineTo(width, yLine)
                ctx.stroke()
            }

            for (var drawSeriesIndex = 0; drawSeriesIndex < entries.length; ++drawSeriesIndex) {
                var entry = entries[drawSeriesIndex] || {}
                var drawPoints = entry.points || []
                var started = false
                ctx.beginPath()
                ctx.strokeStyle = root.seriesColor(entry.color)
                ctx.lineWidth = 1.5
                for (var drawPointIndex = 0; drawPointIndex < drawPoints.length; ++drawPointIndex) {
                    var point = drawPoints[drawPointIndex]
                    var xValue = Number(point.x)
                    var y = Number(point.y)
                    if (!isFinite(xValue) || !isFinite(y)) {
                        continue
                    }
                    if (xValue < root.visibleStartSeconds || xValue > root.visibleStartSeconds + root.boundedDuration()) {
                        continue
                    }
                    var x = ((xValue - root.visibleStartSeconds) / root.boundedDuration()) * width
                    var yPixel = height - ((y - minY) / Math.max(0.001, maxY - minY)) * height
                    if (!started) {
                        ctx.moveTo(x, yPixel)
                        started = true
                    } else {
                        ctx.lineTo(x, yPixel)
                    }
                }
                if (started) {
                    ctx.stroke()
                }
            }
        }
    }

    onTrackKindChanged: curveCanvas.requestPaint()
    onSeriesListChanged: curveCanvas.requestPaint()
    onVisibleStartSecondsChanged: curveCanvas.requestPaint()
    onVisibleDurationSecondsChanged: curveCanvas.requestPaint()

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: "#e2e8f0"
    }
}
