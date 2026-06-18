# Data Source State Prototype Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a UI-only data source state prototype with an `在线数据` entry, history selection state, disabled recording in history view, and recording review state feedback.

**Architecture:** Extend `AppController` with explicit data-source selection state and derived display status. Keep the existing `RecordingSessionModel` for historical rows, and make `RecordingSessionsPanel.qml` render a fixed `在线数据` entry above the existing session list. Reuse the current timeline and status bar, changing only their state bindings and visibility rules.

**Tech Stack:** ROS 2 ament C++, Qt/QML 2.15, QAbstractListModel, GTest, qmllint, colcon.

---

## File Structure

- `include/data_recorder/app_controller.hpp`: add data-source state Qt properties and invokable selection methods.
- `src/app_controller.cpp`: implement online/history selection, state text, recording enablement, and history recording guard.
- `qml/components/RecordingSessionsPanel.qml`: retitle panel to `数据`, render a fixed compact `在线数据` row, add selected backgrounds, and emit row selection requests.
- `qml/components/StatusBar.qml`: bind the record button to `controller.canRecord` and display the controller state text.
- `qml/components/TimelinePanel.qml`: continue showing `回到实时` only when recording and not following live edge; no history-specific exit button.
- `qml/Main.qml`: pass `appController` into `RecordingSessionsPanel`.
- `test/test_ui_models.cpp`: model/controller state tests.
- `test/test_qml_structure.cpp`: static QML structure tests for the data source panel.
- `test/test_qml_smoke.cpp`: interactive QML tests for selecting `在线数据` and history rows.

---

### Task 1: Controller Data Source State

**Files:**
- Modify: `include/data_recorder/app_controller.hpp`
- Modify: `src/app_controller.cpp`
- Test: `test/test_ui_models.cpp`

- [ ] **Step 1: Write failing controller tests**

Add tests near the existing `AppController` tests:

```cpp
TEST(AppController, StartsInOnlineDataSourceState)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);

  EXPECT_FALSE(controller.historyMode());
  EXPECT_EQ(controller.selectedSessionRow(), -1);
  EXPECT_TRUE(controller.canRecord());
  EXPECT_EQ(controller.statusText().toStdString(), "实时查看");
  EXPECT_EQ(controller.modeText().toStdString(), "实时查看");
}

TEST(AppController, SelectingHistoryDisablesRecordingAndUpdatesStatus)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);

  controller.selectHistorySession(0);

  EXPECT_TRUE(controller.historyMode());
  EXPECT_EQ(controller.selectedSessionRow(), 0);
  EXPECT_FALSE(controller.canRecord());
  EXPECT_EQ(controller.statusText().toStdString(), "历史查看：2026-05-31_07-46-20");
  EXPECT_EQ(controller.modeText().toStdString(), "历史查看");

  controller.toggleRecording();
  EXPECT_FALSE(controller.recording());
}

TEST(AppController, SelectingOnlineDataRestoresRecordingAvailability)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);

  controller.selectHistorySession(1);
  controller.selectOnlineData();

  EXPECT_FALSE(controller.historyMode());
  EXPECT_EQ(controller.selectedSessionRow(), -1);
  EXPECT_TRUE(controller.canRecord());
  EXPECT_EQ(controller.statusText().toStdString(), "实时查看");
}

TEST(AppController, RecordingReviewStateHasDistinctStatusText)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);

  controller.toggleRecording();
  EXPECT_EQ(controller.statusText().toStdString(), "录制中");
  EXPECT_EQ(controller.modeText().toStdString(), "录制中");

  controller.setPlayheadSeconds(0.0);
  EXPECT_TRUE(controller.recording());
  EXPECT_FALSE(controller.followingLiveEdge());
  EXPECT_EQ(controller.statusText().toStdString(), "录制中回看");
  EXPECT_EQ(controller.modeText().toStdString(), "录制中回看");

  controller.returnToLiveEdge();
  EXPECT_TRUE(controller.followingLiveEdge());
  EXPECT_EQ(controller.statusText().toStdString(), "录制中");
}
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --continue-on-error --mixin release compile-commands ccache --packages-select data_recorder
```

Expected: build fails because `historyMode`, `selectedSessionRow`, `canRecord`, `selectHistorySession`, and `selectOnlineData` do not exist.

- [ ] **Step 3: Implement minimal controller API**

In `include/data_recorder/app_controller.hpp`, add:

```cpp
Q_PROPERTY(bool historyMode READ historyMode NOTIFY dataSourceChanged)
Q_PROPERTY(int selectedSessionRow READ selectedSessionRow NOTIFY dataSourceChanged)
Q_PROPERTY(bool canRecord READ canRecord NOTIFY canRecordChanged)
```

Add public methods:

```cpp
bool historyMode() const;
int selectedSessionRow() const;
bool canRecord() const;
Q_INVOKABLE void selectOnlineData();
Q_INVOKABLE void selectHistorySession(int row);
```

Add signals:

```cpp
void dataSourceChanged();
void canRecordChanged();
```

Add private fields:

```cpp
bool history_mode_{false};
int selected_session_row_{-1};
```

In `src/app_controller.cpp`:

- `canRecord()` returns `!history_mode_`.
- `selectHistorySession(row)` ignores invalid rows and ignores calls while `recording_` is true.
- `selectHistorySession(row)` sets `history_mode_ = true`, `selected_session_row_ = row`, `following_live_edge_ = false`, and updates `status_text_` to `历史查看：<folderName>`.
- `selectOnlineData()` sets `history_mode_ = false`, `selected_session_row_ = -1`, and status text based on current recording/follow state.
- `toggleRecording()` returns immediately when `!canRecord()`.
- When starting recording, status becomes `录制中`.
- When stopping recording, status becomes `实时查看`.
- `setPlayheadSeconds()` sets status to `录制中回看` when it detaches from live edge.
- `returnToLiveEdge()` sets status to `录制中` when recording.
- `modeText()` returns the same user-facing state family: `历史查看`, `录制中回看`, `录制中`, or `实时查看`.

Use existing `RecordingSessionModel::FolderNameRole` to get the selected history label.

- [ ] **Step 4: Run controller tests to verify green**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --continue-on-error --mixin release compile-commands ccache --packages-select data_recorder && source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --event-handlers console_direct+ --ctest-args --output-on-failure -R test_ui_models
```

Expected: `test_ui_models` passes.

### Task 2: Data Panel Structure

**Files:**
- Modify: `qml/components/RecordingSessionsPanel.qml`
- Modify: `qml/Main.qml`
- Test: `test/test_qml_structure.cpp`

- [ ] **Step 1: Write failing QML structure tests**

Add assertions to `QmlStructure`:

```cpp
TEST(QmlStructure, RecordingSessionsPanelActsAsDataSourceSelector)
{
  const std::string main_text = read_text(qml_dir() / "Main.qml");
  const std::string panel_text = read_text(qml_dir() / "components" / "RecordingSessionsPanel.qml");

  expect_contains(main_text, "controller: appController");
  expect_contains(panel_text, "title: \"数据\"");
  expect_contains(panel_text, "property var controller");
  expect_contains(panel_text, "objectName: \"onlineDataSourceRow\"");
  expect_contains(panel_text, "text: \"在线数据\"");
  expect_contains(panel_text, "root.controller.selectOnlineData()");
  expect_contains(panel_text, "root.controller.selectHistorySession(index)");
  expect_contains(panel_text, "root.controller.selectedSessionRow === index");
  expect_contains(panel_text, "root.controller.historyMode");
  expect_contains(panel_text, "height: 32");
  expect_contains(panel_text, "color: selected ? \"#e8f1ff\"");
  expect_not_contains(panel_text, "当前 ROS topics");
}
```

- [ ] **Step 2: Run structure test to verify failure**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --continue-on-error --mixin release compile-commands ccache --packages-select data_recorder && source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --event-handlers console_direct+ --ctest-args --output-on-failure -R test_qml_structure
```

Expected: structure test fails because the online data row and controller binding are absent.

- [ ] **Step 3: Implement data source selector QML**

In `qml/Main.qml`, update `RecordingSessionsPanel`:

```qml
RecordingSessionsPanel {
    SplitView.fillHeight: true
    SplitView.minimumHeight: 80
    model: appController.recordingSessionModel
    controller: appController
}
```

In `qml/components/RecordingSessionsPanel.qml`:

- Add `property var controller`.
- Change `title` to `"数据"`.
- Replace the `ListView anchors.fill: parent` shape with a top-level `ColumnLayout`.
- Add a fixed `Rectangle` row above the `ListView`:
  - `objectName: "onlineDataSourceRow"`
  - `height: 32`
  - selected when `controller && !controller.historyMode`
  - background `#e8f1ff` when selected, `#ffffff` otherwise
  - left blue selection bar visible when selected
  - green status dot
  - label text `在线数据`
  - mouse click calls `root.controller.selectOnlineData()`
- Keep the historical `ListView` below, with each delegate:
  - `readonly property bool selected: root.controller && root.controller.historyMode && root.controller.selectedSessionRow === index`
  - background `#e8f1ff` when selected
  - left blue selection bar visible when selected
  - click calls `root.controller.selectHistorySession(index)`

- [ ] **Step 4: Run structure test to verify green**

Run the same `test_qml_structure` command. Expected: selected structure test passes.

### Task 3: Status Bar And Smoke Interactions

**Files:**
- Modify: `qml/components/StatusBar.qml`
- Test: `test/test_qml_smoke.cpp`

- [ ] **Step 1: Write failing QML smoke tests**

Add this test near the existing `QmlSmokeTest` cases:

```cpp
TEST_F(QmlSmokeTest, DataSourceRowsSwitchBetweenOnlineAndHistoryState)
{
  auto * online_row = qobject_cast<QQuickItem *>(find_required(root_, "onlineDataSourceRow"));
  auto * history_row = qobject_cast<QQuickItem *>(find_required(root_, "historyDataSourceRow_0"));
  QObject * record_button = find_required(root_, "recordButton");
  ASSERT_NE(online_row, nullptr);
  ASSERT_NE(history_row, nullptr);
  ASSERT_NE(record_button, nullptr);

  EXPECT_FALSE(controller_->historyMode());
  EXPECT_EQ(controller_->statusText().toStdString(), "实时查看");
  EXPECT_TRUE(record_button->property("enabled").toBool());

  QPoint history_position =
    history_row->mapToScene(QPointF(history_row->width() / 2.0, history_row->height() / 2.0))
      .toPoint();
  QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, history_position);
  QCoreApplication::processEvents();

  EXPECT_TRUE(controller_->historyMode());
  EXPECT_EQ(controller_->selectedSessionRow(), 0);
  EXPECT_EQ(controller_->statusText().toStdString(), "历史查看：2026-05-31_07-46-20");
  EXPECT_FALSE(record_button->property("enabled").toBool());

  ASSERT_TRUE(QMetaObject::invokeMethod(record_button, "clicked"));
  EXPECT_FALSE(controller_->recording());

  QPoint online_position =
    online_row->mapToScene(QPointF(online_row->width() / 2.0, online_row->height() / 2.0))
      .toPoint();
  QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, online_position);
  QCoreApplication::processEvents();

  EXPECT_FALSE(controller_->historyMode());
  EXPECT_EQ(controller_->selectedSessionRow(), -1);
  EXPECT_EQ(controller_->statusText().toStdString(), "实时查看");
  EXPECT_TRUE(record_button->property("enabled").toBool());
}
```

- [ ] **Step 2: Run smoke test to verify failure**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --continue-on-error --mixin release compile-commands ccache --packages-select data_recorder && source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --event-handlers console_direct+ --ctest-args --output-on-failure -R test_qml_smoke
```

Expected: smoke test fails before implementation or if object names/state are missing.

- [ ] **Step 3: Implement status bar binding**

In `qml/components/StatusBar.qml`, change record button enabled binding:

```qml
enabled: !!root.controller && root.controller.canRecord
```

Keep the record button text as:

```qml
text: root.isRecording ? "停止" : "录制"
```

Keep the status label bound to `root.statusText`, which now comes from controller state.

If smoke tests need stable historical row lookup, add `objectName: "historyDataSourceRow_" + index` to the historical delegate in `RecordingSessionsPanel.qml`.

- [ ] **Step 4: Run smoke tests to verify green**

Run the same `test_qml_smoke` command. Expected: selected smoke tests pass.

### Task 4: Terminology, Lint, Build, Full Test, Commit

**Files:**
- Modify: `docs/ui_terminology.md`
- Verify all changed files.

- [ ] **Step 1: Update terminology**

Add rows to `docs/ui_terminology.md`:

```markdown
| 数据 | Data | `Data` | 在线数据源和历史采集记录所在的左侧面板 |
| 数据源选择器 | Data Source Selector | `DataSourceSelector` | 在在线数据和历史记录之间切换当前查看数据源的列表 |
| 在线数据 | Online Data | `OnlineData` | 当前连接的 ROS topic 数据源 |
| 历史查看 | Historical Review | `HistoricalReview` | 查看已完成采集记录的 UI 状态 |
| 录制中回看 | Recording Review | `RecordingReview` | 录制仍在继续但播放头脱离实时端的 UI 状态 |
```

- [ ] **Step 2: Run qmllint**

Run:

```bash
source ~/.local/ros2_rc && rr && qmllint -I qml/components qml/Main.qml qml/components/*.qml
```

Expected: no lint errors.

- [ ] **Step 3: Build package**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build --symlink-install --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --continue-on-error --mixin release compile-commands ccache --packages-select data_recorder
```

Expected: package builds successfully.

- [ ] **Step 4: Run full package tests**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon test --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws --packages-select data_recorder --event-handlers console_direct+ --ctest-args --output-on-failure
```

Expected: all package tests pass.

- [ ] **Step 5: Commit**

Run:

```bash
git status --short
git add include/data_recorder/app_controller.hpp src/app_controller.cpp qml/Main.qml qml/components/RecordingSessionsPanel.qml qml/components/StatusBar.qml docs/ui_terminology.md test/test_ui_models.cpp test/test_qml_structure.cpp test/test_qml_smoke.cpp docs/superpowers/plans/2026-06-18-data-source-state-prototype.md
git commit -m "Prototype data source UI states"
```

Expected: commit succeeds. Leave unrelated untracked files such as `TODO.md` untouched.
