import QtQuick 2.15

Rectangle {
    id: root

    property real totalDurationSeconds: 1
    property real visibleStartSeconds: 0
    property real visibleDurationSeconds: 1
    signal windowRequested(real startSeconds, real durationSeconds)

    readonly property real boundedTotal: Math.max(1, Number(totalDurationSeconds) || 1)
    readonly property real boundedDuration: Math.max(0.001, Math.min(boundedTotal, Number(visibleDurationSeconds) || boundedTotal))
    readonly property real boundedStart: Math.max(0, Math.min(boundedTotal - boundedDuration, Number(visibleStartSeconds) || 0))
    readonly property real thumbX: (boundedStart / boundedTotal) * track.width
    readonly property real thumbWidth: Math.max(36, (boundedDuration / boundedTotal) * track.width)

    height: 18
    color: "#f8fafc"

    function requestWindow(startSeconds, durationSeconds) {
        var nextDuration = Math.max(0.001, Math.min(root.boundedTotal, durationSeconds))
        var nextStart = Math.max(0, Math.min(root.boundedTotal - nextDuration, startSeconds))
        root.windowRequested(nextStart, nextDuration)
    }

    Rectangle {
        id: track

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        height: 8
        color: "#e2e8f0"
        border.color: "#cbd5e1"
        border.width: 1
    }

    Rectangle {
        id: thumb

        x: Math.max(0, Math.min(track.width - width, root.thumbX))
        y: track.y - 3
        width: Math.min(track.width, root.thumbWidth)
        height: 14
        color: "#cbd5e1"
        border.color: "#94a3b8"
        border.width: 1

        MouseArea {
            anchors.fill: parent
            property real pressX: 0
            property real pressStart: 0

            onPressed: function(mouse) {
                pressX = mouse.x
                pressStart = root.boundedStart
            }

            onPositionChanged: function(mouse) {
                if (pressed) {
                    var deltaSeconds = ((mouse.x - pressX) / Math.max(1, track.width)) * root.boundedTotal
                    root.requestWindow(pressStart + deltaSeconds, root.boundedDuration)
                }
            }
        }

        Rectangle {
            id: leftHandle

            width: 5
            height: parent.height
            color: "#64748b"
            anchors.left: parent.left

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.SizeHorCursor
                property real pressX: 0
                property real pressStart: 0
                property real pressDuration: 0

                onPressed: function(mouse) {
                    pressX = mouse.x
                    pressStart = root.boundedStart
                    pressDuration = root.boundedDuration
                }

                onPositionChanged: function(mouse) {
                    if (pressed) {
                        var deltaSeconds = ((mouse.x - pressX) / Math.max(1, track.width)) * root.boundedTotal
                        root.requestWindow(pressStart + deltaSeconds, pressDuration - deltaSeconds)
                    }
                }
            }
        }

        Rectangle {
            id: rightHandle

            width: 5
            height: parent.height
            color: "#64748b"
            anchors.right: parent.right

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.SizeHorCursor
                property real pressX: 0
                property real pressDuration: 0

                onPressed: function(mouse) {
                    pressX = mouse.x
                    pressDuration = root.boundedDuration
                }

                onPositionChanged: function(mouse) {
                    if (pressed) {
                        var deltaSeconds = ((mouse.x - pressX) / Math.max(1, track.width)) * root.boundedTotal
                        root.requestWindow(root.boundedStart, pressDuration + deltaSeconds)
                    }
                }
            }
        }
    }
}
