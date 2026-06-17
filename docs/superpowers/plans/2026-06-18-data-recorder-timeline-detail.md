# Data Recorder Timeline Detail Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refine the Timeline UI so high-zoom curves stay complete, sparse samples show markers, ruler labels are precise and less noisy, and the time range bar resembles the Premiere-style reference.

**Architecture:** Keep the existing QML component boundaries: `TimelineCurveRow.qml` owns Canvas curve drawing, `TimelinePanel.qml` owns ruler rendering and timeline layout, `TimelineViewport.qml` owns time-window math, and `TimelineRangeBar.qml` owns the bottom visible-window control. Add structure tests first, then make focused QML changes without introducing a new charting library or backend data path.

**Tech Stack:** ROS 2 `ament_cmake` package, Qt 6 QML, Canvas drawing, C++ gtest structure/smoke tests, `qmllint`, `colcon`.

---

### File Structure

**Modify:**

- `src/data_recorder/test/test_qml_structure.cpp`: add structure tests for the new Timeline detail contract and update old ruler expectations.
- `src/data_recorder/qml/components/TimelineCurveRow.qml`: add boundary-completed draw points and sparse sample markers.
- `src/data_recorder/qml/components/TimelineViewport.qml`: add a dense tick interval helper for the ruler.
- `src/data_recorder/qml/components/TimelinePanel.qml`: switch the ruler to one dense tick sequence, label every 10th tick, and use stable `minutes:seconds.milliseconds` labels.
- `src/data_recorder/qml/components/TimelineRangeBar.qml`: restyle the visible-window scrollbar while preserving stable track-coordinate drag math.

**Do not modify:**

- `install/data_recorder/...`: generated install tree.
- Backend C++ data models: this iteration is UI-only.

---

### Task 1: Structure Tests For Timeline Detail Contract

**Files:**

- Modify: `src/data_recorder/test/test_qml_structure.cpp`

- [ ] **Step 1: Write failing structure tests**

Replace the current `TimelineViewportRenderingRulesAreExplicit` test with:

```cpp
TEST(QmlStructure, TimelineViewportRenderingRulesAreExplicit)
{
  const std::string panel_text = read_text(qml_dir() / "components" / "TimelinePanel.qml");
  const std::string curve_text = read_text(qml_dir() / "components" / "TimelineCurveRow.qml");
  const std::string viewport_text = read_text(qml_dir() / "components" / "TimelineViewport.qml");
  const std::string range_text = read_text(qml_dir() / "components" / "TimelineRangeBar.qml");

  expect_contains(panel_text, "viewport.isTimeVisible(root.playheadSeconds)");
  expect_contains(panel_text, "rulerTickTimes");
  expect_contains(panel_text, "property int rulerLabelTickStride: 10");
  expect_contains(panel_text, "index % root.rulerLabelTickStride === 0");
  expect_contains(panel_text, "function formatTickLabel");
  expect_contains(panel_text, "totalMinutes");
  expect_contains(panel_text, "padStart(2, \"0\")");
  expect_contains(panel_text, "padStart(3, \"0\")");

  expect_contains(viewport_text, "function denseTickInterval");
  expect_contains(viewport_text, "var targetPixels = 10");

  expect_contains(curve_text, "property real plotTopPadding: 4");
  expect_contains(curve_text, "property real plotBottomPadding: 4");
  expect_contains(curve_text, "property real sampleMarkerSpacingThreshold: 12");
  expect_contains(curve_text, "function collectDrawablePoints");
  expect_contains(curve_text, "function interpolateBoundaryPoint");
  expect_contains(curve_text, "function collectVisibleSamples");
  expect_contains(curve_text, "function shouldDrawSampleMarkers");
  expect_contains(curve_text, "function drawSampleMarkers");
  expect_contains(curve_text, "boundary: true");
  expect_contains(curve_text, "boundary: false");
  expect_contains(curve_text, "ctx.arc");

  expect_contains(range_text, "id: thumbBody");
  expect_contains(range_text, "color: \"#9aa8ba\"");
  expect_contains(range_text, "id: leftHandleGrip");
  expect_contains(range_text, "id: rightHandleGrip");
  expect_contains(range_text, "mapToItem(track");
  expect_contains(range_text, "pressTrackX");
}
```

Update the existing `TimelineCurveAreaHasAdaptiveWindowAndRangeBar` test so it no longer requires
`majorTickTimes` or `minorTickTimes`. Keep its existing checks for:

```cpp
expect_contains(panel_text, "property real visibleStartSeconds");
expect_contains(panel_text, "property real visibleDurationSeconds");
expect_contains(panel_text, "function formatTickLabel");
expect_contains(panel_text, "TimelineRangeBar {");
expect_contains(panel_text, "wheel.modifiers & Qt.ShiftModifier");
```

- [ ] **Step 2: Run structure test and verify RED**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --continue-on-error --mixin release compile-commands ccache --packages-select data_recorder
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --event-handlers console_direct+ --ctest-args -R test_qml_structure --output-on-failure
```

Expected: `test_qml_structure` fails because `rulerTickTimes`, dense tick interval, curve boundary
completion, marker drawing, and the new range bar style are not implemented yet.

- [ ] **Step 3: Commit RED test**

Run:

```bash
git -C /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder add test/test_qml_structure.cpp
git -C /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder commit -m "test: specify timeline detail rendering contract"
```

---

### Task 2: Complete Curves At Viewport Boundaries And Draw Sparse Markers

**Files:**

- Modify: `src/data_recorder/qml/components/TimelineCurveRow.qml`
- Test: `src/data_recorder/test/test_qml_structure.cpp`

- [ ] **Step 1: Add curve helper properties and functions**

In `TimelineCurveRow.qml`, add this property near the existing plot padding properties:

```qml
property real sampleMarkerSpacingThreshold: 12
```

Add these functions after `plotHeight()`:

```qml
function visibleEndSeconds() {
    return visibleStartSeconds + boundedDuration()
}

function numericPoint(point) {
    var xValue = Number(point.x)
    var yValue = Number(point.y)
    if (!isFinite(xValue) || !isFinite(yValue)) {
        return null
    }
    return { "x": xValue, "y": yValue, "boundary": false }
}

function interpolateBoundaryPoint(leftPoint, rightPoint, targetX) {
    var span = rightPoint.x - leftPoint.x
    if (!isFinite(span) || Math.abs(span) < 0.000000001) {
        return null
    }
    var ratio = (targetX - leftPoint.x) / span
    return {
        "x": targetX,
        "y": leftPoint.y + (rightPoint.y - leftPoint.y) * ratio,
        "boundary": true
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
    ctx.fillStyle = "#ffffff"
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
```

- [ ] **Step 2: Replace the existing per-series stroke loop**

Inside `Canvas.onPaint`, replace the current `for (var drawSeriesIndex = 0; ... )` block with:

```qml
for (var drawSeriesIndex = 0; drawSeriesIndex < entries.length; ++drawSeriesIndex) {
    var entry = entries[drawSeriesIndex] || {}
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
```

- [ ] **Step 3: Run structure test and verify curve expectations pass**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --event-handlers console_direct+ --ctest-args -R test_qml_structure --output-on-failure
```

Expected: the curve-related assertions pass. The test may still fail on ruler and range bar
assertions.

- [ ] **Step 4: Commit curve renderer change**

Run:

```bash
git -C /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder add qml/components/TimelineCurveRow.qml
git -C /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder commit -m "Improve timeline curve detail rendering"
```

---

### Task 3: Dense Ruler Ticks And 10-Tick Labels

**Files:**

- Modify: `src/data_recorder/qml/components/TimelineViewport.qml`
- Modify: `src/data_recorder/qml/components/TimelinePanel.qml`
- Test: `src/data_recorder/test/test_qml_structure.cpp`

- [ ] **Step 1: Add a dense tick interval helper**

In `TimelineViewport.qml`, add this function after `minorTickInterval(widthValue)`:

```qml
function denseTickInterval(widthValue) {
    var intervals = [0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 30, 60, 120, 300]
    var targetPixels = 10
    var rawInterval = boundedVisibleDuration / Math.max(1, widthValue / targetPixels)
    for (var index = 0; index < intervals.length; ++index) {
        if (intervals[index] >= rawInterval) {
            return intervals[index]
        }
    }
    return intervals[intervals.length - 1]
}
```

- [ ] **Step 2: Add label stride and stable label formatting**

In `TimelinePanel.qml`, add this property near `property bool listsReady`:

```qml
property int rulerLabelTickStride: 10
```

Replace `formatTickLabel(seconds)` with:

```qml
function formatTickLabel(seconds) {
    var totalMs = Math.max(0, Math.round(Number(seconds || 0) * 1000))
    var ms = totalMs % 1000
    var totalSeconds = Math.floor(totalMs / 1000)
    var s = totalSeconds % 60
    var totalMinutes = Math.floor(totalSeconds / 60)
    return totalMinutes + ":" +
        s.toString().padStart(2, "0") + "." +
        ms.toString().padStart(3, "0")
}
```

- [ ] **Step 3: Replace the ruler tick model and repeater**

Inside the `Rectangle { id: ruler ... }` block in `TimelinePanel.qml`, replace:

```qml
readonly property var majorTickTimes: viewport.tickTimes(width, viewport.majorTickInterval(width))
readonly property var minorTickTimes: viewport.tickTimes(width, viewport.minorTickInterval(width))
```

with:

```qml
readonly property var rulerTickTimes: viewport.tickTimes(width, viewport.denseTickInterval(width))
```

Replace both existing tick `Repeater` blocks with this single repeater:

```qml
Repeater {
    model: ruler.rulerTickTimes

    delegate: Item {
        required property int index

        readonly property real tickTime: ruler.rulerTickTimes[index]
        readonly property bool labeledTick: index % root.rulerLabelTickStride === 0

        x: viewport.xAtTime(tickTime, ruler.width)
        width: 1
        height: ruler.height

        Rectangle {
            width: 1
            height: labeledTick ? 11 : 6
            color: labeledTick ? "#94a3b8" : "#cbd5e1"
        }

        Label {
            anchors.top: parent.top
            anchors.topMargin: 12
            anchors.horizontalCenter: parent.horizontalCenter
            visible: labeledTick
            text: root.formatTickLabel(tickTime)
            color: "#64748b"
            font.pixelSize: 10
        }
    }
}
```

- [ ] **Step 4: Run structure test and verify ruler expectations pass**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --event-handlers console_direct+ --ctest-args -R test_qml_structure --output-on-failure
```

Expected: ruler-related assertions pass. The test may still fail on range bar style assertions.

- [ ] **Step 5: Commit ruler change**

Run:

```bash
git -C /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder add qml/components/TimelineViewport.qml qml/components/TimelinePanel.qml
git -C /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder commit -m "Refine timeline ruler tick labels"
```

---

### Task 4: Premiere-Style Timeline Range Bar

**Files:**

- Modify: `src/data_recorder/qml/components/TimelineRangeBar.qml`
- Test: `src/data_recorder/test/test_qml_structure.cpp`

- [ ] **Step 1: Update the range bar geometry and colors**

In `TimelineRangeBar.qml`, replace:

```qml
readonly property real thumbWidth: Math.max(36, (boundedDuration / boundedTotal) * track.width)

height: 18
color: "#f8fafc"
```

with:

```qml
readonly property real thumbWidth: Math.max(44, (boundedDuration / boundedTotal) * track.width)

height: 16
color: "#f1f5f9"
```

Replace the `track` rectangle body with:

```qml
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
```

- [ ] **Step 2: Replace the thumb body and drag mouse area**

Inside `Rectangle { id: thumb ... }`, replace the existing geometry and visual properties with:

```qml
x: Math.max(0, Math.min(track.width - width, root.thumbX))
y: track.y - 5
width: Math.min(track.width, root.thumbWidth)
height: 15
color: "transparent"
```

Then add this rectangle as the first child of `thumb`:

```qml
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
```

Keep the existing thumb `MouseArea` drag logic, but add the cursor shape:

```qml
cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
```

- [ ] **Step 3: Replace the left and right handles**

Replace the existing `leftHandle` rectangle with:

```qml
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
```

Replace the existing `rightHandle` rectangle with:

```qml
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
```

- [ ] **Step 4: Run structure test and verify GREEN**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --event-handlers console_direct+ --ctest-args -R test_qml_structure --output-on-failure
```

Expected: `test_qml_structure` passes.

- [ ] **Step 5: Commit range bar style change**

Run:

```bash
git -C /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder add qml/components/TimelineRangeBar.qml
git -C /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder commit -m "Restyle timeline range bar"
```

---

### Task 5: Verification And Cleanup

**Files:**

- Verify: `src/data_recorder/qml/Main.qml`
- Verify: `src/data_recorder/qml/components/*.qml`
- Verify: all package tests

- [ ] **Step 1: Run QML lint**

Run:

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder && qmllint -I qml/components qml/Main.qml qml/components/*.qml
```

Expected: exits with code `0`.

- [ ] **Step 2: Build package**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --continue-on-error --mixin release compile-commands ccache --packages-select data_recorder
```

Expected: `data_recorder` builds successfully.

- [ ] **Step 3: Run package tests**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --event-handlers console_direct+ --ctest-args --output-on-failure
```

Expected: all `data_recorder` tests pass, including `test_qml_structure` and `test_qml_smoke`.

- [ ] **Step 4: Inspect git status**

Run:

```bash
git -C /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder status --short
```

Expected: no uncommitted source or test changes remain. If the implementation commits left only this
plan file modified, commit it with:

```bash
git -C /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder add docs/superpowers/plans/2026-06-18-data-recorder-timeline-detail.md
git -C /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder commit -m "docs: add timeline detail implementation plan"
```

---

### Spec Coverage Checklist

- High-zoom curve continuity: Task 2 adds boundary interpolation and draw-list completion.
- Sparse data point markers: Task 2 adds `sampleMarkerSpacingThreshold`, visible sample collection,
  and marker drawing.
- 10-tick ruler labels: Task 3 adds `rulerLabelTickStride: 10`.
- `minutes:seconds.milliseconds` labels: Task 3 replaces `formatTickLabel`.
- Dense ruler ticks: Task 3 adds `denseTickInterval` with a `10` px target.
- Premiere-like time scrollbar: Task 4 restyles track, thumb body, and resize handles.
- Verification: Task 5 runs QML lint, package build, and package tests.
