import QtQuick 2.15
import "."
import "curve_plot.js" as CurvePlot

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

    // 曲线缓存：仅在 seriesList 变化时重建（见 rebuildCache）。平移/缩放只重绘、不重建。
    // _cachedSeries: [{ xs: Float64Array, ys: Float64Array, color }]（仅含可见序列）
    property var _cachedSeries: []
    property real _cachedMinY: -1
    property real _cachedMaxY: 1

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

    function xToPixel(xValue, widthValue) {
        return ((xValue - visibleStartSeconds) / boundedDuration()) * widthValue
    }

    function yToPixel(yValue, minY, maxY, top, plotHeightValue) {
        return top + (1 - ((yValue - minY) / Math.max(0.001, maxY - minY))) * plotHeightValue
    }

    // 把 seriesList 一次性转成扁平 Float64Array 缓存 + 全局纵轴范围。
    function rebuildCache() {
        var built = CurvePlot.buildSeriesCache(root.seriesList || [])
        root._cachedSeries = built.series
        root._cachedMinY = built.minY
        root._cachedMaxY = built.maxY
    }

    function drawSampleMarkers(ctx, xs, ys, lo, hi, color, minY, maxY, top, plotHeightValue, widthValue) {
        ctx.fillStyle = Theme.surface
        ctx.strokeStyle = color
        ctx.lineWidth = 1
        for (var index = lo; index <= hi; ++index) {
            var x = xToPixel(xs[index], widthValue)
            var y = yToPixel(ys[index], minY, maxY, top, plotHeightValue)
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
            var minY = root._cachedMinY
            var maxY = root._cachedMaxY

            ctx.strokeStyle = Theme.gridLine
            ctx.lineWidth = 1
            for (var gridY = 0; gridY <= 2; ++gridY) {
                var yLine = top + (gridY / 2) * plotHeight
                ctx.beginPath()
                ctx.moveTo(0, yLine)
                ctx.lineTo(width, yLine)
                ctx.stroke()
            }

            var start = root.visibleStartSeconds
            var end = root.visibleEndSeconds()
            var cache = root._cachedSeries || []
            for (var s = 0; s < cache.length; ++s) {
                var entry = cache[s]
                var xs = entry.xs
                var ys = entry.ys
                if (xs.length === 0) {
                    continue
                }
                var color = root.seriesColor(entry.color)

                var poly = CurvePlot.drawablePolyline(xs, ys, start, end)
                if (poly.xs.length >= 2) {
                    ctx.beginPath()
                    ctx.strokeStyle = color
                    ctx.lineWidth = 1.5
                    for (var p = 0; p < poly.xs.length; ++p) {
                        var lineX = root.xToPixel(poly.xs[p], width)
                        var lineY = root.yToPixel(poly.ys[p], minY, maxY, top, plotHeight)
                        if (p === 0) {
                            ctx.moveTo(lineX, lineY)
                        } else {
                            ctx.lineTo(lineX, lineY)
                        }
                    }
                    ctx.stroke()
                }

                var range = CurvePlot.visibleIndexRange(xs, start, end)
                var lo = range.lo
                var hi = range.hi
                if (hi >= lo) {
                    var visibleCount = hi - lo + 1
                    var firstPx = root.xToPixel(xs[lo], width)
                    var lastPx = root.xToPixel(xs[hi], width)
                    var avgSpacing = visibleCount < 2 ?
                        width : Math.abs(lastPx - firstPx) / Math.max(1, visibleCount - 1)
                    if (visibleCount === 1 || avgSpacing >= root.sampleMarkerSpacingThreshold) {
                        root.drawSampleMarkers(
                            ctx, xs, ys, lo, hi, color, minY, maxY, top, plotHeight, width)
                    }
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
    onSeriesListChanged: { root.rebuildCache(); curveCanvas.requestPaint() }
    onMessageDotsChanged: dotsCanvas.requestPaint()
    onIsExpandedChanged: { curveCanvas.requestPaint(); dotsCanvas.requestPaint() }
    onShowDataChanged: { curveCanvas.requestPaint(); dotsCanvas.requestPaint() }
    onVisibleStartSecondsChanged: { curveCanvas.requestPaint(); dotsCanvas.requestPaint() }
    onVisibleDurationSecondsChanged: { curveCanvas.requestPaint(); dotsCanvas.requestPaint() }

    Component.onCompleted: root.rebuildCache()

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.gridLine
    }
}
