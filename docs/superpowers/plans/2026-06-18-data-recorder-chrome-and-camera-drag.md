# Data Recorder Chrome And Camera Drag Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the top header, move recording state into the status bar, and make camera preview drag show insertion by shifting neighboring tiles before drop.

**Architecture:** Keep the UI-only change inside QML and QML structure/smoke tests. `Main.qml` owns the high-level chrome layout, `StatusBar.qml` owns the record button and status text, and `CameraGridPanel.qml` owns transient drag preview ordering while keeping `visualOrder` committed only on drop.

**Tech Stack:** ROS 2 `ament_cmake` package, Qt 6 QML, C++ gtest structure/smoke tests, `qmllint`, `colcon`.

---

### File Structure

**Modify:**

- `src/data_recorder/test/test_qml_structure.cpp`: add structure tests for header removal, status bar recording controls, and camera drag preview order helpers.
- `src/data_recorder/qml/Main.qml`: remove the `AppHeader` child from the root `ColumnLayout`.
- `src/data_recorder/qml/components/StatusBar.qml`: move record button and status text here, remove output directory text.
- `src/data_recorder/qml/components/CameraGridPanel.qml`: add transient preview-order helpers and use them for tile and placeholder layout during drag.

**Delete:**

- `src/data_recorder/qml/components/AppHeader.qml`: no longer part of the UI.

**Do not modify:**

- `install/data_recorder/...`: generated install tree.
- `src/data_recorder/src/app_controller.cpp` and `include/data_recorder/app_controller.hpp`: controller behavior is unchanged.

---

### Task 1: Structure Tests For Chrome And Camera Drag Contract

**Files:**

- Modify: `src/data_recorder/test/test_qml_structure.cpp`

- [ ] **Step 1: Add failing structure tests**

Add this test after `MainLayoutUsesFlushLeftWorkspace`:

```cpp
TEST(QmlStructure, AppChromeUsesStatusBarForRecording)
{
  const std::string main_text = read_text(qml_dir() / "Main.qml");
  const std::string status_text = read_text(qml_dir() / "components" / "StatusBar.qml");

  expect_not_contains(main_text, "AppHeader");
  EXPECT_FALSE(std::filesystem::exists(qml_dir() / "components" / "AppHeader.qml"));
  expect_contains(status_text, "implicitHeight: 32");
  expect_contains(status_text, "objectName: \"recordButton\"");
  expect_contains(status_text, "root.controller.toggleRecording()");
  expect_contains(status_text, "readonly property bool isRecording");
  expect_contains(status_text, "readonly property string statusText");
  expect_contains(status_text, "text: root.statusText");
  expect_contains(status_text, "text: \"磁盘 --\"");
  expect_not_contains(status_text, "outputDirectory");
  expect_not_contains(status_text, "保存目录");
}
```

Replace the existing `CameraGridUsesExplicitLayoutAndDragPreview` test with:

```cpp
TEST(QmlStructure, CameraGridUsesExplicitLayoutAndDragPreview)
{
  const std::string grid_text = read_text(qml_dir() / "components" / "CameraGridPanel.qml");

  expect_not_contains(grid_text, "GridView {");
  expect_contains(grid_text, "Repeater {");
  expect_contains(grid_text, "readonly property string placeholderSourceKey");
  expect_contains(grid_text, "function chooseLayout");
  expect_contains(grid_text, "function layoutForIndex");
  expect_contains(grid_text, "function previewSequence");
  expect_contains(grid_text, "function previewLayoutForKey");
  expect_contains(grid_text, "function placeholderLayout");
  expect_contains(grid_text, "function floatingPreviewLayout");
  expect_contains(grid_text, "root.previewLayoutForKey(cameraCell.sourceKey, cameraCell.index)");
  expect_contains(grid_text, "sourceKey: root.placeholderSourceKey");
  expect_contains(grid_text, "root.previewSequence()");
  expect_contains(grid_text, "function updateDropInsertIndex");
  expect_contains(grid_text, "function commitDropInsertIndex");
  expect_contains(grid_text, "id: dropPlaceholder");
  expect_contains(grid_text, "id: floatingPreview");
}
```

- [ ] **Step 2: Build and run structure test to verify RED**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --continue-on-error --mixin release compile-commands ccache --packages-select data_recorder
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --event-handlers console_direct+ --ctest-args -R test_qml_structure --output-on-failure
```

Expected: `test_qml_structure` fails because `AppHeader.qml` still exists, `Main.qml` still
instantiates `AppHeader`, `StatusBar.qml` still contains `outputDirectory`, and the camera grid
preview-order helpers do not exist yet.

- [ ] **Step 3: Commit RED test**

Run:

```bash
git -C /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder add test/test_qml_structure.cpp
git -C /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder commit -m "test: specify chrome and camera drag behavior"
```

---

### Task 2: Move Recording Chrome Into StatusBar

**Files:**

- Modify: `src/data_recorder/qml/Main.qml`
- Modify: `src/data_recorder/qml/components/StatusBar.qml`
- Delete: `src/data_recorder/qml/components/AppHeader.qml`
- Test: `src/data_recorder/test/test_qml_structure.cpp`

- [ ] **Step 1: Remove AppHeader from Main.qml**

In `Main.qml`, delete this block from the root `ColumnLayout`:

```qml
AppHeader {
    Layout.fillWidth: true
    controller: appController
}
```

- [ ] **Step 2: Replace StatusBar.qml content**

Replace the entire contents of `src/data_recorder/qml/components/StatusBar.qml` with:

```qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root

    property var controller
    readonly property bool isRecording: !!controller && controller.recording
    readonly property string statusText: controller && controller.statusText ? controller.statusText : "就绪"

    implicitHeight: 32
    color: "#ffffff"
    border.color: "#d5dce8"
    border.width: 1

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 12
        spacing: 10

        Button {
            objectName: "recordButton"
            Layout.preferredWidth: 64
            Layout.preferredHeight: 24
            text: root.isRecording ? "停止" : "录制"
            enabled: !!root.controller
            onClicked: root.controller.toggleRecording()
        }

        Label {
            text: root.statusText
            color: root.isRecording ? "#dc2626" : "#166534"
            font.pixelSize: 11
            font.bold: true
            elide: Text.ElideRight
        }

        Item {
            Layout.fillWidth: true
        }

        Label {
            text: "磁盘 --"
            color: "#64748b"
            font.pixelSize: 11
        }
    }
}
```

- [ ] **Step 3: Delete AppHeader.qml**

Run:

```bash
git -C /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder rm qml/components/AppHeader.qml
```

- [ ] **Step 4: Run structure test and verify chrome assertions pass**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --event-handlers console_direct+ --ctest-args -R test_qml_structure --output-on-failure
```

Expected: chrome-related assertions pass. The test may still fail on camera drag preview-order
assertions.

- [ ] **Step 5: Commit chrome layout change**

Run:

```bash
git -C /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder add qml/Main.qml qml/components/StatusBar.qml
git -C /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder commit -m "Move recording chrome into status bar"
```

---

### Task 3: Add Camera Drag Preview Reordering Helpers

**Files:**

- Modify: `src/data_recorder/qml/components/CameraGridPanel.qml`
- Test: `src/data_recorder/test/test_qml_structure.cpp`

- [ ] **Step 1: Add a placeholder sentinel key**

In `CameraGridPanel.qml`, add this readonly property near `readonly property real tileGap: 4`:

```qml
readonly property string placeholderSourceKey: "__drop_placeholder__"
```

- [ ] **Step 2: Replace index layout helpers**

Replace the existing `layoutForIndex`, `targetIndexAfterRemovingSource`, and `placeholderLayout`
functions with:

```qml
function layoutForIndex(itemIndex, itemCount) {
    var count = Math.max(1, itemCount === undefined ? cameraProxy.count : itemCount)
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

function previewSequence() {
    var sequence = []
    var draggedKey = root.dragActive ? root.dragSourceKey : ""
    for (var index = 0; index < cameraProxy.count; ++index) {
        var camera = cameraProxy.get(index)
        if (camera.sourceKey !== draggedKey) {
            sequence.push({
                sourceKey: camera.sourceKey,
                proxyIndex: index
            })
        }
    }

    if (root.dragActive && root.dropInsertIndex >= 0) {
        var insertIndex = root.dropInsertIndex
        if (root.dragSourceIndex >= 0 && insertIndex > root.dragSourceIndex) {
            insertIndex -= 1
        }
        insertIndex = Math.max(0, Math.min(sequence.length, insertIndex))
        sequence.splice(insertIndex, 0, {
            sourceKey: root.placeholderSourceKey,
            proxyIndex: -1
        })
    }

    return sequence
}

function previewIndexForKey(sourceKey, fallbackIndex) {
    if (!root.dragActive) {
        return fallbackIndex
    }
    var sequence = previewSequence()
    for (var index = 0; index < sequence.length; ++index) {
        if (sequence[index].sourceKey === sourceKey) {
            return index
        }
    }
    return fallbackIndex
}

function previewLayoutForKey(sourceKey, fallbackIndex) {
    if (root.dragActive && sourceKey === root.dragSourceKey) {
        return layoutForIndex(fallbackIndex, cameraProxy.count)
    }
    var sequence = previewSequence()
    var index = root.dragActive ? previewIndexForKey(sourceKey, fallbackIndex) : fallbackIndex
    var count = root.dragActive ? Math.max(1, sequence.length) : cameraProxy.count
    return layoutForIndex(index, count)
}

function placeholderLayout() {
    if (!root.dragActive || root.dropInsertIndex < 0) {
        return {
            x: 0,
            y: 0,
            width: 0,
            height: 0
        }
    }
    return previewLayoutForKey(root.placeholderSourceKey, 0)
}

function floatingPreviewLayout() {
    if (root.dragSourceIndex < 0) {
        return {
            x: 0,
            y: 0,
            width: 1,
            height: 1
        }
    }
    return layoutForIndex(root.dragSourceIndex, cameraProxy.count)
}
```

- [ ] **Step 3: Update insert hit testing**

In `insertIndexAtPoint(xPosition, yPosition)`, replace:

```qml
var itemLayout = layoutForIndex(index)
```

with:

```qml
var itemLayout = layoutForIndex(index, count)
```

- [ ] **Step 4: Update the camera cell layout binding**

In the `Repeater` delegate, replace:

```qml
readonly property var cellLayout: root.layoutForIndex(index)
```

with:

```qml
readonly property var cellLayout: root.previewLayoutForKey(cameraCell.sourceKey, cameraCell.index)
```

- [ ] **Step 5: Update floating preview layout**

In `CameraPreviewTile { id: floatingPreview ... }`, add:

```qml
readonly property var previewLayout: root.floatingPreviewLayout()
```

Then replace:

```qml
width: root.dragSourceIndex >= 0 ? root.layoutForIndex(root.dragSourceIndex).width : 1
height: root.dragSourceIndex >= 0 ? root.layoutForIndex(root.dragSourceIndex).height : 1
```

with:

```qml
width: previewLayout.width
height: previewLayout.height
```

- [ ] **Step 6: Run structure test and verify GREEN**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --event-handlers console_direct+ --ctest-args -R test_qml_structure --output-on-failure
```

Expected: `test_qml_structure` passes.

- [ ] **Step 7: Commit camera drag helper change**

Run:

```bash
git -C /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder add qml/components/CameraGridPanel.qml
git -C /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder commit -m "Preview camera insertion during drag"
```

---

### Task 4: Verification And Cleanup

**Files:**

- Verify: `src/data_recorder/qml/Main.qml`
- Verify: `src/data_recorder/qml/components/*.qml`
- Verify: all package tests

- [ ] **Step 1: Run QML lint**

Run:

```bash
cd /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder && qmllint -I qml/components qml/Main.qml qml/components/*.qml
```

Expected: exits with code `0`. The deleted `AppHeader.qml` is not included because the shell glob
only expands files that still exist.

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

Expected: all `data_recorder` tests pass, including `test_qml_smoke` locating and clicking the moved
`recordButton`.

- [ ] **Step 4: Inspect git status**

Run:

```bash
git -C /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder status --short
```

Expected: no uncommitted source or test changes remain. If only this plan file is uncommitted,
commit it with:

```bash
git -C /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder add docs/superpowers/plans/2026-06-18-data-recorder-chrome-and-camera-drag.md
git -C /home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder commit -m "docs: add chrome and camera drag implementation plan"
```

---

### Spec Coverage Checklist

- Remove `AppHeader`: Task 2 removes the QML usage and deletes the file.
- Move recording control/status to status bar: Task 2 rewrites `StatusBar.qml`.
- Remove saved output directory: Task 2 rewrites `StatusBar.qml`, Task 1 tests absence.
- Keep record button smoke path: Task 4 runs `test_qml_smoke`.
- Shift camera tiles during drag: Task 3 adds transient `previewSequence()` and preview layout by key.
- Commit order only on drop: Task 3 leaves `visualOrder` mutation inside `commitDropInsertIndex()`.
- Verification: Task 4 runs QML lint, build, and all package tests.
