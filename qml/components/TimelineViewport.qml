import QtQml 2.15

QtObject {
    id: root

    objectName: "timelineViewport"

    property real totalDurationSeconds: 1
    property real visibleStartSeconds: 0
    property real visibleDurationSeconds: 1
    property real minimumVisibleDurationSeconds: 0.05

    readonly property real boundedTotalDuration: Math.max(1, Number(totalDurationSeconds) || 1)
    readonly property real boundedVisibleDuration: Math.max(
        minimumVisibleDurationSeconds,
        Math.min(boundedTotalDuration, Number(visibleDurationSeconds) || boundedTotalDuration))
    readonly property real visibleEndSeconds: Math.min(
        boundedTotalDuration,
        visibleStartSeconds + boundedVisibleDuration)

    function clamp(value, low, high) {
        return Math.max(low, Math.min(high, value))
    }

    function setWindow(startSeconds, durationSeconds) {
        var duration = clamp(
            Number(durationSeconds) || boundedTotalDuration,
            minimumVisibleDurationSeconds,
            boundedTotalDuration)
        visibleDurationSeconds = duration
        visibleStartSeconds = clamp(Number(startSeconds) || 0, 0, Math.max(0, boundedTotalDuration - duration))
    }

    function panBySeconds(deltaSeconds) {
        setWindow(visibleStartSeconds + Number(deltaSeconds || 0), boundedVisibleDuration)
    }

    function panByWheel(deltaY) {
        var direction = deltaY > 0 ? -1 : 1
        panBySeconds(direction * boundedVisibleDuration / 8)
    }

    function zoomAt(anchorX, widthValue, deltaY) {
        var oldDuration = boundedVisibleDuration
        var factor = deltaY > 0 ? 0.86 : 1.16
        var newDuration = clamp(oldDuration * factor, minimumVisibleDurationSeconds, boundedTotalDuration)
        var anchorRatio = clamp(anchorX / Math.max(1, widthValue), 0, 1)
        var anchorTime = visibleStartSeconds + oldDuration * anchorRatio
        setWindow(anchorTime - newDuration * anchorRatio, newDuration)
    }

    function timeAtX(xPosition, widthValue) {
        return clamp(
            visibleStartSeconds + (xPosition / Math.max(1, widthValue)) * boundedVisibleDuration,
            0,
            boundedTotalDuration)
    }

    function xAtTime(seconds, widthValue) {
        return ((Number(seconds) - visibleStartSeconds) / boundedVisibleDuration) * widthValue
    }

    function isTimeVisible(seconds) {
        var value = Number(seconds)
        return isFinite(value) && value >= visibleStartSeconds && value <= visibleEndSeconds
    }

    function majorTickInterval(widthValue) {
        var intervals = [0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 30, 60, 120, 300]
        var targetPixels = 64
        var rawInterval = boundedVisibleDuration / Math.max(1, widthValue / targetPixels)
        for (var index = 0; index < intervals.length; ++index) {
            if (intervals[index] >= rawInterval) {
                return intervals[index]
            }
        }
        return intervals[intervals.length - 1]
    }

    function minorTickInterval(widthValue) {
        var major = majorTickInterval(widthValue)
        if ((major / 5 / boundedVisibleDuration) * widthValue >= 10) {
            return major / 5
        }
        return major / 2
    }

    function tickTimes(widthValue, interval) {
        var boundedInterval = Math.max(0.000001, Number(interval) || majorTickInterval(widthValue))
        var first = Math.ceil(visibleStartSeconds / boundedInterval) * boundedInterval
        var ticks = []
        for (var tick = first; tick <= visibleEndSeconds + boundedInterval * 0.001; tick += boundedInterval) {
            ticks.push(tick)
        }
        return ticks
    }
}
