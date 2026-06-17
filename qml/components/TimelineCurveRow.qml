import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtCharts 2.15

Rectangle {
    id: root

    property string trackKind: "empty"
    property var seriesList: []
    property real timeScale: 1.0

    height: 48
    color: trackKind === "empty" ? "#f8fafc" : "#ffffff"

    ChartView {
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

        ValueAxis { id: axisX; min: 0; max: 80 / Math.max(0.2, root.timeScale); labelsVisible: false; gridVisible: true }
        ValueAxis { id: axisY; min: -1.4; max: 2.4; labelsVisible: false; gridVisible: false }

        Repeater {
            model: root.seriesList
            delegate: LineSeries {
                axisX: axisX
                axisY: axisY
                color: modelData.color
                width: 1.5
                Component.onCompleted: {
                    clear()
                    var points = modelData.points || []
                    for (var i = 0; i < points.length; ++i) {
                        append(points[i].x, points[i].y)
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: "#e2e8f0"
    }
}
