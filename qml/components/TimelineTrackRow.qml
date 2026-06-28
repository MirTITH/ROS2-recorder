import QtQuick 2.15
import "."

Rectangle {
    id: root

    property string trackKind: "empty"
    property var seriesList: []
    property bool isExpanded: false
    property bool showData: true
    property var messageDots: []
    property real xMax: 60
    property real visibleStartSeconds: 0
    property real visibleDurationSeconds: 60
    property real plotTopPadding: 4
    property real plotBottomPadding: 4
    property real sampleMarkerSpacingThreshold: 12
    readonly property int collapsedHeight: 32
    readonly property int expandedHeight: 120

    height: root.isExpanded ? root.expandedHeight : root.collapsedHeight
    color: trackKind === "empty" ? Theme.surfaceAlt : Theme.surface

    function boundedDuration() {
        return Math.max(0.001, Number(visibleDurationSeconds) || 1)
    }

    function seriesColor(value) {
        var text = String(value || "")
        return /^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.test(text) ? text : "#2563eb"
    }

    function plotTop() {
        return Math.min(height / 2, Math.max(0, plotTopPadding))
    }

    function plotHeight() {
        return Math.max(1, height - plotTop() - Math.max(0, plotBottomPadding))
    }

    function visibleEndSeconds() {
        return visibleStartSeconds + boundedDuration()
    }

    function numericPoint(point) {
        var xValue = Number(point.x)
        var yValue = Number(point.y)
        if (!isFinite(xValue) || !isFinite(yValue)) {
            return null
        }
        return { x: xValue, y: yValue, boundary: false }
    }

    function interpolateBoundaryPoint(leftPoint, rightPoint, targetX) {
        var span = rightPoint.x - leftPoint.x
        if (!isFinite(span) || Math.abs(span) < 0.000000001) {
            return null
        }
        var ratio = (targetX - leftPoint.x) / span
        return {
            x: targetX,
            y: leftPoint.y + (rightPoint.y - leftPoint.y) * ratio,
            boundary: true
        }
    }

    function collectDrawablePoints(points) {
        var start = visibleStartSeconds
        var end = visibleEndSeconds()
        var before = null
        var after = null
        var inside = []

        for (var index = 0; index < points.length; ++index) {
            var candidate = numericPoint(points[index])
            if (candidate === null) {
                continue
            }
            if (candidate.x < start) {
                before = candidate
            } else if (candidate.x > end) {
                after = candidate
                break
            } else {
                inside.push(candidate)
            }
        }

        var drawable = []
        var leftSource = inside.length > 0 ? inside[0] : after
        if (before !== null && leftSource !== null && before.x < start && leftSource.x > start) {
            var leftBoundary = interpolateBoundaryPoint(before, leftSource, start)
            if (leftBoundary !== null) {
                drawable.push(leftBoundary)
            }
        }

        for (var insideIndex = 0; insideIndex < inside.length; ++insideIndex) {
            drawable.push(inside[insideIndex])
        }

        var rightSource = inside.length > 0 ? inside[inside.length - 1] : before
        if (rightSource !== null && after !== null && rightSource.x < end && after.x > end) {
            var rightBoundary = interpolateBoundaryPoint(rightSource, after, end)
            if (rightBoundary !== null) {
                drawable.push(rightBoundary)
            }
        }

        return drawable
    }

    function collectVisibleSamples(points) {
        var start = visibleStartSeconds
        var end = visibleEndSeconds()
        var samples = []
        for (var index = 0; index < points.length; ++index) {
            var candidate = numericPoint(points[index])
            if (candidate !== null && candidate.x >= start && candidate.x <= end) {
                samples.push(candidate)
            }
        }
        return samples
    }

    function xToPixel(xValue, widthValue) {
        return ((xValue - visibleStartSeconds) / boundedDuration()) * widthValue
    }

    function yToPixel(yValue, minY, maxY, top, plotHeightValue) {
        return top + (1 - ((yValue - minY) / Math.max(0.001, maxY - minY))) * plotHeightValue
    }

    function averageSampleSpacing(samples, widthValue) {
        if (samples.length < 2) {
            return widthValue
        }
        var firstX = xToPixel(samples[0].x, widthValue)
        var lastX = xToPixel(samples[samples.length - 1].x, widthValue)
        return Math.abs(lastX - firstX) / Math.max(1, samples.length - 1)
    }

    function shouldDrawSampleMarkers(samples, widthValue) {
        return samples.length === 1 || averageSampleSpacing(samples, widthValue) >= sampleMarkerSpacingThreshold
    }

    function drawSampleMarkers(ctx, samples, color, minY, maxY, top, plotHeightValue, widthValue) {
        ctx.fillStyle = Theme.surface
        ctx.strokeStyle = color
        ctx.lineWidth = 1
        for (var index = 0; index < samples.length; ++index) {
            var sample = samples[index]
            var x = xToPixel(sample.x, widthValue)
            var y = yToPixel(sample.y, minY, maxY, top, plotHeightValue)
            ctx.beginPath()
            ctx.arc(x, y, 2, 0, Math.PI * 2)
            ctx.fill()
            ctx.stroke()
        }
    }

    Canvas {
        id: curveCanvas

        anchors.fill: parent
        visible: root.showData && root.trackKind === "numeric" && root.isExpanded
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.fillStyle = Theme.surface
            ctx.fillRect(0, 0, width, height)

            var top = root.plotTop()
            var plotHeight = root.plotHeight()
            var entries = root.seriesList || []
            var minY = -1
            var maxY = 1
            for (var seriesIndex = 0; seriesIndex < entries.length; ++seriesIndex) {
                var rangeEntry = entries[seriesIndex] || {}
                if (rangeEntry.visible === false) {
                    continue
                }
                var points = rangeEntry.points || []
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

            ctx.strokeStyle = Theme.gridLine
            ctx.lineWidth = 1
            for (var gridY = 0; gridY <= 2; ++gridY) {
                var yLine = top + (gridY / 2) * plotHeight
                ctx.beginPath()
                ctx.moveTo(0, yLine)
                ctx.lineTo(width, yLine)
                ctx.stroke()
            }

            for (var drawSeriesIndex = 0; drawSeriesIndex < entries.length; ++drawSeriesIndex) {
                var entry = entries[drawSeriesIndex] || {}
                if (entry.visible === false) {
                    continue
                }
                var sourcePoints = entry.points || []
                var color = root.seriesColor(entry.color)
                var drawablePoints = root.collectDrawablePoints(sourcePoints)

                if (drawablePoints.length >= 2) {
                    ctx.beginPath()
                    ctx.strokeStyle = color
                    ctx.lineWidth = 1.5
                    for (var drawPointIndex = 0; drawPointIndex < drawablePoints.length; ++drawPointIndex) {
                        var point = drawablePoints[drawPointIndex]
                        var x = root.xToPixel(point.x, width)
                        var yPixel = root.yToPixel(point.y, minY, maxY, top, plotHeight)
                        if (drawPointIndex === 0) {
                            ctx.moveTo(x, yPixel)
                        } else {
                            ctx.lineTo(x, yPixel)
                        }
                    }
                    ctx.stroke()
                }

                var visibleSamples = root.collectVisibleSamples(sourcePoints)
                if (root.shouldDrawSampleMarkers(visibleSamples, width)) {
                    root.drawSampleMarkers(ctx, visibleSamples, color, minY, maxY, top, plotHeight, width)
                }
            }
        }
    }

    Canvas {
        id: dotsCanvas

        anchors.fill: parent
        // 折叠态：所有 rosbag 数据行（numeric/empty）都画节奏点；相机行不画。
        visible: root.showData && !root.isExpanded && root.trackKind !== "camera"
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.fillStyle = Theme.surface
            ctx.fillRect(0, 0, width, height)

            var dots = root.messageDots || []
            var baselineY = height / 2
            ctx.fillStyle = Theme.tickStrong
            var start = root.visibleStartSeconds
            var end = root.visibleEndSeconds()
            for (var i = 0; i < dots.length; ++i) {
                var t = Number(dots[i])
                if (!isFinite(t) || t < start || t > end) {
                    continue
                }
                var x = root.xToPixel(t, width)
                ctx.beginPath()
                ctx.arc(x, baselineY, 1.5, 0, Math.PI * 2)
                ctx.fill()
            }
        }
    }

    onTrackKindChanged: { curveCanvas.requestPaint(); dotsCanvas.requestPaint() }
    onSeriesListChanged: curveCanvas.requestPaint()
    onMessageDotsChanged: dotsCanvas.requestPaint()
    onIsExpandedChanged: { curveCanvas.requestPaint(); dotsCanvas.requestPaint() }
    onShowDataChanged: { curveCanvas.requestPaint(); dotsCanvas.requestPaint() }
    onVisibleStartSecondsChanged: { curveCanvas.requestPaint(); dotsCanvas.requestPaint() }
    onVisibleDurationSecondsChanged: { curveCanvas.requestPaint(); dotsCanvas.requestPaint() }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.gridLine
    }
}
