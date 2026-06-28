# 时间轴数值曲线 · Plan 3：QML 展开/折叠渲染 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 Plan 2 的模型 role 接到 QML：话题行可展开/折叠（chevron 切换，行高随之变），折叠态在轨道区画 `messageDots` 节奏点，展开态画 `seriesList` 多条彩色曲线并只画 `visible` 的，展开时左栏列出彩色 series chip 可点切显隐。

**Architecture:** `TimelineInfoRow`（左栏）与 `TimelineTrackRow`（轨道区）由 `TrackInfoColumn`/`TrackLaneColumn` 两个并行 `Repeater` 同序迭代同一 `topicModel`，因此把两者行高都绑到 `model.isExpanded ? 120 : 32` 即可保持左右对齐。`TimelineInfoRow` 第二行加 chevron（`enabled: isPlottable`，点击调 `controller.setTopicExpanded(topicName, !isExpanded)`），展开时下方 `Repeater` 列 series chip（点击调 `controller.setSeriesVisible(topicName, key, !visible)`）。`TimelineTrackRow` 折叠时沿基线画 `messageDots`，展开时画曲线 Canvas 并在绘制循环里 `if (entry.visible === false) continue` 跳过隐藏 series、按可见 series 自动缩放 y 轴。

**Tech Stack:** Qt6 QML（QtQuick 2.15），C++ gtest（test_qml_structure 纯文本断言 + test_qml_smoke 加载校验）。

**这是整个特性的 Plan 3 / 5**（见 spec [2026-06-28-timeline-numeric-curves-design.md](../specs/2026-06-28-timeline-numeric-curves-design.md)）。本计划只动 QML 与 QML 测试，不动 C++ 数据流（Plan 4 实时、Plan 5 历史负责把真实曲线喂进模型）。

**风格提醒：** 本仓 QML 用 4 空格缩进、双引号、`Theme.*` 颜色常量、`objectName` 便于测试定位。**构建/测试命令**：
```bash
source ~/.local/ros2_rc
cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder
colcon test --packages-select data_recorder --event-handlers console_direct+ \
  --ctest-args -R '<test_name>'
colcon test-result --verbose
```
git 仓库根在 `src/data_recorder`；提交在该目录下执行。QML 改动需 `colcon build`（install 目录会重新拷贝 qml/）后才被 smoke 测试看到。

---

## 文件结构

- Modify `qml/components/TimelineInfoRow.qml` — 加 `isExpanded`/`isPlottable`/`seriesList` 属性、信号；行高随展开；第二行加 chevron；展开时列 series chip。
- Modify `qml/components/TimelineTrackRow.qml` — 加 `isExpanded`/`messageDots` 属性；行高随展开；折叠画点、展开画可见曲线（跳过 `visible===false`，y 轴按可见缩放）。
- Modify `qml/components/TrackInfoColumn.qml` — 把新 role 绑给 `TimelineInfoRow`，接 chevron / chip 信号到 `controller`。
- Modify `qml/components/TrackLaneColumn.qml` — 把新 role 绑给 `TimelineTrackRow`。
- Modify `test/test_qml_structure.cpp` — 文本断言新结构存在。

---

## Task 1: TimelineTrackRow 折叠点 + 展开可见曲线 + 行高

**Files:**
- Modify: `qml/components/TimelineTrackRow.qml`

- [ ] **Step 1: 加属性与行高**

In `qml/components/TimelineTrackRow.qml`, after `property var seriesList: []` (line 8) add:
```qml
    property bool isExpanded: false
    property var messageDots: []
```

Change the height line (line 16) from `height: 48` to:
```qml
    height: isExpanded ? 120 : 32
```

- [ ] **Step 2: 折叠态画 messageDots（新增 Canvas）**

In `qml/components/TimelineTrackRow.qml`, the existing curve `Canvas { id: curveCanvas ... }` is `visible: root.trackKind === "numeric"`. Change that visibility (line 161) to only show when expanded:
```qml
        visible: root.trackKind === "numeric" && root.isExpanded
```

Then add a second Canvas right after the `curveCanvas` closing brace (after line 230, before the `onTrackKindChanged` handlers) for the collapsed message-dots track:
```qml
    Canvas {
        id: dotsCanvas

        anchors.fill: parent
        // 折叠态：所有 rosbag 数据行（numeric/empty）都画节奏点；相机行不画。
        visible: !root.isExpanded && root.trackKind !== "camera"
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.fillStyle = Theme.surface
            ctx.fillRect(0, 0, width, height)

            var dots = root.messageDots || []
            var baselineY = height / 2
            ctx.fillStyle = Theme.tickStrong
            var start = root.visibleStartSeconds
            var end = root.visibleEndSeconds()
            for (var i = 0; i < dots.length; ++i) {
                var t = Number(dots[i])
                if (!isFinite(t) || t < start || t > end) {
                    continue
                }
                var x = root.xToPixel(t, width)
                ctx.beginPath()
                ctx.arc(x, baselineY, 1.5, 0, Math.PI * 2)
                ctx.fill()
            }
        }
    }
```

- [ ] **Step 3: 展开曲线跳过隐藏 series + 仅按可见 series 缩放 y 轴**

In `qml/components/TimelineTrackRow.qml`, in the `curveCanvas` `onPaint`, the y-range loop (lines 176–185) and the draw loop (line 201) currently process every entry. Make both skip hidden series.

Replace the y-range loop (lines 176–185):
```qml
            for (var seriesIndex = 0; seriesIndex < entries.length; ++seriesIndex) {
                var rangeEntry = entries[seriesIndex] || {}
                if (rangeEntry.visible === false) {
                    continue
                }
                var points = rangeEntry.points || []
                for (var pointIndex = 0; pointIndex < points.length; ++pointIndex) {
                    var yValue = Number(points[pointIndex].y)
                    if (isFinite(yValue)) {
                        minY = Math.min(minY, yValue)
                        maxY = Math.max(maxY, yValue)
                    }
                }
            }
```

In the draw loop, add a skip right after `var entry = entries[drawSeriesIndex] || {}` (line 202):
```qml
            for (var drawSeriesIndex = 0; drawSeriesIndex < entries.length; ++drawSeriesIndex) {
                var entry = entries[drawSeriesIndex] || {}
                if (entry.visible === false) {
                    continue
                }
                var sourcePoints = entry.points || []
```

- [ ] **Step 4: 展开/折叠 + 数据变化时重绘**

In `qml/components/TimelineTrackRow.qml`, the existing repaint handlers (lines 232–235) only repaint `curveCanvas`. Add handlers so both canvases repaint on the relevant changes. Replace the block:
```qml
    onTrackKindChanged: { curveCanvas.requestPaint(); dotsCanvas.requestPaint() }
    onSeriesListChanged: curveCanvas.requestPaint()
    onMessageDotsChanged: dotsCanvas.requestPaint()
    onIsExpandedChanged: { curveCanvas.requestPaint(); dotsCanvas.requestPaint() }
    onVisibleStartSecondsChanged: { curveCanvas.requestPaint(); dotsCanvas.requestPaint() }
    onVisibleDurationSecondsChanged: { curveCanvas.requestPaint(); dotsCanvas.requestPaint() }
```

- [ ] **Step 5: 构建确认 QML 无语法错（smoke 加载）**

Run:
```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder && \
colcon test --packages-select data_recorder --event-handlers console_direct+ \
  --ctest-args -R test_qml_smoke && colcon test-result --verbose
```
Expected: `test_qml_smoke` PASS（QML 解析无误，Main.qml 正常加载）。

- [ ] **Step 6: 提交**

```bash
cd ~/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add qml/components/TimelineTrackRow.qml
git commit -m "feat(curves): TimelineTrackRow 折叠点 + 展开可见曲线 + 行高随展开

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: TimelineInfoRow chevron + series chip + 行高

**Files:**
- Modify: `qml/components/TimelineInfoRow.qml`

- [ ] **Step 1: 加属性与信号**

In `qml/components/TimelineInfoRow.qml`, after `property bool isCamera: false` (line 13) add:
```qml
    property bool isExpanded: false
    property bool isPlottable: false
    property var seriesList: []
    signal toggleExpandRequested()
    signal seriesVisibilityRequested(string seriesKey, bool visible)
```

Change the height line (line 16) from `height: 48` to:
```qml
    height: isExpanded ? 120 : 32
```

- [ ] **Step 2: 第二行加 chevron（在 frequency·backend 标签左侧）**

In `qml/components/TimelineInfoRow.qml`, inside the second `RowLayout` (line 38), insert the chevron Button as the first child, before the frequency Label (line 42):
```qml
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Button {
                id: expandToggleButton

                objectName: "expandToggleButton_" + root.topicName
                enabled: root.isPlottable
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
                padding: 0
                Accessible.name: root.isExpanded ? "折叠曲线" : "展开曲线"
                onClicked: root.toggleExpandRequested()

                background: Rectangle {
                    color: expandToggleButton.hovered ? Theme.gridLine : "transparent"
                    border.width: 0
                }

                contentItem: Label {
                    text: root.isExpanded ? "∨" : ">"
                    color: root.isPlottable ? Theme.textPrimary : Theme.textMuted
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Label {
                Layout.fillWidth: true
                text: root.frequencyText + " · " + root.backendName
                color: Theme.textMuted
                font.pixelSize: 10
                elide: Text.ElideRight
            }
```
(Keep the existing `cameraVisibilityButton` Button after the Label unchanged.)

- [ ] **Step 3: 展开时列 series chip**

In `qml/components/TimelineInfoRow.qml`, the outer `ColumnLayout` (line 21) has two children (topic label + RowLayout). Add a third child after the `RowLayout` closing brace (after line 76, still inside the ColumnLayout) — a Flow of clickable chips:
```qml
        Flow {
            Layout.fillWidth: true
            visible: root.isExpanded
            spacing: 6

            Repeater {
                model: root.isExpanded ? (root.seriesList || []) : []

                delegate: Rectangle {
                    required property var modelData

                    objectName: "seriesChip_" + root.topicName + "_" + (modelData.key || "")
                    height: 16
                    radius: 8
                    width: chipRow.implicitWidth + 12
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.gridLine
                    opacity: (modelData.visible === false) ? 0.45 : 1.0

                    Row {
                        id: chipRow
                        anchors.centerIn: parent
                        spacing: 4

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 8
                            height: 8
                            radius: 4
                            color: modelData.color || "#2563eb"
                        }

                        Label {
                            text: modelData.label || modelData.key || ""
                            color: Theme.textPrimary
                            font.pixelSize: 9
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.seriesVisibilityRequested(
                            modelData.key || "", !(modelData.visible !== false))
                    }
                }
            }
        }
```

- [ ] **Step 4: 构建确认 QML 加载**

Run:
```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder && \
colcon test --packages-select data_recorder --event-handlers console_direct+ \
  --ctest-args -R test_qml_smoke && colcon test-result --verbose
```
Expected: `test_qml_smoke` PASS。

- [ ] **Step 5: 提交**

```bash
cd ~/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add qml/components/TimelineInfoRow.qml
git commit -m "feat(curves): TimelineInfoRow chevron + series chip + 行高随展开

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: 列绑定 — 把新 role 接到行并联动 controller

**Files:**
- Modify: `qml/components/TrackInfoColumn.qml`
- Modify: `qml/components/TrackLaneColumn.qml`

- [ ] **Step 1: TrackInfoColumn 绑定 + 信号**

In `qml/components/TrackInfoColumn.qml`, replace the `TimelineInfoRow { ... }` delegate (lines 134–146) with:
```qml
                TimelineInfoRow {
                    width: infoColumn.width
                    topicName: model.topicName
                    frequencyText: model.frequencyText
                    backendName: model.backendName
                    isVisible: model.isVisible
                    isCamera: model.isCamera
                    isExpanded: model.isExpanded
                    isPlottable: model.isPlottable
                    seriesList: model.seriesList
                    onToggleVisibleRequested: {
                        if (root.controller && root.controller.toggleTopicVisible) {
                            root.controller.toggleTopicVisible(index)
                        }
                    }
                    onToggleExpandRequested: {
                        if (root.controller && root.controller.setTopicExpanded) {
                            root.controller.setTopicExpanded(model.topicName, !model.isExpanded)
                        }
                    }
                    onSeriesVisibilityRequested: function(seriesKey, visible) {
                        if (root.controller && root.controller.setSeriesVisible) {
                            root.controller.setSeriesVisible(model.topicName, seriesKey, visible)
                        }
                    }
                }
```

- [ ] **Step 2: TrackLaneColumn 绑定**

In `qml/components/TrackLaneColumn.qml`, replace the `TimelineTrackRow { ... }` delegate (lines 177–184) with:
```qml
                    TimelineTrackRow {
                        width: trackLaneColumn.width
                        trackKind: model.trackKind
                        seriesList: model.seriesList
                        isExpanded: model.isExpanded
                        messageDots: model.messageDots
                        xMax: root.effectiveDurationSeconds
                        visibleStartSeconds: viewport.visibleStartSeconds
                        visibleDurationSeconds: viewport.boundedVisibleDuration
                    }
```

- [ ] **Step 3: 构建确认加载**

Run:
```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder && \
colcon test --packages-select data_recorder --event-handlers console_direct+ \
  --ctest-args -R test_qml_smoke && colcon test-result --verbose
```
Expected: `test_qml_smoke` PASS（行加载、绑定不报未知属性）。

- [ ] **Step 4: 提交**

```bash
cd ~/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add qml/components/TrackInfoColumn.qml qml/components/TrackLaneColumn.qml
git commit -m "feat(curves): 列绑定 isExpanded/isPlottable/messageDots + chevron/chip 联动 controller

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: QML 结构测试断言

**Files:**
- Modify: `test/test_qml_structure.cpp`

- [ ] **Step 1: 看现有结构测试的 helper 命名**

Read `test/test_qml_structure.cpp` 顶部，确认可用的 helper：`read_text(path)`、`expect_contains(text, needle)`、`qml_dir()`（或等价物）。本 task 沿用之。

- [ ] **Step 2: 加断言**

In `test/test_qml_structure.cpp`, append a new test at the end of the file (before any trailing namespace close), using the same helpers the file already uses (replace `read_text`/`qml_dir`/`expect_contains` with the file's actual helper names if different):
```cpp
TEST(QmlStructure, TimelineRowsSupportExpandCollapseCurves)
{
  const std::string info = read_text(qml_dir() / "components" / "TimelineInfoRow.qml");
  expect_contains(info, "expandToggleButton");
  expect_contains(info, "toggleExpandRequested");
  expect_contains(info, "seriesVisibilityRequested");
  expect_contains(info, "isExpanded ? 120 : 32");

  const std::string track = read_text(qml_dir() / "components" / "TimelineTrackRow.qml");
  expect_contains(track, "dotsCanvas");
  expect_contains(track, "messageDots");
  expect_contains(track, "entry.visible === false");
  expect_contains(track, "isExpanded ? 120 : 32");

  const std::string info_col = read_text(qml_dir() / "components" / "TrackInfoColumn.qml");
  expect_contains(info_col, "setTopicExpanded");
  expect_contains(info_col, "setSeriesVisible");

  const std::string lane_col = read_text(qml_dir() / "components" / "TrackLaneColumn.qml");
  expect_contains(lane_col, "messageDots");
  expect_contains(lane_col, "isExpanded");
}
```

- [ ] **Step 3: 跑结构测试 + 全量不回归**

Run:
```bash
source ~/.local/ros2_rc && cd ~/Documents/Woosh/ros2_recorder_ws
colcon build --packages-select data_recorder && \
colcon test --packages-select data_recorder && colcon test-result --verbose
```
Expected: `test_qml_structure`、`test_qml_smoke` 及全量 0 failures。

- [ ] **Step 4: 提交**

```bash
cd ~/Documents/Woosh/ros2_recorder_ws/src/data_recorder
git add test/test_qml_structure.cpp
git commit -m "test(curves): QML 结构断言展开/折叠曲线渲染就位

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## 验收（Plan 3 完成标志）
- `colcon build` 通过，`test_qml_smoke`（加载 Main.qml 无 QML 错误）+ `test_qml_structure`（新断言）+ 全量 0 failures。
- 产出：话题行 chevron 展开/折叠（行高 32↔120 左右栏同步）、折叠态 `messageDots` 节奏点、展开态多条 `visible` 曲线 + 自动 y 缩放、series chip 点击切显隐，全部经 `controller.setTopicExpanded`/`setSeriesVisible` 联动模型。
- Plan 4（实时）/ Plan 5（历史）把真实曲线/折叠点喂进模型后，本 UI 即可显示真实数据。

## Self-Review 结论
- **Spec 覆盖**：对应 spec「交互与布局」「模型与 QML 改动·QML」三处（chevron、折叠点、展开曲线、显隐 chip、行高同步）。实时/历史数据流在 Plan 4/5。
- **占位符**：无 TBD；每步给完整 QML 片段与命令。Task 4 Step 2 标注「若 helper 名不同则替换」是对既有测试工具的适配指引（先读文件确认），非占位。
- **类型一致**：QML role 名 `isExpanded`/`isPlottable`/`messageDots`/`seriesList`、series 字段 `key`/`label`/`color`/`visible`、controller 方法 `setTopicExpanded(topicName,bool)`/`setSeriesVisible(topicName,key,bool)`、行高表达式 `isExpanded ? 120 : 32`、`objectName` 前缀 `expandToggleButton_`/`seriesChip_` 跨文件一致，且与 Plan 2 的 C++ 模型输出对齐。
