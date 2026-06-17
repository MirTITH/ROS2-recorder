# Data Recorder UI Refinement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refine the Qt/QML UI so camera previews, splitters, panel chrome, and the Timeline behave consistently with the approved UI refinement spec.

**Architecture:** Keep the feature UI-only and local to QML components, with one small C++ static structure test that guards important QML contracts. Use focused QML components for reusable behavior: `ResizeHandle` for splitters, `CameraPreviewTile` for square camera content, `TimelineRangeBar` for the custom time-window control, and `TimelineCurveRow` for margin-free canvas curves.

**Tech Stack:** ROS 2 ament CMake package, C++17, GoogleTest, Qt 6, Qt Quick/QML, Qt Quick Controls, Canvas, SVG image assets.

---

## File Structure

- Modify `src/data_recorder/CMakeLists.txt`
  - Add a new static QML structure test target.
- Create `src/data_recorder/test/test_qml_structure.cpp`
  - Reads QML files as text and verifies structural contracts that are hard to assert through the smoke test.
- Modify `src/data_recorder/qml/Main.qml`
  - Add `ResizeHandle` handles to every `SplitView`.
- Modify `src/data_recorder/qml/components/ResizeHandle.qml`
  - Rename the public orientation API to `lineOrientation` and enlarge the invisible hit area.
- Modify `src/data_recorder/qml/components/Panel.qml`
  - Keep rounded corners only on the outer panel shell and make internal title chrome square.
- Modify `src/data_recorder/qml/components/CameraPreviewTile.qml`
  - Make tiles square-cornered and draw aspect-preserved simulated video content.
- Modify `src/data_recorder/qml/components/CameraGridPanel.qml`
  - Replace `GridView` with explicit `Repeater` geometry, centered partial rows, floating drag preview, and drop placeholder.
- Create `src/data_recorder/qml/assets/icons/eye.svg`
  - SVG icon for visible camera previews.
- Create `src/data_recorder/qml/assets/icons/eye-off.svg`
  - SVG icon for hidden camera previews.
- Modify `src/data_recorder/qml/components/TimelineInfoRow.qml`
  - Remove raw track-kind display and move the camera visibility control to a right-aligned second-line SVG icon button.
- Modify `src/data_recorder/qml/components/TimelinePanel.qml`
  - Add visible time-window state, adaptive ruler ticks, Shift + wheel playhead movement, and `TimelineRangeBar`.
- Modify `src/data_recorder/qml/components/TimelineCurveRow.qml`
  - Replace `ChartView` drawing with a full-rect `Canvas` so curves align exactly with the time ruler.
- Create `src/data_recorder/qml/components/TimelineRangeBar.qml`
  - Custom bottom range scrollbar with draggable thumb and resizable ends.

## Shared Commands

Run these from the workspace root unless a task says otherwise.

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws
```

Build:

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --continue-on-error --mixin release compile-commands ccache --packages-select data_recorder
```

Package tests:

```bash
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --event-handlers console_direct+
```

QML lint:

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder && qmllint -I qml/components qml/Main.qml qml/components/*.qml
```

Launch UI for manual verification:

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws && source ~/.local/ros2_rc && rs && ros2 run data_recorder data_recorder --ros-args -p config_path:=/home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder/config/example_config.yaml
```

---

### Task 1: Static QML Structure Test Harness

**Files:**
- Create: `src/data_recorder/test/test_qml_structure.cpp`
- Modify: `src/data_recorder/CMakeLists.txt`

- [ ] **Step 1: Write the failing static structure tests**

Create `src/data_recorder/test/test_qml_structure.cpp` with this content:

```cpp
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{

std::filesystem::path qml_dir()
{
  return std::filesystem::path(DATA_RECORDER_QML_DIR);
}

std::string read_text(const std::filesystem::path & path)
{
  std::ifstream input(path);
  if (!input.is_open()) {
    ADD_FAILURE() << "Failed to open " << path;
    return {};
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

std::size_t count_token(const std::string & text, const std::string & token)
{
  std::size_t count = 0;
  std::size_t position = 0;
  while ((position = text.find(token, position)) != std::string::npos) {
    ++count;
    position += token.size();
  }
  return count;
}

void expect_contains(const std::string & text, const std::string & token)
{
  EXPECT_NE(text.find(token), std::string::npos) << token;
}

void expect_not_contains(const std::string & text, const std::string & token)
{
  EXPECT_EQ(text.find(token), std::string::npos) << token;
}

}  // namespace

TEST(QmlStructure, EverySplitViewUsesCustomResizeHandle)
{
  const std::filesystem::path main_path = qml_dir() / "Main.qml";
  const std::filesystem::path timeline_path = qml_dir() / "components" / "TimelinePanel.qml";

  const std::string main_text = read_text(main_path);
  const std::string timeline_text = read_text(timeline_path);

  EXPECT_EQ(count_token(main_text, "SplitView {"), count_token(main_text, "handle: ResizeHandle"));
  EXPECT_EQ(
    count_token(timeline_text, "SplitView {"), count_token(timeline_text, "handle: ResizeHandle"));
  expect_contains(read_text(qml_dir() / "components" / "ResizeHandle.qml"), "property int lineOrientation");
}

TEST(QmlStructure, PanelChromeHasOnlyOuterRoundedCorners)
{
  const std::string panel_text = read_text(qml_dir() / "components" / "Panel.qml");

  EXPECT_EQ(count_token(panel_text, "radius:"), 1U);
  expect_contains(panel_text, "radius: 6");
}

TEST(QmlStructure, CameraPreviewTileIsSquareCornered)
{
  const std::string tile_text = read_text(qml_dir() / "components" / "CameraPreviewTile.qml");

  expect_not_contains(tile_text, "radius:");
}
```

- [ ] **Step 2: Add the test target to CMake**

In `src/data_recorder/CMakeLists.txt`, inside `if(BUILD_TESTING)`, after the `test_qml_smoke` target, add:

```cmake
  ament_add_gtest(test_qml_structure test/test_qml_structure.cpp)
  target_compile_definitions(test_qml_structure PRIVATE
    DATA_RECORDER_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
  )
```

- [ ] **Step 3: Build the new test target**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --continue-on-error --mixin release compile-commands ccache --packages-select data_recorder
```

Expected: build succeeds, because the new test only reads files at runtime.

- [ ] **Step 4: Run the new test and verify it fails**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --ctest-args -R test_qml_structure --output-on-failure --event-handlers console_direct+
```

Expected: `test_qml_structure` fails because `Main.qml` does not yet use custom handles for all `SplitView` instances, `ResizeHandle.qml` does not yet expose `lineOrientation`, `Panel.qml` has more than one `radius:`, and `CameraPreviewTile.qml` still has `radius: 3`.

---

### Task 2: Unified Splitter Handles And Panel Chrome

**Files:**
- Modify: `src/data_recorder/qml/components/ResizeHandle.qml`
- Modify: `src/data_recorder/qml/Main.qml`
- Modify: `src/data_recorder/qml/components/TimelinePanel.qml`
- Modify: `src/data_recorder/qml/components/Panel.qml`
- Modify: `src/data_recorder/qml/components/CameraPreviewTile.qml`
- Test: `src/data_recorder/test/test_qml_structure.cpp`

- [ ] **Step 1: Replace `ResizeHandle.qml` with the orientation-based handle**

Replace the full contents of `src/data_recorder/qml/components/ResizeHandle.qml` with:

```qml
import QtQuick 2.15

Item {
    id: root

    property int lineOrientation: Qt.Vertical
    readonly property bool isVerticalLine: lineOrientation === Qt.Vertical
    readonly property bool hovered: hoverHandler.hovered

    implicitWidth: isVerticalLine ? 8 : 1
    implicitHeight: isVerticalLine ? 1 : 8

    Rectangle {
        anchors.centerIn: parent
        width: root.isVerticalLine ? (root.hovered ? 3 : 1) : parent.width
        height: root.isVerticalLine ? parent.height : (root.hovered ? 3 : 1)
        color: root.hovered ? "#2563eb" : "#cbd5e1"
    }

    HoverHandler {
        id: hoverHandler
        cursorShape: root.isVerticalLine ? Qt.SizeHorCursor : Qt.SizeVerCursor
    }
}
```

- [ ] **Step 2: Add handles to every `SplitView` in `Main.qml`**

In `src/data_recorder/qml/Main.qml`, add these `handle` lines directly under each `orientation` line:

```qml
// For the main top/bottom split:
handle: ResizeHandle { lineOrientation: Qt.Horizontal }

// For the lower left/right split:
handle: ResizeHandle { lineOrientation: Qt.Vertical }

// For the left column sessions/tags split:
handle: ResizeHandle { lineOrientation: Qt.Horizontal }

// For the right column event-markers/timeline split:
handle: ResizeHandle { lineOrientation: Qt.Horizontal }
```

The resulting `SplitView` declarations in `Main.qml` must contain four `SplitView {` tokens and four `handle: ResizeHandle` tokens.

- [ ] **Step 3: Update the Timeline information-pane splitter handle**

In `src/data_recorder/qml/components/TimelinePanel.qml`, change:

```qml
handle: ResizeHandle { vertical: true }
```

to:

```qml
handle: ResizeHandle { lineOrientation: Qt.Vertical }
```

- [ ] **Step 4: Keep only the outer panel rounded corner**

In `src/data_recorder/qml/components/Panel.qml`, remove the `radius: 6` line from the title-bar `Rectangle`.

The only remaining radius declaration in `Panel.qml` must be:

```qml
radius: 6
```

on the root `Rectangle`.

- [ ] **Step 5: Make the camera preview tile square-cornered**

In `src/data_recorder/qml/components/CameraPreviewTile.qml`, remove:

```qml
radius: 3
```

Do not add another `radius:` declaration inside this file.

- [ ] **Step 6: Run the static structure test**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --ctest-args -R test_qml_structure --output-on-failure --event-handlers console_direct+
```

Expected: `test_qml_structure` passes the three tests added in Task 1.

- [ ] **Step 7: Run QML lint**

Run:

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder && qmllint -I qml/components qml/Main.qml qml/components/*.qml
```

Expected: command exits with code 0.

- [ ] **Step 8: Commit**

Run:

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add CMakeLists.txt test/test_qml_structure.cpp qml/Main.qml qml/components/ResizeHandle.qml qml/components/TimelinePanel.qml qml/components/Panel.qml qml/components/CameraPreviewTile.qml
git commit -m "Unify panel chrome and split handles"
```

---

### Task 3: Explicit Camera Grid Layout And Drag Preview

**Files:**
- Modify: `src/data_recorder/test/test_qml_structure.cpp`
- Modify: `src/data_recorder/qml/components/CameraGridPanel.qml`
- Modify: `src/data_recorder/qml/components/CameraPreviewTile.qml`

- [ ] **Step 1: Add a failing static test for the camera grid contract**

Append this test to `src/data_recorder/test/test_qml_structure.cpp`:

```cpp
TEST(QmlStructure, CameraGridUsesExplicitLayoutAndDragPreview)
{
  const std::string grid_text = read_text(qml_dir() / "components" / "CameraGridPanel.qml");

  expect_not_contains(grid_text, "GridView {");
  expect_contains(grid_text, "Repeater {");
  expect_contains(grid_text, "function chooseLayout");
  expect_contains(grid_text, "function layoutForIndex");
  expect_contains(grid_text, "function cameraAspectRatio");
  expect_contains(grid_text, "function updateDropInsertIndex");
  expect_contains(grid_text, "function commitDropInsertIndex");
  expect_contains(grid_text, "id: dropPlaceholder");
  expect_contains(grid_text, "id: floatingPreview");
}
```

- [ ] **Step 2: Run the new test and verify it fails**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --ctest-args -R test_qml_structure --output-on-failure --event-handlers console_direct+
```

Expected: `CameraGridUsesExplicitLayoutAndDragPreview` fails because `CameraGridPanel.qml` still uses `GridView` and has no floating preview or drop placeholder.

- [ ] **Step 3: Add drag and layout state to `CameraGridPanel.qml`**

Near the existing root properties in `src/data_recorder/qml/components/CameraGridPanel.qml`, keep `visualOrder`, replace the old drag-only fields with:

```qml
property string dragSourceKey: ""
property int dragSourceIndex: -1
property int dropInsertIndex: -1
property real dragX: 0
property real dragY: 0
property bool sourceRefreshActive: false
readonly property bool dragActive: dragSourceKey.length > 0
readonly property real tileGap: 4
```

- [ ] **Step 4: Replace old move-during-drag functions with insert-on-release functions**

Remove the existing functions `moveCamera`, `startDragKey`, `finishDragKey`, `moveDragTopicTo`, and `dragToPoint`.

Add these functions before the `ListModel` declarations:

```qml
function cameraAspectRatio(resolutionText) {
    var match = /^([0-9]+)x([0-9]+)$/.exec(String(resolutionText || ""))
    if (!match) {
        return 16 / 9
    }
    var widthValue = Number(match[1])
    var heightValue = Number(match[2])
    if (!isFinite(widthValue) || !isFinite(heightValue) || heightValue <= 0) {
        return 16 / 9
    }
    return Math.max(0.25, Math.min(4.0, widthValue / heightValue))
}

function chooseLayout(areaWidth, areaHeight, count) {
    var boundedCount = Math.max(1, count)
    var best = { columns: 1, rows: boundedCount, cellWidth: areaWidth, cellHeight: areaHeight / boundedCount }
    var bestScore = -1
    for (var columns = 1; columns <= boundedCount; ++columns) {
        var rows = Math.ceil(boundedCount / columns)
        var cellWidth = areaWidth / columns
        var cellHeight = areaHeight / rows
        var previewHeight = Math.max(1, cellHeight - 20)
        var previewAspect = cellWidth / previewHeight
        var aspectPenalty = Math.abs(Math.log(previewAspect / (16 / 9)))
        var usefulArea = cellWidth * previewHeight * boundedCount
        var emptyCells = rows * columns - boundedCount
        var score = usefulArea - emptyCells * cellWidth * previewHeight * 0.45 - aspectPenalty * 8000
        if (score > bestScore) {
            bestScore = score
            best = { columns: columns, rows: rows, cellWidth: cellWidth, cellHeight: cellHeight }
        }
    }
    return best
}

function layoutForIndex(itemIndex) {
    var count = Math.max(1, cameraProxy.count)
    var availableWidth = Math.max(1, previewArea.width)
    var availableHeight = Math.max(1, previewArea.height)
    var layout = chooseLayout(availableWidth, availableHeight, count)
    var row = Math.floor(itemIndex / layout.columns)
    var column = itemIndex % layout.columns
    var itemsInRow = Math.min(layout.columns, count - row * layout.columns)
    var rowWidth = itemsInRow * layout.cellWidth
    var rowOffsetX = Math.max(0, (availableWidth - rowWidth) / 2)
    return {
        x: rowOffsetX + column * layout.cellWidth + root.tileGap / 2,
        y: row * layout.cellHeight + root.tileGap / 2,
        width: Math.max(1, layout.cellWidth - root.tileGap),
        height: Math.max(1, layout.cellHeight - root.tileGap)
    }
}

function insertIndexAtPoint(xPosition, yPosition) {
    var count = cameraProxy.count
    if (count <= 0) {
        return 0
    }
    var bestIndex = count
    var bestDistance = Number.MAX_VALUE
    for (var index = 0; index < count; ++index) {
        var itemLayout = layoutForIndex(index)
        var centerX = itemLayout.x + itemLayout.width / 2
        var centerY = itemLayout.y + itemLayout.height / 2
        var distance = Math.pow(centerX - xPosition, 2) + Math.pow(centerY - yPosition, 2)
        if (distance < bestDistance) {
            bestDistance = distance
            bestIndex = xPosition < centerX ? index : index + 1
        }
    }
    return Math.max(0, Math.min(count, bestIndex))
}

function targetIndexAfterRemovingSource() {
    if (dropInsertIndex < 0) {
        return -1
    }
    var targetIndex = dropInsertIndex
    if (dragSourceIndex >= 0 && targetIndex > dragSourceIndex) {
        targetIndex -= 1
    }
    return Math.max(0, Math.min(Math.max(0, cameraProxy.count - 1), targetIndex))
}

function placeholderLayout() {
    if (!root.dragActive || root.dropInsertIndex < 0) {
        return { x: 0, y: 0, width: 0, height: 0 }
    }
    return layoutForIndex(targetIndexAfterRemovingSource())
}

function startDrag(sourceKey, sourceIndex, xPosition, yPosition) {
    dragSourceKey = sourceKey
    dragSourceIndex = sourceIndex
    dragX = xPosition
    dragY = yPosition
    updateDropInsertIndex(xPosition, yPosition)
}

function updateDropInsertIndex(xPosition, yPosition) {
    dragX = xPosition
    dragY = yPosition
    dropInsertIndex = insertIndexAtPoint(xPosition, yPosition)
}

function commitDropInsertIndex() {
    if (!root.dragActive || dragSourceIndex < 0 || dropInsertIndex < 0) {
        finishDrag()
        return
    }
    var movedKey = dragSourceKey
    var nextOrder = visualOrder.slice()
    var fromOrderIndex = nextOrder.indexOf(movedKey)
    if (fromOrderIndex >= 0) {
        nextOrder.splice(fromOrderIndex, 1)
        var visibleKeys = []
        for (var proxyIndex = 0; proxyIndex < cameraProxy.count; ++proxyIndex) {
            var proxyKey = cameraProxy.get(proxyIndex).sourceKey
            if (proxyKey !== movedKey) {
                visibleKeys.push(proxyKey)
            }
        }
        var boundedInsert = Math.max(0, Math.min(
            dropInsertIndex > dragSourceIndex ? dropInsertIndex - 1 : dropInsertIndex,
            visibleKeys.length))
        var anchorKey = boundedInsert < visibleKeys.length ? visibleKeys[boundedInsert] : ""
        var orderInsertIndex = anchorKey.length > 0 ? nextOrder.indexOf(anchorKey) : nextOrder.length
        if (orderInsertIndex < 0) {
            orderInsertIndex = nextOrder.length
        }
        nextOrder.splice(orderInsertIndex, 0, movedKey)
        visualOrder = nextOrder
        rebuildVisibleCameras()
    }
    finishDrag()
}

function finishDrag() {
    dragSourceKey = ""
    dragSourceIndex = -1
    dropInsertIndex = -1
}
```

- [ ] **Step 5: Replace the camera grid view with explicit `Item` plus `Repeater`**

In `CameraGridPanel.qml`, replace the existing `GridView` block that starts with `GridView {` and
ends just before the final closing brace of the root `Panel` with:

```qml
Item {
    id: previewArea

    anchors.fill: parent
    anchors.margins: 4
    clip: true

    Repeater {
        model: cameraProxy

        delegate: Item {
            id: cameraCell

            required property int index

            readonly property var cellLayout: root.layoutForIndex(index)

            x: cellLayout.x
            y: cellLayout.y
            width: cellLayout.width
            height: cellLayout.height
            visible: root.dragSourceKey !== model.sourceKey

            CameraPreviewTile {
                anchors.fill: parent
                topicName: model.topicName
                resolutionText: model.resolutionText
                seriesColor: model.seriesColor
            }

            MouseArea {
                anchors.fill: parent
                preventStealing: true

                onPressed: function(mouse) {
                    var point = cameraCell.mapToItem(previewArea, mouse.x, mouse.y)
                    root.startDrag(model.sourceKey, index, point.x, point.y)
                }

                onPositionChanged: function(mouse) {
                    if (pressed) {
                        var point = cameraCell.mapToItem(previewArea, mouse.x, mouse.y)
                        root.updateDropInsertIndex(point.x, point.y)
                    }
                }

                onReleased: root.commitDropInsertIndex()
                onCanceled: root.finishDrag()
            }
        }
    }

    Rectangle {
        id: dropPlaceholder

        readonly property var targetLayout: root.placeholderLayout()

        visible: root.dragActive
        x: targetLayout.x
        y: targetLayout.y
        width: targetLayout.width
        height: targetLayout.height
        color: "transparent"
        border.color: "#2563eb"
        border.width: 2
        opacity: 0.85
    }

    CameraPreviewTile {
        id: floatingPreview

        visible: root.dragActive && root.dragSourceIndex >= 0
        width: root.dragSourceIndex >= 0 ? root.layoutForIndex(root.dragSourceIndex).width : 1
        height: root.dragSourceIndex >= 0 ? root.layoutForIndex(root.dragSourceIndex).height : 1
        x: Math.max(0, Math.min(previewArea.width - width, root.dragX - width / 2))
        y: Math.max(0, Math.min(previewArea.height - height, root.dragY - height / 2))
        z: 10
        opacity: 0.92
        topicName: root.dragSourceIndex >= 0 ? cameraProxy.get(root.dragSourceIndex).topicName : ""
        resolutionText: root.dragSourceIndex >= 0 ? cameraProxy.get(root.dragSourceIndex).resolutionText : ""
        seriesColor: root.dragSourceIndex >= 0 ? cameraProxy.get(root.dragSourceIndex).seriesColor : "#2563eb"
        dragActive: true
    }
}
```

- [ ] **Step 6: Preserve video aspect ratio inside `CameraPreviewTile.qml`**

In `CameraPreviewTile.qml`, add this function near the root properties:

```qml
function videoRect(widthValue, heightValue) {
    var match = /^([0-9]+)x([0-9]+)$/.exec(String(resolutionText || ""))
    var aspect = 16 / 9
    if (match) {
        var parsedWidth = Number(match[1])
        var parsedHeight = Number(match[2])
        if (isFinite(parsedWidth) && isFinite(parsedHeight) && parsedHeight > 0) {
            aspect = parsedWidth / parsedHeight
        }
    }
    var targetWidth = widthValue
    var targetHeight = targetWidth / aspect
    if (targetHeight > heightValue) {
        targetHeight = heightValue
        targetWidth = targetHeight * aspect
    }
    return {
        x: (widthValue - targetWidth) / 2,
        y: (heightValue - targetHeight) / 2,
        width: targetWidth,
        height: targetHeight
    }
}
```

In the `Canvas.onPaint` handler, draw the grid and simulated camera frame inside:

```qml
var rect = root.videoRect(width, height)
ctx.fillStyle = "#020617"
ctx.fillRect(0, 0, width, height)
ctx.save()
ctx.beginPath()
ctx.rect(rect.x, rect.y, rect.width, rect.height)
ctx.clip()
ctx.fillStyle = "#111827"
ctx.fillRect(rect.x, rect.y, rect.width, rect.height)
```

Then change all uses of `width` and `height` in the simulated drawing coordinates to `rect.x + rect.width * ratio` and `rect.y + rect.height * ratio`. Finish the clipped drawing with:

```qml
ctx.restore()
```

- [ ] **Step 7: Run the static structure test**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --ctest-args -R test_qml_structure --output-on-failure --event-handlers console_direct+
```

Expected: `CameraGridUsesExplicitLayoutAndDragPreview` passes.

- [ ] **Step 8: Run QML lint**

Run:

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder && qmllint -I qml/components qml/Main.qml qml/components/*.qml
```

Expected: command exits with code 0.

- [ ] **Step 9: Commit**

Run:

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add test/test_qml_structure.cpp qml/components/CameraGridPanel.qml qml/components/CameraPreviewTile.qml
git commit -m "Refine camera preview layout and drag"
```

---

### Task 4: Timeline Information Pane Eye Icon

**Files:**
- Modify: `src/data_recorder/test/test_qml_structure.cpp`
- Create: `src/data_recorder/qml/assets/icons/eye.svg`
- Create: `src/data_recorder/qml/assets/icons/eye-off.svg`
- Modify: `src/data_recorder/qml/components/TimelineInfoRow.qml`
- Modify: `src/data_recorder/qml/components/TimelinePanel.qml`

- [ ] **Step 1: Add a failing static test for the Timeline information row**

Append this test to `src/data_recorder/test/test_qml_structure.cpp`:

```cpp
TEST(QmlStructure, TimelineInfoUsesEyeSvgAndOmitsTrackKindText)
{
  const std::string row_text = read_text(qml_dir() / "components" / "TimelineInfoRow.qml");

  expect_not_contains(row_text, "root.trackKind");
  expect_not_contains(row_text, "property string trackKind");
  expect_contains(row_text, "../assets/icons/eye.svg");
  expect_contains(row_text, "../assets/icons/eye-off.svg");
  expect_contains(row_text, "id: cameraVisibilityButton");

  EXPECT_TRUE(std::filesystem::exists(qml_dir() / "assets" / "icons" / "eye.svg"));
  EXPECT_TRUE(std::filesystem::exists(qml_dir() / "assets" / "icons" / "eye-off.svg"));
}
```

- [ ] **Step 2: Run the test and verify it fails**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --ctest-args -R test_qml_structure --output-on-failure --event-handlers console_direct+
```

Expected: `TimelineInfoUsesEyeSvgAndOmitsTrackKindText` fails because `TimelineInfoRow.qml` still displays `root.trackKind` and the SVG icons do not exist.

- [ ] **Step 3: Add the eye SVG assets**

Create `src/data_recorder/qml/assets/icons/eye.svg`:

```xml
<svg xmlns="http://www.w3.org/2000/svg" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="#334155" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7S2 12 2 12Z"/>
  <circle cx="12" cy="12" r="3"/>
</svg>
```

Create `src/data_recorder/qml/assets/icons/eye-off.svg`:

```xml
<svg xmlns="http://www.w3.org/2000/svg" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="#64748b" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="m3 3 18 18"/>
  <path d="M10.6 10.6A3 3 0 0 0 12 15a3 3 0 0 0 2.4-4.8"/>
  <path d="M9.9 4.4A10.3 10.3 0 0 1 12 4c6.5 0 10 8 10 8a17.8 17.8 0 0 1-3.2 4.5"/>
  <path d="M6.1 6.1C3.5 7.9 2 12 2 12a17.6 17.6 0 0 0 6.4 6.1A9.5 9.5 0 0 0 12 19c1.1 0 2.1-.2 3-.5"/>
</svg>
```

- [ ] **Step 4: Rewrite `TimelineInfoRow.qml` layout**

In `TimelineInfoRow.qml`, remove:

```qml
property string trackKind: "empty"
```

Replace the root `RowLayout` with this `ColumnLayout`:

```qml
ColumnLayout {
    anchors.fill: parent
    anchors.leftMargin: 8
    anchors.rightMargin: 6
    anchors.topMargin: 5
    anchors.bottomMargin: 5
    spacing: 2

    Label {
        Layout.fillWidth: true
        text: root.topicName
        color: "#111827"
        font.pixelSize: 11
        font.bold: true
        elide: Text.ElideMiddle
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        Label {
            Layout.fillWidth: true
            text: root.frequencyText + " · " + root.backendName
            color: "#64748b"
            font.pixelSize: 10
            elide: Text.ElideRight
        }

        Button {
            id: cameraVisibilityButton

            visible: root.isCamera
            Layout.preferredWidth: 24
            Layout.preferredHeight: 20
            padding: 0
            Accessible.name: root.isVisible ? "隐藏相机预览" : "显示相机预览"
            ToolTip.visible: hovered
            ToolTip.text: root.isVisible ? "隐藏相机预览" : "显示相机预览"
            onClicked: root.toggleVisibleRequested()

            background: Rectangle {
                color: cameraVisibilityButton.hovered ? "#e2e8f0" : "transparent"
                border.width: 0
            }

            contentItem: Image {
                anchors.centerIn: parent
                width: 16
                height: 16
                source: root.isVisible ? "../assets/icons/eye.svg" : "../assets/icons/eye-off.svg"
                fillMode: Image.PreserveAspectFit
            }
        }
    }
}
```

- [ ] **Step 5: Stop passing `trackKind` into `TimelineInfoRow`**

In `TimelinePanel.qml`, inside the `TimelineInfoRow` delegate, remove:

```qml
trackKind: model.trackKind
```

- [ ] **Step 6: Run the static structure test**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --ctest-args -R test_qml_structure --output-on-failure --event-handlers console_direct+
```

Expected: `TimelineInfoUsesEyeSvgAndOmitsTrackKindText` passes.

- [ ] **Step 7: Run QML lint**

Run:

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder && qmllint -I qml/components qml/Main.qml qml/components/*.qml
```

Expected: command exits with code 0.

- [ ] **Step 8: Commit**

Run:

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add test/test_qml_structure.cpp qml/assets/icons/eye.svg qml/assets/icons/eye-off.svg qml/components/TimelineInfoRow.qml qml/components/TimelinePanel.qml
git commit -m "Use eye icons in timeline info rows"
```

---

### Task 5: Margin-Free Curves, Adaptive Ruler, And Timeline Range Bar

**Files:**
- Modify: `src/data_recorder/test/test_qml_structure.cpp`
- Create: `src/data_recorder/qml/components/TimelineRangeBar.qml`
- Modify: `src/data_recorder/qml/components/TimelinePanel.qml`
- Modify: `src/data_recorder/qml/components/TimelineCurveRow.qml`

- [ ] **Step 1: Add a failing static test for the curve area contract**

Append this test to `src/data_recorder/test/test_qml_structure.cpp`:

```cpp
TEST(QmlStructure, TimelineCurveAreaHasAdaptiveWindowAndRangeBar)
{
  const std::string panel_text = read_text(qml_dir() / "components" / "TimelinePanel.qml");
  const std::string curve_text = read_text(qml_dir() / "components" / "TimelineCurveRow.qml");

  expect_contains(panel_text, "property real visibleStartSeconds");
  expect_contains(panel_text, "property real visibleDurationSeconds");
  expect_contains(panel_text, "function niceTickInterval");
  expect_contains(panel_text, "function formatTickLabel");
  expect_contains(panel_text, "function nudgePlayhead");
  expect_contains(panel_text, "TimelineRangeBar {");
  expect_contains(panel_text, "wheel.modifiers & Qt.ShiftModifier");
  expect_not_contains(panel_text, "Math.floor(root.effectiveDurationSeconds / 5) + 1");

  expect_not_contains(curve_text, "ChartView");
  expect_contains(curve_text, "Canvas {");
  expect_contains(curve_text, "property real visibleStartSeconds");
  expect_contains(curve_text, "property real visibleDurationSeconds");

  EXPECT_TRUE(std::filesystem::exists(qml_dir() / "components" / "TimelineRangeBar.qml"));
}
```

- [ ] **Step 2: Run the test and verify it fails**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --ctest-args -R test_qml_structure --output-on-failure --event-handlers console_direct+
```

Expected: `TimelineCurveAreaHasAdaptiveWindowAndRangeBar` fails because the Timeline still uses a fixed 5-second ruler, `TimelineCurveRow.qml` still uses `ChartView`, and `TimelineRangeBar.qml` does not exist.

- [ ] **Step 3: Create `TimelineRangeBar.qml`**

Create `src/data_recorder/qml/components/TimelineRangeBar.qml`:

```qml
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
            drag.target: thumb
            drag.axis: Drag.XAxis
            drag.minimumX: 0
            drag.maximumX: Math.max(0, track.width - thumb.width)
            onPositionChanged: {
                if (pressed) {
                    root.requestWindow((thumb.x / Math.max(1, track.width)) * root.boundedTotal, root.boundedDuration)
                }
            }
            onReleased: root.requestWindow((thumb.x / Math.max(1, track.width)) * root.boundedTotal, root.boundedDuration)
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
```

- [ ] **Step 4: Replace `TimelineCurveRow.qml` with Canvas drawing**

Replace the full contents of `src/data_recorder/qml/components/TimelineCurveRow.qml` with:

```qml
import QtQuick 2.15

Rectangle {
    id: root

    property string trackKind: "empty"
    property var seriesList: []
    property real xMax: 80
    property real visibleStartSeconds: 0
    property real visibleDurationSeconds: 80

    height: 48
    color: trackKind === "empty" ? "#f8fafc" : "#ffffff"

    function boundedDuration() {
        return Math.max(0.001, Number(visibleDurationSeconds) || 1)
    }

    function seriesColor(value) {
        var text = String(value || "")
        return /^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.test(text) ? text : "#2563eb"
    }

    Canvas {
        id: curveCanvas

        anchors.fill: parent
        visible: root.trackKind === "numeric"
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.fillStyle = "#ffffff"
            ctx.fillRect(0, 0, width, height)

            var entries = root.seriesList || []
            var minY = -1
            var maxY = 1
            for (var seriesIndex = 0; seriesIndex < entries.length; ++seriesIndex) {
                var points = (entries[seriesIndex] || {}).points || []
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

            ctx.strokeStyle = "#e2e8f0"
            ctx.lineWidth = 1
            for (var gridY = 0; gridY <= 2; ++gridY) {
                var yLine = (gridY / 2) * height
                ctx.beginPath()
                ctx.moveTo(0, yLine)
                ctx.lineTo(width, yLine)
                ctx.stroke()
            }

            for (var drawSeriesIndex = 0; drawSeriesIndex < entries.length; ++drawSeriesIndex) {
                var entry = entries[drawSeriesIndex] || {}
                var drawPoints = entry.points || []
                var started = false
                ctx.beginPath()
                ctx.strokeStyle = root.seriesColor(entry.color)
                ctx.lineWidth = 1.5
                for (var drawPointIndex = 0; drawPointIndex < drawPoints.length; ++drawPointIndex) {
                    var point = drawPoints[drawPointIndex]
                    var xValue = Number(point.x)
                    var y = Number(point.y)
                    if (!isFinite(xValue) || !isFinite(y)) {
                        continue
                    }
                    if (xValue < root.visibleStartSeconds || xValue > root.visibleStartSeconds + root.boundedDuration()) {
                        continue
                    }
                    var x = ((xValue - root.visibleStartSeconds) / root.boundedDuration()) * width
                    var yPixel = height - ((y - minY) / Math.max(0.001, maxY - minY)) * height
                    if (!started) {
                        ctx.moveTo(x, yPixel)
                        started = true
                    } else {
                        ctx.lineTo(x, yPixel)
                    }
                }
                if (started) {
                    ctx.stroke()
                }
            }
        }
    }

    onTrackKindChanged: curveCanvas.requestPaint()
    onSeriesListChanged: curveCanvas.requestPaint()
    onVisibleStartSecondsChanged: curveCanvas.requestPaint()
    onVisibleDurationSecondsChanged: curveCanvas.requestPaint()

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: "#e2e8f0"
    }
}
```

- [ ] **Step 5: Add visible-window helpers to `TimelinePanel.qml`**

Near the existing root properties in `TimelinePanel.qml`, replace `property real timeScale: 1.0` with:

```qml
property real visibleStartSeconds: 0
property real visibleDurationSeconds: 80
```

Add these helper functions before `timeString(seconds)`:

```qml
function clamp(value, low, high) {
    return Math.max(low, Math.min(high, value))
}

function boundedVisibleDuration() {
    return Math.max(0.001, Math.min(effectiveDurationSeconds, Number(visibleDurationSeconds) || effectiveDurationSeconds))
}

function visibleEndSeconds() {
    return Math.min(effectiveDurationSeconds, visibleStartSeconds + boundedVisibleDuration())
}

function setVisibleWindow(startSeconds, durationSeconds) {
    var duration = Math.max(0.001, Math.min(effectiveDurationSeconds, Number(durationSeconds) || effectiveDurationSeconds))
    visibleDurationSeconds = duration
    visibleStartSeconds = clamp(Number(startSeconds) || 0, 0, Math.max(0, effectiveDurationSeconds - duration))
}

function playheadX(widthValue) {
    return clamp(((playheadSeconds - visibleStartSeconds) / boundedVisibleDuration()) * widthValue, 0, widthValue)
}

function seekFromCurveX(xPosition) {
    var seconds = visibleStartSeconds + (xPosition / Math.max(1, curveViewport.width)) * boundedVisibleDuration()
    if (controller && controller.setPlayheadSeconds) {
        controller.setPlayheadSeconds(clamp(seconds, 0, effectiveDurationSeconds))
    }
}

function zoomVisibleWindow(deltaY, anchorX, widthValue) {
    var oldDuration = boundedVisibleDuration()
    var factor = deltaY > 0 ? 0.86 : 1.16
    var newDuration = clamp(oldDuration * factor, 0.05, effectiveDurationSeconds)
    var anchorRatio = clamp(anchorX / Math.max(1, widthValue), 0, 1)
    var anchorTime = visibleStartSeconds + oldDuration * anchorRatio
    setVisibleWindow(anchorTime - newDuration * anchorRatio, newDuration)
}

function nudgePlayhead(deltaY) {
    var step = boundedVisibleDuration() / 40
    var direction = deltaY > 0 ? 1 : -1
    if (controller && controller.setPlayheadSeconds) {
        controller.setPlayheadSeconds(clamp(playheadSeconds + direction * step, 0, effectiveDurationSeconds))
    }
}

function niceTickInterval(widthValue) {
    var intervals = [0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 30, 60, 120, 300]
    var targetPixels = 110
    var rawInterval = boundedVisibleDuration() / Math.max(1, widthValue / targetPixels)
    for (var index = 0; index < intervals.length; ++index) {
        if (intervals[index] >= rawInterval) {
            return intervals[index]
        }
    }
    return intervals[intervals.length - 1]
}

function firstTick(interval) {
    return Math.ceil(visibleStartSeconds / interval) * interval
}

function tickCount(widthValue) {
    var interval = niceTickInterval(widthValue)
    return Math.max(1, Math.floor((visibleEndSeconds() - firstTick(interval)) / interval) + 1)
}

function tickTimeAt(index, widthValue) {
    var interval = niceTickInterval(widthValue)
    return firstTick(interval) + index * interval
}

function formatTickLabel(seconds) {
    if (boundedVisibleDuration() < 2) {
        return Math.round(seconds * 1000) + "ms"
    }
    if (boundedVisibleDuration() < 90) {
        return seconds.toFixed(seconds < 10 ? 1 : 0) + "s"
    }
    var totalSeconds = Math.floor(seconds)
    var minutes = Math.floor(totalSeconds / 60)
    var remainder = totalSeconds % 60
    return minutes + ":" + remainder.toString().padStart(2, "0")
}
```

Remove the old functions `boundedTimeScale()` and `curveDurationSeconds()` after replacing them.

- [ ] **Step 6: Replace the fixed 5-second ruler with adaptive ticks**

In `TimelinePanel.qml`, inside the `ruler` `Rectangle`, replace the existing `Repeater` with:

```qml
Repeater {
    model: root.tickCount(ruler.width)

    delegate: Item {
        required property int index

        readonly property real tickTime: root.tickTimeAt(index, ruler.width)

        x: ((tickTime - root.visibleStartSeconds) / root.boundedVisibleDuration()) * ruler.width
        width: 1
        height: ruler.height

        Rectangle {
            width: 1
            height: 9
            color: "#94a3b8"
        }

        Label {
            anchors.top: parent.top
            anchors.topMargin: 11
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.formatTickLabel(tickTime)
            color: "#64748b"
            font.pixelSize: 10
        }
    }
}
```

- [ ] **Step 7: Wire visible-window state into curve rows and wheel behavior**

In the `TimelineCurveRow` delegate inside `TimelinePanel.qml`, replace:

```qml
timeScale: root.timeScale
xMax: root.effectiveDurationSeconds
```

with:

```qml
xMax: root.effectiveDurationSeconds
visibleStartSeconds: root.visibleStartSeconds
visibleDurationSeconds: root.boundedVisibleDuration()
```

In the curve-area `MouseArea.onWheel`, replace the body with:

```qml
if (wheel.modifiers & Qt.ShiftModifier) {
    root.nudgePlayhead(wheel.angleDelta.y)
} else {
    root.zoomVisibleWindow(wheel.angleDelta.y, wheel.x, curveViewport.width)
}
wheel.accepted = true
```

- [ ] **Step 8: Add the bottom range bar**

In `TimelinePanel.qml`, wrap the existing `curveViewport` item and the new range bar in the right-side `ColumnLayout` by adding this after `curveViewport`:

```qml
TimelineRangeBar {
    Layout.fillWidth: true
    totalDurationSeconds: root.effectiveDurationSeconds
    visibleStartSeconds: root.visibleStartSeconds
    visibleDurationSeconds: root.boundedVisibleDuration()
    onWindowRequested: function(startSeconds, durationSeconds) {
        root.setVisibleWindow(startSeconds, durationSeconds)
    }
}
```

Keep `curveViewport` above the range bar with `Layout.fillHeight: true`.

- [ ] **Step 9: Initialize the visible window when the component loads**

Replace:

```qml
Component.onCompleted: listsReady = true
```

with:

```qml
Component.onCompleted: {
    root.setVisibleWindow(0, root.effectiveDurationSeconds)
    listsReady = true
}
```

- [ ] **Step 10: Run the static structure test**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --ctest-args -R test_qml_structure --output-on-failure --event-handlers console_direct+
```

Expected: `TimelineCurveAreaHasAdaptiveWindowAndRangeBar` passes.

- [ ] **Step 11: Run QML lint**

Run:

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder && qmllint -I qml/components qml/Main.qml qml/components/*.qml
```

Expected: command exits with code 0.

- [ ] **Step 12: Commit**

Run:

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add test/test_qml_structure.cpp qml/components/TimelineRangeBar.qml qml/components/TimelinePanel.qml qml/components/TimelineCurveRow.qml
git commit -m "Refine timeline ruler and curve controls"
```

---

### Task 6: Full Verification And Visual Check

**Files:**
- Verify: `src/data_recorder/qml/Main.qml`
- Verify: `src/data_recorder/qml/components/*.qml`
- Verify: `src/data_recorder/test/*.cpp`

- [ ] **Step 1: Run QML lint**

Run:

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder && qmllint -I qml/components qml/Main.qml qml/components/*.qml
```

Expected: command exits with code 0.

- [ ] **Step 2: Build the package**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --continue-on-error --mixin release compile-commands ccache --packages-select data_recorder
```

Expected: package `data_recorder` builds successfully.

- [ ] **Step 3: Run package tests**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --event-handlers console_direct+
```

Expected: all `data_recorder` tests pass, including `test_qml_structure` and `test_qml_smoke`.

- [ ] **Step 4: Launch the UI**

Run:

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws && source ~/.local/ros2_rc && rs && ros2 run data_recorder data_recorder --ros-args -p config_path:=/home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder/config/example_config.yaml
```

Expected: the Qt window opens with the example config.

- [ ] **Step 5: Verify splitter behavior**

Manual checks:

- Drag the horizontal splitter between the camera preview area and lower workspace.
- Drag the vertical splitter between the left column and right workspace.
- Drag the splitters inside the left and right columns.
- Drag the Timeline information-pane splitter.
- Confirm every handle is a 1 px gray line at rest, turns blue and thicker on hover, and shows the correct resize cursor.

- [ ] **Step 6: Verify camera preview behavior**

Manual checks:

- Resize the camera preview area vertically.
- Confirm the simulated video content keeps its aspect ratio.
- Confirm camera tiles are square-cornered.
- Hide or show camera previews from the Timeline eye icons.
- Confirm one, two, and three visible cameras are centered instead of leaving abrupt right-side empty grid cells.
- Drag the leftmost visible camera to the far right.
- Confirm the dragged tile follows the mouse and the drop placeholder shows the release position.

- [ ] **Step 7: Verify Timeline behavior**

Manual checks:

- Confirm the ruler appears only above the curve area, not above the Timeline information pane.
- Confirm curve drawing starts at the same left x-position as the ruler and ends at the same right x-position.
- Confirm raw `numeric`, `empty`, and `camera` text is not visible in topic rows.
- Confirm camera rows show a right-aligned eye icon on the second line.
- Wheel over the curve area without modifiers and confirm the visible time window zooms.
- Confirm ruler labels and tick spacing change as zoom changes.
- Shift + wheel over the curve area and confirm the playhead moves horizontally.
- Drag the bottom range bar thumb and confirm the visible time window pans.
- Drag both ends of the range bar thumb and confirm the visible duration changes.

- [ ] **Step 8: Check final git state**

Run:

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git status --short
```

Expected: only intentionally untracked reference files remain, or the worktree is clean if those files were already handled outside this plan.
