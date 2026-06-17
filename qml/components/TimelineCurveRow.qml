import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtCharts 2.15

Rectangle {
    id: root

    property string trackKind: "empty"
    property var seriesList: []
    property real xMax: 80
    property real timeScale: 1.0

    function boundedXMax() {
        return isFinite(xMax) ? Math.max(1, xMax) : 80
    }

    function boundedTimeScale() {
        return isFinite(timeScale) ? Math.max(0.2, timeScale) : 1.0
    }

    function seriesColor(value) {
        var text = String(value || "")
        return /^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.test(text) ? text : "#2563eb"
    }

    height: 48
    color: trackKind === "empty" ? "#f8fafc" : "#ffffff"

    ChartView {
        id: chart

        anchors.fill: parent
        visible: root.trackKind === "numeric"
        antialiasing: true
        animationOptions: ChartView.NoAnimation
        backgroundColor: "transparent"
        plotAreaColor: "transparent"
        legend.visible: false
        margins.left: 0
        margins.right: 0
        margins.top: 0
        margins.bottom: 0

        ValueAxis { id: axisX; min: 0; max: root.boundedXMax() / root.boundedTimeScale(); labelsVisible: false; gridVisible: true }
        ValueAxis { id: axisY; min: -1.4; max: 2.4; labelsVisible: false; gridVisible: false }

        function rebuildSeries() {
            removeAllSeries()
            if (root.trackKind !== "numeric") {
                return
            }

            var entries = root.seriesList || []
            for (var seriesIndex = 0; seriesIndex < entries.length; ++seriesIndex) {
                var entry = entries[seriesIndex] || {}
                var lineSeries = createSeries(ChartView.SeriesTypeLine, "series-" + seriesIndex, axisX, axisY)
                lineSeries.color = root.seriesColor(entry.color)
                lineSeries.width = 1.5

                var points = entry.points || []
                for (var pointIndex = 0; pointIndex < points.length; ++pointIndex) {
                    var point = points[pointIndex]
                    if (!point || typeof point !== "object") {
                        continue
                    }

                    var x = Number(point.x)
                    var y = Number(point.y)
                    if (isFinite(x) && isFinite(y)) {
                        lineSeries.append(x, y)
                    }
                }
            }
        }

        Component.onCompleted: rebuildSeries()
    }

    onTrackKindChanged: chart.rebuildSeries()
    onSeriesListChanged: chart.rebuildSeries()

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: "#e2e8f0"
    }
}
