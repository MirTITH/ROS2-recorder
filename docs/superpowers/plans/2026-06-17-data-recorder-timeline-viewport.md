# Data Recorder Timeline Viewport Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Refactor the Timeline UI so ruler, curve rows, playhead, wheel gestures, and the range bar share one viewport state object.

**Architecture:** Add `TimelineViewport.qml` as a non-visual `QtObject` that owns visible-window math, conversion helpers, panning, zooming, and tick generation. `TimelinePanel.qml` will consume that object, while `TimelineRangeBar.qml` and `TimelineCurveRow.qml` will keep their focused rendering/control responsibilities.

**Tech Stack:** ROS 2 package with Qt 6 QML, C++ gtest smoke/structure tests, `qmllint`, `colcon`.

---

### Task 1: Structure Tests For Timeline Viewport Contract

**Files:**
- Modify: `src/data_recorder/test/test_qml_structure.cpp`

- [x] **Step 1: Write failing structure tests**

Add a new test that checks:

```cpp
TEST(QmlStructure, TimelineUsesViewportObjectForWindowMath)
{
  const std::string panel_text = read_text(qml_dir() / "components" / "TimelinePanel.qml");
  const std::string viewport_text = read_text(qml_dir() / "components" / "TimelineViewport.qml");

  EXPECT_TRUE(std::filesystem::exists(qml_dir() / "components" / "TimelineViewport.qml"));
  expect_contains(panel_text, "TimelineViewport {");
  expect_contains(panel_text, "id: viewport");
  expect_not_contains(panel_text, "function nudgePlayhead");
  expect_not_contains(panel_text, "root.nudgePlayhead");
  expect_contains(viewport_text, "function panByWheel");
  expect_contains(viewport_text, "function zoomAt");
  expect_contains(viewport_text, "function timeAtX");
  expect_contains(viewport_text, "function xAtTime");
  expect_contains(viewport_text, "function isTimeVisible");
}
```

Add a second test that checks:

```cpp
TEST(QmlStructure, TimelineViewportRenderingRulesAreExplicit)
{
  const std::string panel_text = read_text(qml_dir() / "components" / "TimelinePanel.qml");
  const std::string curve_text = read_text(qml_dir() / "components" / "TimelineCurveRow.qml");
  const std::string range_text = read_text(qml_dir() / "components" / "TimelineRangeBar.qml");

  expect_contains(panel_text, "viewport.isTimeVisible(root.playheadSeconds)");
  expect_contains(panel_text, "minorTickTimes");
  expect_contains(panel_text, "majorTickTimes");
  expect_contains(curve_text, "property real plotTopPadding: 4");
  expect_contains(curve_text, "property real plotBottomPadding: 4");
  expect_contains(curve_text, "plotHeight");
  expect_contains(range_text, "mapToItem(track");
  expect_contains(range_text, "pressTrackX");
}
```

Update the existing `TimelineCurveAreaHasAdaptiveWindowAndRangeBar` test so it no longer expects `function niceTickInterval` or `function nudgePlayhead` in `TimelinePanel.qml`.

- [x] **Step 2: Run structure test and verify RED**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --continue-on-error --mixin release compile-commands ccache --packages-select data_recorder
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --ctest-args -R test_qml_structure --output-on-failure --event-handlers console_direct+
```

Expected: `test_qml_structure` fails because `TimelineViewport.qml`, viewport usage, plot padding, and stable range bar drag math are not implemented yet.

### Task 2: QML Smoke Tests For Shift Wheel And Playhead Visibility

**Files:**
- Modify: `src/data_recorder/test/test_qml_smoke.cpp`

- [x] **Step 1: Write failing smoke tests**

Add helpers to find QML objects by property:

```cpp
QObject * find_by_property(QObject * object, const char * property_name, const QVariant & value)
{
  if (object == nullptr) {
    return nullptr;
  }
  const QVariant property_value = object->property(property_name);
  if (property_value.isValid() && property_value == value) {
    return object;
  }
  if (auto * item = qobject_cast<QQuickItem *>(object)) {
    for (QQuickItem * child_item : item->childItems()) {
      if (auto * found = find_by_property(child_item, property_name, value)) {
        return found;
      }
    }
  }
  for (QObject * child : object->children()) {
    if (auto * found = find_by_property(child, property_name, value)) {
      return found;
    }
  }
  return nullptr;
}
```

Add:

```cpp
TEST_F(QmlSmokeTest, ShiftWheelPansTimelineWithoutMovingPlayhead)
{
  QObject * viewport = find_required(root_, "timelineViewport");
  auto * curve_mouse_area = qobject_cast<QQuickItem *>(find_required(root_, "timelineCurveMouseArea"));
  ASSERT_NE(curve_mouse_area, nullptr);

  ASSERT_TRUE(QMetaObject::invokeMethod(viewport, "setWindow", Q_ARG(QVariant, 10.0), Q_ARG(QVariant, 20.0)));
  controller_->setPlayheadSeconds(15.0);
  const double previous_playhead = controller_->playheadSeconds();
  const double previous_start = viewport->property("visibleStartSeconds").toDouble();

  const QPoint position =
    curve_mouse_area->mapToScene(QPointF(curve_mouse_area->width() / 2.0, curve_mouse_area->height() / 2.0)).toPoint();
  QWheelEvent wheel_event(
    position,
    window_->mapToGlobal(position),
    QPoint(),
    QPoint(0, -120),
    Qt::NoButton,
    Qt::ShiftModifier,
    Qt::NoScrollPhase,
    false);
  EXPECT_TRUE(QCoreApplication::sendEvent(window_, &wheel_event));
  QCoreApplication::processEvents();

  EXPECT_DOUBLE_EQ(controller_->playheadSeconds(), previous_playhead);
  EXPECT_GT(viewport->property("visibleStartSeconds").toDouble(), previous_start);
}
```

Add:

```cpp
TEST_F(QmlSmokeTest, PlayheadLineHidesOutsideVisibleWindow)
{
  QObject * viewport = find_required(root_, "timelineViewport");
  auto * curve_playhead = qobject_cast<QQuickItem *>(find_required(root_, "timelineCurvePlayhead"));
  auto * ruler_playhead = qobject_cast<QQuickItem *>(find_required(root_, "timelineRulerPlayhead"));
  ASSERT_NE(curve_playhead, nullptr);
  ASSERT_NE(ruler_playhead, nullptr);

  ASSERT_TRUE(QMetaObject::invokeMethod(viewport, "setWindow", Q_ARG(QVariant, 20.0), Q_ARG(QVariant, 10.0)));
  controller_->setPlayheadSeconds(5.0);
  QCoreApplication::processEvents();

  EXPECT_FALSE(curve_playhead->isVisible());
  EXPECT_FALSE(ruler_playhead->isVisible());

  controller_->setPlayheadSeconds(25.0);
  QCoreApplication::processEvents();

  EXPECT_TRUE(curve_playhead->isVisible());
  EXPECT_TRUE(ruler_playhead->isVisible());
}
```

- [x] **Step 2: Run smoke test and verify RED**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --continue-on-error --mixin release compile-commands ccache --packages-select data_recorder
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --ctest-args -R test_qml_smoke --output-on-failure --event-handlers console_direct+
```

Expected: `test_qml_smoke` fails because `timelineViewport`, `timelineCurveMouseArea`, and playhead object names do not exist yet.

### Task 3: Add TimelineViewport.qml

**Files:**
- Create: `src/data_recorder/qml/components/TimelineViewport.qml`

- [x] **Step 1: Implement viewport object**

Create:

```qml
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
```

- [x] **Step 2: Run qmllint for the new component**

Run:

```bash
qmllint -I qml/components qml/components/TimelineViewport.qml
```

Expected: no errors.

### Task 4: Refactor TimelinePanel.qml To Use TimelineViewport

**Files:**
- Modify: `src/data_recorder/qml/components/TimelinePanel.qml`

- [x] **Step 1: Replace local viewport math with TimelineViewport**

Add:

```qml
TimelineViewport {
    id: viewport
    totalDurationSeconds: root.effectiveDurationSeconds
}
```

Replace `visibleStartSeconds` and `visibleDurationSeconds` root state with aliases:

```qml
readonly property real visibleStartSeconds: viewport.visibleStartSeconds
readonly property real visibleDurationSeconds: viewport.boundedVisibleDuration
```

Replace `boundedVisibleDuration()`, `visibleEndSeconds()`, `setVisibleWindow()`, `playheadX()`,
`seekFromCurveX()`, `zoomVisibleWindow()`, `nudgePlayhead()`, `niceTickInterval()`, `firstTick()`,
`tickCount()`, and `tickTimeAt()` usage with viewport calls.

Add helpers:

```qml
function seekFromCurveX(xPosition) {
    if (controller && controller.setPlayheadSeconds) {
        controller.setPlayheadSeconds(viewport.timeAtX(xPosition, curveViewport.width))
    }
}

function formatTickLabel(seconds) {
    if (viewport.boundedVisibleDuration < 2) {
        return Math.round(seconds * 1000) + "ms"
    }
    if (viewport.boundedVisibleDuration < 90) {
        return seconds.toFixed(seconds < 10 ? 1 : 0) + "s"
    }
    var totalSeconds = Math.floor(seconds)
    var minutes = Math.floor(totalSeconds / 60)
    var remainder = totalSeconds % 60
    return minutes + ":" + remainder.toString().padStart(2, "0")
}
```

Add major and minor tick arrays in `ruler`:

```qml
readonly property var majorTickTimes: viewport.tickTimes(width, viewport.majorTickInterval(width))
readonly property var minorTickTimes: viewport.tickTimes(width, viewport.minorTickInterval(width))
```

Use minor tick `Repeater` for short unlabeled ticks and major tick `Repeater` for labels.

Set playhead rectangles:

```qml
objectName: "timelineRulerPlayhead"
visible: viewport.isTimeVisible(root.playheadSeconds)
x: viewport.xAtTime(root.playheadSeconds, parent.width) - width / 2
```

and:

```qml
objectName: "timelineCurvePlayhead"
visible: viewport.isTimeVisible(root.playheadSeconds)
x: viewport.xAtTime(root.playheadSeconds, parent.width) - width / 2
```

Set curve mouse area:

```qml
objectName: "timelineCurveMouseArea"
onWheel: function(wheel) {
    if (wheel.modifiers & Qt.ShiftModifier) {
        viewport.panByWheel(wheel.angleDelta.y)
    } else {
        viewport.zoomAt(wheel.x, curveViewport.width, wheel.angleDelta.y)
    }
    wheel.accepted = true
}
```

Update `TimelineRangeBar`:

```qml
visibleStartSeconds: viewport.visibleStartSeconds
visibleDurationSeconds: viewport.boundedVisibleDuration
onWindowRequested: function(startSeconds, durationSeconds) {
    viewport.setWindow(startSeconds, durationSeconds)
}
```

Update `Component.onCompleted`:

```qml
viewport.setWindow(0, root.effectiveDurationSeconds)
listsReady = true
```

- [x] **Step 2: Run structure and smoke tests**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --continue-on-error --mixin release compile-commands ccache --packages-select data_recorder
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --ctest-args -R "test_qml_structure|test_qml_smoke" --output-on-failure --event-handlers console_direct+
```

Expected: `TimelineUsesViewportObjectForWindowMath` passes after this task; `TimelineViewportRenderingRulesAreExplicit` still fails until Tasks 5 and 6 are implemented.

### Task 5: Fix TimelineRangeBar Stable Drag Coordinates

**Files:**
- Modify: `src/data_recorder/qml/components/TimelineRangeBar.qml`

- [x] **Step 1: Replace moving-local mouse deltas**

For the thumb and both resize handles:

```qml
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
```

For left handle use `root.requestWindow(pressStart + deltaSeconds, pressDuration - deltaSeconds)`.

For right handle use `root.requestWindow(pressStart, pressDuration + deltaSeconds)`.

- [x] **Step 2: Run structure test**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --continue-on-error --mixin release compile-commands ccache --packages-select data_recorder
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --ctest-args -R test_qml_structure --output-on-failure --event-handlers console_direct+
```

Expected: `TimelineRangeBar` structure assertions pass.

### Task 6: Add Curve Plot Padding

**Files:**
- Modify: `src/data_recorder/qml/components/TimelineCurveRow.qml`

- [x] **Step 1: Add padded plot area**

Add:

```qml
property real plotTopPadding: 4
property real plotBottomPadding: 4

function plotTop() {
    return Math.min(height / 2, Math.max(0, plotTopPadding))
}

function plotHeight() {
    return Math.max(1, height - plotTop() - Math.max(0, plotBottomPadding))
}
```

Use:

```qml
var top = root.plotTop()
var plotHeight = root.plotHeight()
var yLine = top + (gridY / 2) * plotHeight
var yPixel = top + (1 - ((y - minY) / Math.max(0.001, maxY - minY))) * plotHeight
```

Keep x mapping unchanged so curve rows stay horizontally flush with the ruler.

- [x] **Step 2: Run structure test**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --continue-on-error --mixin release compile-commands ccache --packages-select data_recorder
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --ctest-args -R test_qml_structure --output-on-failure --event-handlers console_direct+
```

Expected: curve padding structure assertions pass.

### Task 7: Final Verification And Commit

**Files:**
- Verify all changed files.

- [x] **Step 1: Run QML lint**

Run:

```bash
qmllint -I qml/components qml/Main.qml qml/components/*.qml
```

Expected: no errors.

- [x] **Step 2: Run full package build and tests**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --continue-on-error --mixin release compile-commands ccache --packages-select data_recorder
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --event-handlers console_direct+
```

Expected: package builds and all `data_recorder` tests pass.

- [x] **Step 3: Review diff**

Run:

```bash
git -C src/data_recorder diff --stat
git -C src/data_recorder diff
```

Expected: changes are limited to the timeline viewport implementation, tests, and this plan.

- [x] **Step 4: Commit**

Run:

```bash
git -C src/data_recorder add docs/superpowers/plans/2026-06-17-data-recorder-timeline-viewport.md qml/components/TimelineViewport.qml qml/components/TimelinePanel.qml qml/components/TimelineRangeBar.qml qml/components/TimelineCurveRow.qml test/test_qml_structure.cpp test/test_qml_smoke.cpp
git -C src/data_recorder commit -m "Refactor timeline viewport interactions"
```

Expected: commit succeeds.
