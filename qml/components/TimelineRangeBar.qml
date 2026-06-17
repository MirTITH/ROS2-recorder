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
    readonly property real thumbWidth: Math.max(44, (boundedDuration / boundedTotal) * track.width)

    height: 16
    color: "#f1f5f9"

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
        height: 5
        color: "#d7dde5"
        border.color: "#b8c2cf"
        border.width: 1
    }

    Rectangle {
        id: thumb

        x: Math.max(0, Math.min(track.width - width, root.thumbX))
        y: track.y - 5
        width: Math.min(track.width, root.thumbWidth)
        height: 15
        color: "transparent"

        Rectangle {
            id: thumbBody

            anchors.fill: parent
            anchors.leftMargin: 7
            anchors.rightMargin: 7
            anchors.topMargin: 2
            anchors.bottomMargin: 2
            color: "#9aa8ba"
            border.color: "#718096"
            border.width: 1
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            property real pressTrackX: 0
            property real pressStart: 0
            property real pressDuration: 0

            function trackX(mouse) {
                return mapToItem(track, mouse.x, mouse.y).x
            }

            onPressed: function(mouse) {
                pressTrackX = trackX(mouse)
                pressStart = root.boundedStart
                pressDuration = root.boundedDuration
            }

            onPositionChanged: function(mouse) {
                if (pressed) {
                    var deltaSeconds = ((trackX(mouse) - pressTrackX) / Math.max(1, track.width)) * root.boundedTotal
                    root.requestWindow(pressStart + deltaSeconds, pressDuration)
                }
            }
        }

        Rectangle {
            id: leftHandle

            width: 7
            height: parent.height
            color: "#64748b"
            anchors.left: parent.left

            Rectangle {
                id: leftHandleGrip

                width: 1
                height: parent.height - 4
                anchors.centerIn: parent
                color: "#c6d0dc"
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.SizeHorCursor
                property real pressTrackX: 0
                property real pressStart: 0
                property real pressDuration: 0

                function trackX(mouse) {
                    return mapToItem(track, mouse.x, mouse.y).x
                }

                onPressed: function(mouse) {
                    pressTrackX = trackX(mouse)
                    pressStart = root.boundedStart
                    pressDuration = root.boundedDuration
                }

                onPositionChanged: function(mouse) {
                    if (pressed) {
                        var deltaSeconds = ((trackX(mouse) - pressTrackX) / Math.max(1, track.width)) * root.boundedTotal
                        root.requestWindow(pressStart + deltaSeconds, pressDuration - deltaSeconds)
                    }
                }
            }
        }

        Rectangle {
            id: rightHandle

            width: 7
            height: parent.height
            color: "#64748b"
            anchors.right: parent.right

            Rectangle {
                id: rightHandleGrip

                width: 1
                height: parent.height - 4
                anchors.centerIn: parent
                color: "#c6d0dc"
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.SizeHorCursor
                property real pressTrackX: 0
                property real pressStart: 0
                property real pressDuration: 0

                function trackX(mouse) {
                    return mapToItem(track, mouse.x, mouse.y).x
                }

                onPressed: function(mouse) {
                    pressTrackX = trackX(mouse)
                    pressStart = root.boundedStart
                    pressDuration = root.boundedDuration
                }

                onPositionChanged: function(mouse) {
                    if (pressed) {
                        var deltaSeconds = ((trackX(mouse) - pressTrackX) / Math.max(1, track.width)) * root.boundedTotal
                        root.requestWindow(pressStart, pressDuration + deltaSeconds)
                    }
                }
            }
        }
    }
}
