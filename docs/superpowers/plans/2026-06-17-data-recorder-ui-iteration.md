# Data Recorder UI Iteration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rework the existing Qt/QML data recorder prototype into a denser tool-style UI with an auto-fit camera grid, Premiere-like timeline, compact session/tag styling, and basic keyboard/playhead interactions.

**Architecture:** Keep the app as a minimal C++/Qt/QML ROS 2 prototype. Extend the existing Qt models and `AppController` with UI-only state, then replace the QML layout with smaller reusable components. Do not add ROS subscriptions, real recording, output directory scanning, annotation persistence, or config hot reload.

**Tech Stack:** ROS 2 Humble, ament_cmake, C++17, Qt 6 Core/QML/Quick/QuickControls2/Charts/Widgets, yaml-cpp, GTest, QML `SplitView`, QML `GridView`/`ListView`.

---

## File Map

- Modify `CMakeLists.txt`: install `docs/` and `config/`; keep tests and Qt dependencies.
- Modify `README.md`: update example config path and UI verification checklist.
- Modify `docs/ui_terminology.md`: add timeline/camera/tag terms.
- Modify `include/data_recorder/ui_models.hpp` and `src/ui_models.cpp`: add track-kind roles, camera visibility, multi-series data, richer session rows, marker shortcut selection.
- Modify `include/data_recorder/app_controller.hpp` and `src/app_controller.cpp`: add live-edge/playhead-following/mode/marker UI state.
- Modify `test/test_ui_models.cpp`: add model/controller tests first.
- Verify `src/data_recorder.cpp`: required config parameter behavior remains unchanged; no CLI config support is added.
- Modify `qml/Main.qml`: remove standalone `TopicListPanel`, compose camera grid and redesigned timeline.
- Modify `qml/components/Panel.qml`: 20 px title bar and compact panel chrome.
- Modify `qml/components/StatusBar.qml`: remove playhead time and zoom display.
- Create `qml/components/TagChip.qml`: shared compact colored chip/dot.
- Create `qml/components/CameraGridPanel.qml`: auto-fit camera grid wrapper with UI-only visual ordering.
- Create `qml/components/CameraPreviewTile.qml`: compact non-overlapping preview tile.
- Create `qml/components/SplitHandle.qml`: reusable SplitView handle.
- Create `qml/components/TimelineInfoRow.qml`: left timeline row.
- Create `qml/components/TimelineCurveRow.qml`: right timeline row.
- Replace most of `qml/components/TimelinePanel.qml`: information pane, curve area, ruler, drag/scroll/zoom.
- Modify `qml/components/EventMarkersPanel.qml`: point/range visual styles and shortcut-compatible selection.
- Modify `qml/components/RecordingSessionsPanel.qml`: compact rows and session tag chip.
- Modify `qml/components/RecordingTagsPanel.qml`: shared tag chip styling.
- Leave `qml/components/TopicListPanel.qml` present but unused in this iteration to avoid extra churn.

Do not stage unrelated untracked directories unless the task explicitly says so. At the time this plan was written, `docs/reference/` and `docs/superpowers/feedbacks/` were untracked in the package repo.

---

## Task 1: Update Package Docs, Install Rules, And Terminology

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `README.md`
- Modify: `docs/ui_terminology.md`

- [ ] **Step 1: Update CMake install directories**

In `CMakeLists.txt`, replace the old `install(DIRECTORY doc ...)` block with these two install blocks:

```cmake
install(DIRECTORY docs
  DESTINATION share/${PROJECT_NAME}
)
install(DIRECTORY config
  DESTINATION share/${PROJECT_NAME}
)
```

Keep the existing `install(DIRECTORY qml ...)` and `install(DIRECTORY include/ ...)` blocks unchanged.

- [ ] **Step 2: Update README run command**

In `README.md`, replace the config path in the run command with:

```bash
source ~/.local/ros2_rc && rs && ros2 run data_recorder data_recorder \
  --ros-args -p config_file:=/home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder/config/example_config.yaml
```

Update the UI verification list to:

```markdown
- Resize splitters and verify hover handles become blue with resize cursors.
- Toggle Record/Stop with the button and Space key.
- Verify the camera grid has no scroll bars and does not crop previews.
- Hide camera topics from the Timeline and verify the camera area collapses when none are visible.
- Select recording tags and event marker buttons.
- Drag the Timeline playhead and verify it follows the mouse continuously.
- Wheel over Timeline information rows to scroll vertically.
- Wheel over Timeline curves to zoom horizontally.
```

- [ ] **Step 3: Update terminology**

Append these rows to the terminology table in `docs/ui_terminology.md` before the naming rules section:

```markdown
| 时间轴信息面板 | Timeline Information Pane | `TimelineInfoPane` | 时间轴左侧 topic 信息、播放头时间、可见性控制区域 |
| 曲线区 | Curve Area | `CurveArea` | 时间轴右侧曲线、时间尺、播放头所在区域 |
| 相机网格 | Camera Grid | `CameraGrid` | 自动排列所有可见相机预览的区域 |
| 标签片 | Tag Chip | `TagChip` | 采集记录和记录标签中复用的彩色标签 |
| 实时端 | Live Edge | `LiveEdge` | 录制中模拟的最新采集时间位置 |
| 轨道类型 | Track Kind | `TrackKind` | topic 在时间轴中的 `camera`、`numeric`、`empty` 分类 |
```

- [ ] **Step 4: Build to verify install rules**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build \
  --symlink-install \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --continue-on-error \
  --mixin release compile-commands ccache \
  --packages-select data_recorder
```

Expected: build succeeds.

- [ ] **Step 5: Verify installed directories**

Run:

```bash
test -d /home/nros/Documents/Woosh/ros2_recorder_ws/install/data_recorder/share/data_recorder/docs
test -d /home/nros/Documents/Woosh/ros2_recorder_ws/install/data_recorder/share/data_recorder/config
test -f /home/nros/Documents/Woosh/ros2_recorder_ws/install/data_recorder/share/data_recorder/config/example_config.yaml
```

Expected: all commands exit 0.

- [ ] **Step 6: Commit**

Run:

```bash
git add CMakeLists.txt README.md docs/ui_terminology.md
git commit -m "Update package docs and installed config paths"
```

---

## Task 2: Extend Topic And Session Models With UI Roles

**Files:**
- Modify: `include/data_recorder/ui_models.hpp`
- Modify: `src/ui_models.cpp`
- Modify: `test/test_ui_models.cpp`

- [ ] **Step 1: Write failing topic model tests**

Add these tests to `test/test_ui_models.cpp` after `TopicListModel.FlagsAreNotEditable`:

```cpp
TEST(TopicListModel, ClassifiesCameraNumericAndEmptyTracks)
{
  data_recorder::TopicEntry tf_topic;
  tf_topic.topic_name = "/tf";
  tf_topic.backend_name = "rosbag";
  tf_topic.ui_category = data_recorder::TopicUiCategory::NumericTrack;

  data_recorder::TopicEntry joint_topic;
  joint_topic.topic_name = "/joint_states";
  joint_topic.backend_name = "rosbag";
  joint_topic.ui_category = data_recorder::TopicUiCategory::NumericTrack;

  data_recorder::TopicEntry camera_topic;
  camera_topic.topic_name = "/camera/image_raw";
  camera_topic.backend_name = "video";
  camera_topic.ui_category = data_recorder::TopicUiCategory::CameraPreview;

  data_recorder::TopicListModel model;
  model.set_topics({tf_topic, joint_topic, camera_topic});

  EXPECT_EQ(model.data(model.index(0, 0), data_recorder::TopicListModel::TrackKindRole).toString().toStdString(), "empty");
  EXPECT_FALSE(model.data(model.index(0, 0), data_recorder::TopicListModel::IsDrawableRole).toBool());
  EXPECT_TRUE(model.data(model.index(0, 0), data_recorder::TopicListModel::SeriesListRole).toList().isEmpty());

  EXPECT_EQ(model.data(model.index(1, 0), data_recorder::TopicListModel::TrackKindRole).toString().toStdString(), "numeric");
  EXPECT_TRUE(model.data(model.index(1, 0), data_recorder::TopicListModel::IsDrawableRole).toBool());
  EXPECT_GE(model.data(model.index(1, 0), data_recorder::TopicListModel::SeriesListRole).toList().size(), 2);

  EXPECT_EQ(model.data(model.index(2, 0), data_recorder::TopicListModel::TrackKindRole).toString().toStdString(), "camera");
  EXPECT_TRUE(model.data(model.index(2, 0), data_recorder::TopicListModel::IsCameraRole).toBool());
  EXPECT_FALSE(model.data(model.index(2, 0), data_recorder::TopicListModel::IsDrawableRole).toBool());
  EXPECT_FALSE(model.data(model.index(2, 0), data_recorder::TopicListModel::ResolutionTextRole).toString().isEmpty());
}

TEST(TopicListModel, CountsVisibleCameraRows)
{
  data_recorder::TopicEntry camera_topic;
  camera_topic.topic_name = "/camera/image_raw";
  camera_topic.backend_name = "video";
  camera_topic.ui_category = data_recorder::TopicUiCategory::CameraPreview;

  data_recorder::TopicEntry other_camera_topic = camera_topic;
  other_camera_topic.topic_name = "/left_camera/image_raw";

  data_recorder::TopicListModel model;
  model.set_topics({camera_topic, other_camera_topic});

  EXPECT_EQ(model.visibleCameraCount(), 2);
  model.toggleVisible(0);
  EXPECT_EQ(model.visibleCameraCount(), 1);
  model.toggleVisible(1);
  EXPECT_EQ(model.visibleCameraCount(), 0);
}
```

- [ ] **Step 2: Run build and verify the tests fail to compile**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build \
  --symlink-install \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --continue-on-error \
  --mixin release compile-commands ccache \
  --packages-select data_recorder
```

Expected: build fails because `TrackKindRole`, `IsDrawableRole`, `SeriesListRole`, `IsCameraRole`, `ResolutionTextRole`, and `visibleCameraCount()` do not exist yet.

- [ ] **Step 3: Extend the topic model header**

In `include/data_recorder/ui_models.hpp`, extend `TopicListModel::Roles` to:

```cpp
  enum Roles
  {
    TopicNameRole = Qt::UserRole + 1,
    BackendNameRole,
    CategoryRole,
    IsVisibleRole,
    FrequencyTextRole,
    SeriesColorRole,
    SeriesRole,
    TrackKindRole,
    IsCameraRole,
    IsDrawableRole,
    SeriesListRole,
    ResolutionTextRole,
  };
```

Add this public method:

```cpp
  Q_INVOKABLE int visibleCameraCount() const;
```

Extend `TopicRow` to:

```cpp
    QString track_kind;
    bool is_camera{false};
    bool is_drawable{false};
    QString resolution_text;
    QVariantList series_list;
```

- [ ] **Step 4: Implement track classification helpers**

In `src/ui_models.cpp`, add these helpers inside the anonymous namespace:

```cpp
QString track_kind_for_topic(const TopicEntry & topic)
{
  if (topic.ui_category == TopicUiCategory::CameraPreview) {
    return QStringLiteral("camera");
  }
  if (topic.topic_name == "/tf" || topic.topic_name == "/tf_static") {
    return QStringLiteral("empty");
  }
  if (topic.topic_name == "/joint_states") {
    return QStringLiteral("numeric");
  }
  return QStringLiteral("numeric");
}

QVariantList make_series_list(int row, const QString & track_kind)
{
  QVariantList series_list;
  if (track_kind != QStringLiteral("numeric")) {
    return series_list;
  }

  const int series_count = row == 0 ? 2 : 3;
  for (int series_index = 0; series_index < series_count; ++series_index) {
    QVariantMap series;
    series.insert(QStringLiteral("name"), QStringLiteral("series_%1").arg(series_index + 1));
    series.insert(QStringLiteral("color"), QString::fromLatin1(kSeriesColors[
      static_cast<std::size_t>((row + series_index) % kSeriesColors.size())]));

    QVariantList points;
    points.reserve(80);
    for (int i = 0; i < 80; ++i) {
      QVariantMap point;
      point.insert(QStringLiteral("x"), i);
      point.insert(
        QStringLiteral("y"),
        std::sin((static_cast<double>(i) / 8.0) + series_index) + static_cast<double>(series_index) * 0.4);
      points.push_back(point);
    }
    series.insert(QStringLiteral("points"), points);
    series_list.push_back(series);
  }
  return series_list;
}

QString make_resolution_text(int row)
{
  return row % 2 == 0 ? QStringLiteral("1280x720") : QStringLiteral("1920x1080");
}
```

- [ ] **Step 5: Extend `TopicListModel::data()` and `roleNames()`**

Add cases to `TopicListModel::data()`:

```cpp
    case TrackKindRole:
      return row.track_kind;
    case IsCameraRole:
      return row.is_camera;
    case IsDrawableRole:
      return row.is_drawable;
    case SeriesListRole:
      return row.series_list;
    case ResolutionTextRole:
      return row.resolution_text;
```

Add role names:

```cpp
    {TrackKindRole, "trackKind"},
    {IsCameraRole, "isCamera"},
    {IsDrawableRole, "isDrawable"},
    {SeriesListRole, "seriesList"},
    {ResolutionTextRole, "resolutionText"},
```

- [ ] **Step 6: Update visibility changes and visible camera count**

Update `setData()` so visibility changes emit `IsVisibleRole` through `dataChanged`:

```cpp
  row.is_visible = next_visible;
  emit dataChanged(index, index, {IsVisibleRole});
  return true;
```

Add this method:

```cpp
int TopicListModel::visibleCameraCount() const
{
  int count = 0;
  for (const auto & row : topics_) {
    if (row.is_camera && row.is_visible) {
      ++count;
    }
  }
  return count;
}
```

The model does not emit a count signal in this task; `AppController` will expose its own count in Task 3.

- [ ] **Step 7: Populate new row fields**

In `TopicListModel::set_topics()`, set the new fields:

```cpp
    row.track_kind = track_kind_for_topic(row.topic);
    row.is_camera = row.track_kind == QStringLiteral("camera");
    row.is_drawable = row.track_kind == QStringLiteral("numeric");
    row.resolution_text = row.is_camera ? make_resolution_text(static_cast<int>(i)) : QString();
    row.series_list = make_series_list(static_cast<int>(i), row.track_kind);
    row.series = row.series_list.isEmpty() ? QVariantList{} :
      row.series_list.first().toMap().value(QStringLiteral("points")).toList();
```

- [ ] **Step 8: Extend recording session roles and tests**

Add this test after `AppController.ExposesPopulatedModels`:

```cpp
TEST(RecordingSessionModel, ExposesFolderDurationSizeAndTagRoles)
{
  data_recorder::RecordingSessionModel model;

  ASSERT_GT(model.rowCount(), 0);
  const auto row = model.index(0, 0);
  EXPECT_FALSE(model.data(row, data_recorder::RecordingSessionModel::FolderNameRole).toString().isEmpty());
  EXPECT_FALSE(model.data(row, data_recorder::RecordingSessionModel::ShortDurationRole).toString().isEmpty());
  EXPECT_FALSE(model.data(row, data_recorder::RecordingSessionModel::FullDurationRole).toString().isEmpty());
  EXPECT_FALSE(model.data(row, data_recorder::RecordingSessionModel::SizeTextRole).toString().isEmpty());
  EXPECT_FALSE(model.data(row, data_recorder::RecordingSessionModel::TagNameRole).toString().isEmpty());
  EXPECT_FALSE(model.data(row, data_recorder::RecordingSessionModel::TagColorRole).toString().isEmpty());
}
```

Update `RecordingSessionModel::Roles` to:

```cpp
  enum Roles
  {
    NameRole = Qt::UserRole + 1,
    SizeRole,
    DurationRole,
    FolderNameRole,
    ShortDurationRole,
    FullDurationRole,
    SizeTextRole,
    TagNameRole,
    TagColorRole,
  };
```

Update `RecordingSessionRow` to include:

```cpp
    QString folder_name;
    QString short_duration;
    QString full_duration;
    QString size_text;
    QString tag_name;
    QString tag_color;
```

Seed sessions with:

```cpp
  sessions_ = {
    {QStringLiteral("2026-05-31_07-46-20"), QStringLiteral("1.2 GB"), QStringLiteral("00:00:24.123"),
      QStringLiteral("2026-05-31_07-46-20"), QStringLiteral("24s"), QStringLiteral("00:00:24.123"),
      QStringLiteral("1.2 GB"), QStringLiteral("成功"), QStringLiteral("#2f9e44")},
    {QStringLiteral("2026-05-31_07-47-06"), QStringLiteral("860 MB"), QStringLiteral("00:12:35.000"),
      QStringLiteral("2026-05-31_07-47-06"), QStringLiteral("12m35s"), QStringLiteral("00:12:35.000"),
      QStringLiteral("860 MB"), QStringLiteral("力控"), QStringLiteral("#7c4dff")},
    {QStringLiteral("用户自己改的名称"), QStringLiteral("2.4 GB"), QStringLiteral("02:34:35.500"),
      QStringLiteral("用户自己改的名称"), QStringLiteral("154m35s"), QStringLiteral("02:34:35.500"),
      QStringLiteral("2.4 GB"), QStringLiteral("失败"), QStringLiteral("#e03131")},
  };
```

Return both old and new roles in `data()` and `roleNames()` so existing QML remains compatible during the transition.

- [ ] **Step 9: Run model tests**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build \
  --symlink-install \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --continue-on-error \
  --mixin release compile-commands ccache \
  --packages-select data_recorder

source ~/.local/ros2_rc && rr && colcon test \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --packages-select data_recorder \
  --ctest-args -R test_ui_models \
  --event-handlers console_direct+
```

Expected: build succeeds and `test_ui_models` passes.

- [ ] **Step 10: Commit**

Run:

```bash
git add include/data_recorder/ui_models.hpp src/ui_models.cpp test/test_ui_models.cpp
git commit -m "Extend UI models for timeline iteration"
```

---

## Task 3: Extend AppController With Playhead, Live Edge, And Shortcut State

**Files:**
- Modify: `include/data_recorder/app_controller.hpp`
- Modify: `include/data_recorder/ui_models.hpp`
- Modify: `src/app_controller.cpp`
- Modify: `src/ui_models.cpp`
- Modify: `test/test_ui_models.cpp`

- [ ] **Step 1: Write failing controller tests**

Add these tests after `AppController.SetPlayheadSecondsUpdatesClampsAndEmits`:

```cpp
TEST(AppController, RecordingStartsFollowingLiveEdge)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);

  controller.toggleRecording();
  controller.advanceLiveEdge(3.5);

  EXPECT_TRUE(controller.recording());
  EXPECT_TRUE(controller.followingLiveEdge());
  EXPECT_DOUBLE_EQ(controller.liveEdgeSeconds(), 3.5);
  EXPECT_DOUBLE_EQ(controller.playheadSeconds(), 3.5);
  EXPECT_EQ(controller.modeText().toStdString(), "录制中");
}

TEST(AppController, ScrubbingDuringRecordingDetachesAndCanReturnToLive)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);

  controller.toggleRecording();
  controller.advanceLiveEdge(10.0);
  controller.setPlayheadSeconds(4.0);

  EXPECT_FALSE(controller.followingLiveEdge());
  EXPECT_DOUBLE_EQ(controller.playheadSeconds(), 4.0);

  controller.advanceLiveEdge(12.0);
  EXPECT_DOUBLE_EQ(controller.liveEdgeSeconds(), 12.0);
  EXPECT_DOUBLE_EQ(controller.playheadSeconds(), 4.0);

  controller.returnToLiveEdge();
  EXPECT_TRUE(controller.followingLiveEdge());
  EXPECT_DOUBLE_EQ(controller.playheadSeconds(), 12.0);
}

TEST(AppController, TriggerMarkerShortcutSelectsMarker)
{
  const auto config = make_config_fixture();
  data_recorder::AppController controller(config);

  EXPECT_TRUE(controller.triggerMarkerShortcut("1"));
  EXPECT_EQ(controller.selectedMarkerShortcut().toStdString(), "1");
  EXPECT_FALSE(controller.triggerMarkerShortcut("missing"));
  EXPECT_EQ(controller.selectedMarkerShortcut().toStdString(), "1");
}
```

- [ ] **Step 2: Run build and verify failure**

Run the same `colcon build --packages-select data_recorder` command from Task 2.

Expected: build fails because the new controller methods and properties do not exist.

- [ ] **Step 3: Extend controller header**

In `include/data_recorder/app_controller.hpp`, add properties:

```cpp
  Q_PROPERTY(double liveEdgeSeconds READ liveEdgeSeconds NOTIFY liveEdgeSecondsChanged)
  Q_PROPERTY(bool followingLiveEdge READ followingLiveEdge NOTIFY followingLiveEdgeChanged)
  Q_PROPERTY(QString modeText READ modeText NOTIFY modeTextChanged)
  Q_PROPERTY(QString selectedMarkerShortcut READ selectedMarkerShortcut NOTIFY selectedMarkerShortcutChanged)
  Q_PROPERTY(int visibleCameraCount READ visibleCameraCount NOTIFY visibleCameraCountChanged)
```

Add public methods:

```cpp
  double liveEdgeSeconds() const;
  bool followingLiveEdge() const;
  QString modeText() const;
  QString selectedMarkerShortcut() const;
  int visibleCameraCount() const;

  Q_INVOKABLE void advanceLiveEdge(double seconds);
  Q_INVOKABLE void returnToLiveEdge();
  Q_INVOKABLE bool triggerMarkerShortcut(const QString & shortcut);
  Q_INVOKABLE void toggleTopicVisible(int row);
```

Add signals:

```cpp
  void liveEdgeSecondsChanged();
  void followingLiveEdgeChanged();
  void modeTextChanged();
  void selectedMarkerShortcutChanged();
  void visibleCameraCountChanged();
```

Add members:

```cpp
  double live_edge_seconds_{0.0};
  bool following_live_edge_{false};
  QString selected_marker_shortcut_;
```

- [ ] **Step 4: Implement controller state**

In `src/app_controller.cpp`, add getters matching the header. Implement `modeText()` as:

```cpp
QString AppController::modeText() const
{
  if (recording_) {
    return following_live_edge_ ? QStringLiteral("录制中") : QStringLiteral("查看");
  }
  return QStringLiteral("查看");
}
```

Update `toggleRecording()`:

```cpp
void AppController::toggleRecording()
{
  recording_ = !recording_;
  if (recording_) {
    following_live_edge_ = true;
    playhead_seconds_ = live_edge_seconds_;
    status_text_ = QStringLiteral("录制中（界面原型）");
  } else {
    following_live_edge_ = false;
    status_text_ = QStringLiteral("已停止");
  }
  emit recordingChanged();
  emit followingLiveEdgeChanged();
  emit playheadSecondsChanged();
  emit statusTextChanged();
  emit modeTextChanged();
}
```

Update `setPlayheadSeconds()`:

```cpp
void AppController::setPlayheadSeconds(double seconds)
{
  const double clamped_seconds = std::max(0.0, seconds);
  const bool was_following = following_live_edge_;
  if (recording_) {
    following_live_edge_ = false;
  }
  if (playhead_seconds_ != clamped_seconds) {
    playhead_seconds_ = clamped_seconds;
    emit playheadSecondsChanged();
  }
  if (was_following != following_live_edge_) {
    emit followingLiveEdgeChanged();
    emit modeTextChanged();
  }
}
```

Add:

```cpp
void AppController::advanceLiveEdge(double seconds)
{
  const double clamped_seconds = std::max(0.0, seconds);
  if (live_edge_seconds_ == clamped_seconds) {
    return;
  }
  live_edge_seconds_ = clamped_seconds;
  emit liveEdgeSecondsChanged();
  if (recording_ && following_live_edge_) {
    playhead_seconds_ = live_edge_seconds_;
    emit playheadSecondsChanged();
  }
}

void AppController::returnToLiveEdge()
{
  const bool was_following = following_live_edge_;
  following_live_edge_ = recording_;
  playhead_seconds_ = live_edge_seconds_;
  if (was_following != following_live_edge_) {
    emit followingLiveEdgeChanged();
    emit modeTextChanged();
  }
  emit playheadSecondsChanged();
}

int AppController::visibleCameraCount() const
{
  return topic_model_.visibleCameraCount();
}

void AppController::toggleTopicVisible(int row)
{
  const int previous_count = visibleCameraCount();
  topic_model_.toggleVisible(row);
  const int next_count = visibleCameraCount();
  if (previous_count != next_count) {
    emit visibleCameraCountChanged();
  }
}
```

- [ ] **Step 5: Implement marker shortcut selection**

Add a helper in `EventMarkerModel`:

Header:

```cpp
  Q_INVOKABLE bool selectByShortcut(const QString & shortcut);
```

Implementation:

```cpp
bool EventMarkerModel::selectByShortcut(const QString & shortcut)
{
  const auto normalized = shortcut.toLower();
  for (int row = 0; row < static_cast<int>(markers_.size()); ++row) {
    if (QString::fromStdString(markers_.at(static_cast<std::size_t>(row)).shortcut).toLower() == normalized) {
      select(row);
      return true;
    }
  }
  return false;
}
```

Implement controller method:

```cpp
bool AppController::triggerMarkerShortcut(const QString & shortcut)
{
  if (!event_marker_model_.selectByShortcut(shortcut)) {
    return false;
  }
  selected_marker_shortcut_ = shortcut.toLower();
  emit selectedMarkerShortcutChanged();
  return true;
}
```

- [ ] **Step 6: Run controller tests**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build \
  --symlink-install \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --continue-on-error \
  --mixin release compile-commands ccache \
  --packages-select data_recorder

source ~/.local/ros2_rc && rr && colcon test \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --packages-select data_recorder \
  --ctest-args -R test_ui_models \
  --event-handlers console_direct+
```

Expected: build succeeds and `test_ui_models` passes.

- [ ] **Step 7: Commit**

Run:

```bash
git add include/data_recorder/app_controller.hpp include/data_recorder/ui_models.hpp src/app_controller.cpp src/ui_models.cpp test/test_ui_models.cpp
git commit -m "Add UI-only timeline controller state"
```

---

## Task 4: Add Shared Compact QML Components

**Files:**
- Modify: `qml/components/Panel.qml`
- Create: `qml/components/SplitHandle.qml`
- Create: `qml/components/TagChip.qml`
- Create: `qml/components/TimelineInfoRow.qml`
- Create: `qml/components/TimelineCurveRow.qml`

- [ ] **Step 1: Create compact split handle**

Create `qml/components/SplitHandle.qml`:

```qml
import QtQuick 2.15

Item {
    id: root

    property bool vertical: true
    property bool hovered: hoverHandler.hovered

    implicitWidth: vertical ? 5 : 1
    implicitHeight: vertical ? 1 : 5

    Rectangle {
        anchors.centerIn: parent
        width: root.vertical ? (root.hovered ? 3 : 1) : parent.width
        height: root.vertical ? parent.height : (root.hovered ? 3 : 1)
        color: root.hovered ? "#2563eb" : "#cbd5e1"
    }

    HoverHandler {
        id: hoverHandler
        cursorShape: root.vertical ? Qt.SizeHorCursor : Qt.SizeVerCursor
    }
}
```

- [ ] **Step 2: Update Panel title height**

In `Panel.qml`, change the title bar preferred height to 20:

```qml
Layout.preferredHeight: 20
```

Set title label font to 11 px:

```qml
font.pixelSize: 11
```

Reduce body margins in child panels rather than adding margins in `Panel.qml`.

- [ ] **Step 3: Create TagChip component**

Create `qml/components/TagChip.qml`:

```qml
import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: root

    property string label: ""
    property color chipColor: "#94a3b8"
    property int maxTextWidth: 72
    readonly property real luminance: 0.2126 * chipColor.r + 0.7152 * chipColor.g + 0.0722 * chipColor.b
    readonly property bool dotOnly: labelText.implicitWidth > maxTextWidth

    implicitWidth: dotOnly ? 10 : Math.min(maxTextWidth, labelText.implicitWidth) + 12
    implicitHeight: 18
    radius: dotOnly ? width / 2 : 9
    color: chipColor

    Label {
        id: labelText
        anchors.centerIn: parent
        visible: !root.dotOnly
        text: root.label
        color: root.luminance > 0.56 ? "#111827" : "#ffffff"
        font.pixelSize: 10
        elide: Text.ElideRight
        width: Math.min(root.maxTextWidth, implicitWidth)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    ToolTip.visible: hoverHandler.hovered && root.label.length > 0
    ToolTip.text: root.label

    HoverHandler {
        id: hoverHandler
    }
}
```

- [ ] **Step 4: Create TimelineInfoRow component**

Create `qml/components/TimelineInfoRow.qml`:

```qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root

    property string topicName: ""
    property string frequencyText: ""
    property string backendName: ""
    property string trackKind: "empty"
    property bool isVisible: true
    property bool isCamera: false
    signal toggleVisibleRequested()

    height: 48
    color: "#f8fafc"
    border.color: "#dbe3ef"
    border.width: 0

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 6
        spacing: 6

        Button {
            visible: root.isCamera
            Layout.preferredWidth: 26
            Layout.preferredHeight: 24
            text: root.isVisible ? "◉" : "○"
            font.pixelSize: 13
            onClicked: root.toggleVisibleRequested()
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1

            Label {
                Layout.fillWidth: true
                text: root.topicName
                color: "#111827"
                font.pixelSize: 11
                font.bold: true
                elide: Text.ElideMiddle
            }

            Label {
                Layout.fillWidth: true
                text: root.frequencyText + " · " + root.backendName + " · " + root.trackKind
                color: "#64748b"
                font.pixelSize: 10
                elide: Text.ElideRight
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: "#e2e8f0"
    }
}
```

- [ ] **Step 5: Create TimelineCurveRow component**

Create `qml/components/TimelineCurveRow.qml`:

```qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtCharts 2.15

Rectangle {
    id: root

    property string trackKind: "empty"
    property var seriesList: []
    property real timeScale: 1.0

    height: 48
    color: trackKind === "empty" ? "#f8fafc" : "#ffffff"

    ChartView {
        anchors.fill: parent
        visible: root.trackKind === "numeric"
        antialiasing: true
        animationOptions: ChartView.NoAnimation
        backgroundColor: "transparent"
        plotAreaColor: "transparent"
        legend.visible: false
        margins.left: 0
        margins.right: 0
        margins.top: 0
        margins.bottom: 0

        ValueAxis { id: axisX; min: 0; max: 80 / Math.max(0.2, root.timeScale); labelsVisible: false; gridVisible: true }
        ValueAxis { id: axisY; min: -1.4; max: 2.4; labelsVisible: false; gridVisible: false }

        Repeater {
            model: root.seriesList
            delegate: LineSeries {
                axisX: axisX
                axisY: axisY
                color: modelData.color
                width: 1.5
                Component.onCompleted: {
                    clear()
                    var points = modelData.points || []
                    for (var i = 0; i < points.length; ++i) {
                        append(points[i].x, points[i].y)
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: "#e2e8f0"
    }
}
```

- [ ] **Step 6: Run QML lint**

Run:

```bash
qmllint -I qml/components qml/Main.qml qml/components/*.qml
```

Expected: exit 0.

- [ ] **Step 7: Commit**

Run:

```bash
git add qml/components/Panel.qml qml/components/SplitHandle.qml qml/components/TagChip.qml qml/components/TimelineInfoRow.qml qml/components/TimelineCurveRow.qml
git commit -m "Add compact QML UI primitives"
```

---

## Task 5: Rework Camera Preview Into Auto-Fit Grid

**Files:**
- Create: `qml/components/CameraPreviewTile.qml`
- Create: `qml/components/CameraGridPanel.qml`
- Modify: `qml/Main.qml`

- [ ] **Step 1: Create CameraPreviewTile**

Create `qml/components/CameraPreviewTile.qml`:

```qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root

    property string topicName: ""
    property string resolutionText: "1280x720"
    property color seriesColor: "#2563eb"
    property bool dragActive: false

    color: dragActive ? "#172554" : "#0f172a"
    radius: 3
    clip: true
    border.color: dragActive ? seriesColor : "#334155"
    border.width: 1
    scale: dragActive ? 0.98 : 1.0

    Behavior on scale {
        NumberAnimation { duration: 80 }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 20
            color: "#111827"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                spacing: 6

                Label {
                    Layout.fillWidth: true
                    text: root.topicName
                    color: "#e5e7eb"
                    font.pixelSize: 10
                    elide: Text.ElideMiddle
                }

                Label {
                    text: root.resolutionText
                    color: "#cbd5e1"
                    font.pixelSize: 10
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Canvas {
                id: previewCanvas
                anchors.fill: parent
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    ctx.fillStyle = "#111827"
                    ctx.fillRect(0, 0, width, height)
                    ctx.strokeStyle = "#475569"
                    ctx.lineWidth = 1
                    for (var x = 0; x < width; x += Math.max(24, width / 8)) {
                        ctx.beginPath()
                        ctx.moveTo(x, 0)
                        ctx.lineTo(x, height)
                        ctx.stroke()
                    }
                    for (var y = 0; y < height; y += Math.max(18, height / 6)) {
                        ctx.beginPath()
                        ctx.moveTo(0, y)
                        ctx.lineTo(width, y)
                        ctx.stroke()
                    }
                    ctx.strokeStyle = root.seriesColor
                    ctx.lineWidth = 3
                    ctx.strokeRect(width * 0.18, height * 0.18, width * 0.64, height * 0.64)
                }

                Connections {
                    target: root
                    function onSeriesColorChanged() {
                        previewCanvas.requestPaint()
                    }
                }
            }
        }
    }
}
```

- [ ] **Step 2: Create CameraGridPanel**

Create `qml/components/CameraGridPanel.qml`:

```qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQml.Models 2.15

Panel {
    id: root

    property var model
    property int visibleCameraCount: 0
    title: "相机预览"

    visible: visibleCameraCount > 0
    SplitView.preferredHeight: visible ? 260 : 0
    SplitView.minimumHeight: visible ? 140 : 0
    SplitView.maximumHeight: visible ? 520 : 0

    DelegateModel {
        id: visualModel

        model: root.model
        filterOnGroup: "visibleCamera"
        groups: [
            DelegateModelGroup {
                name: "visibleCamera"
                includeByDefault: false
            }
        ]

        delegate: Item {
            id: cameraCell

            width: grid.cellWidth
            height: grid.cellHeight
            visible: model.isCamera && model.isVisible

            DelegateModel.inVisibleCamera: model.isCamera && model.isVisible
            Drag.active: dragHandler.active
            Drag.source: cameraCell
            Drag.hotSpot.x: width / 2
            Drag.hotSpot.y: height / 2

            CameraPreviewTile {
                anchors.fill: parent
                anchors.margins: 2
                topicName: model.topicName
                resolutionText: model.resolutionText
                seriesColor: model.seriesColor
                dragActive: dragHandler.active
            }

            DropArea {
                anchors.fill: parent
                onEntered: function(drag) {
                    if (!drag.source || drag.source === cameraCell) {
                        return
                    }
                    visualModel.items.move(
                        drag.source.DelegateModel.itemsIndex,
                        cameraCell.DelegateModel.itemsIndex,
                        1)
                }
            }

            DragHandler {
                id: dragHandler
                target: null
            }
        }
    }

    GridView {
        id: grid
        anchors.fill: parent
        anchors.margins: 4
        clip: true
        interactive: false
        model: visualModel

        readonly property int columns: Math.max(1, Math.ceil(Math.sqrt(Math.max(1, root.visibleCameraCount) * width / Math.max(1, height))))
        readonly property int rows: Math.max(1, Math.ceil(Math.max(1, root.visibleCameraCount) / columns))
        cellWidth: width / columns
        cellHeight: height / rows
    }
}
```

This uses the unified topic model. `DelegateModel.items.move()` changes only the current visual order; the parsed config and C++ topic model order stay unchanged.

- [ ] **Step 3: Replace camera strip in Main.qml**

In `qml/Main.qml`, replace the current upper `Item` containing `ScrollView` and `RowLayout` with:

```qml
            CameraGridPanel {
                model: appController.topicModel
                visibleCameraCount: appController.visibleCameraCount
            }
```

- [ ] **Step 4: Run QML lint and build**

Run:

```bash
qmllint -I qml/components qml/Main.qml qml/components/*.qml

source ~/.local/ros2_rc && rr && colcon build \
  --symlink-install \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --continue-on-error \
  --mixin release compile-commands ccache \
  --packages-select data_recorder
```

Expected: both commands pass.

- [ ] **Step 5: Commit**

Run:

```bash
git add qml/Main.qml qml/components/CameraGridPanel.qml qml/components/CameraPreviewTile.qml
git commit -m "Replace camera strip with auto-fit grid"
```

---

## Task 6: Rebuild Timeline Panel

**Files:**
- Modify: `qml/components/TimelinePanel.qml`
- Modify: `qml/Main.qml`
- Modify: `qml/components/TimelineInfoRow.qml`
- Modify: `qml/components/TimelineCurveRow.qml`

- [ ] **Step 1: Replace TimelinePanel structure**

Replace `qml/components/TimelinePanel.qml` with:

```qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Panel {
    id: root

    property var controller
    property var model
    property real durationSeconds: 60
    property real timeScale: 1.0
    readonly property real effectiveDurationSeconds: Math.max(1, Number(durationSeconds) || 1)
    readonly property real playheadSeconds: controller ? Number(controller.playheadSeconds) : 0
    title: "时间轴"

    function timeString(seconds) {
        var totalMs = Math.max(0, Math.round(seconds * 1000))
        var ms = totalMs % 1000
        var totalSeconds = Math.floor(totalMs / 1000)
        var s = totalSeconds % 60
        var m = Math.floor(totalSeconds / 60) % 60
        var h = Math.floor(totalSeconds / 3600)
        return h.toString().padStart(2, "0") + ":" +
            m.toString().padStart(2, "0") + ":" +
            s.toString().padStart(2, "0") + "." +
            ms.toString().padStart(3, "0")
    }

    function seekFromCurveX(xPosition) {
        var seconds = Math.max(0, Math.min(effectiveDurationSeconds, (xPosition / Math.max(1, curveViewport.width)) * effectiveDurationSeconds / Math.max(0.2, timeScale)))
        if (controller && controller.setPlayheadSeconds) {
            controller.setPlayheadSeconds(seconds)
        }
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal
        handle: SplitHandle { vertical: true }

        ColumnLayout {
            SplitView.preferredWidth: 300
            SplitView.minimumWidth: 190
            SplitView.maximumWidth: 520
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                color: "#eef2f7"
                border.color: "#dbe3ef"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8

                    Label {
                        Layout.fillWidth: true
                        text: root.timeString(root.playheadSeconds)
                        color: "#111827"
                        font.pixelSize: 12
                        font.bold: true
                    }

                    Button {
                        visible: root.controller && root.controller.recording && !root.controller.followingLiveEdge
                        text: "回到实时"
                        font.pixelSize: 10
                        onClicked: root.controller.returnToLiveEdge()
                    }
                }
            }

            ListView {
                id: infoList
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: root.model
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                onContentYChanged: curveList.contentY = contentY

                delegate: TimelineInfoRow {
                    width: ListView.view.width
                    topicName: model.topicName
                    frequencyText: model.frequencyText
                    backendName: model.backendName
                    trackKind: model.trackKind
                    isVisible: model.isVisible
                    isCamera: model.isCamera
                    onToggleVisibleRequested: root.controller.toggleTopicVisible(index)
                }
            }
        }

        ColumnLayout {
            SplitView.fillWidth: true
            spacing: 0

            Rectangle {
                id: ruler
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                color: "#ffffff"
                border.color: "#dbe3ef"

                Repeater {
                    model: Math.floor(root.effectiveDurationSeconds / 5) + 1
                    delegate: Item {
                        required property int index
                        x: ((index * 5) / root.effectiveDurationSeconds) * ruler.width * Math.max(0.2, root.timeScale)
                        width: 1
                        height: ruler.height
                        Rectangle { width: 1; height: 9; color: "#94a3b8" }
                        Label {
                            anchors.top: parent.top
                            anchors.topMargin: 11
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: (index * 5) + "s"
                            color: "#64748b"
                            font.pixelSize: 10
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onPressed: root.seekFromCurveX(mouse.x)
                    onPositionChanged: if (pressed) root.seekFromCurveX(mouse.x)
                }
            }

            Item {
                id: curveViewport
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ListView {
                    id: curveList
                    anchors.fill: parent
                    model: root.model
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: false
                    delegate: TimelineCurveRow {
                        width: ListView.view.width
                        trackKind: model.trackKind
                        seriesList: model.seriesList
                        timeScale: root.timeScale
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    onPressed: root.seekFromCurveX(mouse.x)
                    onPositionChanged: if (pressed) root.seekFromCurveX(mouse.x)
                    onWheel: function(wheel) {
                        root.timeScale = Math.max(0.25, Math.min(6.0, root.timeScale + (wheel.angleDelta.y > 0 ? 0.15 : -0.15)))
                        wheel.accepted = true
                    }
                }

                Rectangle {
                    width: 2
                    height: parent.height
                    x: Math.max(0, Math.min(parent.width - width, (root.playheadSeconds / root.effectiveDurationSeconds) * parent.width * Math.max(0.2, root.timeScale) - width / 2))
                    color: "#dc2626"
                    z: 5
                }
            }
        }
    }
}
```

- [ ] **Step 2: Update Main.qml timeline model**

In `qml/Main.qml`, pass the unified topic model to Timeline:

```qml
TimelinePanel {
    SplitView.fillWidth: true
    controller: appController
    model: appController.topicModel
}
```

Remove the `TopicListPanel` block from the layout.

- [ ] **Step 3: Run QML lint**

Run:

```bash
qmllint -I qml/components qml/Main.qml qml/components/*.qml
```

Expected: exit 0.

- [ ] **Step 4: Build**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build \
  --symlink-install \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --continue-on-error \
  --mixin release compile-commands ccache \
  --packages-select data_recorder
```

Expected: build succeeds.

- [ ] **Step 5: Commit**

Run:

```bash
git add qml/Main.qml qml/components/TimelinePanel.qml qml/components/TimelineInfoRow.qml qml/components/TimelineCurveRow.qml
git commit -m "Rebuild timeline with information pane"
```

---

## Task 7: Update Sessions, Tags, Event Marker Styling, And Keyboard Shortcuts

**Files:**
- Modify: `qml/components/RecordingSessionsPanel.qml`
- Modify: `qml/components/RecordingTagsPanel.qml`
- Modify: `qml/components/EventMarkersPanel.qml`
- Modify: `qml/Main.qml`
- Modify: `qml/components/StatusBar.qml`

- [ ] **Step 1: Update recording sessions panel**

Replace the session delegate in `RecordingSessionsPanel.qml` with a compact row:

```qml
delegate: Rectangle {
    width: ListView.view.width
    height: 46
    color: "#ffffff"

    ToolTip.visible: hoverHandler.hovered
    ToolTip.text: model.folderName + "\n时长: " + model.fullDuration + "\n磁盘: " + model.sizeText

    HoverHandler { id: hoverHandler }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 2

        Label {
            Layout.fillWidth: true
            text: model.folderName
            color: "#111827"
            font.pixelSize: 12
            font.bold: true
            elide: Text.ElideMiddle
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: model.shortDuration
                color: "#64748b"
                font.pixelSize: 11
            }
            TagChip {
                label: model.tagName
                chipColor: model.tagColor
                maxTextWidth: 54
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: "#e2e8f0"
    }
}
```

Set `ListView.spacing: 0`.

- [ ] **Step 2: Update recording tags panel**

Replace the Button delegate in `RecordingTagsPanel.qml` with a checkable transparent button containing `TagChip`:

```qml
delegate: Button {
    id: tagButton
    implicitHeight: 22
    leftPadding: 0
    rightPadding: 0
    background: Rectangle {
        color: model.isSelected ? "#dbeafe" : "transparent"
        radius: 4
    }
    contentItem: TagChip {
        label: model.name
        chipColor: model.color
        maxTextWidth: 72
    }
    onClicked: {
        if (root.model && root.model.select) {
            root.model.select(index)
        }
    }
}
```

- [ ] **Step 3: Update event marker styles**

In `EventMarkersPanel.qml`, change the delegate content to show a visual kind cue:

```qml
contentItem: RowLayout {
    spacing: 6
    Rectangle {
        Layout.preferredWidth: model.kind === "range" ? 22 : 8
        Layout.preferredHeight: model.kind === "range" ? 6 : 8
        radius: model.kind === "range" ? 3 : 4
        color: model.color
    }
    Label {
        Layout.fillWidth: true
        text: model.shortcut + "  " + model.name
        color: model.isSelected ? "#ffffff" : "#162033"
        font.pixelSize: 12
        font.bold: true
        elide: Text.ElideRight
    }
}
```

Keep `onClicked: root.model.select(index)` for this task. Keyboard shortcut wiring calls the controller in Step 4.

- [ ] **Step 4: Add key handling in Main.qml**

Add to `ApplicationWindow`:

```qml
    focus: true
    activeFocusOnTab: true

    Shortcut {
        sequence: "Space"
        context: Qt.ApplicationShortcut
        onActivated: appController.toggleRecording()
    }

    Keys.onPressed: function(event) {
        if (event.text && event.text.length === 1 && event.key !== Qt.Key_Space) {
            if (appController.triggerMarkerShortcut(event.text)) {
                event.accepted = true
            }
        }
    }
```

This iteration has no text inputs, so Space and marker shortcuts are global application interactions.

- [ ] **Step 5: Update StatusBar**

Remove playhead time and zoom text from `StatusBar.qml`. Keep output directory and simple status placeholders:

```qml
Label {
    Layout.fillWidth: true
    text: "保存目录: " + root.outputDirectory
    color: "#475569"
    font.pixelSize: 11
    elide: Text.ElideMiddle
}

Label {
    text: "磁盘 --"
    color: "#64748b"
    font.pixelSize: 11
}
```

- [ ] **Step 6: Run QML lint and build**

Run:

```bash
qmllint -I qml/components qml/Main.qml qml/components/*.qml

source ~/.local/ros2_rc && rr && colcon build \
  --symlink-install \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --continue-on-error \
  --mixin release compile-commands ccache \
  --packages-select data_recorder
```

Expected: both commands pass.

- [ ] **Step 7: Commit**

Run:

```bash
git add qml/Main.qml qml/components/RecordingSessionsPanel.qml qml/components/RecordingTagsPanel.qml qml/components/EventMarkersPanel.qml qml/components/StatusBar.qml
git commit -m "Update session tag and marker interactions"
```

---

## Task 8: Add Live Edge Simulation In QML

**Files:**
- Modify: `qml/Main.qml`

- [ ] **Step 1: Add recording timer**

Inside `ApplicationWindow` in `qml/Main.qml`, add:

```qml
Timer {
    id: liveEdgeTimer
    interval: 100
    repeat: true
    running: appController.recording
    onTriggered: appController.advanceLiveEdge(appController.liveEdgeSeconds + interval / 1000.0)
}
```

This keeps live-edge simulation in QML so C++ remains deterministic for tests.

- [ ] **Step 2: Run QML lint and launch check**

Run:

```bash
qmllint -I qml/components qml/Main.qml qml/components/*.qml

source ~/.local/ros2_rc && rr && colcon build \
  --symlink-install \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --continue-on-error \
  --mixin release compile-commands ccache \
  --packages-select data_recorder
```

Expected: both commands pass.

- [ ] **Step 3: Commit**

Run:

```bash
git add qml/Main.qml
git commit -m "Simulate live recording playhead"
```

---

## Task 9: Final UI Verification

**Files:**
- Verify: `qml/Main.qml`
- Verify: `qml/components/*.qml`
- Verify: `README.md`
- Verify: `CMakeLists.txt`

- [ ] **Step 1: Run full QML lint**

Run:

```bash
qmllint -I qml/components qml/Main.qml qml/components/*.qml
```

Expected: exit 0.

- [ ] **Step 2: Run full package build**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon build \
  --symlink-install \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --continue-on-error \
  --mixin release compile-commands ccache \
  --packages-select data_recorder
```

Expected: build succeeds.

- [ ] **Step 3: Run full tests**

Run:

```bash
source ~/.local/ros2_rc && rr && colcon test \
  --base-paths /home/nros/Documents/Woosh/ros2_recorder_ws \
  --packages-select data_recorder \
  --event-handlers console_direct+

source ~/.local/ros2_rc && rr && colcon test-result \
  --test-result-base /home/nros/Documents/Woosh/ros2_recorder_ws/build/data_recorder \
  --verbose
```

Expected: all tests pass and test-result reports `0 errors, 0 failures`.

- [ ] **Step 4: Verify missing-config behavior**

Run:

```bash
set +e
source ~/.local/ros2_rc && rs && ros2 run data_recorder data_recorder > /tmp/data_recorder_missing_config.out 2>&1
status=$?
printf 'exit=%s\n' "$status"
sed -n '1,120p' /tmp/data_recorder_missing_config.out
exit 0
```

Expected: `exit=1`, and output includes the ROS parameter example.

- [ ] **Step 5: Verify UI launch behavior**

Run:

```bash
source ~/.local/ros2_rc && rs && ros2 run data_recorder data_recorder \
  --ros-args -p config_file:=/home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder/config/example_config.yaml
```

In another shell, run:

```bash
xdotool search --name DataRecorder
```

Expected: `xdotool` returns a window id.

- [ ] **Step 6: Manual UI checks**

With the app open:

1. Drag main splitters; handles should show 1 px gray default and blue hover feedback.
2. Confirm panel title bars are compact.
3. Confirm camera previews are in a no-scroll grid and are not cropped.
4. Toggle camera visibility from Timeline information rows; all hidden cameras should collapse the Camera Preview Area.
5. Click Record and press Space; both should toggle record state.
6. While recording, confirm playhead advances; drag playhead and confirm `回到实时` appears.
7. Click `回到实时`; playhead returns to live edge.
8. Drag the ruler and curve area; playhead follows continuously.
9. Wheel over information rows; tracks scroll vertically.
10. Wheel over curve area; horizontal time scale changes.
11. Press event marker shortcuts such as `1`, `2`, and `c`; matching marker type highlights.

- [ ] **Step 7: Stop launched UI**

Stop the launched UI with `Ctrl-C` in the terminal that started it. Then verify:

```bash
ps -ef | rg '[d]ata_recorder' || true
```

Expected: no `data_recorder` process remains.

- [ ] **Step 8: Final status**

Run:

```bash
git status --short
git log --oneline -8
```

Expected: only known unrelated untracked feedback/reference directories remain, or the working tree is clean if those were intentionally added by a separate task.
