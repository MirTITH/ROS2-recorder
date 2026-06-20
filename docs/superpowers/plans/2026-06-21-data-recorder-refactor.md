# data_recorder 重构实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在正式后端实现前，对 `data_recorder` 前端/控制层做结构性重构：删死代码、统一命名（轨道区/Track Area）、集中颜色、拆分大文件、相机过滤+重排下沉 C++、信号收敛——行为不变（仅两处授权例外）。

**Architecture:** 7 个顺序相位，每相位是一个可独立验证的增量，结束时全部 5 个测试目标必须绿灯。命名/拆分会改动行号，故 Phase 1 在此完整给出；Phase 2–6 给出稳定产物（新文件全文、映射表、测试改动）与基于**精确搜索锚点**（而非行号）的步骤，每相位执行前按当时文件状态最终确认。

**Tech Stack:** C++17 / Qt 6 (Qml/Quick/QuickControls2/Charts) / ament_cmake / ament_cmake_gtest / yaml-cpp。

**设计依据:** [docs/superpowers/specs/2026-06-21-data-recorder-refactor-design.md](../specs/2026-06-21-data-recorder-refactor-design.md)

---

## 通用命令（每个 phase 反复使用）

> 别名 `rr`/`rs` 是用户环境里的 ROS 工作区脚本；以下用其展开等价命令。工作区根：`/home/nros/Documents/Woosh/ros2_recorder_ws`。

**BUILD:**
```bash
source ~/.local/ros2_rc && \
colcon build --symlink-install \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --packages-select data_recorder \
  --mixin release compile-commands ccache
```

**TEST（全部 5 个目标）:**
```bash
source ~/.local/ros2_rc && \
colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --packages-select data_recorder --event-handlers console_direct+ && \
colcon test-result --all --verbose
```
预期绿灯输出含：`test_config_model`、`test_usage_help`、`test_qml_smoke`、`test_qml_structure`、`test_ui_models` 全部 `Passed`，`colcon test-result` 末行 `0 errors, 0 failures`。

---

## Phase 0 — 基线

### Task 0.1: 确认起点全绿

- [ ] **Step 1: 构建**

Run: BUILD
Expected: `Finished <<< data_recorder`，无报错。

- [ ] **Step 2: 跑全部测试**

Run: TEST
Expected: 5 个目标全 `Passed`，`0 errors, 0 failures`。

- [ ] **Step 3: 不提交**

基线已绿。若有红灯，先停下排查——重构只从绿灯出发。

---

## Phase 1 — 删死代码（行为不变）

**File Structure（本相位涉及文件）:**
- Modify: `include/data_recorder/app_controller.hpp` — 移除 cameraModel/trackModel
- Modify: `src/app_controller.cpp` — 移除其填充与访问器
- Modify: `include/data_recorder/config_model.hpp` — 移除 camera_topics/track_topics
- Modify: `src/config_model.cpp` — 移除其填充
- Modify: `include/data_recorder/ui_models.hpp` — 移除 CategoryRole/SeriesRole
- Modify: `src/ui_models.cpp` — 移除其 data/roleNames/赋值 与 topic_category_name 助手
- Modify: `test/test_config_model.cpp`、`test/test_ui_models.cpp`、`test/test_qml_smoke.cpp`
- Delete: `qml/components/CameraPreviewPanel.qml`、`TopicListPanel.qml`、`TopicTrack.qml`

### Task 1.1: 移除 cameraModel/trackModel + camera_topics/track_topics 死链（单次提交）

> 这四者互相依赖（app_controller.cpp 读 config.camera_topics；测试又设置它），必须同一提交内一起改才能保持可编译。

- [ ] **Step 1: 改 `include/data_recorder/app_controller.hpp`**

删除这两行 Q_PROPERTY：
```cpp
  Q_PROPERTY(TopicListModel * cameraModel READ cameraModel CONSTANT)
  Q_PROPERTY(TopicListModel * trackModel READ trackModel CONSTANT)
```
删除这两行访问器声明：
```cpp
  TopicListModel * cameraModel();
  TopicListModel * trackModel();
```
删除这两行成员：
```cpp
  TopicListModel camera_model_;
  TopicListModel track_model_;
```

- [ ] **Step 2: 改 `src/app_controller.cpp`**

删除构造函数里这两行：
```cpp
  camera_model_.set_topics(config.camera_topics);
  track_model_.set_topics(config.track_topics);
```
删除这两个访问器定义（整段）：
```cpp
TopicListModel * AppController::cameraModel()
{
  return &camera_model_;
}

TopicListModel * AppController::trackModel()
{
  return &track_model_;
}
```

- [ ] **Step 3: 改 `include/data_recorder/config_model.hpp`**

在 `struct ConfigData` 中删除：
```cpp
  std::vector<TopicEntry> camera_topics;
  std::vector<TopicEntry> track_topics;
```

- [ ] **Step 4: 改 `src/config_model.cpp`**

把这段（在 `for (const auto & topic_node ...)` 循环内）：
```cpp
        config.topics.push_back(topic);
        if (topic.ui_category == TopicUiCategory::CameraPreview) {
          config.camera_topics.push_back(topic);
        } else {
          config.track_topics.push_back(topic);
        }
```
替换为：
```cpp
        config.topics.push_back(topic);
```

- [ ] **Step 5: 改 `test/test_config_model.cpp`**

删除这 6 行（camera_topics/track_topics 断言）：
```cpp
  ASSERT_EQ(config.track_topics.size(), 2u);
  ASSERT_EQ(config.camera_topics.size(), 1u);
  EXPECT_EQ(config.camera_topics[0].topic_name, "/camera/image_raw");
  EXPECT_EQ(config.camera_topics[0].backend_name, "video");
  EXPECT_EQ(config.camera_topics[0].params.at("codec"), "libx264");
  EXPECT_EQ(config.camera_topics[0].params.at("crf"), "23");
```
保留其前的 `ASSERT_EQ(config.topics.size(), 3u);`。codec/crf 的覆盖改为通过 `config.topics` 验证——在被删 6 行处插入：
```cpp
  const auto camera_it = std::find_if(
    config.topics.begin(), config.topics.end(),
    [](const auto & t) { return t.topic_name == "/camera/image_raw"; });
  ASSERT_NE(camera_it, config.topics.end());
  EXPECT_EQ(camera_it->backend_name, "video");
  EXPECT_EQ(camera_it->params.at("codec"), "libx264");
  EXPECT_EQ(camera_it->params.at("crf"), "23");
```
并确认文件顶部已 `#include <algorithm>`（若无则添加）。

- [ ] **Step 6: 改 `test/test_ui_models.cpp`（fixture + ExposesPopulatedModels）**

在 `make_config_fixture()` 中删除：
```cpp
  config.camera_topics = {camera_topic};
  config.track_topics = {numeric_topic};
```
在 `TEST(AppController, ExposesPopulatedModels)` 中删除这两行：
```cpp
  ASSERT_NE(controller.cameraModel(), nullptr);
  ASSERT_NE(controller.trackModel(), nullptr);
```
并把这段：
```cpp
  EXPECT_EQ(controller.cameraModel()->rowCount(), 1);
  EXPECT_EQ(controller.trackModel()->rowCount(), 1);
  EXPECT_EQ(
    controller.trackModel()
      ->data(controller.trackModel()->index(0, 0), data_recorder::TopicListModel::TopicNameRole)
      .toString()
      .toStdString(),
    "/joint_states");
```
替换为（用 topicModel 验证同等内容——topicModel 第 0 行就是 numeric_topic `/joint_states`）：
```cpp
  EXPECT_EQ(
    controller.topicModel()
      ->data(controller.topicModel()->index(0, 0), data_recorder::TopicListModel::TopicNameRole)
      .toString()
      .toStdString(),
    "/joint_states");
```

- [ ] **Step 7: 改 `test/test_qml_smoke.cpp`（fixture）**

删除：
```cpp
  config.camera_topics = {camera_topic};
  config.track_topics = {tf_topic, joint_topic};
```

- [ ] **Step 8: 构建并跑测试**

Run: BUILD 然后 TEST
Expected: 全绿。`test_config_model` / `test_ui_models` / `test_qml_smoke` 仍 Passed。

- [ ] **Step 9: 提交**

```bash
git add -A
git commit -m "refactor: remove unused cameraModel/trackModel and camera/track topic partition"
```

### Task 1.2: 移除未用的 TopicListModel role（category / series）

> 仅 `category`(CategoryRole) 与 `series`(SeriesRole) 在 live QML 中零引用、只被测试用；`seriesColor` 仍在用，保留。

- [ ] **Step 1: 改 `include/data_recorder/ui_models.hpp`**

在 `enum Roles` 中删除 `CategoryRole,` 与 `SeriesRole,` 两个枚举项。
在 `struct TopicRow` 中删除成员：
```cpp
    QVariantList series;
```

- [ ] **Step 2: 改 `src/ui_models.cpp`（data / roleNames / set_topics / 助手）**

在 `data()` 的 switch 中删除两段：
```cpp
    case CategoryRole:
      return topic_category_name(row.topic.ui_category);
```
```cpp
    case SeriesRole:
      return row.series;
```
在 `roleNames()` 中删除两行：
```cpp
    {CategoryRole, "category"},
```
```cpp
    {SeriesRole, "series"},
```
在 `set_topics()` 中删除这段对 `row.series` 的赋值：
```cpp
    row.series = row.series_list.isEmpty() ?
      QVariantList{} :
      row.series_list.first().toMap().value(QStringLiteral("points")).toList();
```
删除现在无人调用的助手函数 `topic_category_name`（其唯一调用点刚被删；位于文件顶部匿名 namespace，签名形如 `QString topic_category_name(TopicUiCategory category)`）。

- [ ] **Step 3: 改 `test/test_ui_models.cpp`（ExposesTopicRoles）**

删除 CategoryRole 与 SeriesRole 两处断言：
```cpp
  EXPECT_EQ(
    model.data(index, data_recorder::TopicListModel::CategoryRole).toString().toStdString(),
    "numeric");
```
```cpp
  EXPECT_FALSE(model.data(index, data_recorder::TopicListModel::SeriesRole).toList().isEmpty());
```
（numeric 序列数据的覆盖仍由别处的 `SeriesListRole` 断言保证，见 `ClassifiesCameraNumericAndEmptyTracks`。）

- [ ] **Step 4: 构建并跑测试**

Run: BUILD 然后 TEST
Expected: 全绿。

- [ ] **Step 5: 提交**

```bash
git add -A
git commit -m "refactor: drop unused TopicListModel category/series roles"
```

### Task 1.3: 删除 3 个孤儿 QML 组件

> 已验证 `CameraPreviewPanel` / `TopicListPanel` / `TopicTrack` 全库零引用（含测试、Loader、cpp）。

- [ ] **Step 1: 删文件**

```bash
git rm qml/components/CameraPreviewPanel.qml qml/components/TopicListPanel.qml qml/components/TopicTrack.qml
```

- [ ] **Step 2: 构建并跑测试**

Run: BUILD 然后 TEST
Expected: 全绿（`test_qml_smoke` 加载 Main.qml 不引用这三者；`test_qml_structure` 不引用）。

- [ ] **Step 3: 提交**

```bash
git add -A
git commit -m "refactor: delete orphan QML components (CameraPreviewPanel, TopicListPanel, TopicTrack)"
```

---

## Phase 2 — 命名清扫（曲线 → 轨道区 / Track Area）

**行为不变；纯重命名。** 执行前先 `grep -rn "urve" qml/ src/ test/ docs/ui_terminology.md` 复核锚点仍在。

**重命名映射（QML 标识符 / objectName / 文件名）:**

| 现名 | 新名 | 位置 |
| --- | --- | --- |
| `TimelineCurveRow.qml`（文件） | `TimelineTrackRow.qml` | `git mv`，并改 TimelinePanel 内 `TimelineCurveRow {` → `TimelineTrackRow {` |
| `curveList` | `trackLaneList` | TimelinePanel |
| `curveColumn` | `trackLaneColumn` | TimelinePanel |
| `curveViewport` | `trackLaneViewport` | TimelinePanel |
| `eventCurveRepeater` | `eventLaneRepeater` | TimelinePanel |
| `seekFromCurveX` | `seekFromLaneX` | TimelinePanel（定义 + 3 处调用：ruler MouseArea、laneViewport MouseArea） |
| `syncCurveToInfo` | `syncLaneToInfo` | TimelinePanel（定义 + `onContentYChanged`） |
| `syncInfoToCurve` | `syncInfoToLane` | TimelinePanel（定义 + `onContentYChanged`） |
| objectName `timelineCurveMouseArea` | `timelineLaneMouseArea` | TimelinePanel |
| objectName `timelineCurvePlayhead` | `timelineLanePlayhead` | TimelinePanel |

> `TimelineCurveRow.qml` 内部那个 `curveCanvas` id 与画曲线相关，是 numeric 轨道的真实画法，**保留**（它确实画曲线）。重命名只针对“区域/容器”层面的 curve 误用。

### Task 2.1: 重命名文件与 TimelinePanel 内标识符

- [ ] **Step 1: 重命名组件文件**

```bash
git mv qml/components/TimelineCurveRow.qml qml/components/TimelineTrackRow.qml
```

- [ ] **Step 2: 按映射表改 `qml/components/TimelinePanel.qml`**

逐项把上表“现名”替换为“新名”（含 `TimelineCurveRow {` → `TimelineTrackRow {`）。完成后该文件 `grep -i curve` 应只剩 0 处（容器层 curve 已清空；canvas 在另一文件）。

- [ ] **Step 3: 更新 `test/test_qml_structure.cpp`**

在 `TimelineCurveAreaHasAdaptiveWindowAndRangeBar` 与 `TimelineViewportRenderingRulesAreExplicit` 两个测试里，把读取路径
`qml_dir() / "components" / "TimelineCurveRow.qml"` 改为 `"TimelineTrackRow.qml"`。
测试函数名 `TimelineCurveAreaHasAdaptiveWindowAndRangeBar` 改为 `TimelineTrackAreaHasAdaptiveWindowAndRangeBar`。

- [ ] **Step 4: 更新 `test/test_qml_smoke.cpp`**

把 objectName 查找 `"timelineCurveMouseArea"`（约 L371）改为 `"timelineLaneMouseArea"`；
`"timelineCurvePlayhead"`（约 L406）改为 `"timelineLanePlayhead"`。

- [ ] **Step 5: 更新术语表 `docs/ui_terminology.md`**

把 `| 曲线区 | Curve Area | \`CurveArea\` | 时间轴右侧曲线、时间尺、播放头所在区域 |` 改为
`| 轨道区 | Track Area | \`TrackArea\` | 时间轴右侧各类轨道、时间尺、播放头所在区域 |`。
核对 `话题列表/TopicList`、`话题轨道/TopicTrack` 两词条：`TopicListPanel`/`TopicTrack.qml` 已删，
把 `TopicTrack` 的 Code Symbol 一列更新为现役组件 `TimelineTrackRow`，`TopicList` 词条若已无对应组件则删除该行。

- [ ] **Step 6: 构建并跑测试**

Run: BUILD 然后 TEST
Expected: 全绿（含改名后的测试函数）。

- [ ] **Step 7: 提交**

```bash
git add -A
git commit -m "refactor: rename timeline curve area to track area (Track Area)"
```

---

## Phase 3 — Theme 单例 + 颜色集中

**授权例外：** 允许合并近似色，视觉可略变。新文件全文如下（稳定，可直接落地）。

### Task 3.1: 建立 Theme 单例并验证 app 仍能加载

- [ ] **Step 1: 创建 `qml/components/Theme.qml`**

```qml
pragma Singleton
import QtQuick 2.15

QtObject {
    // —— 表面 / 背景 ——
    readonly property color surface: "#ffffff"      // 面板/内容背景
    readonly property color surfaceAlt: "#f1f5f9"   // 次级背景（信息列、空轨道）
    readonly property color windowBg: "#e9edf3"     // 窗口底色
    readonly property color rowSelected: "#e8f1ff"  // 选中行

    // —— 线条 / 边框 ——
    readonly property color border: "#cbd5e1"       // 面板/单元边框
    readonly property color gridLine: "#e2e8f0"     // 图表网格线 / 行分隔
    readonly property color tickStrong: "#94a3b8"   // 强调刻度

    // —— 文字 ——
    readonly property color textPrimary: "#111827"
    readonly property color textSecondary: "#475569"
    readonly property color textMuted: "#64748b"

    // —— 强调 / 语义 ——
    readonly property color accent: "#2563eb"       // 主强调（选中框、拖拽高亮、默认序列色）
    readonly property color accentSoft: "#dbeafe"   // 弱强调填充
    readonly property color danger: "#dc2626"       // 播放头 / 停止 / 错误

    // —— 相机瓦片暗底 ——
    readonly property color cameraTileBg: "#162033"
}
```

- [ ] **Step 2: 创建 `qml/components/qmldir`**

```
singleton Theme 1.0 Theme.qml
```

- [ ] **Step 3: 在 Main.qml 与一个组件里试用，验证 import 接通**

在 `qml/Main.qml` 顶部已有 `import "components"`；把 `ApplicationWindow` 的 `color: "#e9edf3"` 改为 `color: Theme.windowBg`。

- [ ] **Step 4: 验证 app 仍能加载（关键——先确认 singleton 接线正确再大规模替换）**

Run: BUILD 然后
```bash
source ~/.local/ros2_rc && \
colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --packages-select data_recorder --event-handlers console_direct+ \
  --ctest-args -R test_qml_smoke && colcon test-result --all
```
Expected: `test_qml_smoke` Passed（它真正加载 Main.qml；若 singleton 未接通会在此失败）。
若失败：回退方案——改用 C++ 注册，在 `src/data_recorder.cpp` 的 engine 加载前加
`qmlRegisterSingletonType(QUrl::fromLocalFile(qml_dir + "/components/Theme.qml"), "App", 1, 0, "Theme");`
并在各 QML `import App 1.0`；据此调整本相位 import 写法。

- [ ] **Step 5: 提交**

```bash
git add -A
git commit -m "feat: add Theme singleton for centralized chrome colors"
```

### Task 3.2: 逐文件替换 chrome 颜色字面量为 Theme.*

> 仅替换 chrome 色；**数据色不动**：`TagChip` 的 `color`/`markerColor`、config 注入的 tag/marker 色、`seriesColor`（来自模型）、`kSeriesColors`（C++）。
> 用此映射表（左=现字面量，右=token）。同一文件改完即编译+冒烟一次，避免一次性铺开难定位。

**颜色 → token 映射：**
```
#ffffff → Theme.surface
#f8fafc #f6f8fb #eef2f7 #f1f5f9 #fbfdff #eef5ff → Theme.surfaceAlt（按上下文，行/面板浅底）
#e9edf3 → Theme.windowBg
#e8f1ff → Theme.rowSelected
#cbd5e1 #dbe3ef #d7dde5 #c6d0dc #b8c2cf #d5dce8 → Theme.border
#e2e8f0 #e5e7eb → Theme.gridLine
#94a3b8 #9aa8ba → Theme.tickStrong
#111827 #0f172a #1f2937 #020617 → Theme.textPrimary（深文字；#020617/#0f172a 若用于相机暗底则归 cameraTileBg）
#475569 #334155 → Theme.textSecondary
#64748b #718096 → Theme.textMuted
#2563eb → Theme.accent
#dbeafe #bfdbfe → Theme.accentSoft
#dc2626 → Theme.danger
#162033 → Theme.cameraTileBg
```

- [ ] **Step 1: 按受影响文件清单逐个替换**

需改文件（Phase 1/2 后存活、含 chrome 色）：`Main.qml`、`Panel.qml`、`StatusBar.qml`、
`ResizeHandle.qml`、`TimelinePanel.qml`、`TimelineTrackRow.qml`、`TimelineInfoRow.qml`、
`TimelineRangeBar.qml`、`EventTrackInfoRow.qml`、`EventTrackRow.qml`、`CameraGridPanel.qml`、
`CameraPreviewTile.qml`、`RecordingSessionsPanel.qml`、`RecordingTagsPanel.qml`、`TagChip.qml`。
每文件改完跑一次 BUILD（QML 改动经 symlink-install 生效）。

- [ ] **Step 2: 更新 `test/test_qml_structure.cpp` 中断言 hex 的处**

这些断言现在校验字面量，替换后会失配——按实际改成断言 `Theme.*` 或去掉过度具体的颜色断言：
`PanelChromeIsSquareCornered` 不涉色（保留）；
`RecordingSessionsPanelActsAsDataSourceSelector` 中 `expect_contains(panel_text, "color: selected ? \"#e8f1ff\"")` → 改为
`expect_contains(panel_text, "Theme.rowSelected")`；
`TimelineViewportRenderingRulesAreExplicit` 中 range bar 的 `expect_contains(range_text, "color: \"#9aa8ba\"")` → 改为
`expect_contains(range_text, "Theme.tickStrong")`。
（执行时 `grep -n "#[0-9a-fA-F]\{6\}" test/test_qml_structure.cpp` 复核是否还有其它 hex 断言一并处理。）

- [ ] **Step 3: 全量核对无残留 chrome hex**

Run:
```bash
grep -rnE "\"#[0-9a-fA-F]{6}\"" qml/ | grep -vE "Theme.qml|TagChip.qml"
```
Expected: 仅剩数据色相关（如 TagChip 的默认 fallback、若有）。逐条确认每处都是“数据色”而非漏网 chrome 色。

- [ ] **Step 4: 构建并跑全部测试**

Run: BUILD 然后 TEST
Expected: 全绿。

- [ ] **Step 5: 提交**

```bash
git add -A
git commit -m "refactor: route chrome colors through Theme, collapse near-duplicate shades"
```

---

## Phase 4 — 拆分 TimelinePanel 与 EventTrackRow（行为不变）

> 结构性拆分，diff 依赖 Phase 2/3 后状态，执行前按当时文件最终确认。每抽出一个组件即 BUILD+TEST 一次。
> 拆分须保持 `test_qml_structure` 仍能找到其断言的 id/函数/字符串——**这些 id 与函数随之搬到新文件即可**，必要时更新测试中的读取路径到新文件。

### Task 4.1: 从 TimelinePanel 抽出左“轨道信息列”

- [ ] **Step 1: 新建 `qml/components/TrackInfoColumn.qml`**

把 TimelinePanel 左栏 `ColumnLayout`（含播放头时间 Label + “回到实时”按钮的顶栏、`infoList` Flickable 及其 `infoColumn`、事件信息 Repeater、topic 信息 Repeater）整体搬入新组件。
对外暴露 `property var controller`、`property var model`、`property var eventMarkerModel`、`property var timeFormatFns`（或把 `timeString` 等格式化函数随列一起搬入），
并暴露 `property alias contentY: infoList.contentY` 供滚动同步。信号 `signal toggleVisibleRequested(int index)` 透传给 controller，或直接持有 controller 调 `toggleTopicVisible`。

- [ ] **Step 2: TimelinePanel 改为实例化 `TrackInfoColumn`**，删除被搬走的块；滚动同步函数 `scrollRows/syncLaneToInfo/syncInfoToLane` 改为操作 `trackInfoColumn.contentY` 与 `trackLaneList.contentY`。

- [ ] **Step 3: BUILD + TEST**，更新 `test_qml_structure`/`test_qml_smoke` 中因搬迁而变更的路径/对象（如 `EventTrackInfoRow`、`TimelineInfoRow` 现位于 `TrackInfoColumn` 内——若测试是按 TimelinePanel 源码 grep，把对应断言改读 `TrackInfoColumn.qml`）。
Expected: 全绿。

- [ ] **Step 4: 提交** `git commit -m "refactor: extract TrackInfoColumn from TimelinePanel"`

### Task 4.2: 从 TimelinePanel 抽出右“轨道列”

- [ ] **Step 1: 新建 `qml/components/TrackLaneColumn.qml`**

把右栏 `ColumnLayout`（`ruler` Rectangle + tick Repeater + ruler 播放头、`trackLaneViewport` Item + `trackLaneMouseArea` + `trackLaneList` Flickable + 事件轨道 Repeater + topic 轨道 Repeater + lane 播放头、`TimelineRangeBar`）搬入。
对外暴露 `property var controller/model/eventMarkerModel`、`property var viewport`、`property real playheadSeconds`、ruler 格式化 `property int rulerLabelTickStride`，
并 `property alias contentY: trackLaneList.contentY`。把 `seekFromLaneX` 随列搬入。

- [ ] **Step 2: TimelinePanel 退为编排**：实例化 `TimelineViewport` + `TrackInfoColumn` + `TrackLaneColumn`，居中是横向 `SplitView`，只保留滚动同步（双向绑定两列 contentY）与 `Component.onCompleted` 初始化 viewport 窗口。

- [ ] **Step 3: BUILD + TEST**，相应更新 `test_qml_structure`（`TimelineCurveArea…`→已改名的测试里对 ruler/`rulerTickTimes`/`formatTickLabel`/`TimelineRangeBar {` 的断言改读 `TrackLaneColumn.qml`；对 `viewport.isTimeVisible` 等仍在 TimelinePanel 的断言保留）。`test_qml_smoke` 对 `timelineLaneMouseArea`/`timelineLanePlayhead` 的查找是按 objectName 全树搜索，搬迁后仍可命中，无需改。
Expected: 全绿。

- [ ] **Step 4: 提交** `git commit -m "refactor: extract TrackLaneColumn; TimelinePanel becomes orchestrator"`

### Task 4.3: 消除 EventTrackRow 的 4× MouseArea 重复

- [ ] **Step 1: 新建 `qml/components/MarkerDragArea.qml`**

抽出 point/range 体/左右把手共有的 MouseArea 模式：`onWheel`(缩放/平移)、右键 `requestDelete`、左键 `startDrag`、`onPositionChanged` `updateDrag`、`onReleased/onCanceled` `finishDrag`。
对外暴露 `property string dragMode`（"point"/"range"/"left"/"right"）、`property var row`（回调宿主，提供 `zoomAtLocalX/requestDelete/startDrag/updateDrag/finishDrag/localXFromMouse`）、几何属性（cursorShape、anchors 由使用处设定）、`property var startSecondsProvider/endSecondsProvider`（或直接传 instanceDelegate 引用取 normalizedStart/End）。

- [ ] **Step 2: EventTrackRow 内 4 个 MouseArea 改用 `MarkerDragArea`**，各自仅设 `dragMode`、可见性、anchors/尺寸、cursorShape。删除重复逻辑。

- [ ] **Step 3: BUILD + TEST**。`test_qml_structure.EventMarkersRenderAsTimelineTracks` 断言了一批 `id:`/函数/字符串都在 `EventTrackRow.qml`——确保 `startDrag/updateDrag/finishDrag/requestDelete/requestDeleteAll` 与 `id: leftResizeHandle`/`rightResizeHandle` 等仍在 EventTrackRow（把手 Item 的 id 保留在 EventTrackRow，内部用 MarkerDragArea）。`cursorShape: Qt.SizeHorCursor`/`PointingHandCursor` 计数断言（≥2）仍需满足——MarkerDragArea 用 4 次，cursorShape 由使用处设定，计数仍成立。必要时把测试中按字符串计数的断言调整到新结构。
Expected: 全绿。

- [ ] **Step 4: 提交** `git commit -m "refactor: extract MarkerDragArea, remove 4x MouseArea duplication in EventTrackRow"`

---

## Phase 5 — CameraGridPanel：相机模型下沉 C++ + 拆分

**模型先行**，再拆视图，避免拆“即将被替换”的 JS。

### Task 5.1: 实现 `CameraGridModel`（C++，含 gtest）

**Files:**
- Create: `include/data_recorder/camera_grid_model.hpp`、`src/camera_grid_model.cpp`、`test/test_camera_grid_model.cpp`
- Modify: `CMakeLists.txt`（加源文件与测试目标）

- [ ] **Step 1: 写失败测试 `test/test_camera_grid_model.cpp`**

```cpp
#include <gtest/gtest.h>

#include "data_recorder/camera_grid_model.hpp"
#include "data_recorder/ui_models.hpp"

namespace
{
data_recorder::TopicEntry make_topic(const std::string & name, const std::string & backend,
  data_recorder::TopicUiCategory cat)
{
  data_recorder::TopicEntry t;
  t.topic_name = name;
  t.backend_name = backend;
  t.ui_category = cat;
  return t;
}
}  // namespace

TEST(CameraGridModel, ExposesOnlyVisibleCameras)
{
  data_recorder::TopicListModel source;
  source.set_topics({
    make_topic("/joint_states", "rosbag", data_recorder::TopicUiCategory::NumericTrack),
    make_topic("/camera/image_raw", "video", data_recorder::TopicUiCategory::CameraPreview),
    make_topic("/right_camera/image_raw", "video", data_recorder::TopicUiCategory::CameraPreview),
  });

  data_recorder::CameraGridModel model(&source);
  EXPECT_EQ(model.rowCount(), 2);
  EXPECT_EQ(
    model.data(model.index(0, 0), data_recorder::CameraGridModel::TopicNameRole)
      .toString().toStdString(),
    "/camera/image_raw");
}

TEST(CameraGridModel, ReactsToVisibilityToggle)
{
  data_recorder::TopicListModel source;
  source.set_topics({
    make_topic("/camera/image_raw", "video", data_recorder::TopicUiCategory::CameraPreview),
    make_topic("/right_camera/image_raw", "video", data_recorder::TopicUiCategory::CameraPreview),
  });
  data_recorder::CameraGridModel model(&source);
  ASSERT_EQ(model.rowCount(), 2);

  source.toggleVisible(0);  // hide /camera/image_raw
  EXPECT_EQ(model.rowCount(), 1);
  EXPECT_EQ(
    model.data(model.index(0, 0), data_recorder::CameraGridModel::TopicNameRole)
      .toString().toStdString(),
    "/right_camera/image_raw");
}

TEST(CameraGridModel, MoveCameraReordersAndRemembersAcrossToggle)
{
  data_recorder::TopicListModel source;
  source.set_topics({
    make_topic("/a/image_raw", "video", data_recorder::TopicUiCategory::CameraPreview),
    make_topic("/b/image_raw", "video", data_recorder::TopicUiCategory::CameraPreview),
    make_topic("/c/image_raw", "video", data_recorder::TopicUiCategory::CameraPreview),
  });
  data_recorder::CameraGridModel model(&source);

  model.moveCamera(0, 2);  // a -> after b,c  => order b,c,a
  EXPECT_EQ(model.data(model.index(0, 0),
    data_recorder::CameraGridModel::TopicNameRole).toString().toStdString(), "/b/image_raw");
  EXPECT_EQ(model.data(model.index(2, 0),
    data_recorder::CameraGridModel::TopicNameRole).toString().toStdString(), "/a/image_raw");
}
```

- [ ] **Step 2: 加 CMake 目标，确认测试 FAIL（编译失败=红）**

在 `CMakeLists.txt` 的 `DATA_RECORDER_SOURCES` 加 `src/camera_grid_model.cpp`；
在 `DATA_RECORDER_HEADERS` 加 `include/data_recorder/camera_grid_model.hpp`；
在 `if(BUILD_TESTING)` 块加：
```cmake
  ament_add_gtest(test_camera_grid_model test/test_camera_grid_model.cpp)
  target_link_libraries(test_camera_grid_model data_recorder_core)
```
Run: BUILD
Expected: 编译失败（`camera_grid_model.hpp` 不存在）——即红灯。

- [ ] **Step 3: 写 `include/data_recorder/camera_grid_model.hpp`**

```cpp
#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QByteArray>
#include <QVariant>

#include <string>
#include <vector>

#include "data_recorder/ui_models.hpp"

namespace data_recorder
{

class CameraGridModel : public QAbstractListModel
{
  Q_OBJECT
  Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
  enum Roles
  {
    TopicNameRole = Qt::UserRole + 1,
    BackendNameRole,
    ResolutionTextRole,
    SeriesColorRole,
  };

  explicit CameraGridModel(TopicListModel * source, QObject * parent = nullptr);

  int rowCount(const QModelIndex & parent = QModelIndex()) const override;
  QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  Q_INVOKABLE void moveCamera(int from, int to);

signals:
  void countChanged();

private:
  struct Camera
  {
    QString topic_name;
    QString backend_name;
    QString resolution_text;
    QString series_color;
  };

  QString key_of(const QString & topic, const QString & backend) const;
  void rebuild();

  TopicListModel * source_;
  std::vector<QString> order_;       // 记忆的全体相机顺序（key）
  std::vector<Camera> visible_;      // 当前可见行（按 order_ 过滤）
};

}  // namespace data_recorder
```

- [ ] **Step 4: 写 `src/camera_grid_model.cpp`**

```cpp
#include "data_recorder/camera_grid_model.hpp"

#include <algorithm>

namespace data_recorder
{

CameraGridModel::CameraGridModel(TopicListModel * source, QObject * parent)
: QAbstractListModel(parent), source_(source)
{
  if (source_ != nullptr) {
    connect(source_, &QAbstractItemModel::dataChanged, this, [this]() { rebuild(); });
    connect(source_, &QAbstractItemModel::rowsInserted, this, [this]() { rebuild(); });
    connect(source_, &QAbstractItemModel::rowsRemoved, this, [this]() { rebuild(); });
    connect(source_, &QAbstractItemModel::modelReset, this, [this]() { rebuild(); });
  }
  rebuild();
}

QString CameraGridModel::key_of(const QString & topic, const QString & backend) const
{
  return topic + QStringLiteral("|") + backend;
}

void CameraGridModel::rebuild()
{
  beginResetModel();

  struct SourceCamera { QString key, topic, backend, resolution, color; bool visible; };
  std::vector<SourceCamera> cameras;
  const int rows = source_ ? source_->rowCount() : 0;
  for (int row = 0; row < rows; ++row) {
    const auto idx = source_->index(row, 0);
    if (!source_->data(idx, TopicListModel::IsCameraRole).toBool()) {
      continue;
    }
    SourceCamera c;
    c.topic = source_->data(idx, TopicListModel::TopicNameRole).toString();
    c.backend = source_->data(idx, TopicListModel::BackendNameRole).toString();
    c.resolution = source_->data(idx, TopicListModel::ResolutionTextRole).toString();
    c.color = source_->data(idx, TopicListModel::SeriesColorRole).toString();
    c.visible = source_->data(idx, TopicListModel::IsVisibleRole).toBool();
    c.key = key_of(c.topic, c.backend);
    cameras.push_back(std::move(c));
  }

  // 新相机追加到 order_ 末尾，保持既有顺序记忆。
  for (const auto & c : cameras) {
    if (std::find(order_.begin(), order_.end(), c.key) == order_.end()) {
      order_.push_back(c.key);
    }
  }
  // 移除已不存在的相机 key。
  order_.erase(
    std::remove_if(order_.begin(), order_.end(), [&](const QString & key) {
      return std::none_of(cameras.begin(), cameras.end(),
        [&](const SourceCamera & c) { return c.key == key; });
    }),
    order_.end());

  // 按 order_ 过滤出可见行。
  visible_.clear();
  for (const auto & key : order_) {
    const auto it = std::find_if(cameras.begin(), cameras.end(),
      [&](const SourceCamera & c) { return c.key == key; });
    if (it != cameras.end() && it->visible) {
      visible_.push_back(Camera{it->topic, it->backend, it->resolution, it->color});
    }
  }

  endResetModel();
  emit countChanged();
}

int CameraGridModel::rowCount(const QModelIndex & parent) const
{
  if (parent.isValid()) {
    return 0;
  }
  return static_cast<int>(visible_.size());
}

QVariant CameraGridModel::data(const QModelIndex & index, int role) const
{
  if (!index.isValid() || index.row() < 0 ||
    index.row() >= static_cast<int>(visible_.size()))
  {
    return {};
  }
  const auto & c = visible_.at(static_cast<std::size_t>(index.row()));
  switch (role) {
    case TopicNameRole: return c.topic_name;
    case BackendNameRole: return c.backend_name;
    case ResolutionTextRole: return c.resolution_text;
    case SeriesColorRole: return c.series_color;
    default: return {};
  }
}

QHash<int, QByteArray> CameraGridModel::roleNames() const
{
  return {
    {TopicNameRole, "topicName"},
    {BackendNameRole, "backendName"},
    {ResolutionTextRole, "resolutionText"},
    {SeriesColorRole, "seriesColor"},
  };
}

void CameraGridModel::moveCamera(int from, int to)
{
  const int n = static_cast<int>(visible_.size());
  if (from < 0 || from >= n || to < 0 || to >= n || from == to) {
    return;
  }
  // 把可见行的 from/to 映射回 order_ 的下标。
  const QString from_key = key_of(visible_[from].topic_name, visible_[from].backend_name);
  const QString to_key = key_of(visible_[to].topic_name, visible_[to].backend_name);
  const auto from_it = std::find(order_.begin(), order_.end(), from_key);
  const auto to_it = std::find(order_.begin(), order_.end(), to_key);
  if (from_it == order_.end() || to_it == order_.end()) {
    return;
  }
  const QString moved = *from_it;
  order_.erase(from_it);
  const auto insert_it = std::find(order_.begin(), order_.end(), to_key);
  order_.insert(insert_it, moved);  // 插到目标 key 之前；与当前 drop 语义一致
  rebuild();  // 与现有 QML 落点 rebuild 行为等价（重置式）
}

}  // namespace data_recorder
```

> 说明：`moveCamera` 用重置式 rebuild，行为与现有 QML 落点 `rebuildVisibleCameras()` 一致；
> 如需平滑动画再换 `beginMoveRows` 优化（视图是 Repeater + 显式布局，重置可接受）。

- [ ] **Step 5: BUILD + 跑该测试，确认绿**

Run: BUILD 然后
```bash
source ~/.local/ros2_rc && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --packages-select data_recorder --event-handlers console_direct+ --ctest-args -R test_camera_grid_model && \
colcon test-result --all
```
Expected: `test_camera_grid_model` 3 个用例 Passed。

- [ ] **Step 6: 提交** `git commit -m "feat: add CameraGridModel (C++ visible-camera filter + reorder)"`

### Task 5.2: AppController 暴露 cameraGridModel，并由它派生 visibleCameraCount

- [ ] **Step 1: 改 `include/data_recorder/app_controller.hpp`**

`#include "data_recorder/camera_grid_model.hpp"`；
加属性 `Q_PROPERTY(CameraGridModel * cameraGridModel READ cameraGridModel CONSTANT)`；
加访问器声明 `CameraGridModel * cameraGridModel();`；
加成员 `CameraGridModel camera_grid_model_;`（注意构造顺序：须在 `topic_model_` 之后声明，因其构造依赖 `&topic_model_`）。

- [ ] **Step 2: 改 `src/app_controller.cpp`**

构造初始化列表里在 `topic_model_` 之后构造 `camera_grid_model_(&topic_model_)`；
`refreshVisibleCameraCount()` 改为读 `camera_grid_model_.rowCount()`（替代 `topic_model_.visibleCameraCount()`），
并把已有的 `connect(&topic_model_, dataChanged/modelReset, refreshVisibleCameraCount)` 保留即可（CameraGridModel 已自更新，count 仍能刷新）。
加访问器定义 `CameraGridModel * AppController::cameraGridModel() { return &camera_grid_model_; }`。

- [ ] **Step 3: BUILD + TEST**
Expected: 全绿（现有 `visibleCameraCount` 行为不变——值仍是可见相机数）。

- [ ] **Step 4: 提交** `git commit -m "feat: expose cameraGridModel from AppController, derive visibleCameraCount"`

### Task 5.3: 拆分 CameraGridPanel 视图层，改用 cameraGridModel

> diff 依赖 Phase 3 后的 CameraGridPanel 状态，执行前最终确认。

- [ ] **Step 1: 新建 `qml/components/CameraGridLayout.qml`（QtObject 助手）**

把布局几何函数搬入：`cameraAspectRatio`、`averageCameraAspectRatio`、`chooseLayout`、`layoutForIndex`、`insertIndexAtPoint`、`previewSequence`、`previewLayoutForKey`、`previewIndexForKey`、`placeholderLayout`、`floatingPreviewLayout`。
以属性接收 `previewArea` 尺寸、`tileGap`、`model`(=cameraGridModel)、拖拽态（dragActive/dragSourceKey/dropInsertIndex…）。

- [ ] **Step 2: 新建 `qml/components/CameraDragController.qml`（QtObject 状态机）**

把拖放状态机搬入：`beginDragPress/activatePendingDrag/updateDragPosition/startDrag/updateDropInsertIndex/commitDropInsertIndex/finishDrag` 及其状态属性。`commitDropInsertIndex` 落点处改为调用 `model.moveCamera(fromVisibleIndex, toVisibleIndex)`（用可见行下标，不再维护 JS 的 visualOrder/sourceCameras/cameraProxy）。

- [ ] **Step 3: 重写 `CameraGridPanel.qml` 主体**

删除 `sourceCameras`/`cameraProxy` 两个 ListModel、`Instantiator`、`Connections`、`rebuildVisibleCameras`/`refreshSourceList`/`findSourceIndex`/`findProxyIndex`/`findSourceRowIndex`/`makeSourceKey`/`cameraObject`/`visibleDebugOrder` 等。
`property var model` 直接绑定到 `appController.cameraGridModel`（在 Main.qml 传入）；`Repeater { model: root.model }`，delegate 用 `topicName/resolutionText/seriesColor` 角色；位置走 `CameraGridLayout`；拖拽走 `CameraDragController`；`dropPlaceholder`/`floatingPreview` 保留（视图层挤开预览不变）。

- [ ] **Step 4: 改 `qml/Main.qml`**

`CameraGridPanel` 的 `model:` 由 `appController.topicModel` 改为 `appController.cameraGridModel`；
`visibleCameraCount: appController.visibleCameraCount` 保留。

- [ ] **Step 5: BUILD + TEST**

更新 `test_qml_structure.CameraGridUsesExplicitLayoutAndDragPreview`：其断言的函数（`chooseLayout`/`layoutForIndex`/`cameraAspectRatio`/`previewSequence`/`previewLayoutForKey`/`placeholderLayout`/`floatingPreviewLayout`/`updateDropInsertIndex`/`commitDropInsertIndex`）现位于 `CameraGridLayout.qml`/`CameraDragController.qml`——把对应 `read_text(... "CameraGridPanel.qml")` 拆成读对应新文件；`id: dropPlaceholder`/`id: floatingPreview`/`Repeater {`/`placeholderSourceKey` 仍在 `CameraGridPanel.qml`，保留。
`test_qml_smoke.CameraGridShowsVisibleTilesAndCountsRows`（若有，按 objectName/行为）应仍通过——人工核对拖拽挤开效果（README UI Verification）。
Expected: 全绿。

- [ ] **Step 6: 提交** `git commit -m "refactor: CameraGridPanel binds cameraGridModel; split layout + drag controller"`

---

## Phase 6 — toggleRecording 信号收敛（TODO #3）

### Task 6.1: 仅在属性实际变化时发对应信号

- [ ] **Step 1: 改 `src/app_controller.cpp` 的 `toggleRecording()`**

把现有整段：
```cpp
void AppController::toggleRecording()
{
  if (!canRecord()) {
    return;
  }

  recording_ = !recording_;
  if (recording_) {
    following_live_edge_ = true;
    playhead_seconds_ = live_edge_seconds_;
  } else {
    following_live_edge_ = false;
  }
  status_text_ = statusTextForCurrentState();
  emit recordingChanged();
  emit followingLiveEdgeChanged();
  emit playheadSecondsChanged();
  emit statusTextChanged();
  emit modeTextChanged();
}
```
替换为（守卫每个信号）：
```cpp
void AppController::toggleRecording()
{
  if (!canRecord()) {
    return;
  }

  const bool prev_recording = recording_;
  const bool prev_following = following_live_edge_;
  const double prev_playhead = playhead_seconds_;
  const QString prev_status = status_text_;
  const QString prev_mode = modeText();

  recording_ = !recording_;
  if (recording_) {
    following_live_edge_ = true;
    playhead_seconds_ = live_edge_seconds_;
  } else {
    following_live_edge_ = false;
  }
  status_text_ = statusTextForCurrentState();

  if (prev_recording != recording_) {
    emit recordingChanged();
  }
  if (prev_following != following_live_edge_) {
    emit followingLiveEdgeChanged();
  }
  if (prev_playhead != playhead_seconds_) {
    emit playheadSecondsChanged();
  }
  if (prev_status != status_text_) {
    emit statusTextChanged();
  }
  if (prev_mode != modeText()) {
    emit modeTextChanged();
  }
}
```

- [ ] **Step 2: BUILD + TEST**

Expected: 全绿。`test_qml_smoke` 的 recording spy 断言 `recording_spy.count() == 1`（Space 切换一次）仍成立——`recording_` 每次都翻转，`recordingChanged` 必发。

- [ ] **Step 3: 提交** `git commit -m "fix: emit toggleRecording change signals only on actual value change"`

- [ ] **Step 4: 勾掉 TODO**

在 `TODO.md` 删除“优化 `AppController::toggleRecording()` 的信号发射”一行；
若此时“重命名曲线区”“重构前端代码”均已完成，一并勾除/删除对应行。

---

## 贯穿项（在相应相位顺带完成 / 或最后单独处理）

### Task X.1: 展示模型 / 占位数据源留缝
- 在 `src/ui_models.cpp` 把 `make_series_list`/`make_resolution_text`/`make_frequency_text` 及 `RecordingSessionModel` 写死的 3 条 session 收拢到一个清晰命名的边界（如新增 `populateTopicPlaceholders()` / `populateSessionPlaceholders()` 私有函数，或独立 `demo_data.cpp`），`set_topics`/构造只调用它。
- 目的：后端接入时只替换这一处。**仅留缝，不实现后端。** 单独提交 `refactor: isolate placeholder data behind a single seam`。

### Task X.2: 测试去脆化（建议，征得同意后做）
- 保留行为型 `test_qml_smoke` 与 C++ 模型测试；精简 `test_qml_structure` 中“只复述实现细节”的白盒断言（像素尺寸、颜色、纯 id 名），保留真正表达结构契约的断言。
- 单独提交，便于回溯。

---

## 验收（全部相位完成后）
- 每相位结束：5 个测试目标全绿（新增 `test_camera_grid_model` 共 6 个目标）。
- 人工核对 README “UI Verification” 清单（splitter 蓝把手、Record/Stop+Space、相机网格无滚动条不裁剪、可见性折叠、播放头拖拽、信息行竖滚、轨道横向滚轮缩放、**相机拖拽实时挤开**）。
- `grep -rnE "\"#[0-9a-fA-F]{6}\"" qml/ | grep -v Theme.qml` 仅剩数据色。
- `docs/ui_terminology.md` 与现役组件名一致；`TODO.md` 对应项勾除。

## Self-Review 记录
- **Spec coverage:** A→Phase1；B→Phase2；C(大文件)→Phase4/5；C(相机模型)→Phase5；D(数据源缝)→Task X.1；E(信号)→Phase6；F(颜色)→Phase3；G(测试脆弱)→各相位同步改 + Task X.2。全覆盖。
- **类型一致:** `CameraGridModel` role 名（topicName/backendName/resolutionText/seriesColor）与 QML delegate 使用一致；`cameraGridModel` 属性名 hpp/cpp/Main.qml/测试统一。
- **占位扫描:** Phase 2/4/5 的结构步骤明确标注“执行前按当时文件状态最终确认”，并以搜索锚点（精确代码串）而非行号定位——这是顺序重构的必要 JIT，非占位。新文件（Theme.qml/qmldir/CameraGridModel.*/测试）均给出完整内容。
