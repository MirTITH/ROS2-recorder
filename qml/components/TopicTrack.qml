import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtCharts 2.15

Rectangle {
    id: root

    property string topicName: ""
    property string backendName: ""
    property string frequencyText: ""
    property color seriesColor: "#2563eb"
    property var series: []
    property bool visibleState: true

    height: 86
    radius: 6
    color: visibleState ? "#ffffff" : "#f1f5f9"
    border.color: visibleState ? "#dbe3ef" : "#cbd5e1"
    border.width: 1
    opacity: visibleState ? 1.0 : 0.58

    function refreshSeries() {
        lineSeries.clear()
        var values = root.series || []
        var maxX = 1
        var minY = 0
        var maxY = 1

        for (var i = 0; i < values.length; ++i) {
            var point = values[i]
            var x = Number(point.x)
            var y = Number(point.y)
            if (!isFinite(x) || !isFinite(y)) {
                continue
            }
            lineSeries.append(x, y)
            maxX = Math.max(maxX, x)
            if (lineSeries.count === 1) {
                minY = y
                maxY = y
            } else {
                minY = Math.min(minY, y)
                maxY = Math.max(maxY, y)
            }
        }

        if (minY === maxY) {
            minY -= 1
            maxY += 1
        }
        axisX.max = maxX
        axisY.min = minY
        axisY.max = maxY
    }

    onSeriesChanged: refreshSeries()
    Component.onCompleted: refreshSeries()

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 10

        Rectangle {
            Layout.preferredWidth: 130
            Layout.fillHeight: true
            radius: 4
            color: "#f8fafc"
            border.color: "#e2e8f0"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 3

                Label {
                    Layout.fillWidth: true
                    text: root.topicName
                    color: "#162033"
                    font.pixelSize: 11
                    font.bold: true
                    elide: Text.ElideMiddle
                }

                Label {
                    Layout.fillWidth: true
                    text: root.backendName
                    color: "#475569"
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }

                Label {
                    Layout.fillWidth: true
                    text: root.frequencyText
                    color: "#64748b"
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
            }
        }

        ChartView {
            id: chart

            Layout.fillWidth: true
            Layout.fillHeight: true
            antialiasing: true
            animationOptions: ChartView.NoAnimation
            backgroundColor: "transparent"
            legend.visible: false
            margins.left: 0
            margins.right: 0
            margins.top: 0
            margins.bottom: 0
            plotAreaColor: "#f8fafc"

            ValueAxis {
                id: axisX
                min: 0
                max: 80
                labelsVisible: false
                gridVisible: true
                minorGridVisible: false
            }

            ValueAxis {
                id: axisY
                min: -1
                max: 1
                labelsVisible: false
                gridVisible: true
                minorGridVisible: false
            }

            LineSeries {
                id: lineSeries
                axisX: axisX
                axisY: axisY
                color: root.seriesColor
                width: 2
            }
        }
    }
}
